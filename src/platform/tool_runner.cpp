/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_runner.h"

#include "listen.h"
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
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/uio.h>
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

// The made-up hops of a trace to an address outside the home network; the
// target follows as the last hop. A hop without an address never answers.
struct PretendHop {
    std::string address;
    std::string name;  // empty: the address has no reverse name
    int32_t time_us{model::kPingLost};
};

const std::vector<PretendHop>& pretend_hops() {
    static const std::vector<PretendHop> hops{
        {"192.0.2.1", "router.example", 912},
        {"198.51.100.1", "", 8804},
        {"198.51.100.9", "core1.example.net", 9420},
        {"", "", model::kPingLost},
        {"203.0.113.5", "edge2.example.net", 12043},
        {"203.0.113.77", "peer.example.org", 104208},
    };
    return hops;
}

constexpr int32_t kPretendRouterUs = 912;
constexpr int32_t kPretendTargetUs = 12877;
constexpr const char* kPretendRouterName = "router.example";
constexpr const char* kPretendHostName = "host.example";
// A made-up probe goes out this long after the one before it settled, so
// the pictures can catch a trace under way (the live runner sends the next
// one at once).
constexpr uint32_t kPretendProbeGapMs = 100;

// Whether `text` is an IPv4 address of a home network (10/8, 172.16/12,
// 192.168/16): a made-up trace reaches it in one hop.
bool private_ipv4(const std::string& text) {
    if (!model::is_ipv4_text(text)) {
        return false;
    }
    const int first = std::stoi(text);
    const int second = std::stoi(text.substr(text.find('.') + 1));
    return first == 10 || (first == 172 && second >= 16 && second <= 31) || (first == 192 && second == 168);
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
        if (job.kind == model::ToolKind::Listen) {
            save_asked_ = false;
            kept_bytes_ = 0;
            kept_full_ = false;
        }
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
        switch (job_.kind) {
            case model::ToolKind::Ping:
                latest_.ping.state = model::ToolState::Stopped;
                break;
            case model::ToolKind::Dns:
                latest_.dns.state = model::ToolState::Stopped;
                break;
            case model::ToolKind::Trace:
                latest_.trace.state = model::ToolState::Stopped;
                break;
            case model::ToolKind::Listen:
                latest_.listen.state = model::ToolState::Stopped;
                if (latest_.listen.save == model::CaptureSave::Saving) {
                    latest_.listen.save = model::CaptureSave::Saved;
                }
                break;
        }
        fresh_ = true;
    }

    bool save_capture() override {
        advance();
        model::ListenRun& run = latest_.listen;
        if (closed_ || run.state == model::ToolState::Idle || run.save != model::CaptureSave::None) {
            return false;
        }
        if (running_ && job_.kind == model::ToolKind::Listen) {
            // The run shows Saving from now on, and Saved when it ends.
            save_asked_ = true;
            advance();
            return true;
        }
        if (kept_bytes_ == 0) {
            return false;
        }
        run.save = model::CaptureSave::Saved;
        run.save_name = kPretendCaptureName;
        run.save_bytes = kept_bytes_;
        run.save_full = kept_full_;
        fresh_ = true;
        return true;
    }

    bool take(model::ToolsData& data) override {
        advance();
        if (!fresh_) {
            return false;
        }
        data.ping = latest_.ping;
        data.dns = latest_.dns;
        data.trace = latest_.trace;
        data.listen = latest_.listen;
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
        switch (job_.kind) {
            case model::ToolKind::Ping:
                keep(ping_at(elapsed, sent, done), latest_.ping);
                break;
            case model::ToolKind::Dns:
                keep(dns_at(elapsed, sent, done), latest_.dns);
                break;
            case model::ToolKind::Trace:
                keep(trace_at(elapsed, sent, done), latest_.trace);
                break;
            case model::ToolKind::Listen:
                // Listening sends nothing: `sent` stays 0.
                keep(listen_at(elapsed, done), latest_.listen);
                break;
        }
        if (sent > counted_) {
            count_sent(sent - counted_);
            counted_ = sent;
        }
        if (done) {
            running_ = false;
        }
    }

    template <typename Run>
    void keep(Run run, Run& latest) {
        if (run != latest) {
            latest = std::move(run);
            fresh_ = true;
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

    // A trace: the target's name is looked up first, then one probe per
    // hop, each sent kPretendProbeGapMs after the one before settled, and a
    // PTR question for each hop that answers, answered kPretendLookupMs
    // later.
    model::TraceRun trace_at(uint32_t elapsed, uint32_t& sent, bool& done) const {
        model::TraceRun run;
        run.state = model::ToolState::Running;
        run.target = job_.target;
        uint32_t at = 0;
        std::string target_name = kPretendHostName;
        if (!model::is_ipv4_text(job_.target)) {
            sent = 1;
            at = kPretendLookupMs;
            if (elapsed < at) {
                return run;
            }
            if (ends_with(job_.target, kNoSuchSuffix)) {
                run.state = model::ToolState::Failed;
                run.error = model::ToolError::NoSuchName;
                done = true;
                return run;
            }
            run.address = pretend_addresses(job_.target).front();
            target_name = job_.target;
        } else {
            run.address = job_.target;
        }

        std::vector<PretendHop> hops;
        if (private_ipv4(run.address)) {
            hops.push_back({run.address, kPretendRouterName, kPretendRouterUs});
        } else {
            hops = pretend_hops();
            hops.push_back({run.address, target_name, kPretendTargetUs});
        }
        uint32_t names_end = 0;
        for (std::size_t index = 0; index < hops.size(); ++index) {
            if (elapsed < at) {
                return run;  // the next probe has not gone out yet
            }
            const PretendHop& hop = hops[index];
            const bool silent = hop.address.empty();
            const uint32_t settled = at + (silent ? static_cast<uint32_t>(model::kTraceHopTimeoutMs)
                                                  : static_cast<uint32_t>(hop.time_us / 1000) + 1u);
            model::TraceHop shown;
            shown.ttl = static_cast<int32_t>(index) + 1;
            ++sent;
            if (elapsed >= settled) {
                shown.time_us = hop.time_us;
                shown.address = hop.address;
                if (!silent) {
                    ++sent;  // its PTR question
                    names_end = std::max(names_end, settled + kPretendLookupMs);
                    if (elapsed >= settled + kPretendLookupMs) {
                        shown.name = hop.name;
                    }
                }
            }
            run.hops.push_back(shown);
            if (elapsed < settled) {
                return run;
            }
            at = settled + kPretendProbeGapMs;
        }
        run.end = model::TraceEnd::Reached;
        if (elapsed >= names_end) {
            run.state = model::ToolState::Done;
            done = true;
        }
        return run;
    }

    // A listen run: the made-up capture of the time so far, read by the
    // same code as the helper's stream (in small pieces, so records arrive
    // split), each frame at its own time.
    // Notes how much the reader kept, for a save after the run.
    model::ListenRun listen_at(uint32_t elapsed, bool& done) {
        constexpr uint32_t kLimitMs = static_cast<uint32_t>(model::kListenSeconds) * 1000u;
        constexpr std::size_t kPiece = 7;
        const uint32_t shown = std::min(elapsed, kLimitMs);
        model::ListenRun run;
        run.state = model::ToolState::Running;
        run.interface_name = job_.target;
        run.elapsed_s = static_cast<int32_t>(shown / 1000);
        const std::vector<uint8_t> stream = pretend_capture(shown);
        CaptureReader reader;
        reader.keep(model::kCaptureSaveMax);
        HeardDevices heard(pretend_own_ports());
        const auto on_frame = [&heard](const uint8_t* frame, std::size_t size, uint32_t seconds) {
            heard.note_frame(frame, size, static_cast<int32_t>(seconds));
        };
        for (std::size_t at = 0; at < stream.size(); at += kPiece) {
            reader.feed(stream.data() + at, std::min(kPiece, stream.size() - at), on_frame);
        }
        run.frames = heard.frames();
        run.devices = heard.devices();
        kept_bytes_ = reader.kept().size();
        kept_full_ = reader.kept_full();
        if (elapsed >= kLimitMs) {
            run.state = model::ToolState::Done;
            done = true;
        }
        if (save_asked_) {
            run.save = done ? model::CaptureSave::Saved : model::CaptureSave::Saving;
            run.save_name = kPretendCaptureName;
            run.save_bytes = kept_bytes_;
            run.save_full = kept_full_;
        }
        return run;
    }

    bool running_{false};
    bool closed_{false};
    bool fresh_{false};
    ToolJob job_;
    uint32_t started_ms_{0};
    uint32_t counted_{0};      // packets of this job already added to the Sent count
    model::ToolsData latest_;  // sent is not used
    // The latest listen run: whether a save was asked while it ran, and how
    // much its reader kept.
    bool save_asked_{false};
    uint64_t kept_bytes_{0};
    bool kept_full_{false};
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
// The ICMP errors a trace reads from its socket's error queue.
constexpr uint8_t kIcmpUnreachable = 3;
constexpr uint8_t kIcmpTimeExceeded = 11;
constexpr auto kHopTimeout = std::chrono::milliseconds(model::kTraceHopTimeoutMs);
// At most this many messages are read from one socket per wake, so a flood
// can never hold the loop (what is left is read at the next wake).
constexpr int kMaxReads = 32;

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

// Writes an echo request with `sequence` into `packet` (kEchoSize bytes).
// The system puts its own identifier in bytes 4-5 and checks the sum
// again; it is filled in all the same.
void fill_echo(uint8_t* packet, uint16_t sequence) {
    std::memset(packet, 0, kEchoSize);
    packet[0] = kIcmpEchoRequest;
    packet[6] = static_cast<uint8_t>(sequence >> 8);
    packet[7] = static_cast<uint8_t>(sequence & 0xFF);
    for (std::size_t index = kIcmpHeader; index < kEchoSize; ++index) {
        packet[index] = static_cast<uint8_t>(index);
    }
    const uint16_t sum = icmp_checksum(packet, kEchoSize);
    packet[2] = static_cast<uint8_t>(sum >> 8);
    packet[3] = static_cast<uint8_t>(sum & 0xFF);
}

std::string ipv4_text(const in_addr& address) {
    char text[INET_ADDRSTRLEN] = {};
    return inet_ntop(AF_INET, &address, text, sizeof(text)) ? std::string(text) : std::string();
}

// One message of a trace socket's error queue that answers a probe.
struct ProbeError {
    bool usable{false};  // an ICMP Time exceeded or Destination unreachable about one of our echoes
    uint8_t type{0};     // kIcmpTimeExceeded or kIcmpUnreachable
    int sequence{-1};    // the echo's sequence number, from the copy of it the error carries
    std::string from;    // who sent the error (the hop)
};

// Takes one message off the error queue of `fd`. Returns false when the
// queue is empty or cannot be read; otherwise true, with `out.usable` set
// only for a message this code understands. Every length is checked, and
// the parts are copied out rather than read in place.
bool take_probe_error(int fd, ProbeError& out) {
    out = ProbeError{};
    uint8_t data[64];
    union {
        cmsghdr align;
        uint8_t bytes[CMSG_SPACE(sizeof(sock_extended_err) + sizeof(sockaddr_in)) + 64];
    } control{};
    iovec part{data, sizeof(data)};
    sockaddr_in peer{};
    msghdr message{};
    message.msg_name = &peer;
    message.msg_namelen = sizeof(peer);
    message.msg_iov = &part;
    message.msg_iovlen = 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    const ssize_t size = ::recvmsg(fd, &message, MSG_ERRQUEUE | MSG_DONTWAIT);
    if (size < 0) {
        return false;
    }
    // The data is the echo the error is about, from its ICMP header on.
    if (static_cast<std::size_t>(size) < kIcmpHeader || data[0] != kIcmpEchoRequest) {
        return true;
    }
    for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != IPPROTO_IP || header->cmsg_type != IP_RECVERR ||
            header->cmsg_len < CMSG_LEN(sizeof(sock_extended_err) + sizeof(sockaddr_in))) {
            continue;
        }
        sock_extended_err error{};
        std::memcpy(&error, CMSG_DATA(header), sizeof(error));
        if (error.ee_origin != SO_EE_ORIGIN_ICMP ||
            (error.ee_type != kIcmpTimeExceeded && error.ee_type != kIcmpUnreachable)) {
            continue;
        }
        // The sender of the error follows the error (SO_EE_OFFENDER).
        sockaddr_in offender{};
        std::memcpy(&offender, CMSG_DATA(header) + sizeof(error), sizeof(offender));
        if (offender.sin_family != AF_INET) {
            continue;
        }
        out.from = ipv4_text(offender.sin_addr);
        if (out.from.empty()) {
            continue;
        }
        out.usable = true;
        out.type = error.ee_type;
        out.sequence = (data[6] << 8) | data[7];
        break;
    }
    return true;
}

