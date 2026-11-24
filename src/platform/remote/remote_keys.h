/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

namespace platform::remote {

// The key a browser names in POST /key (k=<name>), as the key path knows it
// (an LV_KEY_* code, platform::kKeyHelp or a printable ASCII character):
// - "c32" to "c126": that ASCII character by its code. Form bodies are not
//   decoded, so characters such as '&', '=', '+' or '%' can't be sent as
//   themselves.
// - "esc", "enter", "backspace", "up", "down", "left", "right", "help".
// Returns false for anything else. There is no name for a held key or for
// fn+J: a browser can only press and let go.
bool key_from_name(std::string_view name, uint32_t& key);

// The keys a paired browser sent and the UI thread has not pressed yet. The
// server's thread adds them, the UI thread takes them, both through the
// mutex. At most kMaxWaiting wait, and at most kMaxPerSecond are taken in
// any one second (counted in fixed one-second windows).
class KeyQueue {
public:
    static constexpr std::size_t kMaxWaiting = 16;
    static constexpr int kMaxPerSecond = 20;

    enum class Push {
        Added,
        Full,    // kMaxWaiting keys wait already
        TooFast, // kMaxPerSecond keys came in this second already
    };

    // Server thread. `now_ms` is on the steady clock.
    Push push(uint32_t key, int64_t now_ms);
    // UI thread: the oldest waiting key. Returns false when none waits.
    bool pop(uint32_t& key);
    // Forgets the waiting keys and the count of this second.
    void clear();

private:
    std::mutex mutex_;
    std::deque<uint32_t> keys_;
    int64_t window_start_ms_{0};
    int window_count_{0};
};

// The Block and Allow requests of a paired browser (POST /api/v1/block):
// which device (its IPv4 address on the hotspot, dotted) and whether to
// block it or let it through again. The server's thread adds them, the UI
// thread takes them, both through the mutex. At most kMaxWaiting wait; the
// UI thread decides whether each one applies.
struct BlockRequest {
    std::string address;
    bool block{true};
};

class BlockQueue {
public:
    static constexpr std::size_t kMaxWaiting = 4;

    // Server thread. False when kMaxWaiting requests wait already.
    bool push(BlockRequest request);
    // UI thread: the oldest waiting request. Returns false when none waits.
    bool pop(BlockRequest& request);
    // Forgets the waiting requests.
    void clear();

private:
    std::mutex mutex_;
    std::deque<BlockRequest> requests_;
};

// The paired browser's other requests: start or stop a health check
// (POST /api/v1/check), save a new report (POST /api/v1/report), the
// Modes page's controls (POST /api/v1/mode, /vpn, /hotspot, /keep and
// /leak) and the Tools and Watch pages' (POST /api/v1/tool, /lan, /survey,
// /watch and /trap). The server's thread adds them, the UI thread takes
// them, both through the mutex. At most kMaxWaiting wait; the UI thread
// decides whether each one applies.
enum class Action : int32_t {
    StartCheck,
    StopCheck,
    SaveReport,
    Mode,            // text: "client", "router", "share" or "laptop"
    VpnOn,           // the VPN with the file picked last
    VpnOff,
    VpnFile,         // text: the WireGuard file to pick, by name without ".conf"
    HotspotName,     // text: the new name; empty puts the default name back
    HotspotPassword, // a new random password
    KeepOn,
    KeepOff,
    StartLeak,
    StopLeak,
    ToolRun,         // tool: "ping", "dns", "trace", "ports" or "speed"; text: the target (empty for speed)
    ToolStop,        // tool: the one to stop, only while it runs
    LanTest,         // text: the address to test against
    LanStop,
    SurveyStart,
    SurveyStop,
    WatchStart,
    WatchStop,
    TrapStart,
    TrapStop,
};

struct ActionRequest {
    Action action{Action::SaveReport};
    std::string text; // decoded already; checked by the UI thread
    std::string tool; // ToolRun and ToolStop: the tool's name; else empty
};

class ActionQueue {
public:
    static constexpr std::size_t kMaxWaiting = 4;

    // Server thread. False when kMaxWaiting requests wait already.
    bool push(Action action, std::string text = {}, std::string tool = {});
    // UI thread: the oldest waiting request. Returns false when none waits.
    bool pop(ActionRequest& request);
    // Forgets the waiting requests.
    void clear();

private:
    std::mutex mutex_;
    std::deque<ActionRequest> actions_;
};

// True for an IPv4 address written as four decimal numbers of 0 to 255
// joined by dots, without leading zeros ("10.42.0.11").
bool dotted_ipv4(std::string_view text);

} // namespace platform::remote
