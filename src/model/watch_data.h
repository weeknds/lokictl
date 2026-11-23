/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Watch: passive alerts about the network LokiCTL is on. It only listens
// (through the capture helper's `watch` command) and warns; it never sends
// and never blocks anything.
namespace model {

// Every threshold of Watch, in one place.
// A scan: one source tries this many different ports on the Cardputer...
constexpr int32_t kWatchScanPorts = 10;
// ...within this many seconds.
constexpr int32_t kWatchScanWindowS = 10;
// An ARP flood: more than this many ARP frames in one second.
constexpr int32_t kWatchArpFloodPerSecond = 50;
// After a scan or flood alert, the same source (or the flood) raises no new
// alert until it has been quiet this long.
constexpr int32_t kWatchQuietS = 60;
// Two devices on one address: a second hardware address claims an address
// whose first one was heard this recently.
constexpr int32_t kWatchSameAddressWindowS = 120;
// With no known devices yet, the first this many seconds learn the devices
// heard instead of raising "New device" for each.
constexpr int32_t kWatchLearnSeconds = 60;
// The log keeps the newest this many alerts (in memory only).
constexpr std::size_t kWatchMaxAlerts = 100;
// The saved list of known devices holds at most this many.
constexpr std::size_t kWatchKnownMax = 64;
// The helper is run for this long at a time, then run again.
constexpr int32_t kWatchHelperSeconds = 1800;

enum class WatchAlertKind : int32_t {
    NewDevice = 0,  // a hardware address not on the known list
    FakeRouter,     // the router's address answers from another hardware address
    SameAddress,    // two hardware addresses claim one IPv4 address
    SecondDhcp,     // a second server hands out addresses
    Ipv6Router,     // an IPv6 router announcement from an unknown router
    Scan,           // one source tries many ports on the Cardputer
    ArpFlood,       // very many ARP frames in one second
};

constexpr std::size_t kWatchAlertKindCount = 7;

// The short name a list row shows ("Scan on you"), at most 16 characters.
const char* watch_alert_name(WatchAlertKind kind);
// The full plain explanation and what to do, for the detail view only.
const char* watch_alert_about(WatchAlertKind kind);
// The kind's name in the log ("scan"): never an address.
const char* watch_alert_log_name(WatchAlertKind kind);

struct WatchAlert {
    WatchAlertKind kind{WatchAlertKind::NewDevice};
    std::string time;      // local time it was raised, "07:46"
    std::string address;   // the IPv4 (or IPv6) address of who it is about; empty when not known
    std::string hardware;  // the hardware address of who it is about; empty when not known
    std::string before;    // the hardware address that held it before (FakeRouter, SameAddress),
                           // or the first DHCP server's address (SecondDhcp); else empty
    int32_t count{0};      // Scan: different ports tried; ArpFlood: ARP frames in the busiest second
};

bool operator==(const WatchAlert& left, const WatchAlert& right);
bool operator!=(const WatchAlert& left, const WatchAlert& right);

// One Watch run, as the page shows it.
struct WatchRun {
    ToolState state{ToolState::Idle};  // Running, Stopped or Failed (never Done)
    ToolError error{ToolError::None};
    std::string reason;                // with ToolError::Failed: a short reason
    std::string interface_name;        // the interface it listens on
    int32_t elapsed_s{0};              // how long it has run
    int32_t learn_left_s{0};           // while learning the network: seconds left; else 0
    int32_t frames{0};                 // frames heard
    int32_t raised{0};                 // alerts raised in this run (also those the log dropped)
    std::vector<WatchAlert> alerts;    // the newest first, at most kWatchMaxAlerts
    std::vector<std::string> learned;  // hardware addresses learned as known while learning
};

bool operator==(const WatchRun& left, const WatchRun& right);
bool operator!=(const WatchRun& left, const WatchRun& right);

// What the Watch page shows: the run and the saved list of known devices.
struct WatchData {
    WatchRun run;
    std::vector<std::string> known;  // hardware addresses, "02:00:00:00:00:01"
};

bool operator==(const WatchData& left, const WatchData& right);
bool operator!=(const WatchData& left, const WatchData& right);

bool watch_known(const WatchData& data, const std::string& hardware);

} // namespace model
