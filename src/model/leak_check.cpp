/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "leak_check.h"

namespace model {
namespace {

constexpr const char* kVpnOffFact = "The VPN is off";
constexpr const char* kVpnOffAdvice = "Turn the VPN on in Modes, then run the test again.";

// The line of a check that has no verdict yet.
LeakLine waiting_line(const LeakRun& run, const char* name) {
    LeakLine line;
    line.title = name;
    const bool running = run.state == ToolState::Running;
    line.text = std::string(name) + (running ? ": checking…" : ": not checked");
    line.fact = running ? "Checking now" : "Not checked yet";
    line.advice = "Press 6 to run the test.";
    return line;
}

LeakLine dns_line(const LeakRun& run) {
    if (run.dns == LeakVerdict::Waiting) {
        return waiting_line(run, "Name lookups");
    }
    LeakLine line;
    line.verdict = run.dns;
    line.title = "Name lookups";
    switch (run.dns) {
        case LeakVerdict::Pass:
            line.text = "Name lookups: through VPN";
            line.fact = run.dns_servers > 1 ? std::to_string(run.dns_servers) + " servers, all by the tunnel"
                                            : run.dns_server + " goes by the tunnel";
            line.advice = "The sites you look up are asked through the VPN, so this network can't see them.";
            break;
        case LeakVerdict::Fail:
            line.text = "Name lookups: NOT through VPN";
            line.fact = run.dns_server + (run.dns_interface.empty() ? " goes around the VPN"
                                                                    : " goes by " + run.dns_interface);
            line.advice = "This network can see every site you look up. Fix: add a DNS line to the WireGuard "
                          "file, then turn the VPN off and on.";
            break;
        case LeakVerdict::Unknown:
            line.text = "Name lookups: can't tell";
            line.fact = run.dns_server.empty() ? run.dns_reason : run.dns_server + ": " + run.dns_reason;
            line.advice = run.dns_server.empty()
                              ? "LokiCTL could not see where names are asked."
                              : "A program on this device answers names and asks on. LokiCTL can't see where.";
            break;
        case LeakVerdict::Info:
        case LeakVerdict::Waiting:
            line.text = "Name lookups: VPN is off";
            line.fact = kVpnOffFact;
            line.advice = kVpnOffAdvice;
            break;
    }
    return line;
}

LeakLine ipv6_line(const LeakRun& run) {
    if (run.ipv6 == LeakVerdict::Waiting) {
        return waiting_line(run, "IPv6");
    }
    LeakLine line;
    line.verdict = run.ipv6;
    line.title = "IPv6";
    switch (run.ipv6) {
        case LeakVerdict::Pass:
            line.text = "IPv6: no leak";
            if (run.ipv6_way == LeakIpv6::NoNetwork) {
                line.fact = "This network has no IPv6";
                line.advice = "Nothing can go around the VPN over IPv6 here.";
            } else if (run.ipv6_way == LeakIpv6::NoWayOut) {
                line.fact = "IPv6 has no way out";
                line.advice = "IPv6 is blocked, so nothing goes around the VPN over it.";
            } else {
                line.fact = "IPv6 goes by the tunnel";
                line.advice = "IPv6 goes through the VPN too.";
            }
            break;
        case LeakVerdict::Fail:
            line.text = "IPv6: NOT through VPN";
            line.fact = "IPv6 goes out by " + (run.ipv6_interface.empty() ? std::string("the network")
                                                                          : run.ipv6_interface);
            line.advice = "Sites using IPv6 see your own address. Fix: add ::/0 and an IPv6 address to the "
                          "WireGuard file.";
            break;
        case LeakVerdict::Unknown:
            line.text = "IPv6: can't tell";
            line.fact = "The way out is not known";
            line.advice = "LokiCTL could not see where IPv6 goes.";
            break;
        case LeakVerdict::Info:
        case LeakVerdict::Waiting:
            line.text = "IPv6: VPN is off";
            line.fact = kVpnOffFact;
            line.advice = kVpnOffAdvice;
            break;
    }
    return line;
}

LeakLine address_line(const LeakRun& run) {
    if (run.address == LeakVerdict::Waiting) {
        return waiting_line(run, "Public address");
    }
    LeakLine line;
    line.verdict = run.address;
    line.title = "Public address";
    switch (run.address) {
        case LeakVerdict::Pass:
            line.text = "Public address: " + run.public_address;
            line.fact = "Without VPN: " + run.address_without_vpn;
            line.advice = "Sites see the VPN's address, not your own.";
            break;
        case LeakVerdict::Fail:
            line.text = "Public address: NOT hidden";
            line.fact = "Same as without VPN: " + run.public_address;
            line.advice = "Traffic does not go through the VPN. Fix: check AllowedIPs (0.0.0.0/0) in the "
                          "WireGuard file.";
            break;
        case LeakVerdict::Unknown:
            line.text = "Public address: no answer";
            line.fact = run.address_reason;
            line.advice = "Cloudflare's address page did not answer. Check that the internet works.";
            break;
        case LeakVerdict::Info:
        case LeakVerdict::Waiting:
            line.text = "Public address: " + run.public_address;
            if (!run.vpn_on) {
                line.fact = "Your own address, noted";
                line.advice = "Turn the VPN on in Modes and run again: the test then checks that this "
                              "address is hidden.";
            } else {
                line.fact = "Nothing to compare with yet";
                line.advice = "Run the test once with the VPN off: LokiCTL notes your own address and "
                              "compares it next time.";
            }
            break;
    }
    return line;
}

LeakLine kill_line(const LeakRun& run) {
    if (run.kill == LeakVerdict::Waiting) {
        return waiting_line(run, "Kill switch");
    }
    LeakLine line;
    line.verdict = run.kill;
    line.title = "Kill switch";
    switch (run.kill) {
        case LeakVerdict::Pass:
            line.text = "Kill switch: on";
            line.fact = "Hotspot devices use only the tunnel";
            line.advice = "If the VPN drops, devices on the hotspot get nothing instead of going around it.";
            break;
        case LeakVerdict::Fail:
            line.text = "Kill switch: NOT on";
            line.fact = "Hotspot devices could go around";
            line.advice = "If the VPN drops, hotspot devices would use this network directly. Fix: turn "
                          "the VPN off and on in Modes.";
            break;
        case LeakVerdict::Unknown:
            line.text = "Kill switch: can't tell";
            line.fact = run.kill_reason;
            line.advice = "LokiCTL could not read its firewall.";
            break;
        case LeakVerdict::Info:
        case LeakVerdict::Waiting:
            line.text = "Kill switch: hotspot only";
            line.fact = "No hotspot with the VPN now";
            line.advice = "The kill switch guards devices on the hotspot (Router or Share with the VPN). "
                          "LokiCTL's own traffic has none.";
            break;
    }
    return line;
}

} // namespace

void judge_dns(LeakRun& run, const std::vector<LeakRoute>& routes, const std::string& tunnel) {
    run.dns_servers = static_cast<int32_t>(routes.size());
    run.dns_server.clear();
    run.dns_interface.clear();
    run.dns_reason.clear();
    if (routes.empty()) {
        run.dns = LeakVerdict::Unknown;
        run.dns_reason = "no DNS server set";
        return;
    }
    // A way around the tunnel decides it, whatever the others do.
    for (const LeakRoute& route : routes) {
        if (!route.loopback && route.reachable && !route.interface_name.empty() && route.interface_name != tunnel) {
            run.dns = LeakVerdict::Fail;
            run.dns_server = route.server;
            run.dns_interface = route.interface_name;
            return;
        }
    }
    bool any = false;
    for (const LeakRoute& route : routes) {
        if (route.loopback) {
            run.dns = LeakVerdict::Unknown;
            run.dns_server = route.server;
            run.dns_reason = "a local helper asks";
            return;
        }
        if (route.reachable && route.interface_name.empty()) {
            run.dns = LeakVerdict::Unknown;
            run.dns_reason = "the way out is not known";
            return;
        }
        any = any || route.reachable;
    }
    if (!any) {
        run.dns = LeakVerdict::Unknown;
        run.dns_reason = "no way to the DNS servers";
        return;
    }
    run.dns = LeakVerdict::Pass;
    run.dns_server = routes.front().server;
}

void judge_ipv6(LeakRun& run, bool network_has_ipv6, bool reachable, const std::string& interface_name,
                const std::string& tunnel) {
    run.ipv6_interface.clear();
    if (!network_has_ipv6) {
        run.ipv6 = LeakVerdict::Pass;
        run.ipv6_way = LeakIpv6::NoNetwork;
    } else if (!reachable) {
        run.ipv6 = LeakVerdict::Pass;
        run.ipv6_way = LeakIpv6::NoWayOut;
    } else if (interface_name.empty()) {
        run.ipv6 = LeakVerdict::Unknown;
        run.ipv6_way = LeakIpv6::None;
    } else if (interface_name == tunnel) {
        run.ipv6 = LeakVerdict::Pass;
        run.ipv6_way = LeakIpv6::Tunnel;
    } else {
        run.ipv6 = LeakVerdict::Fail;
        run.ipv6_way = LeakIpv6::Around;
        run.ipv6_interface = interface_name;
    }
}

void judge_address(LeakRun& run, const std::string& address, const std::string& reason) {
    run.public_address = address;
    run.address_reason.clear();
    if (address.empty()) {
        run.address = LeakVerdict::Unknown;
        run.address_reason = reason.empty() ? "no answer from the server" : reason;
    } else if (!run.vpn_on || run.address_without_vpn.empty()) {
        run.address = LeakVerdict::Info;
    } else if (address == run.address_without_vpn) {
        run.address = LeakVerdict::Fail;
    } else {
        run.address = LeakVerdict::Pass;
    }
}

void judge_kill(LeakRun& run, const std::string& reason, bool present) {
    run.kill_reason.clear();
    if (!run.gateway) {
        run.kill = LeakVerdict::Info;
    } else if (!reason.empty()) {
        run.kill = LeakVerdict::Unknown;
        run.kill_reason = reason;
    } else {
        run.kill = present ? LeakVerdict::Pass : LeakVerdict::Fail;
    }
}

void judge_vpn_off(LeakRun& run) {
    run.dns = LeakVerdict::Info;
    run.ipv6 = LeakVerdict::Info;
}

LeakLocal leak_local(const HomeData& home) {
    LeakLocal local;
    local.vpn_on = home.vpn.state == VpnState::On;
    local.gateway = home.ap_active && (local.vpn_on || home.vpn.kill_switch);
    if (local.vpn_on) {
        local.file = home.vpn.file;
    }
    return local;
}

bool leak_run_current(const LeakRun& run, const LeakLocal& now) {
    return run.vpn_on == now.vpn_on && run.gateway == now.gateway && run.file == now.file;
}

LeakLine leak_line(const LeakRun& run, LeakCheck check) {
    switch (check) {
        case LeakCheck::Dns:
            return dns_line(run);
        case LeakCheck::Ipv6:
            return ipv6_line(run);
        case LeakCheck::Address:
            return address_line(run);
        case LeakCheck::Kill:
            return kill_line(run);
    }
    return dns_line(run);
}

int32_t leak_fail_count(const LeakRun& run) {
    int32_t count = 0;
    for (const LeakVerdict verdict : {run.dns, run.ipv6, run.address, run.kill}) {
        count += verdict == LeakVerdict::Fail ? 1 : 0;
    }
    return count;
}

int32_t leak_unknown_count(const LeakRun& run) {
    int32_t count = 0;
    for (const LeakVerdict verdict : {run.dns, run.ipv6, run.address, run.kill}) {
        count += verdict == LeakVerdict::Unknown ? 1 : 0;
    }
    return count;
}

} // namespace model
