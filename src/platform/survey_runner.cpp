/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The runners of survey.h: the live one (device builds) and the pretend
// one. Reading the frames is in survey.cpp, which the checks build without
// LVGL.

#include "survey.h"

#include "lvgl.h"

#include <algorithm>
#include <utility>

#if !USE_DESKTOP
#include "firewall.h"
#include "listen.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>

#ifndef APP_HELPER_PATH
#define APP_HELPER_PATH "/usr/lib/lokictl/lokictl-helper"
#endif
#endif

namespace platform {

namespace {

bool allowed_stick(const std::string& interface_name) {
    return model::valid_interface_name(interface_name) && interface_name != "wlan0" &&
           interface_name != model::kSurveyListenInterface;
}

// ---------------------------------------------------------------------------
// The pretend runner

class PretendSurveyRunner final : public SurveyRunner {
public:
    void recover() override {}

    bool start(const std::string& interface_name) override {
        if (closed_ || !allowed_stick(interface_name) || run_.state == model::ToolState::Running) {
            return false;
        }
        counter_ = SurveyCounter{};
        next_second_ = 0;
        started_ms_ = lv_tick_get();
        run_ = model::SurveyRun{};
        run_.state = model::ToolState::Running;
        run_.interface_name = interface_name;
        fresh_ = true;
        return true;
    }

    void stop() override {
        if (run_.state == model::ToolState::Running) {
            advance();
            run_.state = model::ToolState::Stopped;
            fresh_ = true;
        }
    }

    bool take(model::SurveyRun& run) override {
        if (run_.state == model::ToolState::Running) {
            advance();
        }
        if (!fresh_) {
            return false;
        }
        fresh_ = false;
        run = run_;
        return true;
    }

    void shutdown() override {
        stop();
        closed_ = true;
    }

private:
    void advance() {
        const uint32_t elapsed =
            std::min<uint32_t>(lv_tick_elaps(started_ms_), static_cast<uint32_t>(model::kSurveySeconds) * 1000);
        // Whole seconds are fed as they end, so a picture always shows the
        // same frames.
        std::vector<SurveyFrame> frames;
        while (next_second_ < elapsed / 1000) {
            frames.clear();
            pretend_survey_frames(next_second_, frames);
            for (const SurveyFrame& frame : frames) {
                counter_.note_frame(frame.bytes.data(), frame.bytes.size(), frame.at_ms / 1000, frame.at_ms);
            }
            ++next_second_;
        }
        const int32_t elapsed_ms = static_cast<int32_t>(next_second_ * 1000);
        if (counter_.take_changed() || elapsed_ms != run_.elapsed_ms) {
            counter_.fill(run_);
            run_.elapsed_ms = elapsed_ms;
            fresh_ = true;
        }
        if (elapsed >= static_cast<uint32_t>(model::kSurveySeconds) * 1000) {
            run_.state = model::ToolState::Done;
            fresh_ = true;
        }
    }

    SurveyCounter counter_;
    uint32_t next_second_{0};
    uint32_t started_ms_{0};
    model::SurveyRun run_;
    bool fresh_{false};
    bool closed_{false};
};

#if !USE_DESKTOP

using Clock = std::chrono::steady_clock;
constexpr int kWaitSliceMs = 100;
constexpr int kMaxReads = 32;
// The helper must say "ready" (its listening interface up) within this long.
constexpr auto kReadyLimit = std::chrono::seconds(5);
// On the way out the helper gets this long to remove its listening
// interface.
constexpr auto kPutBackLimit = std::chrono::milliseconds(2500);
// The page's clock moves at least this often.
constexpr auto kPublishEvery = std::chrono::milliseconds(500);

// The note a survey leaves while its listening interface is on the stick:
// the stick's interface, in LokiCTL's runtime folder (cleared at every
// boot, when the listening interface is gone anyway). Empty when there is
// no runtime folder.
std::string note_path() {
    const char* folder = std::getenv("XDG_RUNTIME_DIR");
    if (folder == nullptr || folder[0] != '/') {
        return {};
    }
    return std::string(folder) + "/lokictl-survey-stick";
}

void write_note(const std::string& interface_name) {
    const std::string path = note_path();
    if (!path.empty()) {
        std::ofstream file(path, std::ios::trunc);
        file << interface_name << '\n';
    }
}

void remove_note() {
    const std::string path = note_path();
    if (!path.empty()) {
        std::remove(path.c_str());
    }
}

// What the helper's `survey` exit status means.
void hop_failure(int status, model::ToolError& error, std::string& reason) {
    error = model::ToolError::Failed;
    switch (status) {
        case 2:
            reason = "stick not usable";
            return;
        case 3:
            error = model::ToolError::NotAllowed;
            reason.clear();
            return;
        case 6:
            reason = "stick refused listen mode";
            return;
        case 7:
            reason = "iw not found";
            return;
        case 8:
            reason = "stick is in use";
            return;
        default:
            reason = "stick stopped";
            return;
    }
}

// The firewall helper's `survey` command: a listening interface in monitor
// mode on the stick, hopping, while this holds its standard input open.
class HopProcess {
public:
    ~HopProcess() { stop(); }

