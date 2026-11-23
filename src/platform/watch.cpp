/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "watch.h"

#include "lvgl.h"

#include <algorithm>
#include <ctime>
#include <utility>

#if !USE_DESKTOP
#include <poll.h>
#include <pthread.h>
#include <sys/time.h>
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

// Copies what the detector holds into `run`.
void fill(model::WatchRun& run, const WatchDetector& detector) {
    run.frames = detector.frames();
    run.raised = detector.raised();
    run.alerts = detector.alerts();
    run.learned = detector.learned();
}

class PretendWatchRunner final : public WatchRunner {
public:
    bool start(const WatchJob& job) override {
        if (closed_ || !model::valid_interface_name(job.interface_name) ||
            run_.state == model::ToolState::Running) {
            return false;
        }
        WatchSetup setup = pretend_watch_setup();
        setup.known = job.known;
        learning_ = job.known.empty();
        if (learning_) {
            setup.learn_until_ms = static_cast<int64_t>(model::kWatchLearnSeconds) * 1000;
        }
        detector_ = std::make_unique<WatchDetector>(std::move(setup));
        next_ = 0;
        started_ms_ = lv_tick_get();
        run_ = model::WatchRun{};
        run_.state = model::ToolState::Running;
        run_.interface_name = job.interface_name;
        run_.learn_left_s = learning_ ? model::kWatchLearnSeconds : 0;
        fresh_ = true;
        return true;
    }

    void stop() override {
        if (run_.state == model::ToolState::Running) {
            advance();
            run_.state = model::ToolState::Stopped;
            run_.learn_left_s = 0;
            fresh_ = true;
        }
    }

    bool take(model::WatchRun& run) override {
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
        if (!detector_) {
            return;
        }
        const uint32_t elapsed = lv_tick_elaps(started_ms_);
        const std::vector<WatchFrame>& frames = pretend_watch_frames();
        while (next_ < frames.size() && frames[next_].at_ms <= elapsed) {
            detector_->note_frame(frames[next_].bytes.data(), frames[next_].bytes.size(), frames[next_].at_ms);
            ++next_;
        }
        model::WatchRun next = run_;
        next.elapsed_s = static_cast<int32_t>(elapsed / 1000);
        next.learn_left_s = learning_ ? std::max<int32_t>(model::kWatchLearnSeconds - next.elapsed_s, 0) : 0;
        if (detector_->take_changed()) {
            fill(next, *detector_);
        }
        if (next != run_) {
            run_ = std::move(next);
            fresh_ = true;
        }
    }

    std::unique_ptr<WatchDetector> detector_;
    std::size_t next_{0};
    uint32_t started_ms_{0};
    bool learning_{false};
    model::WatchRun run_;
    bool fresh_{false};
    bool closed_{false};
};

#if !USE_DESKTOP

using Clock = std::chrono::steady_clock;
constexpr int kWaitSliceMs = 100;
constexpr int kMaxReads = 32;
// After the helper ended normally, it runs again after this long.
constexpr auto kRestartPause = std::chrono::seconds(1);

int64_t realtime_ms() {
    timeval now{};
    gettimeofday(&now, nullptr);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
}

std::string local_clock_text(int64_t at_ms) {
    const std::time_t seconds = static_cast<std::time_t>(at_ms / 1000);
    std::tm local{};
    char text[8] = {};
    if (!localtime_r(&seconds, &local) || std::strftime(text, sizeof(text), "%H:%M", &local) == 0) {
        return {};
    }
    return text;
}

class LiveWatchRunner final : public WatchRunner {
public:
    ~LiveWatchRunner() override {
        shutdown();
    }

    bool start(const WatchJob& job) override {
        if (!model::valid_interface_name(job.interface_name)) {
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
            job_ = job;
            stop_flag_.store(false);
            latest_ = model::WatchRun{};
            latest_.state = model::ToolState::Running;
            latest_.interface_name = job.interface_name;
            latest_.learn_left_s = job.known.empty() ? model::kWatchLearnSeconds : 0;
            fresh_ = true;
        }
        wake_.notify_all();
        return true;
    }

    void stop() override {
        stop_flag_.store(true);
    }

    bool take(model::WatchRun& run) override {
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
            thread_ = std::thread(&LiveWatchRunner::run, this);
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

    void publish(const model::WatchRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_) {
            latest_ = run;
            fresh_ = true;
        }
    }