// The reverse names (PTR) of a trace's hops. Each question goes out at
// once, without waiting for the ones before it. One with no answer in
// kDnsTimeout is sent once more, to the next IPv4 nameserver when there is
// one (as lookup() does), then given up. Each question sent is counted with
// count_sent(). One socket per server, connected to it, so only that
// server's answers get through. Only used on the tool runner's thread.
class ReverseNames {
public:
    ReverseNames() : servers_(nameservers()), fds_(servers_.size(), -1), dead_(servers_.size(), false) {}

    ~ReverseNames() {
        for (const int fd : fds_) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
    }

    ReverseNames(const ReverseNames&) = delete;
    ReverseNames& operator=(const ReverseNames&) = delete;

    // Asks for the name of `address`, the address of hop `hop`. An address
    // asked for before is not asked again: its name comes back at once
    // when it is known (otherwise found() gets it later).
    std::string ask(std::size_t hop, const std::string& address, Clock::time_point now) {
        const std::string name = dns::reverse_name(address);
        if (name.empty() || servers_.empty()) {
            return {};
        }
        for (Question& question : questions_) {
            if (question.name == name) {
                question.hops.push_back(hop);
                return question.answer;
            }
        }
        Question question;
        question.name = name;
        question.hops.push_back(hop);
        question.id = static_cast<uint16_t>(random_());
        if (!dns::build_query(question.id, name, dns::kTypePtr, question.message)) {
            return {};
        }
        questions_.push_back(std::move(question));
        send(questions_.back(), now);
        return {};
    }