    // Runs "lokictl-helper survey <interface_name>" with an empty
    // environment, a pipe on its standard input (kept open here; closing
    // it ends the survey), its standard output on a pipe read here, and
    // /dev/null for its standard error; no blocked signals.
    bool start(const std::string& interface_name, model::ToolError& error, std::string& reason) {
        int in[2] = {-1, -1};
        int out[2] = {-1, -1};
        if (pipe2(in, O_CLOEXEC) != 0) {
            error = model::ToolError::Failed;
            reason = "no pipe";
            return false;
        }
        if (pipe2(out, O_CLOEXEC) != 0) {
            close(in[0]);
            close(in[1]);
            error = model::ToolError::Failed;
            reason = "no pipe";
            return false;
        }
        fcntl(out[0], F_SETFL, fcntl(out[0], F_GETFL) | O_NONBLOCK);
        std::string words[] = {"lokictl-helper", "survey", interface_name};
        char* argv[] = {words[0].data(), words[1].data(), words[2].data(), nullptr};
        char* environment[] = {nullptr};
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        posix_spawn_file_actions_init(&actions);
        posix_spawnattr_init(&attributes);
        posix_spawn_file_actions_adddup2(&actions, in[0], 0);
        posix_spawn_file_actions_adddup2(&actions, out[1], 1);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        sigset_t no_signals;
        sigset_t default_signals;
        sigemptyset(&no_signals);
        sigemptyset(&default_signals);
        sigaddset(&default_signals, SIGTERM);
        sigaddset(&default_signals, SIGINT);
        sigaddset(&default_signals, SIGHUP);
        sigaddset(&default_signals, SIGPIPE);
        posix_spawnattr_setsigmask(&attributes, &no_signals);
        posix_spawnattr_setsigdefault(&attributes, &default_signals);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
        pid_t child = -1;
        const int spawned = posix_spawn(&child, APP_HELPER_PATH, &actions, &attributes, argv, environment);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        close(in[0]);
        close(out[1]);
        if (spawned != 0) {
            close(in[1]);
            close(out[0]);
            if (spawned == EACCES || spawned == EPERM) {
                error = model::ToolError::NotAllowed;
                reason.clear();
            } else {
                error = model::ToolError::Failed;
                reason = spawned == ENOENT ? "helper not installed" : "helper could not start";
            }
            return false;
        }
        pid_ = child;
        in_ = in[1];
        out_ = out[0];
        return true;
    }

    // True once the helper said "ready" (reads what it wrote so far).
    bool ready() {
        if (out_ >= 0) {
            char chunk[64];
            for (;;) {
                const ssize_t got = read(out_, chunk, sizeof(chunk));
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                if (got <= 0) {
                    break;
                }
                if (output_.size() < 256) {
                    output_.append(chunk, static_cast<std::size_t>(got));
                }
            }
        }
        return output_.find("ready\n") != std::string::npos;
    }

    // False once the helper has ended (its status is then in status()).
    bool alive() {
        if (pid_ <= 0) {
            return false;
        }
        int status = 0;
        const pid_t done = waitpid(pid_, &status, WNOHANG);
        if (done == pid_ || (done < 0 && errno != EINTR)) {
            status_ = done == pid_ && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            pid_ = -1;
            close_pipes();
            return false;
        }
        return true;
    }

    int status() const { return status_; }

    // Ends the survey: closes the helper's input and sends SIGTERM, waits
    // for it to remove its listening interface (at most kPutBackLimit),
    // then kills it. True when it removed it itself (exit status 0).
    bool stop() {
        if (pid_ <= 0) {
            close_pipes();
            return status_ == 0;
        }
        if (in_ >= 0) {
            close(in_);
            in_ = -1;
        }
        kill(pid_, SIGTERM);
        const Clock::time_point until = Clock::now() + kPutBackLimit;
        while (alive()) {
            if (Clock::now() >= until) {
                kill(pid_, SIGKILL);
                while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
                }
                pid_ = -1;
                status_ = -1;
                close_pipes();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return status_ == 0;
    }

private:
    void close_pipes() {
        if (in_ >= 0) {
            close(in_);
            in_ = -1;
        }
        if (out_ >= 0) {
            close(out_);
            out_ = -1;
        }
    }

    int pid_{-1};
    int in_{-1};
    int out_{-1};
    int status_{-1};
    std::string output_;
};

class LiveSurveyRunner final : public SurveyRunner {
public:
    ~LiveSurveyRunner() override {
        shutdown();
    }