    void run() {
        while (true) {
            WatchJob job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_job_; });
                if (quit_) {
                    if (has_job_) {
                        latest_.state = model::ToolState::Stopped;
                        latest_.learn_left_s = 0;
                        fresh_ = true;
                        has_job_ = false;
                    }
                    busy_ = false;
                    return;
                }
                job = job_;
                has_job_ = false;
            }
            try {
                run_watch(job);
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                latest_.state = model::ToolState::Failed;
                latest_.error = model::ToolError::Failed;
                latest_.reason = "internal error";
                latest_.learn_left_s = 0;
                fresh_ = true;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }

    // Watches `job.interface_name` until stopped, running the helper again
    // each time it ends normally. Sends nothing.
    void run_watch(const WatchJob& job) {
        model::WatchRun run;
        run.state = model::ToolState::Running;
        run.interface_name = job.interface_name;

        WatchSetup setup;
        setup.own_ports = read_own_ports();
        setup.own_address = parse_ipv4(job.own_address);
        setup.gateway = parse_ipv4(job.gateway_address);
        if (setup.gateway != 0) {
            setup.gateway_hardware = read_neighbour_hardware(job.interface_name, setup.gateway);
        }
        setup.ipv6_routers = read_ipv6_routers(job.interface_name);
        setup.known = job.known;
        const bool learning = job.known.empty();
        if (learning) {
            setup.learn_until_ms = realtime_ms() + static_cast<int64_t>(model::kWatchLearnSeconds) * 1000;
        }
        setup.clock_text = local_clock_text;
        WatchDetector detector(std::move(setup));

        const Clock::time_point begin = Clock::now();
        const auto update = [&run, &detector, &begin, learning]() {
            const auto elapsed =
                static_cast<int32_t>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - begin).count());
            run.elapsed_s = elapsed;
            run.learn_left_s = learning ? std::max<int32_t>(model::kWatchLearnSeconds - elapsed, 0) : 0;
            if (detector.take_changed()) {
                fill(run, detector);
            }
        };

        uint8_t buffer[4096];
        while (true) {
            CaptureProcess capture;
            if (!capture.start(job.interface_name, model::kWatchHelperSeconds, run.error, run.reason,
                               CaptureCommand::Watch)) {
                run.state = model::ToolState::Failed;
                run.learn_left_s = 0;
                publish(run);
                return;
            }
            CaptureReader reader;
            const Clock::time_point give_up = Clock::now() + std::chrono::seconds(model::kWatchHelperSeconds + 5);
            enum class End { Running, Stopped, Closed, Broken, Overdue };
            End end = End::Running;
            const auto on_frame = [&detector](const uint8_t* frame, std::size_t size, uint32_t seconds) {
                detector.note_frame(frame, size, static_cast<int64_t>(seconds) * 1000);
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
                update();
                publish(run);
            }

            switch (end) {
                case End::Stopped:
                    capture.stop();
                    update();
                    run.state = model::ToolState::Stopped;
                    run.learn_left_s = 0;
                    publish(run);
                    return;
                case End::Broken:
                    capture.stop();
                    run.state = model::ToolState::Failed;
                    run.error = model::ToolError::Failed;
                    run.reason = "bad capture data";
                    run.learn_left_s = 0;
                    publish(run);
                    return;
                case End::Overdue:
                    capture.stop();
                    break;
                case End::Closed:
                case End::Running: {
                    const int status = capture.finish();
                    if (!capture_exit_ok(status, run.error, run.reason)) {
                        run.state = model::ToolState::Failed;
                        run.learn_left_s = 0;
                        publish(run);
                        return;
                    }
                    break;
                }
            }
            // Ended normally (its time or size limit): again after a pause.
            const Clock::time_point again = Clock::now() + kRestartPause;
            while (Clock::now() < again) {
                if (stopping()) {
                    update();
                    run.state = model::ToolState::Stopped;
                    run.learn_left_s = 0;
                    publish(run);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kWaitSliceMs));
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
    WatchJob job_;
    model::WatchRun latest_;
    bool fresh_{false};
};

#endif

} // namespace

std::unique_ptr<WatchRunner> make_watch_runner(bool live) {
#if !USE_DESKTOP
    if (live) {
        return std::make_unique<LiveWatchRunner>();
    }
#else
    (void)live;
#endif
    return std::make_unique<PretendWatchRunner>();
}

} // namespace platform
