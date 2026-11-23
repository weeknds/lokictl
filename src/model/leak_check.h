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

// The VPN leak test's verdicts and words (model::LeakRun in tool_data.h):
// what each check found means, as plain one-line texts for the Leak page's
// list and longer texts for its detail view. The runners measure; the
// judge_* functions here decide, so the rules can be checked without a
// network (tests/leak_rules.cpp).
namespace model {

// The checks, in the order the Leak page lists them.
enum class LeakCheck : int32_t {
    Dns = 0,  // name lookups
    Ipv6,
    Address,  // the public address
    Kill,     // the hotspot's kill switch
};
constexpr std::size_t kLeakCheckCount = 4;

// One nameserver of the system, as a lookup would reach it.
struct LeakRoute {
    std::string server;          // as /etc/resolv.conf writes it
    bool loopback{false};        // on this device (127.0.0.0/8, ::1): a local helper asks on
    bool reachable{false};       // the system has a way to it
    std::string interface_name;  // the interface it leaves by; empty when not known
};

// The name lookups' verdict from the system's nameservers (at most the
// three the resolver uses), with `tunnel` the tunnel's interface: Fail
// when any goes out by another interface; Unknown with no server, a local
// one (it asks on, out of sight), or no way to any; else Pass.
void judge_dns(LeakRun& run, const std::vector<LeakRoute>& routes, const std::string& tunnel);

// The IPv6 verdict. `network_has_ipv6`: an interface other than the tunnel
// has a global IPv6 address. `reachable` and `interface_name`: the way to a
// public IPv6 address. Pass when the network has no IPv6, IPv6 has no way
// out, or it goes by `tunnel`; Fail when it goes by another interface;
// Unknown when the way out is not known.
void judge_ipv6(LeakRun& run, bool network_has_ipv6, bool reachable, const std::string& interface_name,
                const std::string& tunnel);

// The public address's verdict from what the trace page said (`address`
// empty: `reason` says why). With the VPN off (run.vpn_on false), or with
// nothing to compare with (run.address_without_vpn empty), only Info; Fail
// when it is the address seen without the VPN; else Pass.
void judge_address(LeakRun& run, const std::string& address, const std::string& reason);

// The kill switch's verdict: Info outside the gateway (run.gateway false);
// Unknown when the firewall could not be read (`reason`); else Pass when
// the kill chains are `present`, Fail when not.
void judge_kill(LeakRun& run, const std::string& reason, bool present);

// With the VPN off, name lookups and IPv6 have nothing to test: Info.
void judge_vpn_off(LeakRun& run);

// What a leak test starts with, from the data layer: whether the tunnel is
// up, its file, and whether the VPN gateway runs (a hotspot with the VPN
// wanted, the tunnel up or not).
LeakLocal leak_local(const HomeData& home);

// Whether a finished `run` still describes the VPN as it is `now`: the
// same on or off, the same file and the same gateway. A run that no longer
// does is not shown (its verdicts were for another tunnel).
bool leak_run_current(const LeakRun& run, const LeakLocal& now);

// One check as the Leak page shows it. Every text is one line except
// `advice`, which the detail view wraps.
struct LeakLine {
    std::string text;    // the list's line: "Name lookups: through VPN" (about 30 characters at most)
    LeakVerdict verdict{LeakVerdict::Waiting};
    std::string title;   // the detail's heading: "Name lookups"
    std::string fact;    // the detail's line of facts: "10.0.0.1 goes by wlan0"
    std::string advice;  // what it means and what to do
};

LeakLine leak_line(const LeakRun& run, LeakCheck check);

// How many checks of `run` failed, and how many could not be checked.
int32_t leak_fail_count(const LeakRun& run);
int32_t leak_unknown_count(const LeakRun& run);

} // namespace model
