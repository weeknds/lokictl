/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "trap.h"

#include "lvgl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#if !USE_DESKTOP
#include "firewall.h"
#include "sent_counter.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <ctime>
#include <mutex>
#include <thread>
#endif

namespace platform {

std::vector<uint16_t> read_listening_ports(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return model::parse_listening_ports(text);
}

namespace {

// ---------------------------------------------------------------------------
// The pretend runner

struct PretendHit {
    uint32_t at_ms;
    const char* time;
    const char* address;
    uint16_t source_port;
    uint16_t port;
    const char* sent;
    std::size_t sent_size;  // 0: up to the first '\0'
};

// Made-up connections (documentation and private addresses); the third has
// the longest address and word a row can show.
constexpr char kPretendSmb[] = "\x00\x00\x00\x54\xffSMBr\x00\x00\x00\x00\x18\x53\xc8\x00\x02NT LM 0.12";
const PretendHit kPretendHits[] = {
    {1500, "13:05", "192.0.2.7", 51514, 80, "GET / HTTP/1.1\r\nHost: 192.0.2.10\r\nUser-Agent: curl/8.5.0\r\n\r\n", 0},
    {3000, "13:05", "198.51.100.23", 40112, 23, "root\r\n", 0},
    {4500, "13:06", "192.168.100.234", 49822, 445, kPretendSmb, sizeof(kPretendSmb) - 1},
    {6000, "13:07", "203.0.113.5", 33000, 21, "USER anonymous\r\n", 0},
};

class PretendTrapRunner final : public TrapRunner {
public:
    bool start() override {
        if (closed_ || run_.state == model::ToolState::Running) {
            return false;
        }
        started_ms_ = lv_tick_get();
        next_ = 0;
        run_ = model::TrapRun{};
        run_.state = model::ToolState::Running;
        for (const model::TrapService& service : model::trap_services()) {
            // A web server already runs on 8080 here.
            run_.ports.push_back({service.port,
                                  service.port == 8080 ? model::TrapPortState::InUse : model::TrapPortState::Open});
        }
        fresh_ = true;
        return true;
    }

    void stop() override {
        if (run_.state == model::ToolState::Running) {
            advance();
            run_.state = model::ToolState::Stopped;
            for (model::TrapPort& port : run_.ports) {
                port.state = model::TrapPortState::Off;
            }
            fresh_ = true;
        }
    }

    bool take(model::TrapRun& run) override {
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
        const uint32_t elapsed = lv_tick_elaps(started_ms_);
        model::TrapRun next = run_;
        next.elapsed_s = static_cast<int32_t>(elapsed / 1000);
        while (next_ < std::size(kPretendHits) && kPretendHits[next_].at_ms <= elapsed) {
            const PretendHit& made_up = kPretendHits[next_];
            model::TrapHit hit;
            hit.time = made_up.time;
            hit.address = made_up.address;
            hit.source_port = made_up.source_port;
            hit.port = made_up.port;
            const std::size_t size = made_up.sent_size > 0 ? made_up.sent_size : std::strlen(made_up.sent);
            hit.first_bytes = model::trap_shown_text(reinterpret_cast<const uint8_t*>(made_up.sent), size);
            next.hits.insert(next.hits.begin(), std::move(hit));
            ++next.raised;
            ++next_;
        }
        if (next != run_) {
            run_ = std::move(next);
            fresh_ = true;
        }
    }

