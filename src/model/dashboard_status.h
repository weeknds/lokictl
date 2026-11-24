/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dashboard_findings.h"
#include "home_data.h"
#include "tool_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
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

// The health check as the Security page shows it and may start it: always
// the real run (never the made-up one of sample_dashboard()), and the same
// gate as the Health page (health_start()).
struct DashCheck {
    ToolState state{ToolState::Idle};
    HealthStep step{HealthStep::Sweep};
    int32_t asked{0};      // addresses asked of `total`
    int32_t total{0};
    int32_t scanned{0};    // devices scanned of `hosts`
    int32_t hosts{0};
    int32_t elapsed_s{0};
    std::string reason;    // why it failed, a few lowercase words; empty unless Failed
    bool can{false};       // a check may start now
    std::string why;       // why not (health_gate_text()); empty when it may
    std::string asks;      // what the sweep asks, "10.0.0.1-254"; empty when not known
};

DashCheck dashboard_check(const HomeData& home, const ToolsData& tools);

// The VPN leak test as the Modes page shows it and may start it: the Leak
// page's gate (no tool running, the tunnel not being set up) and its one
// line per check, for a run that still describes the VPN as it is
// (leak_run_current()).
struct DashLeak {
    ToolState state{ToolState::Idle};
    bool shown{false};    // `lines` belong to a finished run that is current
    int32_t fails{0};
    int32_t unknown{0};
    std::vector<std::pair<std::string, LeakVerdict>> lines; // leak_line() text and verdict, in LeakCheck order
    std::string reason;   // why it failed; empty unless Failed
    bool can{false};      // a test may start now
    std::string why;      // why not; empty when it may
};

DashLeak dashboard_leak(const HomeData& home, const ToolsData& tools);

// What the dashboard shows besides the home data: the findings, Watch and
// the trap, the keep-mode setting, the traffic log, and what the Modes
// page needs that only the app knows.
struct DashboardParts {
    DashOverview overview;
    DashCheck check;
    bool watching{false};    // Watch is listening
    bool trap_on{false};     // the trap runs
    int32_t trap_ports{0};   // its fake services that are open
    bool keep_mode{false};   // the mode stays after quitting LokiCTL
    std::vector<std::array<int32_t, 2>> traffic;  // TrafficLog::samples()
    // Why each mode (NetMode order) can't be asked for now, in the `:mode`
    // command's words (app::mode_command_refusal()); empty when it can.
    std::array<std::string, kNetModeCount> mode_why;
    std::string hotspot_name; // the name Router and Share start with (the view model's)
    std::string vpn_pick;     // the WireGuard file a VPN request would use (BaseViewModel::vpn_file())
    bool vpn_add{false};      // a WireGuard file may be added from the browser
    DashLeak leak;
    // The Tools and Watch pages' object (app::dashboard_tools_json()), as
    // JSON text; written as "tools" when it is not empty.
    std::string tools_json;
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
//          "kill_switch":false,"keep":false,"laptop_address":"10.42.0.1",
//          "switch":{"state":"failed","target":"share","error":"Back to Client first"},
//          "why":{"client":"","router":"Already in Router mode","share":"","vpn":"",
//                 "laptop":"Back to Client first"},
//          "next_hotspot_name":"LokiCTL-7F","vpn_files":["home","travel"],
//          "vpn_file":"home","vpn_server":"","vpn_active":false,"gateway_down":false,
//          "vpn_add":true,
//          "leak":{"state":"done","shown":true,"fails":0,"unknown":1,
//                  "lines":[{"text":"Name lookups: through VPN","verdict":"pass"}],
//                  "reason":"","can":true,"why":""}},
//  "block":{"failures":0,"message":""},
//  "traffic":{"down_kbps":1200,"up_kbps":80,"history":[[1100,70],...]},
//  "counts":{"devices":12,"security":1},
//  "check":{"state":"running","step":"sweep","asked":120,"total":254,"scanned":0,
//           "hosts":0,"elapsed_s":12,"reason":"","can":false,"why":"A health check is running",
//           "asks":"10.0.0.1-254","max_devices":32,"max_ports":50},
//  "overview":{"checked":true,"checking":false,"checked_at":"21:44","took_s":94,
//              "fix":1,"look":2,"know":2,"total":12,"device_fix":1,
//              "device_look":2,"fine":9,"unchecked":0,"device_new":2,"hotspot":3,
//              "list_date":"2026-11-23",
//              "things":[{"id":"10.0.0.43","tier":"fix","icon":"shield","title":"...","hint":"...",
//                         "action":"...","fix":"...","device":"10.0.0.43","time":"21:40",
//                         "about":"...","steps":["...","..."],"where":"10.0.0.43 · port 22",
//                         "also":["..."]}],
//              "devices":[{"id":"10.0.0.43","name":"Smart TV","address":"10.0.0.43",
//                          "ring":"cable","router":false,"new":false,"checked":true,
//                          "tier":"fix","tag":"2 holes","services":"...","seen":"now",
//                          "kind":"","hotspot":false,"blocked":false,
//                          "ports":[{"port":22,"name":"Remote login","version":"Dropbear 2015.67",
//                                    "tier":"fix","tag":"2 holes"}],"more_ports":0,
//                          "holes":[{"port":22,"cve":"CVE-2016-7406","score":98,"used":false,
//                                    "text":"..."}],"more_holes":0,
//                          "issues":[{"tier":"fix","title":"2 security holes","about":"...",
//                                     "steps":["...","..."],"port":22}]}]},
//  "watch":{"on":true,"trap":true,"trap_ports":6,"feed":[{"time":"21:42","text":"..."}]},
//  "tools":{...}}
//
// "link" is "wifi", "cable" or "none"; "internet" is internet_name();
// "id" is "client", "router", "share", "vpn" or "laptop" (whose "name" is
// "Laptop link"); "laptop_address" is Laptop link's fixed address
// (kLaptopLinkAddress); "vpn" is vpn_state_name();
// "switch" is the last switch the user asked for (HomeData::mode_switch):
// "state" is switch_state_name(), "error" the Modes page's words for a
// failed one (empty otherwise); "vpn_file" is the tunnel's file while
// "vpn_active" (the tunnel is being set up or up), else "vpn_pick";
// "gateway_down" is the kill switch holding hotspot devices back without
// the tunnel; "verdict" is "waiting", "pass", "fail", "unknown" or "info".
// Nothing secret is in it: no password, no key, only file names and the
// tunnel's server;
// "state" is "idle", "running", "done", "stopped" or "failed"; "step" is
// "sweep" or "ports"; "max_devices" and "max_ports" are what one check
// scans at most (kHealthMaxHosts, common_ports()); "took_s" is null until
// a check finished; "tier" is finding_tier_name(); "ring" is "cable" or
// "wireless"; "total" counts every device, also those the list leaves out;
// "score" is in tenths, null when not known; "block" is
// HomeData::block_failures and block_message; "feed" is the newest 5 of
// DashOverview::watch_lines; "tools" is DashboardParts::tools_json as it
// is (left out when empty).
std::string dashboard_status_json(const HomeData& home, const DashboardParts& parts = DashboardParts());

} // namespace model
