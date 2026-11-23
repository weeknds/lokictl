/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the VPN leak test's rules (leak_check.h): which ways count as a
// leak, and that every list line stays short. Built on the Mac only, as
// lokictl_leak_tests; exits 1 when a check fails.

#include "leak_check.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

constexpr const char* kTunnel = "lokiwg0";

model::LeakRoute route(const char* server, bool reachable, const char* interface_name, bool loopback = false) {
    model::LeakRoute out;
    out.server = server;
    out.reachable = reachable;
    out.interface_name = interface_name;
    out.loopback = loopback;
    return out;
}

void check_dns() {
    model::LeakRun run;
    model::judge_dns(run, {route("10.64.0.1", true, kTunnel)}, kTunnel);
    expect(run.dns == model::LeakVerdict::Pass, "dns through the tunnel passes");

    model::judge_dns(run, {route("10.64.0.1", true, kTunnel), route("10.0.0.1", true, "wlan0")}, kTunnel);
    expect(run.dns == model::LeakVerdict::Fail, "a second server around the tunnel fails");
    expect(run.dns_server == "10.0.0.1" && run.dns_interface == "wlan0", "the leaking server is named");

    model::judge_dns(run, {route("127.0.0.53", true, "lo", true)}, kTunnel);
    expect(run.dns == model::LeakVerdict::Unknown, "a local helper can't be told");

    model::judge_dns(run, {}, kTunnel);
    expect(run.dns == model::LeakVerdict::Unknown, "no server can't be told");

    model::judge_dns(run, {route("10.64.0.1", false, "")}, kTunnel);
    expect(run.dns == model::LeakVerdict::Unknown, "no way to any server can't be told");

    model::judge_dns(run, {route("10.64.0.1", true, kTunnel), route("10.0.0.9", false, "")}, kTunnel);
    expect(run.dns == model::LeakVerdict::Pass, "an unreachable second server is no leak");
}

void check_ipv6() {
    model::LeakRun run;
    model::judge_ipv6(run, false, true, "wlan0", kTunnel);
    expect(run.ipv6 == model::LeakVerdict::Pass, "no IPv6 on the network passes");
    model::judge_ipv6(run, true, false, "", kTunnel);
    expect(run.ipv6 == model::LeakVerdict::Pass, "no IPv6 way out passes");
    model::judge_ipv6(run, true, true, kTunnel, kTunnel);
    expect(run.ipv6 == model::LeakVerdict::Pass, "IPv6 through the tunnel passes");
    model::judge_ipv6(run, true, true, "eth0", kTunnel);
    expect(run.ipv6 == model::LeakVerdict::Fail && run.ipv6_interface == "eth0", "IPv6 around the tunnel fails");
    model::judge_ipv6(run, true, true, "", kTunnel);
    expect(run.ipv6 == model::LeakVerdict::Unknown, "an unknown way can't be told");
}

void check_address() {
    model::LeakRun run;
    run.vpn_on = true;
    model::judge_address(run, "198.51.100.7", {});
    expect(run.address == model::LeakVerdict::Info, "nothing to compare with is only info");
    run.address_without_vpn = "203.0.113.5";
    model::judge_address(run, "198.51.100.7", {});
    expect(run.address == model::LeakVerdict::Pass, "another address passes");
    model::judge_address(run, "203.0.113.5", {});
    expect(run.address == model::LeakVerdict::Fail, "the same address fails");
    model::judge_address(run, "", "no answer from the server");
    expect(run.address == model::LeakVerdict::Unknown && !run.address_reason.empty(), "no answer can't be told");
    run.vpn_on = false;
    model::judge_address(run, "203.0.113.5", {});
    expect(run.address == model::LeakVerdict::Info, "with the VPN off it is only info");
}

void check_kill() {
    model::LeakRun run;
    model::judge_kill(run, {}, false);
    expect(run.kill == model::LeakVerdict::Info, "no gateway is only info");
    run.gateway = true;
    model::judge_kill(run, {}, true);
    expect(run.kill == model::LeakVerdict::Pass, "kill chains there pass");
    model::judge_kill(run, {}, false);
    expect(run.kill == model::LeakVerdict::Fail, "kill chains missing fail");
    model::judge_kill(run, "helper not installed", false);
    expect(run.kill == model::LeakVerdict::Unknown, "an unread firewall can't be told");
}

// Every list line of every verdict, with the longest address, fits about
// 30 characters.
void check_lengths() {
    std::vector<model::LeakRun> runs;
    model::LeakRun base;
    base.state = model::ToolState::Done;
    base.vpn_on = true;
    base.gateway = true;
    base.dns_server = "255.255.255.255";
    base.dns_interface = "wlan0";
    base.dns_reason = "a local helper asks";
    base.ipv6_interface = "wlan0";
    base.address_without_vpn = "203.0.113.5";
    base.address_reason = "no answer from the server";
    base.kill_reason = "helper not installed";
    for (const model::LeakVerdict verdict :
         {model::LeakVerdict::Waiting, model::LeakVerdict::Pass, model::LeakVerdict::Fail,
          model::LeakVerdict::Unknown, model::LeakVerdict::Info}) {
        model::LeakRun run = base;
        run.dns = run.ipv6 = run.address = run.kill = verdict;
        run.public_address = "255.255.255.255";
        runs.push_back(run);
    }
    model::LeakRun running;
    running.state = model::ToolState::Running;
    runs.push_back(running);
    for (const model::LeakRun& run : runs) {
        for (int check = 0; check < static_cast<int>(model::kLeakCheckCount); ++check) {
            const model::LeakLine line = model::leak_line(run, static_cast<model::LeakCheck>(check));
            // "…" counts as one character, three bytes.
            const std::size_t length = line.text.size() - (line.text.find("…") != std::string::npos ? 2 : 0);
            expect(length <= 31, "line too long: " + line.text);
            expect(!line.advice.empty() && !line.fact.empty(), "detail missing: " + line.text);
        }
    }
}

// A finished run is only shown while the VPN is as it was for that run.
void check_current() {
    model::HomeData home;
    home.vpn.state = model::VpnState::On;
    home.vpn.file = "home";
    model::LeakRun run;
    run.vpn_on = true;
    run.file = "home";
    expect(model::leak_run_current(run, model::leak_local(home)), "same tunnel shown");
    home.vpn.file = "travel";
    expect(!model::leak_run_current(run, model::leak_local(home)), "another file hides the run");
    home.vpn.file = "home";
    home.ap_active = true;
    expect(!model::leak_run_current(run, model::leak_local(home)), "the gateway on hides the run");
    home.ap_active = false;
    home.vpn.state = model::VpnState::Off;
    expect(!model::leak_run_current(run, model::leak_local(home)), "the VPN off hides the run");
}

} // namespace

int main() {
    check_dns();
    check_ipv6();
    check_address();
    check_kill();
    check_lengths();
    check_current();
    if (failures == 0) {
        std::printf("leak rules: all checks passed\n");
        return 0;
    }
    return 1;
}