    std::size_t next_{0};
    uint32_t started_ms_{0};
    model::TrapRun run_;
    bool fresh_{false};
    bool closed_{false};
};

#if !USE_DESKTOP

// ---------------------------------------------------------------------------
// The live runner

constexpr int kWaitSliceMs = 100;
constexpr int kListenBacklog = 8;
// The most connections read at once; more are closed at once.
constexpr std::size_t kMaxOpen = 8;

constexpr char kFtpGreeting[] = "220 FTP server ready\r\n";
constexpr char kTelnetGreeting[] = "\r\nlogin: ";
constexpr char kWebPage[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><head><title>Login</title></head><body><p>Please log in.</p></body>\n"
    "</html>";

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string local_clock_text() {
    const std::time_t seconds = std::time(nullptr);
    std::tm local{};
    char text[8] = "";
    if (!localtime_r(&seconds, &local) || std::strftime(text, sizeof(text), "%H:%M", &local) == 0) {
        return "--:--";
    }
    return text;
}

bool low_port(const model::TrapService& service) {
    return service.port != service.listen_port;
}

// A non-blocking IPv4 socket listening on `port` on every address; -1 when
// that fails (something else holds the port).
int open_listener(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(fd, kListenBacklog) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// Sends `text` without waiting (a reply that does not fit is cut); counts
// one packet.
void send_reply(int fd, const char* text, std::size_t size) {
    if (send(fd, text, size, MSG_NOSIGNAL | MSG_DONTWAIT) > 0) {
        count_sent(1);
    }
}

std::vector<uint16_t> listening_now() {
    std::vector<uint16_t> ports = read_listening_ports("/proc/net/tcp");
    const std::vector<uint16_t> six = read_listening_ports("/proc/net/tcp6");
    ports.insert(ports.end(), six.begin(), six.end());
    return ports;
}

bool contains(const std::vector<uint16_t>& ports, uint16_t port) {
    return std::find(ports.begin(), ports.end(), port) != ports.end();
}

class LiveTrapRunner final : public TrapRunner {
public:
    ~LiveTrapRunner() override {
        shutdown();
    }

    bool start() override {
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
            stop_flag_.store(false);
            latest_ = model::TrapRun{};
            latest_.state = model::ToolState::Running;
            for (const model::TrapService& service : model::trap_services()) {
                latest_.ports.push_back({service.port, model::TrapPortState::Off});
            }
            fresh_ = true;
        }
        wake_.notify_all();
        return true;
    }

    void stop() override {
        stop_flag_.store(true);
    }

    bool take(model::TrapRun& run) override {
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
    struct Listener {
        const model::TrapService* service{nullptr};
        int fd{-1};
    };
    struct Open {
        const model::TrapService* service{nullptr};
        int fd{-1};
        uint32_t hit_id{0};
        int64_t deadline_ms{0};
        std::vector<uint8_t> bytes;
        bool replied{false};
    };

    bool start_thread() {
        // The thread never takes the app's stop signals.
        sigset_t stop_signals;
        sigset_t old_mask;
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGTERM);
        sigaddset(&stop_signals, SIGINT);
        const bool masked = pthread_sigmask(SIG_BLOCK, &stop_signals, &old_mask) == 0;
        try {
            thread_ = std::thread(&LiveTrapRunner::thread_main, this);
        } catch (...) {
            // No thread: the run is refused.
        }
        if (masked) {
            pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        }
        return thread_.joinable();
    }

    void publish() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run_ != latest_) {
            latest_ = run_;
            fresh_ = true;
        }
    }

