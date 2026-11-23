/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The trap (a honeypot): the Cardputer pretends to offer a few services.
// Nothing real runs there, so anyone who connects is probing the network.
// Each connection gets a harmless fake greeting, its first bytes are read,
// and it is closed; nothing is ever run or passed on.
namespace model {

// The most connections logged in one minute, in all and per source; more
// are closed at once and only counted.
constexpr int32_t kTrapPerMinute = 20;
constexpr int32_t kTrapPerSourcePerMinute = 5;
// Each connection is read for at most this long, and this many bytes.
constexpr int32_t kTrapReadSeconds = 5;
constexpr std::size_t kTrapReadMax = 512;
// The first bytes kept for the log: printable, at most this many.
constexpr std::size_t kTrapShownMax = 60;
// The log keeps the newest this many connections (in memory only).
constexpr std::size_t kTrapMaxHits = 100;

// What a fake service says first.
enum class TrapGreeting : int32_t {
    None = 0,  // nothing (binary protocols: file sharing, remote desktop)
    Ftp,       // "220 FTP server ready"
    Telnet,    // a login prompt
    Web,       // a plain web page, once the request has come
};

// One fake service. A port under 1024 needs a redirect (the firewall
// helper's trap-on) from `port` to `listen_port`, which LokiCTL listens on;
// the others are listened on directly. The same fixed map is in
// src/helper/lokictl_helper.c.
struct TrapService {
    uint16_t port;         // the port it pretends to serve
    uint16_t listen_port;  // the port LokiCTL listens on
    const char* word;      // what a list row says it tried ("web")
    const char* name;      // the detail's plain name ("Web page (HTTP)")
    TrapGreeting greeting;
};

constexpr std::size_t kTrapServiceCount = 6;
const std::array<TrapService, kTrapServiceCount>& trap_services();
// The service for `port` (the one it pretends to serve, or its listen
// port); nullptr for any other.
const TrapService* trap_service(uint16_t port);

enum class TrapPortState : int32_t {
    Off = 0,  // not open (the trap is off)
    Open,     // listened on, and reachable
    InUse,    // something on the Cardputer already listens there: left alone
    NoHelper, // a low port the firewall helper could not send on
};

struct TrapPort {
    uint16_t port{0};
    TrapPortState state{TrapPortState::Off};
};

bool operator==(const TrapPort& left, const TrapPort& right);
bool operator!=(const TrapPort& left, const TrapPort& right);

// One connection to the trap.
struct TrapHit {
    std::string time;         // local time it came, "13:05"
    std::string address;      // who connected (IPv4)
    uint16_t source_port{0};  // from which port
    uint16_t port{0};         // the fake service's port it tried
    std::string first_bytes;  // what it sent first (trap_shown_text()); empty: nothing yet
};

bool operator==(const TrapHit& left, const TrapHit& right);
bool operator!=(const TrapHit& left, const TrapHit& right);

// One trap run, as the page shows it.
struct TrapRun {
    ToolState state{ToolState::Idle};  // Running, Stopped or Failed (never Done)
    ToolError error{ToolError::None};
    std::string reason;                // with ToolError::Failed: a short reason
    std::string helper_problem;        // why the low ports are not reachable ("helper not installed")
    int32_t elapsed_s{0};              // how long it has run
    std::vector<TrapPort> ports;       // in trap_services() order; empty before the first run
    int32_t raised{0};                 // connections logged in this run (also those the log dropped)
    int32_t dropped{0};                // connections closed at once (over the limits)
    std::vector<TrapHit> hits;         // the newest first, at most kTrapMaxHits
};

bool operator==(const TrapRun& left, const TrapRun& right);
bool operator!=(const TrapRun& left, const TrapRun& right);

// How many of `run.ports` are open.
int32_t trap_open_ports(const TrapRun& run);

// What a connection sent, for the log: printable ASCII only; tabs, line
// ends and runs of them become one space, any other byte a '.', at most
// kTrapShownMax characters, no space at either end.
std::string trap_shown_text(const uint8_t* bytes, std::size_t size);

// The local TCP ports something listens on, from the text of
// /proc/net/tcp or /proc/net/tcp6 (any address, any user; the first line
// holds the column names): sorted, each once. Lines it cannot read are
// skipped.
std::vector<uint16_t> parse_listening_ports(const std::string& text);

// The limits on what is logged (kTrapPerMinute in all,
// kTrapPerSourcePerMinute per source, in any 60 s). It keeps only the
// connections it let through in the last minute, so it never grows past
// kTrapPerMinute entries, whatever a flood of sources does.
class TrapLimiter {
public:
    // True (and counted) when a connection from `source` at `now_ms` may be
    // logged. Times must not go back.
    bool allow(uint32_t source, int64_t now_ms);

private:
    struct Entry {
        uint32_t source{0};
        int64_t at_ms{0};
    };
    std::vector<Entry> recent_;
};

} // namespace model
