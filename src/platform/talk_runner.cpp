/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The runners of talk.h: the live one (device builds) and the pretend one.
// Reading the frames is in talk.cpp, which the checks build without LVGL.

#include "talk.h"

#include "lvgl.h"

#include <algorithm>
#include <utility>

#if !USE_DESKTOP
#include "listen.h"

#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <thread>
#endif

namespace platform {

// ---------------------------------------------------------------------------
// The runners

namespace {

class PretendTalkRunner final : public TalkRunner {
public:
    bool start(const std::string& interface_name) override {
        if (closed_ || !model::valid_interface_name(interface_name) || run_.state == model::ToolState::Running) {
            return false;
        }
        counter_ = std::make_unique<TalkCounter>();
        next_ = 0;
        started_ms_ = lv_tick_get();
        run_ = model::TalkRun{};
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

    bool take(model::TalkRun& run) override {
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
        if (!counter_) {
            return;
        }
        const uint32_t elapsed = lv_tick_elaps(started_ms_);
        const std::vector<TalkFrame>& frames = pretend_talk_frames();
        while (next_ < frames.size() && frames[next_].at_ms <= elapsed) {
            counter_->note_frame(frames[next_].bytes.data(), frames[next_].bytes.size(), frames[next_].at_ms);
            ++next_;
        }
        if (counter_->take_changed()) {
            counter_->fill(run_);
            fresh_ = true;
        }
    }

    std::unique_ptr<TalkCounter> counter_;
    std::size_t next_{0};
    uint32_t started_ms_{0};
    model::TalkRun run_;
    bool fresh_{false};
    bool closed_{false};
};

#if !USE_DESKTOP

using Clock = std::chrono::steady_clock;
constexpr int kWaitSliceMs = 100;
constexpr int kMaxReads = 32;
// After the helper ended normally, it runs again after this long.
constexpr auto kRestartPause = std::chrono::seconds(1);
// While the interface has no address yet or has gone, it tries again after
// this long.
constexpr auto kRetryPause = std::chrono::seconds(3);

class LiveTalkRunner final : public TalkRunner {
public:
    ~LiveTalkRunner() override {
        shutdown();
    }

    bool start(const std::string& interface_name) override {
        if (!model::valid_interface_name(interface_name)) {
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
            job_ = interface_name;
            stop_flag_.store(false);
            latest_ = model::TalkRun{};
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

    bool take(model::TalkRun& run) override {
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
    bool start_thread() {
        // The thread never takes the app's stop signals.
        sigset_t stop_signals;
        sigset_t old_mask;
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGTERM);
        sigaddset(&stop_signals, SIGINT);
        const bool masked = pthread_sigmask(SIG_BLOCK, &stop_signals, &old_mask) == 0;
        try {
            thread_ = std::thread(&LiveTalkRunner::run, this);
        } catch (...) {
            // No thread: the run is refused.
        }
        if (masked) {
            pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        }
        return thread_.joinable();
    }

    bool stopping() const {
        return stop_flag_.load();
    }

    void publish(const model::TalkRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_) {
            latest_ = run;
            fresh_ = true;
        }
    }

    void run() {
        while (true) {
            std::string interface_name;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_job_; });
                if (quit_) {
                    if (has_job_) {
                        latest_.state = model::ToolState::Stopped;
                        fresh_ = true;
                        has_job_ = false;
                    }
                    busy_ = false;
                    return;
                }
                interface_name = job_;
                has_job_ = false;
            }
            try {
                listen(interface_name);
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_.state = model::ToolState::Failed;
                latest_.error = model::ToolError::Failed;
                latest_.reason = "internal error";
                fresh_ = true;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }

    // Waits `pause`, in slices; false when a stop came first.
    bool wait(Clock::duration pause) {
        const Clock::time_point until = Clock::now() + pause;
        while (Clock::now() < until) {
            if (stopping()) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kWaitSliceMs));
        }
        return !stopping();
    }

    // Listens on `interface_name` until stopped, running the helper again
    // each time it ends normally, or after a pause while the interface is
    // not usable yet. Sends nothing.
    void listen(const std::string& interface_name) {
        model::TalkRun run;
        run.state = model::ToolState::Running;
        run.interface_name = interface_name;
        TalkCounter counter;

        uint8_t buffer[4096];
        while (true) {
            CaptureProcess capture;
            if (!capture.start(interface_name, model::kTalkHelperSeconds, run.error, run.reason,
                               CaptureCommand::Names)) {
                run.state = model::ToolState::Failed;
                publish(run);
                return;
            }
            // Never keep(): nothing of this stream is saved.
            CaptureReader reader;
            const Clock::time_point give_up = Clock::now() + std::chrono::seconds(model::kTalkHelperSeconds + 5);
            enum class End { Running, Stopped, Closed, Broken, Overdue };
            End end = End::Running;
            const auto on_frame = [&counter](const uint8_t* frame, std::size_t size, uint32_t seconds) {
                counter.note_frame(frame, size, static_cast<int64_t>(seconds) * 1000);
            };
            while (end == End::Running) {
                if (stopping()) {
                    end = End::Stopped;
                    break;
                }
                pollfd entry{capture.fd(), POLLIN, 0};
                ::poll(&entry, 1, kWaitSliceMs);
                for (int reads = 0; reads < kMaxReads && end == End::Running; ++reads) {
                    const ssize_t got = ::read(capture.fd(), buffer, sizeof(buffer));
                    if (got < 0 && errno == EINTR) {
                        continue;
                    }
                    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    if (got <= 0) {
                        end = End::Closed;
                    } else if (!reader.feed(buffer, static_cast<std::size_t>(got), on_frame)) {
                        end = End::Broken;
                    }
                }
                if (end == End::Running && Clock::now() >= give_up) {
                    end = End::Overdue;
                }
                if (counter.take_changed()) {
                    counter.fill(run);
                    publish(run);
                }
            }

            auto pause = kRestartPause;
            switch (end) {
                case End::Stopped:
                    capture.stop();
                    run.state = model::ToolState::Stopped;
                    publish(run);
                    return;
                case End::Broken:
                    capture.stop();
                    run.state = model::ToolState::Failed;
                    run.error = model::ToolError::Failed;
                    run.reason = "bad capture data";
                    publish(run);
                    return;
                case End::Overdue:
                    capture.stop();
                    break;
                case End::Closed:
                case End::Running: {
                    const int status = capture.finish();
                    // 2: the interface has no IPv4 address yet; 4: it went
                    // away (Share's own interface, while Share stops). The
                    // run waits for it.
                    if (status == 2 || status == 4) {
                        pause = kRetryPause;
                        break;
                    }
                    if (!capture_exit_ok(status, run.error, run.reason)) {
                        run.state = model::ToolState::Failed;
                        publish(run);
                        return;
                    }
                    break;
                }
            }
            if (!wait(pause)) {
                run.state = model::ToolState::Stopped;
                publish(run);
                return;
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::atomic<bool> stop_flag_{false};
    bool closed_{false};
    bool quit_{false};
    bool busy_{false};
    bool has_job_{false};
    std::string job_;
    model::TalkRun latest_;
    bool fresh_{false};
};

#endif

} // namespace

std::unique_ptr<TalkRunner> make_talk_runner(bool live) {
#if !USE_DESKTOP
    if (live) {
        return std::make_unique<LiveTalkRunner>();
    }
#else
    (void)live;
#endif
    return std::make_unique<PretendTalkRunner>();
}

} // namespace platform
