/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "lan_runner.h"

#include "lvgl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#if !USE_DESKTOP
#include "network.h"
#include "sent_counter.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace platform {
namespace {

// ---------------------------------------------------------------------------
// The pretend runner: made-up runs that follow the LVGL time.

// The examples' hotspot address (see model::sample_home_data()).
constexpr const char* kPretendHotspotAddress = "10.42.0.1";
// The made-up phone that tests: host .23 on the server's own network.
std::string pretend_peer(const std::string& address) {
    const std::size_t dot = address.rfind('.');
    return dot == std::string::npos ? std::string() : address.substr(0, dot) + ".23";
}

constexpr uint32_t kPretendConnectMs = 400;
constexpr uint32_t kPretendPhaseMs = static_cast<uint32_t>(model::kLanPhaseSeconds) * 1000;

// A made-up speed for half second `index` of a phase: around `base`
// kbit/s, never quite steady.
int32_t pretend_sample(int32_t base, std::size_t index) {
    static constexpr int32_t kWobble[] = {-31, 12, 40, -8, 25, -17, 6, 33, -22, 14};
    return base + kWobble[index % std::size(kWobble)] * base / 1000;
}

// One made-up test against the pretend server.
struct PretendTest {
    uint32_t start_ms;
    bool to_peer;
    int32_t kbps;
};

constexpr PretendTest kPretendTests[] = {
    {3000, false, 87400},
    {15000, true, 83100},
};

class PretendLanRunner final : public LanRunner {
public:
    bool start_client(const std::string& target) override {
        if (closed_ || run_.state == model::ToolState::Running || !model::valid_lan_target(target)) {
            return false;
        }
        begin(model::LanMode::Client, target);
        run_.phase = model::LanPhase::Connect;
        return true;
    }

    bool start_server(const std::string& address) override {
        if (closed_ || run_.state == model::ToolState::Running || !model::is_ipv4_text(address)) {
            return false;
        }
        begin(model::LanMode::Server, address);
        run_.phase = model::LanPhase::Waiting;
        return true;
    }

    void stop() override {
        if (run_.state == model::ToolState::Running) {
            advance();
            run_.state = model::ToolState::Stopped;
            if (run_.mode == model::LanMode::Server) {
                model::lan_server_exit(run_);
            }
            fresh_ = true;
        }
    }

    bool take(model::LanRun& run) override {
        if (run_.state == model::ToolState::Running) {
            advance();
        }
        if (!fresh_) {
            return false;
        }
        fresh_ = false;
        const std::vector<model::LanPlace> places = std::move(run.places);
        run = run_;
        run.places = places;
        return true;
    }

    void shutdown() override {
        stop();
        closed_ = true;
    }

    std::string interface_address(const std::string& /*interface_name*/) override {
        return kPretendHotspotAddress;
    }

private:
    void begin(model::LanMode mode, const std::string& target) {
        run_ = model::LanRun{};
        run_.state = model::ToolState::Running;
        run_.mode = mode;
        run_.target = target;
        started_ms_ = lv_tick_get();
        next_test_ = 0;
        test_started_ = false;
        seconds_shown_ = 0;
        fresh_ = true;
    }

    void advance() {
        const uint32_t elapsed = lv_tick_elaps(started_ms_);
        model::LanRun next = run_;
        if (run_.mode == model::LanMode::Client) {
            advance_client(next, elapsed);
        } else {
            advance_server(next, elapsed);
        }
        if (next != run_) {
            run_ = std::move(next);
            fresh_ = true;
        }
    }

    // The half-second samples of one phase up to `ms` into it.
    static void fill_phase(model::LanRun& run, bool down, uint32_t ms, int32_t base) {
        model::LanRun copy = run;
        copy.state = model::ToolState::Running;
        copy.phase = down ? model::LanPhase::Download : model::LanPhase::Upload;
        std::vector<int32_t>& samples = down ? run.down_samples : run.up_samples;
        const std::size_t count =
            std::min<std::size_t>(ms / static_cast<uint32_t>(model::kLanSampleMs), model::kLanSamples);
        for (std::size_t index = samples.size(); index < count; ++index) {
            model::IperfEvent event;
            event.kind = model::IperfEventKind::Interval;
            event.seconds = model::kLanSampleMs / 1000.0;
            const int32_t kbps = pretend_sample(base, index);
            event.bps = kbps * 1000.0;
            event.bytes = static_cast<uint64_t>(kbps) * 1000 / 8 * static_cast<uint64_t>(model::kLanSampleMs) / 1000;
            model::lan_client_event(copy, event);
        }
        if (down) {
            run.down_samples = copy.down_samples;
            run.down_bytes = copy.down_bytes;
            run.down_kbps = copy.down_kbps;
        } else {
            run.up_samples = copy.up_samples;
            run.up_bytes = copy.up_bytes;
            run.up_kbps = copy.up_kbps;
        }
    }