    void thread_main() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_job_; });
                if (quit_) {
                    busy_ = false;
                    return;
                }
                has_job_ = false;
                run_ = latest_;
            }
            run_trap();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
            }
        }
    }

    model::TrapPort& port_state(const model::TrapService& service) {
        for (model::TrapPort& port : run_.ports) {
            if (port.port == service.port) {
                return port;
            }
        }
        run_.ports.push_back({service.port, model::TrapPortState::Off});
        return run_.ports.back();
    }

    void close_listener(Listener& listener, model::TrapPortState state) {
        if (listener.fd >= 0) {
            close(listener.fd);
            listener.fd = -1;
        }
        port_state(*listener.service).state = state;
    }

    // Starts the helper's trap-on (which runs until trap_off()) for the low
    // ports listened on, and closes those it did not send on. Returns how
    // many low ports are sent on.
    int redirect() {
        bool any = false;
        for (const Listener& listener : listeners_) {
            any = any || (listener.fd >= 0 && low_port(*listener.service));
        }
        if (!any) {
            return 0;
        }
        std::vector<uint16_t> sent_on;
        const std::string reason = trap_on(sent_on);
        run_.helper_problem = reason;
        int count = 0;
        for (Listener& listener : listeners_) {
            if (listener.fd < 0 || !low_port(*listener.service)) {
                continue;
            }
            if (contains(sent_on, listener.service->port)) {
                ++count;
            } else {
                close_listener(listener,
                               reason.empty() ? model::TrapPortState::InUse : model::TrapPortState::NoHelper);
            }
        }
        return count;
    }

    void run_trap() {
        const int64_t started = now_ms();
        const std::vector<uint16_t> busy = listening_now();
        listeners_.clear();
        for (const model::TrapService& service : model::trap_services()) {
            Listener listener;
            listener.service = &service;
            if (!contains(busy, service.port)) {
                listener.fd = open_listener(service.listen_port);
            }
            port_state(service).state =
                listener.fd >= 0 ? model::TrapPortState::Open : model::TrapPortState::InUse;
            listeners_.push_back(listener);
        }
        int redirected = redirect();
        const auto open_count = model::trap_open_ports(run_);
        if (open_count == 0) {
            run_.state = model::ToolState::Failed;
            run_.error = model::ToolError::Failed;
            run_.reason = run_.helper_problem.empty() ? "every port is in use" : run_.helper_problem;
            if (redirected > 0) {
                trap_off();
            }
            publish();
            return;
        }
        publish();

        limiter_ = model::TrapLimiter{};
        open_.clear();
        hit_ids_.clear();
        while (!stop_flag_.load()) {
            std::vector<pollfd> fds;
            for (const Listener& listener : listeners_) {
                if (listener.fd >= 0) {
                    fds.push_back({listener.fd, POLLIN, 0});
                }
            }
            for (const Open& open : open_) {
                fds.push_back({open.fd, POLLIN, 0});
            }
            const int ready = poll(fds.data(), fds.size(), kWaitSliceMs);
            if (ready < 0 && errno != EINTR) {
                break;
            }
            const int64_t now = now_ms();
            for (const pollfd& entry : fds) {
                if (entry.revents == 0) {
                    continue;
                }
                if (Listener* listener = find_listener(entry.fd)) {
                    accept_all(*listener, now);
                } else if (Open* open = find_open(entry.fd)) {
                    read_some(*open);
                }
            }
            for (Open& open : open_) {
                if (open.fd >= 0 && now >= open.deadline_ms) {
                    finish(open);
                }
            }
            open_.erase(std::remove_if(open_.begin(), open_.end(), [](const Open& open) { return open.fd < 0; }),
                        open_.end());
            if (redirected > 0) {
                redirected = follow_redirect(redirected);
            }
            run_.elapsed_s = static_cast<int32_t>((now - started) / 1000);
            publish();
        }

        for (Open& open : open_) {
            finish(open);
        }
        open_.clear();
        for (Listener& listener : listeners_) {
            close_listener(listener, model::TrapPortState::Off);
        }
        listeners_.clear();
        if (redirected > 0) {
            // The normal time limit also on the way out: the redirect must
            // not outlive LokiCTL.
            run_.helper_problem = trap_off(kHelperTimeLimit);
        }
        run_.state = model::ToolState::Stopped;
        run_.elapsed_s = static_cast<int32_t>((now_ms() - started) / 1000);
        publish();
    }

    // The helper's trap-on checks every few seconds that nothing else
    // began to listen on a low port it sends on, and drops such a port;
    // when no port is left, or it failed, it ends. A low port it no longer
    // sends on is closed here too. Returns how many it still sends on.
    int follow_redirect(int redirected) {
        std::vector<uint16_t> sent_on;
        std::string problem;
        if (!trap_changed(sent_on, problem)) {
            return redirected;
        }
        if (!problem.empty()) {
            run_.helper_problem = problem;
        }
        int count = 0;
        for (Listener& listener : listeners_) {
            if (listener.fd < 0 || !low_port(*listener.service)) {
                continue;
            }
            if (contains(sent_on, listener.service->port)) {
                ++count;
            } else {
                close_listener(listener,
                               problem.empty() ? model::TrapPortState::InUse : model::TrapPortState::NoHelper);
            }
        }
        if (count == 0) {
            trap_off();  // ended already, or nothing of it is used
        }
        return count;
    }

    Listener* find_listener(int fd) {
        for (Listener& listener : listeners_) {
            if (listener.fd == fd) {
                return &listener;
            }
        }
        return nullptr;
    }

    Open* find_open(int fd) {
        for (Open& open : open_) {
            if (open.fd == fd) {
                return &open;
            }
        }
        return nullptr;
    }

    model::TrapHit* find_hit(uint32_t id) {
        for (std::size_t index = 0; index < hit_ids_.size(); ++index) {
            if (hit_ids_[index] == id) {
                return &run_.hits[index];
            }
        }
        return nullptr;
    }

    void accept_all(Listener& listener, int64_t now) {
        // A few at a time, so a flood never starves the rest.
        for (int round = 0; round < 8; ++round) {
            sockaddr_in peer{};
            socklen_t size = sizeof(peer);
            const int fd = accept4(listener.fd, reinterpret_cast<sockaddr*>(&peer), &size, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) {
                return;  // none waiting, or a connection that went again
            }
            const uint32_t source = ntohl(peer.sin_addr.s_addr);
            // The Cardputer itself (a port scan or health check of its own
            // address, or loopback) is no snooper: closed, not counted.
            sockaddr_in own{};
            socklen_t own_size = sizeof(own);
            const bool self = (source >> 24) == 127 ||
                              (getsockname(fd, reinterpret_cast<sockaddr*>(&own), &own_size) == 0 &&
                               own.sin_addr.s_addr == peer.sin_addr.s_addr);
            if (self) {
                close(fd);
                continue;
            }
            if (peer.sin_family != AF_INET || open_.size() >= kMaxOpen || !limiter_.allow(source, now)) {
                close(fd);
                ++run_.dropped;
                continue;
            }
            const model::TrapService& service = *listener.service;
            char text[INET_ADDRSTRLEN] = "";
            inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text));
            model::TrapHit hit;
            hit.time = local_clock_text();
            hit.address = text;
            hit.source_port = ntohs(peer.sin_port);
            hit.port = service.port;
            const uint32_t id = next_id_++;
            run_.hits.insert(run_.hits.begin(), std::move(hit));
            hit_ids_.insert(hit_ids_.begin(), id);
            if (run_.hits.size() > model::kTrapMaxHits) {
                run_.hits.pop_back();
                hit_ids_.pop_back();
            }
            ++run_.raised;

            Open open;
            open.service = &service;
            open.fd = fd;
            open.hit_id = id;
            open.deadline_ms = now + static_cast<int64_t>(model::kTrapReadSeconds) * 1000;
            open.bytes.reserve(model::kTrapReadMax);
            if (service.greeting == model::TrapGreeting::Ftp) {
                send_reply(fd, kFtpGreeting, sizeof(kFtpGreeting) - 1);
            } else if (service.greeting == model::TrapGreeting::Telnet) {
                send_reply(fd, kTelnetGreeting, sizeof(kTelnetGreeting) - 1);
            }
            open_.push_back(std::move(open));
        }
    }

    void read_some(Open& open) {
        uint8_t chunk[model::kTrapReadMax];
        const std::size_t room = model::kTrapReadMax - open.bytes.size();
        const ssize_t got = recv(open.fd, chunk, room, MSG_DONTWAIT);
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        if (got > 0) {
            open.bytes.insert(open.bytes.end(), chunk, chunk + got);
            if (model::TrapHit* hit = find_hit(open.hit_id)) {
                hit->first_bytes = model::trap_shown_text(open.bytes.data(), open.bytes.size());
            }
            if (open.service->greeting == model::TrapGreeting::Web && !open.replied && request_ended(open.bytes)) {
                send_reply(open.fd, kWebPage, sizeof(kWebPage) - 1);
                open.replied = true;
            }
        }
        if (got <= 0 || open.bytes.size() >= model::kTrapReadMax || open.replied) {
            finish(open);
        }
    }

    static bool request_ended(const std::vector<uint8_t>& bytes) {
        if (bytes.size() >= model::kTrapReadMax) {
            return true;
        }
        const std::string text(bytes.begin(), bytes.end());
        return text.find("\r\n\r\n") != std::string::npos || text.find("\n\n") != std::string::npos;
    }

    // Closes a connection; a web request that never ended still gets the
    // page when it sent anything.
    void finish(Open& open) {
        if (open.fd < 0) {
            return;
        }
        if (open.service->greeting == model::TrapGreeting::Web && !open.replied && !open.bytes.empty()) {
            send_reply(open.fd, kWebPage, sizeof(kWebPage) - 1);
            open.replied = true;
        }
        close(open.fd);
        open.fd = -1;
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::atomic<bool> stop_flag_{false};
    bool closed_{false};
    bool quit_{false};
    bool busy_{false};
    bool has_job_{false};
    model::TrapRun latest_;
    bool fresh_{false};
    // The thread's own.
    model::TrapRun run_;
    std::vector<Listener> listeners_;
    std::vector<Open> open_;
    std::vector<uint32_t> hit_ids_;  // alongside run_.hits
    uint32_t next_id_{1};
    model::TrapLimiter limiter_;
};

#endif

} // namespace

std::unique_ptr<TrapRunner> make_trap_runner(bool live) {
#if !USE_DESKTOP
    if (live) {
        return std::make_unique<LiveTrapRunner>();
    }
#else
    (void)live;
#endif
    return std::make_unique<PretendTrapRunner>();
}

} // namespace platform