    void recover() override {
        const std::string path = note_path();
        if (path.empty()) {
            return;
        }
        std::ifstream file(path);
        if (!file) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || busy_ || (!thread_.joinable() && !start_thread())) {
            return;
        }
        busy_ = true;
        has_job_ = true;
        recover_job_ = true;
        job_.clear();
        wake_.notify_all();
    }

    bool start(const std::string& interface_name) override {
        if (!allowed_stick(interface_name)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || busy_) {
                return false;
            }
            if (!thread_.joinable() && !start_thread()) {
                return false;
            }
            busy_ = true;
            has_job_ = true;
            recover_job_ = false;
            job_ = interface_name;
            stop_flag_.store(false);
            latest_ = model::SurveyRun{};
            latest_.state = model::ToolState::Running;
            latest_.interface_name = interface_name;
            fresh_ = true;
        }
        wake_.notify_all();
        return true;
    }

    void stop() override {
        stop_flag_.store(true);
    }

    bool take(model::SurveyRun& run) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!fresh_) {
            return false;
        }
        fresh_ = false;
        run = latest_;
        return true;
    }

    void shutdown() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            quit_ = true;
        }
        stop_flag_.store(true);
        wake_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    // Called with mutex_ held.
    bool start_thread() {
        // The thread never takes the app's stop signals.
        sigset_t stop_signals;
        sigset_t old_mask;
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGTERM);
        sigaddset(&stop_signals, SIGINT);
        const bool masked = pthread_sigmask(SIG_BLOCK, &stop_signals, &old_mask) == 0;
        try {
            thread_ = std::thread(&LiveSurveyRunner::run, this);
        } catch (...) {
            // No thread: the job is refused.
        }
        if (masked) {
            pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        }
        return thread_.joinable();
    }

    bool stopping() const {
        return stop_flag_.load();
    }

    void publish(const model::SurveyRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_) {
            latest_ = run;
            fresh_ = true;
        }
    }

    void run() {
        while (true) {
            std::string interface_name;
            bool recover_job = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_job_; });
                if (has_job_ && quit_ && !recover_job_) {
                    latest_.state = model::ToolState::Stopped;
                    fresh_ = true;
                    has_job_ = false;
                }
                if (quit_ && !has_job_) {
                    busy_ = false;
                    return;
                }
                interface_name = job_;
                recover_job = recover_job_;
                has_job_ = false;
            }
            try {
                if (recover_job) {
                    put_back();
                } else {
                    survey(interface_name);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!recover_job) {
                    latest_.state = model::ToolState::Failed;
                    latest_.error = model::ToolError::Failed;
                    latest_.reason = "internal error";
                    fresh_ = true;
                }
            }
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }

    // A listening interface a killed survey left on the stick: removed
    // with the helper's survey-off (which checks it), then the note goes.
    // A stick that is gone needs nothing: its interfaces went with it.
    void put_back() {
        remove_survey_listener();
        remove_note();
    }

    // Ends the survey: the capture first, then the helper removes its
    // listening interface (or survey-off does, when the helper could not);
    // then the note goes.
    void finish(CaptureProcess& capture, HopProcess& hop) {
        capture.stop();
        if (!hop.stop() && !remove_survey_listener().empty()) {
            return;  // not removed: the note stays for the next start
        }
        remove_note();
    }

    void survey(const std::string& interface_name) {
        model::SurveyRun run;
        run.state = model::ToolState::Running;
        run.interface_name = interface_name;
        SurveyCounter counter;

        write_note(interface_name);
        HopProcess hop;
        CaptureProcess capture;
        if (!hop.start(interface_name, run.error, run.reason)) {
            remove_note();
            run.state = model::ToolState::Failed;
            publish(run);
            return;
        }
        // Waits for the listening interface to be up.
        const Clock::time_point ready_by = Clock::now() + kReadyLimit;
        while (!hop.ready()) {
            if (!hop.alive()) {
                hop_failure(hop.status(), run.error, run.reason);
                if (hop.status() == 2 || hop.status() == 3 || hop.status() == 7 || hop.status() == 8) {
                    remove_note();  // it never touched the stick
                } else {
                    finish(capture, hop);
                }
                run.state = model::ToolState::Failed;
                publish(run);
                return;
            }
            if (stopping() || Clock::now() >= ready_by) {
                finish(capture, hop);
                if (stopping()) {
                    run.state = model::ToolState::Stopped;
                } else {
                    run.state = model::ToolState::Failed;
                    run.error = model::ToolError::Failed;
                    run.reason = "stick did not start";
                }
                publish(run);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kWaitSliceMs));
        }

        const Clock::time_point started = Clock::now();
        const Clock::time_point ends = started + std::chrono::seconds(model::kSurveySeconds);
        Clock::time_point published = started;
        uint8_t buffer[4096];
        enum class End { Running, Done, Stopped, Failed };
        End end = End::Running;
        while (end == End::Running) {
            const auto left = std::chrono::duration_cast<std::chrono::seconds>(ends - Clock::now()).count();
            if (left < 1) {
                end = End::Done;
                break;
            }
            // The capture listens on the listening interface, not the stick.
            if (!capture.start(std::string(model::kSurveyListenInterface), static_cast<int32_t>(left), run.error,
                               run.reason, CaptureCommand::Survey)) {
                end = End::Failed;
                break;
            }
            CaptureReader reader;
            reader.expect_link(kPcapLinkRadiotap);
            bool closed = false;
            const auto on_frame = [&counter, &started](const uint8_t* frame, std::size_t size, uint32_t seconds) {
                const auto at = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
                counter.note_frame(frame, size, seconds, at.count());
            };
            while (end == End::Running && !closed) {
                if (stopping()) {
                    end = End::Stopped;
                    break;
                }
                pollfd entry{capture.fd(), POLLIN, 0};
                ::poll(&entry, 1, kWaitSliceMs);
                for (int reads = 0; reads < kMaxReads; ++reads) {
                    const ssize_t got = ::read(capture.fd(), buffer, sizeof(buffer));
                    if (got < 0 && errno == EINTR) {
                        continue;
                    }
                    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    if (got <= 0) {
                        closed = true;
                        break;
                    }
                    if (!reader.feed(buffer, static_cast<std::size_t>(got), on_frame)) {
                        run.error = model::ToolError::Failed;
                        run.reason = "bad capture data";
                        end = End::Failed;
                        break;
                    }
                }
                if (end == End::Running && !hop.alive()) {
                    run.error = model::ToolError::Failed;
                    run.reason = "stick stopped";
                    end = End::Failed;
                }
                const Clock::time_point now = Clock::now();
                if (end == End::Running && now >= ends) {
                    end = End::Done;
                }
                if (counter.take_changed() || now - published >= kPublishEvery || end != End::Running) {
                    counter.fill(run);
                    run.elapsed_ms = static_cast<int32_t>(std::min<int64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count(),
                        static_cast<int64_t>(model::kSurveySeconds) * 1000));
                    published = now;
                    if (end == End::Running) {
                        publish(run);
                    }
                }
            }
            if (closed && end == End::Running) {
                // The capture ended: its time was up or its size limit was
                // reached (it runs again for the rest), or it failed.
                const int status = capture.finish();
                if (!capture_exit_ok(status, run.error, run.reason)) {
                    end = End::Failed;
                }
            }
        }

        // The last counts and time, whatever ended it.
        counter.fill(run);
        run.elapsed_ms = static_cast<int32_t>(std::min<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count(),
            static_cast<int64_t>(model::kSurveySeconds) * 1000));
        finish(capture, hop);
        switch (end) {
            case End::Done:
            case End::Running:
                run.state = model::ToolState::Done;
                break;
            case End::Stopped:
                run.state = model::ToolState::Stopped;
                break;
            case End::Failed:
                run.state = model::ToolState::Failed;
                break;
        }
        publish(run);
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::atomic<bool> stop_flag_{false};
    bool closed_{false};
    bool quit_{false};
    bool busy_{false};
    bool has_job_{false};
    bool recover_job_{false};
    std::string job_;
    model::SurveyRun latest_;
    bool fresh_{false};
};

#endif

} // namespace

std::unique_ptr<SurveyRunner> make_survey_runner(bool live) {
#if !USE_DESKTOP
    if (live) {
        return std::make_unique<LiveSurveyRunner>();
    }
#else
    (void)live;
#endif
    return std::make_unique<PretendSurveyRunner>();
}

} // namespace platform