    static void advance_client(model::LanRun& run, uint32_t elapsed) {
        if (elapsed < kPretendConnectMs) {
            run.phase = model::LanPhase::Connect;
            run.phase_ms = 0;
            return;
        }
        const uint32_t into = elapsed - kPretendConnectMs;
        fill_phase(run, true, std::min(into, kPretendPhaseMs), 94100);
        if (into < kPretendPhaseMs) {
            run.phase = model::LanPhase::Download;
            run.phase_ms = static_cast<int32_t>(into);
            return;
        }
        fill_phase(run, false, std::min(into - kPretendPhaseMs, kPretendPhaseMs), 91600);
        if (into < 2 * kPretendPhaseMs) {
            run.phase = model::LanPhase::Upload;
            run.phase_ms = static_cast<int32_t>(into - kPretendPhaseMs);
            return;
        }
        run.phase = model::LanPhase::Upload;
        run.phase_ms = static_cast<int32_t>(kPretendPhaseMs);
        run.state = model::ToolState::Done;
    }

    void advance_server(model::LanRun& run, uint32_t elapsed) {
        while (next_test_ < std::size(kPretendTests)) {
            const PretendTest& test = kPretendTests[next_test_];
            if (elapsed < test.start_ms) {
                break;
            }
            const uint32_t into = elapsed - test.start_ms;
            if (!test_started_) {
                model::IperfEvent start;
                start.kind = model::IperfEventKind::Start;
                start.peer = pretend_peer(run.target);
                start.reverse = test.to_peer;
                model::lan_server_event(run, start);
                test_started_ = true;
                seconds_shown_ = 0;
            }
            const uint32_t seconds = std::min(into / 1000, kPretendPhaseMs / 1000);
            for (; seconds_shown_ < seconds; ++seconds_shown_) {
                model::IperfEvent interval;
                interval.kind = model::IperfEventKind::Interval;
                interval.seconds = 1;
                interval.bps = pretend_sample(test.kbps, seconds_shown_) * 1000.0;
                interval.bytes = static_cast<uint64_t>(interval.bps / 8);
                model::lan_server_event(run, interval);
            }
            if (into < kPretendPhaseMs) {
                run.phase_ms = static_cast<int32_t>(into);
                return;
            }
            model::IperfEvent end;
            end.kind = model::IperfEventKind::End;
            const double bps = test.kbps * 1000.0;
            const auto bytes = static_cast<uint64_t>(bps / 8 * model::kLanPhaseSeconds);
            (test.to_peer ? end.sent_bps : end.received_bps) = bps;
            (test.to_peer ? end.sent_bytes : end.received_bytes) = bytes;
            end.has_sent = end.has_received = true;
            model::lan_server_event(run, end);
            test_started_ = false;
            ++next_test_;
        }
    }

    model::LanRun run_;
    uint32_t started_ms_{0};
    std::size_t next_test_{0};
    bool test_started_{false};
    uint32_t seconds_shown_{0};
    bool fresh_{false};
    bool closed_{false};
};

} // namespace

#if !USE_DESKTOP

namespace {

constexpr auto kWaitSlice = std::chrono::milliseconds(100);
constexpr int kWaitSliceMs = 100;
// iperf3's own wait for the other end's answer.
constexpr int32_t kConnectTimeoutMs = 3000;
// After a test the server starts again at once; after a connection that
// was not a test (a port scan, say), after a second.
constexpr auto kServerRestartQuick = std::chrono::milliseconds(200);
constexpr auto kServerRestartSlow = std::chrono::milliseconds(1000);
// A client phase that runs on past its time plus this is ended.
constexpr auto kPhaseGrace = std::chrono::seconds(8);

// One iperf3 child with its output pipe.
class IperfChild {
public:
    IperfChild() = default;
    ~IperfChild() { stop(); }
    IperfChild(const IperfChild&) = delete;
    IperfChild& operator=(const IperfChild&) = delete;

