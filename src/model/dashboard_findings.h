/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "tool_data.h"
#include "trap_data.h"
#include "watch_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The web dashboard's findings: one list of things to fix and one list of
// devices, worked out once on the device from what the app already knows
// (the health check, the Ports page's holes, Watch's alerts, Discover's
// list and the hotspot's devices) and shared by every page of the
// dashboard. The page shows these numbers as they are and never counts
// again. Everything here is plain words; official numbers stay in the
// detail pages.
namespace model {

// How much a thing matters. The same three tiers on every screen: Fix soon
// (a health finding's Risk), Worth a look (Warning) and Good to know
// (Info); Fine for a device with nothing of those.
enum class FindingTier : int32_t {
    Fine = 0,
    Know,  // Good to know
    Look,  // Worth a look
    Fix,   // Fix soon
};

// "ok", "know", "look", "fix" (the JSON's words).
const char* finding_tier_name(FindingTier tier);

// The most things and devices the dashboard lists.
constexpr std::size_t kDashMaxThings = 32;
constexpr std::size_t kDashMaxDevices = 64;
// The most a device's page lists of each.
constexpr std::size_t kDashMaxPorts = 8;
constexpr std::size_t kDashMaxHoles = 4;
constexpr std::size_t kDashMaxIssues = 4;
// The most other findings a thing names (DashThing::also).
constexpr std::size_t kDashMaxAlso = 4;

// One thing to know about: everything found about one subject (a device,
// the Wi-Fi, the cable link, the network), at its worst finding's tier,
// in that finding's words.
struct DashThing {
    std::string id;      // its subject, one per thing: a device's id, "own", "wifi", "cable", "network"
                         // (or an address the device list does not have)
    FindingTier tier{FindingTier::Know};
    std::string icon;    // "shield", "lock", "screen", "wifi", "cable" or "eye"
    std::string title;   // "Smart TV has 2 security holes"
    std::string hint;    // what to do, lowercase, no full stop: "update the device's software"
    std::string action;  // the verdict's link: "Update Smart TV"
    std::string fix;     // the phone's fix button: "Fix Smart TV"
    std::string device;  // DashDevice::id of the device it is about; empty for the rest
    std::string time;    // local time it was found, "21:40"; empty when not known
    // The Security page's detail of the worst finding: what it means, 1 to
    // 3 plain numbered steps, and who it is about ("10.0.0.43 · port 22",
    // "Your Wi-Fi"); then the subject's other findings by title (at most
    // kDashMaxAlso).
    std::string about;
    std::vector<std::string> steps;
    std::string where;
    std::vector<std::string> also;
};

// One open service of a device, for its page.
struct DashPort {
    uint16_t port{0};
    std::string name;     // what it is, in plain words: "Remote login"; "Port 8123" when not known
    std::string version;  // the software it named, short: "Dropbear 2015.67"; empty when it named none
    FindingTier tier{FindingTier::Fine};
    std::string tag;      // "2 holes", "Old", "Fix", "Check", "Info" or "OK"
};

// One known security hole of a device. The list of holes has no words of
// its own for a hole, so `text` says which service and software it is in.
struct DashHole {
    uint16_t port{0};
    std::string cve;       // the official number, "CVE-2016-7406"
    int32_t score{-1};     // the 0-10 severity in tenths (98 is 9.8); -1 not known
    bool used{false};      // attackers are known to use it
    std::string text;      // "A known hole in its remote login, Dropbear 2015.67."
};

// One thing found about a device, for its page: the device's own words.
struct DashIssue {
    FindingTier tier{FindingTier::Know};
    std::string title;               // without the device's name: "2 security holes"
    std::string about;               // what it means, one or two plain sentences
    std::vector<std::string> steps;  // what to do, 2 or 3 short numbered steps
    uint16_t port{0};                // the port it is about; 0 for none
};

// One device on the networks the Cardputer is on (never the Cardputer).
struct DashDevice {
    std::string id;        // its IPv4 address, else its hardware address
    std::string name;      // as shown: its host name, else what it is, else its address
    std::string address;   // IPv4; empty when not known
    bool wireless{false};  // reached over Wi-Fi (the hotspot, or a Wi-Fi uplink); else by the cable
    bool router{false};    // the uplink's router
    bool is_new{false};    // new on the network (Watch, or new on the hotspot)
    bool checked{false};   // a health check or the Ports page scanned it
    FindingTier tier{FindingTier::Fine};
    std::string tag;       // "2 holes", "Fix", "Check", "New", "OK"
    std::string services;  // its open services in plain words; "none"; "not checked"
    std::string seen;      // "now", "4 min", "2 h", "earlier"
    std::string kind;      // what Discover or the check says it is ("printer", "router"); empty when not known
    bool hotspot{false};   // on LokiCTL's own hotspot now
    bool blocked{false};   // LokiCTL's firewall blocks it on the hotspot (HomeClient::blocked)
    // Its page: the open services (at most kDashMaxPorts; `more_ports`
    // more), its known holes (at most kDashMaxHoles, the worst first) and
    // its issues (at most kDashMaxIssues, the worst first). Holes in a
    // version a vendor built ("Debian") are left out: its updates may have
    // fixed them, and an issue (May need updates) says so.
    std::vector<DashPort> ports;
    int32_t more_ports{0};
    std::vector<DashHole> holes;
    int32_t more_holes{0};
    std::vector<DashIssue> issues;
};

// The counts every page shows. Things: subjects at each tier. Devices:
// device_fix + device_look + fine + unchecked == devices.
struct DashCounts {
    int32_t fix{0};
    int32_t look{0};
    int32_t know{0};
    int32_t devices{0};
    int32_t device_fix{0};
    int32_t device_look{0};
    int32_t fine{0};       // checked, and nothing to fix or look at
    int32_t unchecked{0};  // never scanned, and nothing heard about it
    int32_t device_new{0}; // devices new on the network (DashDevice::is_new)
    int32_t hotspot{0};    // devices on LokiCTL's own hotspot (DashDevice::hotspot)
};

// DashOverview::watch_lines holds at most this many: the Watch page's list.
constexpr std::size_t kDashWatchLines = 30;

struct DashOverview {
    bool checked{false};     // a health check has finished
    bool checking{false};    // a health check runs now
    std::string checked_at;  // when the last one finished, "21:44"; empty when not known
    int32_t took_s{-1};      // how long it took, in seconds; -1 when none finished
    std::vector<DashThing> things;    // the worst first; at most kDashMaxThings
    std::vector<DashDevice> devices;  // the worst first, then by address; at most kDashMaxDevices
    std::vector<std::string> watch_lines;  // Watch's newest alerts in plain words, at most kDashWatchLines
    std::vector<std::string> watch_times;  // their local times, "21:42"
    std::string list_date;  // the date of the list of known holes the versions were checked against; empty when none
    DashCounts counts;
};

// Works the overview out from the home data (the hotspot's devices, the
// uplink, the clock), the tools' latest runs and Watch's log. Every count
// is over the lists before they are cut to their limits.
//
// A device scanned by both the health check and the Ports page takes its
// open ports, services, holes and port findings from the one started last
// (ToolsData::latest_check), so a newer scan replaces what an older one
// said; a Ports scan of a device the check did not scan always counts.
DashOverview dashboard_overview(const HomeData& home, const ToolsData& tools, const WatchData& watch);

// Made-up tool runs, Watch log and trap for the dashboard while the home
// data is made up (HomeData::sample): a checked home network with 12
// devices on the sample's addresses (sample_home_data()), 1 to fix, 2
// worth a look and 9 fine, as in the design.
struct DashSample {
    ToolsData tools;
    WatchData watch;
    TrapRun trap;
};

DashSample sample_dashboard();

} // namespace model