    void add_poll_entries(std::vector<pollfd>& entries) const {
        for (std::size_t server = 0; server < fds_.size(); ++server) {
            if (fds_[server] >= 0 && !dead_[server]) {
                entries.push_back({fds_[server], POLLIN, 0});
            }
        }
    }

    // Reads the answers that came in and calls found(hop, name) for every
    // hop whose name is now known. Answers must match a question's ID, name
    // and type.
    template <typename Found>
    void receive(Clock::time_point now, Found found) {
        uint8_t buffer[dns::kMaxMessage];
        for (std::size_t server = 0; server < fds_.size(); ++server) {
            for (int count = 0; count < kMaxReads && fds_[server] >= 0 && !dead_[server]; ++count) {
                const ssize_t size = ::recv(fds_[server], buffer, sizeof(buffer), MSG_DONTWAIT);
                if (size < 0) {
                    if (errno == ECONNREFUSED) {
                        // The server refused (port unreachable): its
                        // questions go to the next one at once.
                        dead_[server] = true;
                        for (Question& question : questions_) {
                            if (!question.done && question.server == server) {
                                question.deadline = now;
                            }
                        }
                    }
                    break;
                }
                dns::Message message;
                if (!dns::parse_message(buffer, static_cast<std::size_t>(size), message) || !message.response ||
                    message.question_type != dns::kTypePtr) {
                    continue;
                }
                for (Question& question : questions_) {
                    if (question.done || message.id != question.id || message.question_name != question.name) {
                        continue;
                    }
                    question.done = true;
                    if (message.rcode == dns::kRcodeNoError) {
                        // The first PTR record, whatever its owner: reverse
                        // zones often answer through a CNAME.
                        for (const dns::Record& record : message.answers) {
                            question.answer = dns::pointer_name(buffer, static_cast<std::size_t>(size), record);
                            if (!question.answer.empty()) {
                                break;
                            }
                        }
                    }
                    if (!question.answer.empty()) {
                        for (const std::size_t hop : question.hops) {
                            found(hop, question.answer);
                        }
                    }
                    break;
                }
            }
        }
    }