    // Starts `/usr/bin/iperf3 <arguments>`. Called on the runner's own
    // thread, which lives until shutdown: the child is killed when that
    // thread ends (PR_SET_PDEATHSIG), so also when LokiCTL is killed.
    bool spawn(const std::vector<std::string>& arguments) {
        if (pid_ > 0) {
            return false;
        }
        // Everything the child needs is made before fork(): after it, the
        // child only makes system calls.
        std::vector<std::string> words;
        words.reserve(arguments.size() + 1);
        words.emplace_back("iperf3");
        words.insert(words.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        argv.reserve(words.size() + 1);
        for (std::string& word : words) {
            argv.push_back(word.data());
        }
        argv.push_back(nullptr);
        char* environment[] = {nullptr};

        int ends[2] = {-1, -1};
        if (pipe2(ends, O_CLOEXEC) != 0) {
            return false;
        }
        const int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null_fd < 0) {
            close(ends[0]);
            close(ends[1]);
            return false;
        }
        const pid_t parent = getpid();
        // No signal handler of LokiCTL's may run in the child.
        sigset_t all_signals;
        sigset_t old_mask;
        sigfillset(&all_signals);
        pthread_sigmask(SIG_SETMASK, &all_signals, &old_mask);
        const pid_t child = fork();
        if (child == 0) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent) {
                _exit(127);
            }
            struct sigaction plain {};
            plain.sa_handler = SIG_DFL;
            for (const int signal_number : {SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGCHLD, SIGQUIT}) {
                sigaction(signal_number, &plain, nullptr);
            }
            if (dup2(null_fd, 0) < 0 || dup2(ends[1], 1) < 0 || dup2(null_fd, 2) < 0) {
                _exit(127);
            }
            // No other file of LokiCTL's reaches iperf3.
#ifdef SYS_close_range
            if (syscall(SYS_close_range, 3U, ~0U, 0U) != 0)
#endif
            {
                for (int fd = 3; fd < 4096; ++fd) {
                    close(fd);
                }
            }
            sigset_t no_signals;
            sigemptyset(&no_signals);
            sigprocmask(SIG_SETMASK, &no_signals, nullptr);
            execve(kIperfPath, argv.data(), environment);
            _exit(127);
        }
        pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        close(ends[1]);
        close(null_fd);
        if (child < 0) {
            close(ends[0]);
            return false;
        }
        fcntl(ends[0], F_SETFL, fcntl(ends[0], F_GETFL) | O_NONBLOCK);
        fd_ = ends[0];
        pid_ = child;
        return true;
    }

    // Waits up to 100 ms for output and hands over what came: false once
    // the output has ended (the child closed it or is gone).
    bool read(std::string& bytes) {
        bytes.clear();
        if (fd_ < 0) {
            return false;
        }
        pollfd entry{fd_, POLLIN, 0};
        const int ready = ::poll(&entry, 1, kWaitSliceMs);
        if (ready <= 0) {
            return ready == 0 || errno == EINTR;
        }
        char buffer[4096];
        for (int round = 0; round < 16; ++round) {
            const ssize_t got = ::read(fd_, buffer, sizeof(buffer));
            if (got > 0) {
                bytes.append(buffer, static_cast<std::size_t>(got));
                continue;
            }
            if (got == 0) {
                return !bytes.empty();  // ended; what came is still read
            }
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        return true;
    }

    // Ends the child at once: SIGTERM, then SIGKILL after 300 ms.
    void stop() {
        close_pipe();
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            reap(300);
        }
    }

    // The output ended: waits up to a second for the child to exit (then
    // kills it). Returns its exit status, or -1.
    int finish() {
        close_pipe();
        return reap(1000);
    }

