/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_runner.h"

#include "sent_counter.h"

#include "lvgl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if !USE_DESKTOP
#include "dns_message.h"

#include <arpa/inet.h>
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
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#endif

namespace platform {
namespace {

// Keeps the last model::kPingShownReplies replies of `all` (by sequence).
void keep_shown_replies(const std::vector<model::PingReply>& all, model::PingRun& run) {
    const std::size_t first = all.size() > model::kPingShownReplies ? all.size() - model::kPingShownReplies : 0;
    run.replies.assign(all.begin() + static_cast<std::ptrdiff_t>(first), all.end());
}

// Works out received, lost and the times from every echo of a run.
void sum_up(const std::vector<model::PingReply>& all, model::PingRun& run) {
    run.received = 0;
    run.lost = 0;
    int64_t total = 0;
    run.min_us = model::kTimeUnknown;
    run.max_us = model::kTimeUnknown;
    for (const model::PingReply& reply : all) {
        if (reply.time_us == model::kPingLost) {
            ++run.lost;
        } else if (reply.time_us >= 0) {
            ++run.received;
            total += reply.time_us;
            run.min_us = run.min_us < 0 ? reply.time_us : std::min(run.min_us, reply.time_us);
            run.max_us = std::max(run.max_us, reply.time_us);
        }
    }
    run.avg_us = run.received > 0 ? static_cast<int32_t>(total / run.received) : model::kTimeUnknown;
    keep_shown_replies(all, run);
}

bool ends_with(const std::string& text, const char* suffix) {
    const std::size_t length = std::char_traits<char>::length(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

// ---------------------------------------------------------------------------
// The pretend runner.

// The made-up round trips of the ten echoes, in microseconds: one takes
// below 10 ms, one over 100 ms and one is lost, so the pictures of a run
// and of its end show all three.
constexpr int32_t kPretendTimes[model::kPingCount] = {
    12431, 8804, 13122, 12716, 11950, 104208, 12230, model::kPingLost, 12877, 12102,
};
// A name takes this long to look up, and a DNS lookup this long.
constexpr uint32_t kPretendLookupMs = 23;
constexpr const char* kPretendServer = "192.0.2.53";
// Names in the reserved .invalid domain never exist.
constexpr const char* kNoSuchSuffix = ".invalid";

uint32_t name_hash(const std::string& text) {
    uint32_t hash = 2166136261u;
    for (const char character : text) {
        hash = (hash ^ static_cast<uint8_t>(character)) * 16777619u;
    }
    return hash;
}

// The made-up addresses of a name: two fixed sets, otherwise one of each
// kind made from the name.
std::vector<std::string> pretend_addresses(const std::string& name) {
    if (name == "example.com") {
        return {"192.0.2.10", "192.0.2.11", "2001:db8::10", "2001:db8::11"};
    }
    if (name == "wikipedia.org") {
        return {"198.51.100.24", "2001:db8:85a3:8d3:1319:8a2e:370:7348"};
    }
    const uint32_t number = 10 + name_hash(name) % 200;
    return {"198.51.100." + std::to_string(number), "2001:db8:1::" + std::to_string(number)};
}

class PretendToolRunner : public ToolRunner {
public:
    bool start(const ToolJob& job) override {
        if (closed_ || running_ || !model::valid_tool_target(job.kind, job.target)) {
            return false;
        }
        running_ = true;
        job_ = job;
        started_ms_ = lv_tick_get();
        counted_ = 0;
        advance();
        return true;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        advance();
        if (!running_) {
            return;  // it had just ended by itself
        }
        running_ = false;
        if (job_.kind == model::ToolKind::Ping) {
            latest_.ping.state = model::ToolState::Stopped;
        } else {
            latest_.dns.state = model::ToolState::Stopped;
        }
        fresh_ = true;
    }

    bool take(model::ToolsData& data) override {
        advance();
        if (!fresh_) {
            return false;
        }
        data.ping = latest_.ping;
        data.dns = latest_.dns;
        fresh_ = false;
        return true;
    }

    void shutdown() override {
        stop();
        closed_ = true;
    }

private:
    // Works out the run at the time passed since the job started.
    void advance() {
        if (!running_) {
            return;
        }
        const uint32_t elapsed = lv_tick_elaps(started_ms_);
        uint32_t sent = 0;
        bool done = false;
        if (job_.kind == model::ToolKind::Ping) {
            model::PingRun run = ping_at(elapsed, sent, done);
            if (run != latest_.ping) {
                latest_.ping = std::move(run);
                fresh_ = true;
            }
        } else {
            model::DnsRun run = dns_at(elapsed, sent, done);
            if (run != latest_.dns) {
                latest_.dns = std::move(run);
                fresh_ = true;
            }
        }
        if (sent > counted_) {
            count_sent(sent - counted_);
            counted_ = sent;
        }
        if (done) {
            running_ = false;
        }
    }

    model::PingRun ping_at(uint32_t elapsed, uint32_t& sent, bool& done) const {
        model::PingRun run;
        run.state = model::ToolState::Running;
        run.target = job_.target;
        uint32_t offset = 0;
        if (!model::is_ipv4_text(job_.target)) {
            // The name is looked up first: one question.
            sent = 1;
            offset = kPretendLookupMs;
            if (elapsed < offset) {
                return run;
            }
            if (ends_with(job_.target, kNoSuchSuffix)) {
                run.state = model::ToolState::Failed;
                run.error = model::ToolError::NoSuchName;
                done = true;
                return run;
            }
            run.address = pretend_addresses(job_.target).front();
        } else {
            run.address = job_.target;
        }

        std::vector<model::PingReply> all;
        uint32_t end = 0;
        for (int32_t index = 0; index < model::kPingCount; ++index) {
            const uint32_t sent_at = offset + static_cast<uint32_t>(index) * 1000u;
            const int32_t time_us = kPretendTimes[index];
            const uint32_t settled_at = sent_at + (time_us == model::kPingLost
                                                       ? static_cast<uint32_t>(model::kPingTimeoutMs)
                                                       : static_cast<uint32_t>(time_us / 1000) + 1u);
            end = std::max(end, settled_at);
            if (elapsed < sent_at) {
                continue;
            }
            ++sent;
            ++run.sent;
            all.push_back({index + 1, elapsed >= settled_at ? time_us : model::kPingWaiting});
        }
        sum_up(all, run);
        if (elapsed >= end) {
            run.state = model::ToolState::Done;
            done = true;
        }
        return run;
    }

    model::DnsRun dns_at(uint32_t elapsed, uint32_t& sent, bool& done) const {
        model::DnsRun run;
        run.state = model::ToolState::Running;
        run.name = job_.target;
        run.server = kPretendServer;
        sent = 2;  // one A and one AAAA question
        if (elapsed < kPretendLookupMs) {
            return run;
        }
        run.state = model::ToolState::Done;
        run.time_ms = static_cast<int32_t>(kPretendLookupMs);
        if (ends_with(job_.target, kNoSuchSuffix)) {
            run.error = model::ToolError::NoSuchName;
        } else {
            run.addresses = pretend_addresses(job_.target);
        }
        done = true;
        return run;
    }

    bool running_{false};
    bool closed_{false};
    bool fresh_{false};
    ToolJob job_;
    uint32_t started_ms_{0};
    uint32_t counted_{0};      // packets of this job already added to the Sent count
    model::ToolsData latest_;  // sent is not used
};

#if !USE_DESKTOP
// ---------------------------------------------------------------------------
// The live runner (device builds).

using Clock = std::chrono::steady_clock;

// The longest single wait, so a stop is seen quickly.
constexpr int kWaitSliceMs = 100;
constexpr auto kPingInterval = std::chrono::milliseconds(1000);
constexpr auto kPingTimeout = std::chrono::milliseconds(model::kPingTimeoutMs);
// A DNS question gets this long for its answer, and is sent once more.
constexpr auto kDnsTimeout = std::chrono::milliseconds(2000);
constexpr int kDnsTries = 2;
constexpr uint16_t kDnsPort = 53;
constexpr const char* kResolvConf = "/etc/resolv.conf";
// ICMP echo request and reply, and the size of the echo: the 8-byte header
// and 24 bytes of data.
constexpr uint8_t kIcmpEchoRequest = 8;
constexpr uint8_t kIcmpEchoReply = 0;
constexpr std::size_t kIcmpHeader = 8;
constexpr std::size_t kEchoSize = 32;

int64_t micros_between(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
}

// Milliseconds until `when`, at least 0 and at most kWaitSliceMs.
int wait_slice(Clock::time_point now, Clock::time_point when) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(when - now).count();
    return static_cast<int>(std::clamp<int64_t>(left, 0, kWaitSliceMs));
}

// Waits at most `ms` (never more than kWaitSliceMs) for `fd` to be
// readable. Returns true when it is.
bool wait_readable(int fd, int ms) {
    pollfd entry{fd, POLLIN, 0};
    const int result = ::poll(&entry, 1, std::clamp(ms, 0, kWaitSliceMs));
    return result > 0 && (entry.revents & (POLLIN | POLLERR)) != 0;
}

std::string error_text(int number) {
    const char* text = std::strerror(number);
    return text ? std::string(text) : "error " + std::to_string(number);
}

// The IPv4 nameservers of /etc/resolv.conf, in order, at most three (as
// the system's resolver reads them).
std::vector<std::string> nameservers() {
    constexpr std::size_t kMaxServers = 3;
    std::vector<std::string> servers;
    std::ifstream file(kResolvConf);
    std::string line;
    while (servers.size() < kMaxServers && std::getline(file, line)) {
        std::istringstream words(line);
        std::string keyword;
        std::string value;
        if (!(words >> keyword >> value) || keyword != "nameserver") {
            continue;
        }
        in_addr address{};
        if (inet_pton(AF_INET, value.c_str(), &address) == 1) {
            servers.push_back(value);
        }
    }
    return servers;
}

uint16_t icmp_checksum(const uint8_t* data, std::size_t size) {
    uint32_t sum = 0;
    for (std::size_t index = 0; index + 1 < size; index += 2) {
        sum += static_cast<uint32_t>((data[index] << 8) | data[index + 1]);
    }
    if (size % 2 != 0) {
        sum += static_cast<uint32_t>(data[size - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

// The outcome of looking a name up.
struct Lookup {
    model::ToolError error{model::ToolError::None};
    std::string reason;
    std::string server;
    std::vector<std::string> addresses;  // IPv4 first, then IPv6
    int32_t time_ms{model::kTimeUnknown};
    bool stopped{false};
};

class LiveToolRunner : public ToolRunner {
public:
    ~LiveToolRunner() override {
        shutdown();
    }

    bool start(const ToolJob& job) override {
        if (!model::valid_tool_target(job.kind, job.target)) {
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
            if (job.kind == model::ToolKind::Ping) {
                latest_.ping = model::PingRun{};
                latest_.ping.state = model::ToolState::Running;
                latest_.ping.target = job.target;
            } else {
                latest_.dns = model::DnsRun{};
                latest_.dns.state = model::ToolState::Running;
                latest_.dns.name = job.target;
            }
            fresh_ = true;
        }
        wake_.notify_all();
        return true;
    }

    void stop() override {
        stop_flag_.store(true);
    }

    bool take(model::ToolsData& data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!fresh_) {
            return false;
        }
        data.ping = latest_.ping;
        data.dns = latest_.dns;
        fresh_ = false;
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
    // Called with the mutex held. Like the data reader, the new thread
    // starts with SIGTERM and SIGINT blocked, so they always reach the main
    // thread.
    bool start_thread() {
        sigset_t stop_signals;
        sigset_t old_mask;
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGTERM);
        sigaddset(&stop_signals, SIGINT);
        const bool masked = pthread_sigmask(SIG_BLOCK, &stop_signals, &old_mask) == 0;
        try {
            thread_ = std::thread(&LiveToolRunner::run, this);
        } catch (...) {
            // No thread: the job is refused.
        }
        if (masked) {
            pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
        }
        return thread_.joinable();
    }

    void run() {
        while (true) {
            ToolJob job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_job_; });
                if (quit_) {
                    if (has_job_) {
                        // Taken but never run: it reads as stopped.
                        mark_stopped(job_.kind);
                        has_job_ = false;
                    }
                    busy_ = false;
                    return;
                }
                job = job_;
                has_job_ = false;
            }
            try {
                if (job.kind == model::ToolKind::Ping) {
                    run_ping(job.target);
                } else {
                    run_dns(job.target);
                }
            } catch (...) {
                // Nothing in a job should throw; if something does (no
                // memory), the run reads as failed.
                std::lock_guard<std::mutex> lock(mutex_);
                if (job.kind == model::ToolKind::Ping) {
                    latest_.ping.state = model::ToolState::Failed;
                    latest_.ping.error = model::ToolError::Failed;
                    latest_.ping.reason = "internal error";
                } else {
                    latest_.dns.state = model::ToolState::Failed;
                    latest_.dns.error = model::ToolError::Failed;
                    latest_.dns.reason = "internal error";
                }
                fresh_ = true;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }

    // Called with the mutex held.
    void mark_stopped(model::ToolKind kind) {
        if (kind == model::ToolKind::Ping) {
            latest_.ping.state = model::ToolState::Stopped;
        } else {
            latest_.dns.state = model::ToolState::Stopped;
        }
        fresh_ = true;
    }

    bool stopping() const {
        return stop_flag_.load();
    }

    void publish(const model::PingRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_.ping) {
            latest_.ping = run;
            fresh_ = true;
        }
    }

    void publish(const model::DnsRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_.dns) {
            latest_.dns = run;
            fresh_ = true;
        }
    }

    // Asks the first IPv4 nameserver of /etc/resolv.conf for the A record of
    // `name`, and for the AAAA record too when `with_ipv6` is set. A
    // question with no answer in kDnsTimeout is sent once more, to the next
    // IPv4 nameserver when there is one (as the system's resolver does when
    // the first one is down), otherwise to the same one; an answer from
    // either is taken. Lookup::server names the server that answered, or
    // the last one asked.
    Lookup lookup(const std::string& name, bool with_ipv6) {
        Lookup result;
        const std::vector<std::string> servers = nameservers();
        if (servers.empty()) {
            result.error = model::ToolError::NoServer;
            return result;
        }
        result.server = servers.front();

        struct Question {
            uint16_t type{dns::kTypeA};
            uint16_t id{0};
            std::vector<uint8_t> message;
            bool answered{false};
            uint8_t rcode{0};
            std::vector<std::string> addresses;
        };
        std::random_device random;
        std::vector<Question> questions(with_ipv6 ? 2 : 1);
        questions[0].type = dns::kTypeA;
        if (with_ipv6) {
            questions[1].type = dns::kTypeAaaa;
        }
        for (Question& question : questions) {
            question.id = static_cast<uint16_t>(random());
            if (!dns::build_query(question.id, name, question.type, question.message)) {
                result.error = model::ToolError::BadTarget;
                return result;
            }
        }

        // One socket per try, each connected to its server: the system then
        // only lets answers from that server through.
        struct Try {
            int fd{-1};
            std::string server;
            bool refused{false};
        };
        std::vector<Try> tries;
        int open_error = 0;
        int send_error = 0;
        // Opens the next try's socket and sends the questions not answered
        // yet. Returns false when the socket could not be made.
        auto start_try = [&]() {
            Try attempt;
            attempt.server = servers[tries.size() % servers.size()];
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(kDnsPort);
            inet_pton(AF_INET, attempt.server.c_str(), &address.sin_addr);
            attempt.fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (attempt.fd < 0 ||
                ::connect(attempt.fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
                open_error = errno;
                if (attempt.fd >= 0) {
                    ::close(attempt.fd);
                }
                return false;
            }
            result.server = attempt.server;
            for (const Question& question : questions) {
                if (question.answered) {
                    continue;
                }
                if (::send(attempt.fd, question.message.data(), question.message.size(), MSG_NOSIGNAL) ==
                    static_cast<ssize_t>(question.message.size())) {
                    count_sent();
                } else {
                    send_error = errno;
                }
            }
            tries.push_back(attempt);
            return true;
        };
        auto close_all = [&tries] {
            for (Try& attempt : tries) {
                if (attempt.fd >= 0) {
                    ::close(attempt.fd);
                    attempt.fd = -1;
                }
            }
        };

        const Clock::time_point started = Clock::now();
        Clock::time_point last_answer = started;
        if (!start_try()) {
            result.error = model::ToolError::Failed;
            result.reason = error_text(open_error);
            return result;
        }
        Clock::time_point deadline = started + kDnsTimeout;
        uint8_t buffer[dns::kMaxMessage];
        while (true) {
            if (stopping()) {
                result.stopped = true;
                break;
            }
            const bool all_answered = std::all_of(questions.begin(), questions.end(),
                                                  [](const Question& question) { return question.answered; });
            if (all_answered) {
                break;
            }
            Clock::time_point now = Clock::now();
            // A server that refused (port unreachable) ends its try at once.
            if (tries.back().refused) {
                deadline = now;
            }
            if (now >= deadline) {
                if (static_cast<int>(tries.size()) >= kDnsTries || !start_try()) {
                    break;
                }
                deadline = now + kDnsTimeout;
            }

            std::vector<pollfd> entries;
            for (const Try& attempt : tries) {
                if (attempt.fd >= 0 && !attempt.refused) {
                    entries.push_back({attempt.fd, POLLIN, 0});
                }
            }
            if (entries.empty()) {
                deadline = now;  // nothing left to wait for on this try
                continue;
            }
            if (::poll(entries.data(), entries.size(), wait_slice(now, deadline)) <= 0) {
                continue;
            }
            for (Try& attempt : tries) {
                if (attempt.fd < 0 || attempt.refused) {
                    continue;
                }
                while (true) {
                    const ssize_t size = ::recv(attempt.fd, buffer, sizeof(buffer), MSG_DONTWAIT);
                    if (size < 0) {
                        if (errno == ECONNREFUSED) {
                            attempt.refused = true;
                        }
                        break;
                    }
                    dns::Message message;
                    if (!dns::parse_message(buffer, static_cast<std::size_t>(size), message) || !message.response) {
                        continue;
                    }
                    for (Question& question : questions) {
                        if (question.answered || message.id != question.id ||
                            message.question_type != question.type || message.question_name != name) {
                            continue;
                        }
                        question.answered = true;
                        question.rcode = message.rcode;
                        last_answer = Clock::now();
                        result.server = attempt.server;
                        for (const dns::Record& record : message.answers) {
                            if (record.type != question.type) {
                                continue;
                            }
                            const std::string text =
                                dns::address_text(buffer, static_cast<std::size_t>(size), record);
                            if (!text.empty() && std::find(question.addresses.begin(), question.addresses.end(),
                                                           text) == question.addresses.end()) {
                                question.addresses.push_back(text);
                            }
                        }
                        break;
                    }
                }
            }
        }
        close_all();
        if (result.stopped) {
            return result;
        }
        const bool refused = std::all_of(tries.begin(), tries.end(), [](const Try& attempt) { return attempt.refused; });

        result.time_ms = static_cast<int32_t>(micros_between(started, last_answer) / 1000);
        bool no_such_name = false;
        bool no_data = false;
        int server_error = -1;
        for (const Question& question : questions) {
            for (const std::string& address : question.addresses) {
                if (result.addresses.size() < model::kDnsMaxAddresses) {
                    result.addresses.push_back(address);
                }
            }
            if (!question.answered) {
                continue;
            }
            if (question.rcode == dns::kRcodeNxDomain) {
                no_such_name = true;
            } else if (question.rcode == dns::kRcodeNoError) {
                no_data = no_data || question.addresses.empty();
            } else {
                server_error = question.rcode;
            }
        }
        if (!result.addresses.empty()) {
            return result;
        }
        if (no_such_name) {
            result.error = model::ToolError::NoSuchName;
        } else if (no_data) {
            result.error = model::ToolError::NoAddress;
        } else if (server_error >= 0) {
            result.error = model::ToolError::Failed;
            result.reason = server_error == dns::kRcodeServFail ? std::string("server failure")
                                                                 : "server error " + std::to_string(server_error);
        } else if (refused) {
            result.error = model::ToolError::Failed;
            result.reason = "DNS server refused";
        } else if (send_error != 0) {
            result.error = model::ToolError::Failed;
            result.reason = error_text(send_error);
        } else {
            result.error = model::ToolError::NoAnswer;
        }
        return result;
    }

    void run_dns(const std::string& name) {
        model::DnsRun run;
        run.state = model::ToolState::Running;
        run.name = name;
        const std::vector<std::string> servers = nameservers();
        run.server = servers.empty() ? std::string() : servers.front();
        publish(run);

        const Lookup found = lookup(name, true);
        run.server = found.server;
        if (found.stopped) {
            run.state = model::ToolState::Stopped;
        } else if (found.error == model::ToolError::NoServer || found.error == model::ToolError::BadTarget ||
                   (found.error == model::ToolError::Failed && found.time_ms == model::kTimeUnknown)) {
            run.state = model::ToolState::Failed;
            run.error = found.error;
            run.reason = found.reason;
        } else {
            run.state = model::ToolState::Done;
            run.error = found.error;
            run.reason = found.reason;
            run.addresses = found.addresses;
            run.time_ms = found.time_ms;
        }
        publish(run);
    }

    void run_ping(const std::string& target) {
        model::PingRun run;
        run.state = model::ToolState::Running;
        run.target = target;
        publish(run);

        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        if (model::is_ipv4_text(target) && inet_pton(AF_INET, target.c_str(), &destination.sin_addr) == 1) {
            run.address = target;
        } else {
            // A name is looked up first (an A question, counted as sent).
            const Lookup found = lookup(target, false);
            if (found.stopped) {
                run.state = model::ToolState::Stopped;
                publish(run);
                return;
            }
            if (found.error != model::ToolError::None || found.addresses.empty() ||
                inet_pton(AF_INET, found.addresses.front().c_str(), &destination.sin_addr) != 1) {
                run.state = model::ToolState::Failed;
                run.error = found.error == model::ToolError::None ? model::ToolError::NoAddress : found.error;
                run.reason = found.reason;
                publish(run);
                return;
            }
            run.address = found.addresses.front();
        }
        publish(run);

        // An unprivileged ICMP datagram socket: allowed for the groups in
        // net.ipv4.ping_group_range, which this code only reads about.
        const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_ICMP);
        if (fd < 0) {
            const int number = errno;
            run.state = model::ToolState::Failed;
            if (number == EACCES || number == EPERM) {
                run.error = model::ToolError::NotAllowed;
            } else {
                run.error = model::ToolError::Failed;
                run.reason = error_text(number);
            }
            publish(run);
            return;
        }

        // Every echo by sequence (index 0 is #1), with the time it went out.
        std::vector<model::PingReply> all;
        std::vector<Clock::time_point> sent_at;
        all.reserve(model::kPingCount);
        sent_at.reserve(model::kPingCount);

        Clock::time_point next_send = Clock::now();
        uint8_t packet[kEchoSize];
        uint8_t buffer[256];
        while (true) {
            if (stopping()) {
                run.state = model::ToolState::Stopped;
                break;
            }
            Clock::time_point now = Clock::now();
            bool changed = false;

            // The next echo, once a second.
            if (static_cast<int32_t>(all.size()) < model::kPingCount && now >= next_send) {
                const auto sequence = static_cast<uint16_t>(all.size() + 1);
                std::memset(packet, 0, sizeof(packet));
                packet[0] = kIcmpEchoRequest;
                // The system puts its own identifier in bytes 4-5 and checks
                // the sum again; it is filled in all the same.
                packet[6] = static_cast<uint8_t>(sequence >> 8);
                packet[7] = static_cast<uint8_t>(sequence & 0xFF);
                for (std::size_t index = kIcmpHeader; index < sizeof(packet); ++index) {
                    packet[index] = static_cast<uint8_t>(index);
                }
                const uint16_t sum = icmp_checksum(packet, sizeof(packet));
                packet[2] = static_cast<uint8_t>(sum >> 8);
                packet[3] = static_cast<uint8_t>(sum & 0xFF);
                const Clock::time_point when = Clock::now();
                const ssize_t written = ::sendto(fd, packet, sizeof(packet), MSG_NOSIGNAL,
                                                 reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
                if (written == static_cast<ssize_t>(sizeof(packet))) {
                    count_sent();
                    ++run.sent;
                    all.push_back({sequence, model::kPingWaiting});
                } else {
                    run.reason = error_text(errno);
                    all.push_back({sequence, model::kPingLost});
                }
                sent_at.push_back(when);
                next_send += kPingInterval;
                if (next_send < when) {
                    next_send = when + kPingInterval;  // never send a burst after a stall
                }
                changed = true;
            }

            // Replies: the system hands over the ICMP message without the IP
            // header, and only the replies to this socket's identifier.
            while (true) {
                const ssize_t size = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
                if (size < 0) {
                    break;
                }
                if (static_cast<std::size_t>(size) < kIcmpHeader || buffer[0] != kIcmpEchoReply || buffer[1] != 0) {
                    continue;
                }
                const int sequence = (buffer[6] << 8) | buffer[7];
                if (sequence < 1 || sequence > static_cast<int>(all.size())) {
                    continue;
                }
                model::PingReply& reply = all[static_cast<std::size_t>(sequence - 1)];
                if (reply.time_us != model::kPingWaiting) {
                    continue;  // a duplicate, or late after the time-out
                }
                const int64_t micros = micros_between(sent_at[static_cast<std::size_t>(sequence - 1)], Clock::now());
                reply.time_us = static_cast<int32_t>(std::clamp<int64_t>(micros, 0, INT32_MAX));
                changed = true;
            }

            // Echoes without a reply in time are lost.
            now = Clock::now();
            Clock::time_point next_timeout = Clock::time_point::max();
            for (std::size_t index = 0; index < all.size(); ++index) {
                if (all[index].time_us != model::kPingWaiting) {
                    continue;
                }
                const Clock::time_point timeout = sent_at[index] + kPingTimeout;
                if (now >= timeout) {
                    all[index].time_us = model::kPingLost;
                    changed = true;
                } else {
                    next_timeout = std::min(next_timeout, timeout);
                }
            }

            if (changed) {
                sum_up(all, run);
                publish(run);
            }
            const bool all_sent = static_cast<int32_t>(all.size()) >= model::kPingCount;
            if (all_sent && next_timeout == Clock::time_point::max()) {
                run.state = model::ToolState::Done;
                break;
            }
            const Clock::time_point wake = all_sent ? next_timeout : std::min(next_send, next_timeout);
            wait_readable(fd, wait_slice(now, wake));
        }
        ::close(fd);
        sum_up(all, run);
        publish(run);
    }

    std::thread thread_;
    // The job slot, the flags and the latest runs; the mutex guards all of
    // them. The stop flag is read without the mutex in every wait.
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_flag_{false};
    bool quit_{false};
    bool closed_{false};
    bool busy_{false};     // a job was started and has not ended yet
    bool has_job_{false};  // job_ waits for the thread
    ToolJob job_;
    model::ToolsData latest_;  // sent is not used
    bool fresh_{false};
};
#endif

} // namespace

std::unique_ptr<ToolRunner> make_tool_runner(bool live) {
#if !USE_DESKTOP
    if (live) {
        return std::make_unique<LiveToolRunner>();
    }
#else
    (void)live;
#endif
    return std::make_unique<PretendToolRunner>();
}

} // namespace platform