    // Sends the questions with no answer in time once more, and gives up on
    // those sent twice already.
    void retry(Clock::time_point now) {
        for (Question& question : questions_) {
            if (question.done || now < question.deadline) {
                continue;
            }
            if (question.tries >= kDnsTries) {
                question.done = true;
            } else {
                send(question, now);
            }
        }
    }

    bool pending() const {
        return std::any_of(questions_.begin(), questions_.end(), [](const Question& question) { return !question.done; });
    }

    // When the next question times out; max() with none waiting.
    Clock::time_point next_deadline() const {
        Clock::time_point next = Clock::time_point::max();
        for (const Question& question : questions_) {
            if (!question.done) {
                next = std::min(next, question.deadline);
            }
        }
        return next;
    }

private:
    struct Question {
        std::string name;               // "4.3.2.1.in-addr.arpa"
        std::vector<std::size_t> hops;  // the hops with this address
        uint16_t id{0};
        std::vector<uint8_t> message;
        int tries{0};
        std::size_t server{0};          // the server of the latest try
        Clock::time_point deadline{};
        bool done{false};               // answered or given up
        std::string answer;             // the name; empty when there is none
    };

    // Sends `question` for its next try: to the first server, then to the
    // next one. A server that cannot be reached makes the try time out at
    // once.
    void send(Question& question, Clock::time_point now) {
        const std::size_t server = static_cast<std::size_t>(question.tries) % servers_.size();
        ++question.tries;
        question.server = server;
        question.deadline = now + kDnsTimeout;
        const int fd = socket_for(server);
        if (fd < 0) {
            question.deadline = now;
            return;
        }
        if (::send(fd, question.message.data(), question.message.size(), MSG_NOSIGNAL) ==
            static_cast<ssize_t>(question.message.size())) {
            count_sent();
        }
    }