private:
    void close_pipe() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    int reap(int timeout_ms) {
        if (pid_ <= 0) {
            return -1;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        int status = 0;
        for (;;) {
            const pid_t done = waitpid(pid_, &status, WNOHANG);
            if (done == pid_) {
                break;
            }
            if (done < 0 && errno != EINTR) {
                pid_ = -1;
                return -1;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(pid_, SIGKILL);
                while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
                }
                pid_ = -1;
                return -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    pid_t pid_{-1};
    int fd_{-1};
};

class LiveLanRunner final : public LanRunner {
public:
    ~LiveLanRunner() override { shutdown(); }

    bool start_client(const std::string& target) override {
        if (!model::valid_lan_target(target)) {
            return false;
        }
        return begin(model::LanMode::Client, target);
    }

    bool start_server(const std::string& address) override {
        if (!model::is_ipv4_text(address)) {
            return false;
        }
        return begin(model::LanMode::Server, address);
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!waiting_ && !busy_) {
            return;
        }
        stop_asked_ = true;
        waiting_ = false;
        if (run_.state == model::ToolState::Running) {
            run_.state = model::ToolState::Stopped;
            if (run_.mode == model::LanMode::Server) {
                model::lan_server_exit(run_);
            }
            fresh_ = true;
        }
        wake_.notify_all();
    }

    bool take(model::LanRun& run) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!fresh_) {
            return false;
        }
        fresh_ = false;
        std::vector<model::LanPlace> places = std::move(run.places);
        run = run_;
        run.places = std::move(places);
        return true;
    }

    void shutdown() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ && !thread_.joinable()) {
                return;
            }
            closed_ = true;
            stop_asked_ = true;
            waiting_ = false;
            if (run_.state == model::ToolState::Running) {
                run_.state = model::ToolState::Stopped;
                fresh_ = true;
            }
            wake_.notify_all();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string interface_address(const std::string& interface_name) override {
        return ::platform::interface_address({}, interface_name);
    }

