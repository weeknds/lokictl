/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace model {

// The tools that can run (the Ping and DNS pages). Only one runs at a time.
enum class ToolKind : int32_t {
    Ping = 0,
    Dns  = 1,
};

const char* tool_name(ToolKind kind); // "ping", "dns"

// Where a tool's latest run stands.
enum class ToolState : int32_t {
    Idle = 0,  // never run
    Running,
    Done,      // ended by itself; the result says how it went
    Stopped,   // ended by the user
    Failed,    // could not run; ToolError says why
};

// Why a run failed or found nothing.
enum class ToolError : int32_t {
    None = 0,
    NotAllowed,  // ping: the system refused the ICMP socket (ping_group_range)
    NoServer,    // no IPv4 nameserver in /etc/resolv.conf
    NoSuchName,  // the DNS server said the name does not exist (NXDOMAIN)
    NoAnswer,    // the DNS server did not answer in time
    NoAddress,   // the name exists but has no address of the kinds asked for
    BadTarget,   // the target is not a valid name or IPv4 address
    Failed,      // anything else; ToolRun::reason says what
};

// Every run sends one echo a second, this many in all.
constexpr int32_t kPingCount = 10;
// An echo with no reply after this long is lost.
constexpr int32_t kPingTimeoutMs = 2000;
// The Ping page shows the last this many replies.
constexpr std::size_t kPingShownReplies = 5;
// A DNS lookup keeps at most this many addresses.
constexpr std::size_t kDnsMaxAddresses = 6;
// A typed target is at most this long.
constexpr std::size_t kToolTargetMax = 63;
// A time not known.
constexpr int32_t kTimeUnknown = -1;

// PingReply::time_us of an echo that is still waiting for its reply, and
// of one given up on.
constexpr int32_t kPingWaiting = -2;
constexpr int32_t kPingLost    = -1;

// One echo of a ping run.
struct PingReply {
    int32_t sequence{0};             // 1 to kPingCount
    int32_t time_us{kPingWaiting};   // round trip in microseconds, kPingWaiting or kPingLost
};

bool operator==(const PingReply& left, const PingReply& right);
bool operator!=(const PingReply& left, const PingReply& right);

// The latest ping run.
struct PingRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;              // with ToolError::Failed, or the last send error
    std::string target;              // as asked: an IPv4 address or a name
    std::string address;             // the IPv4 address pinged; empty while a name is looked up
    int32_t sent{0};                 // echoes handed to the system
    int32_t received{0};             // replies received
    int32_t lost{0};                 // echoes given up on (no reply in kPingTimeoutMs, or the send failed)
    int32_t min_us{kTimeUnknown};
    int32_t avg_us{kTimeUnknown};
    int32_t max_us{kTimeUnknown};
    std::vector<PingReply> replies;  // the last kPingShownReplies echoes, by sequence, oldest first
};

bool operator==(const PingRun& left, const PingRun& right);
bool operator!=(const PingRun& left, const PingRun& right);

// The latest DNS lookup.
struct DnsRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;              // with ToolError::Failed
    std::string name;                // the name looked up
    std::string server;              // the DNS server that answered, or the last one asked (IPv4)
    std::vector<std::string> addresses; // IPv4 first, then IPv6; at most kDnsMaxAddresses
    int32_t time_ms{kTimeUnknown};   // from the first question to the last answer
};

bool operator==(const DnsRun& left, const DnsRun& right);
bool operator!=(const DnsRun& left, const DnsRun& right);

// What the tools show: the latest run of each and the app-wide count of
// packets LokiCTL has sent in this run of the app.
struct ToolsData {
    PingRun ping;
    DnsRun dns;
    uint64_t sent{0};
};

bool operator==(const ToolsData& left, const ToolsData& right);
bool operator!=(const ToolsData& left, const ToolsData& right);

// True while either tool runs.
bool tool_running(const ToolsData& data);

// Whether `text` may be used as a typed target: 1 to kToolTargetMax
// characters, only a-z, 0-9 and '.', no empty label (no dot at either end,
// no two dots in a row). A text of digits and dots only must be an IPv4
// address (four numbers of 0-255). For DNS a name is needed, so an IPv4
// address is refused there.
bool valid_tool_target(ToolKind kind, std::string_view text);

// Whether `text` is an IPv4 address in dotted form ("192.0.2.1").
bool is_ipv4_text(std::string_view text);

} // namespace model
