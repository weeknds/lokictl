/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dashboard_findings.h"
#include "home_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

// The last minute of traffic for the web dashboard's chart: one pair of
// rates (kbit/s, down and up) a second, oldest first. The Remote view
// controller keeps it while its server runs.
class TrafficLog {
public:
    static constexpr std::size_t kLength = 60;

    void clear();
    // Adds the newest pair and drops the oldest one beyond kLength. A
    // negative (not known) rate counts as 0.
    void push(int32_t down_kbps, int32_t up_kbps);
    const std::vector<std::array<int32_t, 2>>& samples() const { return samples_; }

private:
    std::vector<std::array<int32_t, 2>> samples_;
};

// What the dashboard shows besides the home data: the findings, Watch and
// the trap, the keep-mode setting and the traffic log.
struct DashboardParts {
    DashOverview overview;
    bool watching{false};    // Watch is listening
    bool trap_on{false};     // the trap runs
    int32_t trap_ports{0};   // its fake services that are open
    bool keep_mode{false};   // the mode stays after quitting LokiCTL
    std::vector<std::array<int32_t, 2>> traffic;  // TrafficLog::samples()
};

// The web dashboard's GET /api/v1/status: what the app already knows, as
// one JSON object. Built on the UI thread from the data the app has; it
// never asks the system for anything. Every text from the network (the
// Wi-Fi name, device names, addresses) goes through json_text(). Unknown
// numbers are null.
//
// {"v":1,"sample":false,
//  "cardputer":{"address":"10.0.0.12","battery":87,"charging":false},
//  "network":{"name":"Home","link":"wifi","internet":"full"},
//  "mode":{"id":"router","name":"Router","hotspot":true,"hotspot_devices":3,
//          "hotspot_name":"LokiCTL-7F","vpn":"off","vpn_reply_s":null,
//          "kill_switch":false,"keep":false},
//  "traffic":{"down_kbps":1200,"up_kbps":80,"history":[[1100,70],...]},
//  "counts":{"devices":12,"security":1},
//  "overview":{"checked":true,"checking":false,"checked_at":"21:44",
//              "fix":1,"look":2,"know":2,"total":12,"device_fix":1,
//              "device_look":2,"fine":9,"unchecked":0,
//              "things":[{"tier":"fix","icon":"shield","title":"...","hint":"...",
//                         "action":"...","fix":"...","device":"10.0.0.43","time":"21:40"}],
//              "devices":[{"id":"10.0.0.43","name":"Smart TV","address":"10.0.0.43",
//                          "ring":"cable","router":false,"new":false,"checked":true,
//                          "tier":"fix","tag":"2 holes","services":"...","seen":"now"}]},
//  "watch":{"on":true,"trap":true,"trap_ports":6,"feed":[{"time":"21:42","text":"..."}]}}
//
// "link" is "wifi", "cable" or "none"; "internet" is internet_name();
// "id" is "client", "router", "share" or "vpn"; "vpn" is vpn_state_name();
// "tier" is finding_tier_name(); "ring" is "cable" or "wireless"; "total"
// counts every device, also those the list leaves out.
std::string dashboard_status_json(const HomeData& home, const DashboardParts& parts = DashboardParts());

} // namespace model
