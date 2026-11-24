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

// One thing to know about: everything found about one subject (a device,
// the Wi-Fi, the cable link, the network), at its worst finding's tier,
// in that finding's words.
struct DashThing {
    FindingTier tier{FindingTier::Know};
    std::string icon;    // "shield", "lock", "screen", "wifi", "cable" or "eye"
    std::string title;   // "Smart TV has 2 security holes"
    std::string hint;    // what to do, lowercase, no full stop: "update the device's software"
    std::string action;  // the verdict's link: "Update Smart TV"
    std::string fix;     // the phone's fix button: "Fix Smart TV"
    std::string device;  // DashDevice::id of the device it is about; empty for the rest
    std::string time;    // local time it was found, "21:40"; empty when not known
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
};

struct DashOverview {
    bool checked{false};     // a health check has finished
    bool checking{false};    // a health check runs now
    std::string checked_at;  // when the last one finished, "21:44"; empty when not known
    std::vector<DashThing> things;    // the worst first; at most kDashMaxThings
    std::vector<DashDevice> devices;  // the worst first, then by address; at most kDashMaxDevices
    std::vector<std::string> watch_lines;  // Watch's newest alerts in plain words, at most 5
    std::vector<std::string> watch_times;  // their local times, "21:42"
    DashCounts counts;
};

// Works the overview out from the home data (the hotspot's devices, the
// uplink, the clock), the tools' latest runs and Watch's log. Every count
// is over the lists before they are cut to their limits.
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