private:
    bool begin(model::LanMode mode, const std::string& target) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || waiting_ || busy_) {
            return false;
        }
        if (!thread_.joinable() && !start_thread()) {
            return false;
        }
        run_ = model::LanRun{};
        run_.state = model::ToolState::Running;
        run_.mode = mode;
        run_.target = target;
        run_.phase = mode == model::LanMode::Client ? model::LanPhase::Connect : model::LanPhase::Waiting;
        fresh_ = true;
        job_mode_ = mode;
        job_target_ = target;
        stop_asked_ = false;
        waiting_ = true;
        wake_.notify_all();
        return true;
    }

    bool start_thread() {
        // SIGTERM and SIGINT stay with the main thread.
        sigset_t stop_signals;
        sigset_t old_mask;
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGTERM);
        sigaddset(&stop_signals, SIGINT);
        const bool masked = pthread_sigmask(SIG_BLOCK, &stop_signals, &old_mask) == 0;
        try {
            thread_ = std::thread(&LiveLanRunner::thread_main, this);
        } catch (...) {
            // No thread: the run is refused.
        }
        if (masked) {
            pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        }
        return thread_.joinable();
    }

    void thread_main() {
        for (;;) {
            model::LanMode mode;
            std::string target;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return closed_ || waiting_; });
                if (closed_) {
                    return;
                }
                waiting_ = false;
                busy_ = true;
                mode = job_mode_;
                target = job_target_;
            }
            if (geteuid() == 0) {
                fail("Won't run as root");
            } else if (access(kIperfPath, X_OK) != 0) {
                fail("Needs the iperf3 package");
            } else if (mode == model::LanMode::Client) {
                run_client(target);
            } else {
                run_server(target);
            }
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }

    bool stop_asked() {
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_asked_;
    }

    // Changes the run while it still runs (a stop has the last word).
    template <typename Change>
    void update(Change change) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run_.state != model::ToolState::Running) {
            return;
        }
        model::LanRun next = run_;
        change(next);
        if (next != run_) {
            run_ = std::move(next);
            fresh_ = true;
        }
    }

    void fail(const std::string& reason) {
        update([&reason](model::LanRun& run) {
            run.state = model::ToolState::Failed;
            run.reason = reason;
        });
    }

    bool running() {
        std::lock_guard<std::mutex> lock(mutex_);
        return run_.state == model::ToolState::Running && !stop_asked_;
    }

    void run_client(const std::string& target) {
        for (const model::LanPhase phase : {model::LanPhase::Download, model::LanPhase::Upload}) {
            if (!running()) {
                return;
            }
            std::vector<std::string> arguments = {"-c", target, "-4", "-p", std::to_string(model::kLanPort),
                                                  "-t", std::to_string(model::kLanPhaseSeconds),
                                                  "-i", "0.5", "--connect-timeout",
                                                  std::to_string(kConnectTimeoutMs), "--json-stream"};
            if (phase == model::LanPhase::Download) {
                arguments.emplace_back("-R");  // the other end sends
            }
            update([phase](model::LanRun& run) {
                run.phase = phase;
                run.phase_ms = 0;
            });
            IperfChild child;
            if (!child.spawn(arguments)) {
                fail("Can't start iperf3");
                return;
            }
            count_sent(1);
            model::IperfLines lines;
            std::vector<model::IperfEvent> events;
            std::string bytes;
            bool got_end = false;
            const auto started = std::chrono::steady_clock::now();
            const auto limit = started + std::chrono::seconds(model::kLanPhaseSeconds) +
                               std::chrono::milliseconds(kConnectTimeoutMs) + kPhaseGrace;
            bool open = true;
            while (open) {
                open = child.read(bytes);
                events.clear();
                lines.add(bytes, events);
                const auto now = std::chrono::steady_clock::now();
                const auto into = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
                update([&events, &got_end, into](model::LanRun& run) {
                    for (const model::IperfEvent& event : events) {
                        model::lan_client_event(run, event);
                        got_end = got_end || event.kind == model::IperfEventKind::End;
                    }
                    run.phase_ms = static_cast<int32_t>(
                        std::min<int64_t>(into, static_cast<int64_t>(model::kLanPhaseSeconds) * 1000));
                });
                if (!running()) {
                    child.stop();  // stopped, or iperf3 said why it failed
                    return;
                }
                if (now > limit) {
                    child.stop();
                    fail("The test took too long");
                    return;
                }
            }
            const int status = child.finish();
            if (!got_end) {
                fail(status == 127 ? "Can't start iperf3" : "The test was cut off");
                return;
            }
        }
        update([](model::LanRun& run) { run.state = model::ToolState::Done; });
    }

    void run_server(const std::string& address) {
        const std::vector<std::string> arguments = {"-s", "-4", "-B", address, "-p", std::to_string(model::kLanPort),
                                                    "-1", "--json-stream"};
        while (running()) {
            IperfChild child;
            if (!child.spawn(arguments)) {
                fail("Can't start iperf3");
                return;
            }
            model::IperfLines lines;
            std::vector<model::IperfEvent> events;
            std::string bytes;
            bool tested = false;
            std::string cannot_listen;
            auto test_started = std::chrono::steady_clock::now();
            bool open = true;
            while (open) {
                open = child.read(bytes);
                events.clear();
                lines.add(bytes, events);
                const auto now = std::chrono::steady_clock::now();
                for (const model::IperfEvent& event : events) {
                    if (event.kind == model::IperfEventKind::Start) {
                        tested = true;
                        test_started = now;
                        count_sent(1);
                    } else if (event.kind == model::IperfEventKind::Error && !tested &&
                               model::lan_server_cannot_listen(event.error)) {
                        cannot_listen = event.error;
                    }
                }
                const auto into = std::chrono::duration_cast<std::chrono::milliseconds>(now - test_started).count();
                update([&events, into](model::LanRun& run) {
                    for (const model::IperfEvent& event : events) {
                        model::lan_server_event(run, event);
                    }
                    run.phase_ms = run.phase == model::LanPhase::Testing ? static_cast<int32_t>(into) : 0;
                });
                if (!cannot_listen.empty()) {
                    child.stop();
                    fail(model::lan_error_words(cannot_listen, model::LanMode::Server));
                    return;
                }
                if (!running()) {
                    child.stop();
                    return;
                }
                if (tested && now - test_started > std::chrono::seconds(model::kLanServeTestMaxSeconds)) {
                    child.stop();  // a test that runs on and on
                    break;
                }
            }
            const int status = child.finish();
            update([](model::LanRun& run) { model::lan_server_exit(run); });
            if (status == 127 && !tested) {
                fail("Can't start iperf3");
                return;
            }
            // Waits before listening again, in short steps.
            const auto restart = std::chrono::steady_clock::now() + (tested ? kServerRestartQuick : kServerRestartSlow);
            while (running() && std::chrono::steady_clock::now() < restart) {
                std::this_thread::sleep_for(kWaitSlice);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    model::LanRun run_;
    bool fresh_{false};
    bool waiting_{false};    // a job waits for the thread
    bool busy_{false};       // the thread works on a job
    bool stop_asked_{false};
    bool closed_{false};
    model::LanMode job_mode_{model::LanMode::Client};
    std::string job_target_;
};

} // namespace

#endif

std::unique_ptr<LanRunner> make_lan_runner(bool live) {
#if !USE_DESKTOP
    if (live) {
        return std::make_unique<LiveLanRunner>();
    }
#else
    (void)live;
#endif
    return std::make_unique<PretendLanRunner>();
}

} // namespace platform