    // The socket for `server`, opened and connected the first time; -1
    // when it cannot be made or the server refused.
    int socket_for(std::size_t server) {
        if (dead_[server]) {
            return -1;
        }
        if (fds_[server] >= 0) {
            return fds_[server];
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kDnsPort);
        const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0 || inet_pton(AF_INET, servers_[server].c_str(), &address.sin_addr) != 1 ||
            ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            if (fd >= 0) {
                ::close(fd);
            }
            dead_[server] = true;
            return -1;
        }
        fds_[server] = fd;
        return fd;
    }

    std::vector<std::string> servers_;
    std::vector<int> fds_;      // by server; -1 until opened
    std::vector<bool> dead_;    // by server: refused, or no socket
    std::vector<Question> questions_;
    std::random_device random_;
};

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
    // The folder is found here, on the UI thread, once.
    LiveToolRunner() : capture_folder_(capture_folder()) {}

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
            switch (job.kind) {
                case model::ToolKind::Ping:
                    latest_.ping = model::PingRun{};
                    latest_.ping.state = model::ToolState::Running;
                    latest_.ping.target = job.target;
                    break;
                case model::ToolKind::Dns:
                    latest_.dns = model::DnsRun{};
                    latest_.dns.state = model::ToolState::Running;
                    latest_.dns.name = job.target;
                    break;
                case model::ToolKind::Trace:
                    latest_.trace = model::TraceRun{};
                    latest_.trace.state = model::ToolState::Running;
                    latest_.trace.target = job.target;
                    break;
                case model::ToolKind::Listen:
                    latest_.listen = model::ListenRun{};
                    latest_.listen.state = model::ToolState::Running;
                    latest_.listen.interface_name = job.target;
                    // A save asked for the run before is dropped with it.
                    save_flag_.store(false);
                    ++listen_generation_;
                    break;
            }
            fresh_ = true;
        }
        wake_.notify_all();
        return true;
    }

    void stop() override {
        stop_flag_.store(true);
    }

    bool save_capture() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const model::ListenRun& run = latest_.listen;
            if (closed_ || run.state == model::ToolState::Idle || run.save != model::CaptureSave::None) {
                return false;
            }
            if (run.state != model::ToolState::Running && kept_.empty()) {
                return false;
            }
            save_flag_.store(true);
        }
        wake_.notify_all();
        return true;
    }

    bool take(model::ToolsData& data) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!fresh_) {
            return false;
        }
        data.ping = latest_.ping;
        data.dns = latest_.dns;
        data.trace = latest_.trace;
        data.listen = latest_.listen;
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
            bool save = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_job_ || save_flag_.load(); });
                if (quit_) {
                    if (has_job_) {
                        // Taken but never run: it reads as stopped.
                        mark_stopped(job_.kind);
                        has_job_ = false;
                    }
                    busy_ = false;
                    return;
                }
                // A job first: a listen that waits to start sees the save
                // itself.
                if (has_job_) {
                    job = job_;
                    has_job_ = false;
                } else {
                    save = save_flag_.exchange(false);
                }
            }
            if (save) {
                save_kept();
                continue;
            }
            try {
                switch (job.kind) {
                    case model::ToolKind::Ping:
                        run_ping(job.target);
                        break;
                    case model::ToolKind::Dns:
                        run_dns(job.target);
                        break;
                    case model::ToolKind::Trace:
                        run_trace(job.target);
                        break;
                    case model::ToolKind::Listen:
                        run_listen(job.target);
                        break;
                }
            } catch (...) {
                // Nothing in a job should throw; if something does (no
                // memory), the run reads as failed.
                std::lock_guard<std::mutex> lock(mutex_);
                switch (job.kind) {
                    case model::ToolKind::Ping:
                        fail_internal(latest_.ping);
                        break;
                    case model::ToolKind::Dns:
                        fail_internal(latest_.dns);
                        break;
                    case model::ToolKind::Trace:
                        fail_internal(latest_.trace);
                        break;
                    case model::ToolKind::Listen:
                        fail_internal(latest_.listen);
                        break;
                }
                fresh_ = true;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }

    template <typename Run>
    static void fail_internal(Run& run) {
        run.state = model::ToolState::Failed;
        run.error = model::ToolError::Failed;
        run.reason = "internal error";
    }

    // Called with the mutex held.
    void mark_stopped(model::ToolKind kind) {
        switch (kind) {
            case model::ToolKind::Ping:
                latest_.ping.state = model::ToolState::Stopped;
                break;
            case model::ToolKind::Dns:
                latest_.dns.state = model::ToolState::Stopped;
                break;
            case model::ToolKind::Trace:
                latest_.trace.state = model::ToolState::Stopped;
                break;
            case model::ToolKind::Listen:
                latest_.listen.state = model::ToolState::Stopped;
                break;
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

    void publish(const model::TraceRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_.trace) {
            latest_.trace = run;
            fresh_ = true;
        }
    }

    void publish(const model::ListenRun& run) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_.listen) {
            latest_.listen = run;
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

    // Finds the IPv4 address of a Ping or Trace target into `destination`
    // and run.address: the target itself, or the first address of a name
    // (one A question, counted as sent). Returns false when the run ended
    // here (stopped, or the name has no address), with that published.
    template <typename Run>
    bool find_destination(const std::string& target, sockaddr_in& destination, Run& run) {
        destination = sockaddr_in{};
        destination.sin_family = AF_INET;
        if (model::is_ipv4_text(target) && inet_pton(AF_INET, target.c_str(), &destination.sin_addr) == 1) {
            run.address = target;
            return true;
        }
        const Lookup found = lookup(target, false);
        if (found.stopped) {
            run.state = model::ToolState::Stopped;
            publish(run);
            return false;
        }
        if (found.error != model::ToolError::None || found.addresses.empty() ||
            inet_pton(AF_INET, found.addresses.front().c_str(), &destination.sin_addr) != 1) {
            run.state = model::ToolState::Failed;
            run.error = found.error == model::ToolError::None ? model::ToolError::NoAddress : found.error;
            run.reason = found.reason;
            publish(run);
            return false;
        }
        run.address = found.addresses.front();
        return true;
    }

    // An unprivileged ICMP datagram socket: allowed for the groups in
    // net.ipv4.ping_group_range, which this code only reads about. Returns
    // -1 when the system refuses it, with the run failed and published.
    template <typename Run>
    int open_icmp_socket(Run& run) {
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
        }
        return fd;
    }

    void run_ping(const std::string& target) {
        model::PingRun run;
        run.state = model::ToolState::Running;
        run.target = target;
        publish(run);

        sockaddr_in destination{};
        if (!find_destination(target, destination, run)) {
            return;
        }
        publish(run);

        const int fd = open_icmp_socket(run);
        if (fd < 0) {
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
                fill_echo(packet, sequence);
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

    // A trace: one echo per hop on an ICMP datagram socket, each with a time
    // to live one higher than the last. A hop the echo dies at answers with
    // Time exceeded, which the system puts on the socket's error queue
    // (IP_RECVERR) with the hop's address and the start of the echo, so its
    // sequence number (the time to live here); the target answers with an
    // echo reply. The next echo goes out as soon as a hop answers, or after
    // kHopTimeout. Each hop that answers gets a PTR question at once
    // (ReverseNames); the run ends when every name is in or given up.
    void run_trace(const std::string& target) {
        model::TraceRun run;
        run.state = model::ToolState::Running;
        run.target = target;
        publish(run);

        sockaddr_in destination{};
        if (!find_destination(target, destination, run)) {
            return;
        }
        publish(run);

        const int fd = open_icmp_socket(run);
        if (fd < 0) {
            return;
        }
        const int on = 1;
        if (::setsockopt(fd, IPPROTO_IP, IP_RECVERR, &on, sizeof(on)) != 0) {
            run.state = model::ToolState::Failed;
            run.error = model::ToolError::Failed;
            run.reason = error_text(errno);
            ::close(fd);
            publish(run);
            return;
        }

        ReverseNames names;
        bool waiting = false;  // the newest hop's echo has no answer yet
        Clock::time_point sent_at;
        uint8_t packet[kEchoSize];
        uint8_t buffer[256];
        bool changed = false;
        // The newest hop answered from `address`.
        auto answered = [&](const std::string& address, Clock::time_point when, model::TraceEnd end) {
            model::TraceHop& hop = run.hops.back();
            hop.address = address;
            hop.time_us = static_cast<int32_t>(std::clamp<int64_t>(micros_between(sent_at, when), 0, INT32_MAX));
            hop.name = names.ask(run.hops.size() - 1, address, when);
            run.end = end;
            waiting = false;
            changed = true;
        };
        // Whether `sequence` is the newest hop's, which still waits.
        auto expected = [&](int sequence) {
            return waiting && sequence == static_cast<int>(run.hops.size());
        };

        while (true) {
            if (stopping()) {
                run.state = model::ToolState::Stopped;
                break;
            }
            Clock::time_point now = Clock::now();
            changed = false;

            // The next echo, with one more hop to live.
            if (run.end == model::TraceEnd::None && !waiting) {
                if (static_cast<int32_t>(run.hops.size()) >= model::kTraceMaxHops) {
                    run.end = model::TraceEnd::MaxHops;
                    changed = true;
                } else {
                    const int ttl = static_cast<int>(run.hops.size()) + 1;
                    fill_echo(packet, static_cast<uint16_t>(ttl));
                    const Clock::time_point when = Clock::now();
                    if (::setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) != 0 ||
                        ::sendto(fd, packet, sizeof(packet), MSG_NOSIGNAL,
                                 reinterpret_cast<const sockaddr*>(&destination),
                                 sizeof(destination)) != static_cast<ssize_t>(sizeof(packet))) {
                        run.state = model::ToolState::Failed;
                        run.error = model::ToolError::Failed;
                        run.reason = error_text(errno);
                        break;
                    }
                    count_sent();
                    model::TraceHop hop;
                    hop.ttl = ttl;
                    run.hops.push_back(hop);
                    sent_at = when;
                    waiting = true;
                    changed = true;
                }
            }

            // The hops' answers, from the error queue. All of it is read,
            // so the socket does not stay readable (POLLERR).
            ProbeError error;
            for (int count = 0; count < kMaxReads && take_probe_error(fd, error); ++count) {
                if (error.usable && expected(error.sequence)) {
                    answered(error.from, Clock::now(),
                             error.type == kIcmpUnreachable ? model::TraceEnd::Unreachable : model::TraceEnd::None);
                }
            }

            // The target's echo reply. The system hands over only replies to
            // this socket's identifier, without the IP header. An error it
            // reports here goes with a message of the error queue, read above.
            for (int count = 0; count < kMaxReads; ++count) {
                sockaddr_in from{};
                socklen_t from_length = sizeof(from);
                const ssize_t size = ::recvfrom(fd, buffer, sizeof(buffer), MSG_DONTWAIT,
                                                reinterpret_cast<sockaddr*>(&from), &from_length);
                if (size < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    continue;
                }
                if (static_cast<std::size_t>(size) < kIcmpHeader || buffer[0] != kIcmpEchoReply || buffer[1] != 0 ||
                    !expected((buffer[6] << 8) | buffer[7])) {
                    continue;
                }
                const std::string address = ipv4_text(from.sin_addr);
                answered(address.empty() ? run.address : address, Clock::now(), model::TraceEnd::Reached);
            }

            // A hop with no answer in time.
            now = Clock::now();
            if (waiting && now >= sent_at + kHopTimeout) {
                run.hops.back().time_us = model::kPingLost;
                waiting = false;
                changed = true;
            }

            names.receive(now, [&run, &changed](std::size_t index, const std::string& name) {
                if (index < run.hops.size() && run.hops[index].name != name) {
                    run.hops[index].name = name;
                    changed = true;
                }
            });
            names.retry(now);

            if (changed) {
                publish(run);
            }
            if (run.end != model::TraceEnd::None && !waiting && !names.pending()) {
                run.state = model::ToolState::Done;
                break;
            }

            // Waits for an answer, a time-out, or not at all when the next
            // echo is due.
            Clock::time_point wake = names.next_deadline();
            if (waiting) {
                wake = std::min(wake, sent_at + kHopTimeout);
            } else if (run.end == model::TraceEnd::None) {
                wake = now;
            }
            std::vector<pollfd> entries{{fd, POLLIN, 0}};
            names.add_poll_entries(entries);
            ::poll(entries.data(), entries.size(), wait_slice(now, wake));
        }
        ::close(fd);
        publish(run);
    }

    // Listens on `interface_name` for model::kListenSeconds through the
    // capture helper, which alone may open the packet socket and only
    // receives; its stream is read here without privileges. Sends nothing.
    void run_listen(const std::string& interface_name) {
        model::ListenRun run;
        run.state = model::ToolState::Running;
        run.interface_name = interface_name;
        {
            // The run before is gone; so is what it kept.
            std::lock_guard<std::mutex> lock(mutex_);
            kept_ = std::vector<uint8_t>();
            kept_full_ = false;
        }
        HeardDevices heard(read_own_ports());
        CaptureReader reader;
        reader.keep(model::kCaptureSaveMax);
        CaptureFile file;
        std::size_t file_upto = 0;  // how much of what the reader kept is in the file
        CaptureProcess capture;
        if (!capture.start(interface_name, model::kListenSeconds, run.error, run.reason)) {
            run.state = model::ToolState::Failed;
            publish(run);
            return;
        }

        const Clock::time_point begin = Clock::now();
        // The helper ends itself at its time limit; should it stay longer,
        // it is ended here a little after.
        const Clock::time_point give_up = begin + std::chrono::seconds(model::kListenSeconds + 5);
        enum class End { Running, Stopped, Closed, Broken, Overdue };
        End end = End::Running;
        uint8_t buffer[4096];
        while (end == End::Running) {
            if (stopping()) {
                end = End::Stopped;
                break;
            }
            pollfd entry{capture.fd(), POLLIN, 0};
            ::poll(&entry, 1, kWaitSliceMs);
            const Clock::time_point now = Clock::now();
            const auto at_s = static_cast<int32_t>(std::chrono::duration_cast<std::chrono::seconds>(now - begin).count());
            const auto on_frame = [&heard, at_s](const uint8_t* frame, std::size_t size, uint32_t /*seconds*/) {
                heard.note_frame(frame, size, at_s);
            };
            for (int reads = 0; reads < kMaxReads && end == End::Running; ++reads) {
                const ssize_t got = ::read(capture.fd(), buffer, sizeof(buffer));
                if (got < 0 && errno == EINTR) {
                    continue;
                }
                if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                }
                if (got <= 0) {
                    end = End::Closed;  // the helper is done (or failed)
                } else if (!reader.feed(buffer, static_cast<std::size_t>(got), on_frame)) {
                    end = End::Broken;
                }
            }
            if (end == End::Running && now >= give_up) {
                end = End::Overdue;
            }
            run.elapsed_s = std::min(at_s, model::kListenSeconds);
            run.frames = heard.frames();
            run.devices = heard.devices();
            if (run.save == model::CaptureSave::None && save_flag_.exchange(false)) {
                if (file.create(capture_folder_, run.save_reason)) {
                    run.save = model::CaptureSave::Saving;
                    run.save_name = file.name();
                } else {
                    run.save = model::CaptureSave::Failed;
                }
            }
            save_more(file, reader, file_upto, run);
            if (end == End::Running) {
                publish(run);
            }
        }

        switch (end) {
            case End::Stopped:
                capture.stop();
                run.state = model::ToolState::Stopped;
                break;
            case End::Broken:
                capture.stop();
                run.state = model::ToolState::Failed;
                run.error = model::ToolError::Failed;
                run.reason = "bad capture data";
                break;
            case End::Overdue:
                capture.stop();
                run.state = model::ToolState::Done;
                break;
            case End::Closed:
            case End::Running: {
                const int status = capture.finish();
                run.state = capture_exit_ok(status, run.error, run.reason) ? model::ToolState::Done
                                                                           : model::ToolState::Failed;
                break;
            }
        }
        if (file.is_open()) {
            save_more(file, reader, file_upto, run);
            file.close();
            if (run.save == model::CaptureSave::Saving) {
                run.save = model::CaptureSave::Saved;
            }
        }
        // The end of the run and what it kept show together, so a save
        // asked from now on finds them both.
        std::lock_guard<std::mutex> lock(mutex_);
        if (run != latest_.listen) {
            latest_.listen = run;
            fresh_ = true;
        }
        if (run.save == model::CaptureSave::None) {
            kept_ = reader.take_kept();
            kept_full_ = reader.kept_full();
        }
    }

    // While a listen run is saved: adds what the reader kept since the last
    // call to the file, and notes the file's size in `run`.
    static void save_more(CaptureFile& file,
                          const CaptureReader& reader,
                          std::size_t& file_upto,
                          model::ListenRun& run) {
        if (run.save != model::CaptureSave::Saving) {
            return;
        }
        const std::vector<uint8_t>& kept = reader.kept();
        if (file.is_open() && kept.size() > file_upto) {
            if (file.append(kept.data() + file_upto, kept.size() - file_upto, run.save_reason)) {
                file_upto = kept.size();
            } else {
                run.save = model::CaptureSave::Failed;
            }
        }
        run.save_bytes = file.bytes();
        run.save_full = reader.kept_full();
    }

    // Saves the latest listen run, which has ended, from what it kept.
    void save_kept() {
        std::vector<uint8_t> bytes;
        bool full = false;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            model::ListenRun& run = latest_.listen;
            if (run.save != model::CaptureSave::None || run.state == model::ToolState::Idle ||
                run.state == model::ToolState::Running) {
                return;
            }
            bytes = std::move(kept_);
            kept_ = std::vector<uint8_t>();
            full = kept_full_;
            generation = listen_generation_;
            run.save = model::CaptureSave::Saving;
            fresh_ = true;
        }
        CaptureFile file;
        std::string reason;
        const bool saved = !bytes.empty() && file.create(capture_folder_, reason) &&
                           file.append(bytes.data(), bytes.size(), reason);
        if (bytes.empty()) {
            reason = "nothing to save";
        }
        file.close();
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != listen_generation_) {
            return;  // a new listen started meanwhile
        }
        model::ListenRun& run = latest_.listen;
        run.save = saved ? model::CaptureSave::Saved : model::CaptureSave::Failed;
        run.save_name = file.name();
        run.save_reason = saved ? std::string() : reason;
        run.save_bytes = file.bytes();
        run.save_full = full;
        fresh_ = true;
    }

    std::thread thread_;
    // The job slot, the flags and the latest runs; the mutex guards all of
    // them. The stop flag is read without the mutex in every wait.
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_flag_{false};
    // A save was asked (save_capture()); read and cleared by the thread.
    std::atomic<bool> save_flag_{false};
    bool quit_{false};
    bool closed_{false};
    bool busy_{false};     // a job was started and has not ended yet
    bool has_job_{false};  // job_ waits for the thread
    ToolJob job_;
    model::ToolsData latest_;  // sent is not used
    bool fresh_{false};
    // What the latest listen run kept, until it is saved or the next listen
    // starts; and which listen run is the latest.
    std::vector<uint8_t> kept_;
    bool kept_full_{false};
    uint64_t listen_generation_{0};
    const std::string capture_folder_;
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
