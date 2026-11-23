/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the health check's rules (health_check.h): each rule with a
// made-up result that should raise it and one that should not, the choice
// of devices, the order of the findings, and that every text fits its
// place on the screen. Built on the Mac only, as lokictl_health_tests;
// exits 1 when a check fails.

#include "health_check.h"
#include "tool_data.h"
#include "vuln_list.h"

#include <cstdio>
#include <cstring>
#include <fstream>
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

int count_issue(const std::vector<model::HealthFinding>& findings, model::HealthIssue issue) {
    int count = 0;
    for (const model::HealthFinding& finding : findings) {
        count += finding.issue == issue ? 1 : 0;
    }
    return count;
}

const model::HealthFinding* find_issue(const std::vector<model::HealthFinding>& findings, model::HealthIssue issue) {
    for (const model::HealthFinding& finding : findings) {
        if (finding.issue == issue) {
            return &finding;
        }
    }
    return nullptr;
}

model::PortRun scan_with(std::vector<uint16_t> open) {
    model::PortRun scan;
    scan.state = model::ToolState::Done;
    scan.target = "192.0.2.10";
    scan.open = std::move(open);
    return scan;
}

model::HealthHost device(const char* address) {
    model::HealthHost host;
    host.address = address;
    return host;
}

model::HealthHost router() {
    model::HealthHost host = device("192.0.2.1");
    host.router = true;
    return host;
}

// A port verdict and its worst finding, as the check against the list of
// known holes gives them.
void add_verdict(model::PortRun& scan, uint16_t port, int holes, int exploited, bool outdated, const char* vendor) {
    model::PortVerdict verdict;
    verdict.port = port;
    verdict.holes = holes;
    verdict.exploited = exploited;
    verdict.outdated = outdated;
    verdict.vendor = vendor;
    scan.verdicts.push_back(verdict);
    model::ServiceFinding finding;
    finding.port = port;
    finding.kind = holes > 0 ? model::FindingKind::Hole : model::FindingKind::Outdated;
    finding.name = "dnsmasq";
    finding.version = "2.85";
    finding.cve = holes > 0 ? "CVE-2021-3448" : "";
    finding.score = holes > 0 ? 75 : -1;
    finding.vendor = vendor;
    scan.findings.push_back(finding);
}

void check_services() {
    // Telnet and FTP, file sharing: each open port with a rule, at its level.
    const auto telnet = model::host_findings(device("192.0.2.10"), scan_with({21, 23, 445}), 2);
    const model::HealthFinding* found = nullptr;
    expect(count_issue(telnet, model::HealthIssue::Service) == 3, "three service findings");
    for (const model::HealthFinding& finding : telnet) {
        if (finding.port == 23) {
            found = &finding;
        }
    }
    expect(found && found->title == "Telnet open" && found->level == model::RiskLevel::Risk, "Telnet is a risk");
    expect(found && found->who == "192.0.2.10" && found->fact == "Port 23, remote login", "Telnet's who and fact");
    for (const model::HealthFinding& finding : telnet) {
        if (finding.port == 21) {
            expect(finding.level == model::RiskLevel::Warning, "FTP is a warning");
        }
        if (finding.port == 445) {
            expect(finding.level == model::RiskLevel::Info && finding.title == "File sharing open", "SMB is info");
        }
    }
    // SSH alone has no rule.
    expect(model::host_findings(device("192.0.2.10"), scan_with({22, 443}), 2).empty(), "SSH and HTTPS alone: nothing");
}

void check_router_page_and_upnp() {
    auto findings = model::host_findings(router(), scan_with({53, 80, 443}), 0);
    const model::HealthFinding* page = find_issue(findings, model::HealthIssue::RouterPage);
    expect(page && page->port == 80 && page->who == "router", "the router's plain web page");
    expect(model::host_findings(router(), scan_with({53, 443}), 0).empty(), "the router on https only: nothing");
    expect(find_issue(model::host_findings(device("192.0.2.10"), scan_with({80}), 2),
                      model::HealthIssue::RouterPage) == nullptr,
           "another device's web page is not the router's");

    model::HealthHost answering = router();
    answering.upnp = true;
    findings = model::host_findings(answering, scan_with({443}), 0);
    expect(count_issue(findings, model::HealthIssue::Upnp) == 1, "the router answering SSDP");
    findings = model::host_findings(router(), scan_with({1900, 49152}), 0);
    expect(count_issue(findings, model::HealthIssue::Upnp) == 1 &&
               count_issue(findings, model::HealthIssue::Service) == 0,
           "UPnP ports on the router: one finding, no service ones");
    model::HealthHost tv = device("192.0.2.30");
    tv.upnp = true;
    tv.kind = "TV";
    expect(model::host_findings(tv, scan_with({49152}), 3).empty(), "a TV with UPnP: nothing");
}

void check_versions() {
    model::PortRun holes = scan_with({53});
    add_verdict(holes, 53, 3, 0, false, "");
    auto findings = model::host_findings(router(), holes, 0);
    const model::HealthFinding* hole = find_issue(findings, model::HealthIssue::Holes);
    expect(hole && hole->title == "3 known holes" && hole->level == model::RiskLevel::Risk, "three known holes");
    expect(hole && hole->fact == "Port 53, dnsmasq 2.85, worst 7.5", "the holes' fact");

    model::PortRun attacked = scan_with({53});
    add_verdict(attacked, 53, 2, 1, false, "");
    hole = nullptr;
    findings = model::host_findings(device("192.0.2.10"), attacked, 2);
    hole = find_issue(findings, model::HealthIssue::Holes);
    expect(hole && hole->title == "Hole attackers use", "a hole attackers use");

    model::PortRun old = scan_with({53});
    add_verdict(old, 53, 0, 0, true, "");
    findings = model::host_findings(router(), old, 0);
    const model::HealthFinding* outdated = find_issue(findings, model::HealthIssue::Outdated);
    expect(outdated && outdated->title == "Old router software" && outdated->level == model::RiskLevel::Warning,
           "old router software");
    findings = model::host_findings(device("192.0.2.10"), old, 2);
    outdated = find_issue(findings, model::HealthIssue::Outdated);
    expect(outdated && outdated->title == "Old software", "old software on another device");

    model::PortRun vendor = scan_with({53});
    add_verdict(vendor, 53, 10, 1, true, "Debian");
    findings = model::host_findings(device("192.0.2.10"), vendor, 2);
    const model::HealthFinding* check = find_issue(findings, model::HealthIssue::Check);
    expect(check && check->level == model::RiskLevel::Info && findings.size() == 1, "a vendor's build is info");

    // The Cardputer's own SSH as the device reports it: a Debian build.
    model::PortRun own = scan_with({22});
    own.banners.push_back({22, "OpenSSH_10.0p2 Debian-7+deb13u4"});
    std::ifstream list(VULN_LIST_PATH);
    const model::VulnCheck checked = model::check_services(list, own);
    own.verdicts = checked.verdicts;
    own.findings = checked.findings;
    model::HealthHost self = device("192.0.2.12");
    self.own = true;
    findings = model::host_findings(self, own, 1);
    expect(findings.size() == 1 && findings[0].issue == model::HealthIssue::Check &&
               findings[0].level == model::RiskLevel::Info && findings[0].who == "this device",
           "the Cardputer's own SSH shows as info");
}

void check_many_ports() {
    expect(count_issue(model::host_findings(device("192.0.2.10"), scan_with({1, 2, 3, 4, 5, 6, 7, 8, 9}), 2),
                       model::HealthIssue::ManyPorts) == 1,
           "nine open ports");
    expect(count_issue(model::host_findings(device("192.0.2.10"), scan_with({1, 2, 3, 4, 5, 6, 7, 8}), 2),
                       model::HealthIssue::ManyPorts) == 0,
           "eight open ports");
}

void check_wifi() {
    const auto with_security = [](const char* security) {
        model::HealthLocal local;
        local.wifi_name = "Example-Home";
        local.wifi_security = security;
        return model::local_findings(local);
    };
    auto findings = with_security("open");
    expect(findings.size() == 1 && findings[0].issue == model::HealthIssue::OpenWifi &&
               findings[0].level == model::RiskLevel::Risk,
           "open Wi-Fi");
    findings = with_security("WEP");
    expect(findings.size() == 1 && findings[0].issue == model::HealthIssue::WepWifi &&
               findings[0].level == model::RiskLevel::Risk,
           "WEP");
    findings = with_security("WPA");
    expect(findings.size() == 1 && findings[0].issue == model::HealthIssue::WpaWifi &&
               findings[0].level == model::RiskLevel::Warning,
           "WPA1");
    expect(with_security("WPA2").empty() && with_security("WPA2/3").empty() && with_security("OWE").empty(),
           "WPA2, WPA2/3 and OWE: nothing");

    model::HealthLocal local;
    local.wifi_name = "Example-Home";
    local.wifi_security = "WPA2";
    model::WifiNetwork joined{"Example-Home", "WPA2", 70, 6, true};
    model::WifiNetwork mesh{"Example-Home", "WPA2/3", 50, 11, false};
    model::WifiNetwork other{"Example-Cafe", "open", 40, 1, false};
    model::WifiNetwork twin{"Example-Home", "open", 30, 1, false};
    local.nearby = {joined, mesh, other};
    expect(model::local_findings(local).empty(), "a stronger twin and another open network: nothing");
    local.nearby.push_back(twin);
    findings = model::local_findings(local);
    const model::HealthFinding* lookalike = find_issue(findings, model::HealthIssue::LookAlike);
    expect(lookalike && lookalike->level == model::RiskLevel::Warning && lookalike->fact == "Open, channel 1, yours WPA2",
           "an open look-alike");
    local.wifi_security = "WPA";
    local.nearby = {model::WifiNetwork{"Example-Home", "WPA", 30, 1, false}};
    expect(count_issue(model::local_findings(local), model::HealthIssue::LookAlike) == 0,
           "a twin as weak as the joined one is no look-alike");
}

void check_cable() {
    model::HealthLocal local;
    local.cable = true;
    local.cable_name = "eth0";
    local.cable_mbps = 1000;
    local.cable_duplex = "full";
    expect(model::local_findings(local).empty(), "a gigabit cable: nothing");
    local.cable_mbps = 10;
    auto findings = model::local_findings(local);
    expect(findings.size() == 1 && findings[0].issue == model::HealthIssue::SlowCable &&
               findings[0].fact == "10 Mbit/s, full duplex" && findings[0].who == "eth0",
           "a 10 Mbit cable");
    local.cable_mbps = 100;
    local.cable_duplex = "half";
    expect(count_issue(model::local_findings(local), model::HealthIssue::SlowCable) == 1, "half duplex");
    local.cable = false;
    expect(model::local_findings(local).empty(), "no cable: nothing");
}

void check_hosts() {
    model::HealthLocal local;
    local.router = "192.0.2.1";
    local.address = "192.0.2.12";
    std::vector<model::HeardDevice> devices;
    const auto heard = [&devices](const char* address, const char* name, const char* own_port) {
        model::HeardDevice entry;
        entry.address = address;
        entry.name = name;
        entry.own_port = own_port;
        devices.push_back(entry);
    };
    heard("192.0.2.40", "printer", "");
    heard("192.0.2.5", "", "");
    heard("192.0.2.230", "", "wlan0");  // the Cardputer's other port
    heard("198.51.100.7", "", "");       // another network
    heard("192.0.2.255", "", "");        // the broadcast address
    heard("192.0.2.1", "gateway", "");   // the router, named
    heard("192.0.2.40", "", "");         // the printer again
    const uint32_t network = (192u << 24) | (0u << 16) | (2u << 8);
    const auto hosts = model::health_hosts(local, devices, network, 0xFFFFFF00u);
    expect(hosts.size() == 4, "four devices");
    if (hosts.size() == 4) {
        expect(hosts[0].router && hosts[0].name == "gateway", "the router first, with its name");
        expect(hosts[1].own && hosts[1].address == "192.0.2.12", "the Cardputer second");
        expect(hosts[2].address == "192.0.2.5" && hosts[3].address == "192.0.2.40" && hosts[3].name == "printer",
               "the others by address");
    }
    devices.clear();
    for (int index = 2; index < 100; ++index) {
        model::HeardDevice entry;
        entry.address = "192.0.2." + std::to_string(index);
        devices.push_back(entry);
    }
    expect(model::health_hosts(local, devices, network, 0xFFFFFF00u).size() == model::kHealthMaxHosts,
           "at most 32 devices");
}

void check_order_and_summary() {
    std::vector<model::HealthFinding> findings;
    model::HealthLocal local;
    local.cable = true;
    local.cable_mbps = 10;
    findings = model::local_findings(local);
    auto device_findings = model::host_findings(device("192.0.2.10"), scan_with({23, 445}), 2);
    findings.insert(findings.end(), device_findings.begin(), device_findings.end());
    model::sort_findings(findings);
    expect(findings.size() == 3 && findings[0].level == model::RiskLevel::Risk &&
               findings[1].level == model::RiskLevel::Warning && findings[2].level == model::RiskLevel::Info,
           "the worst first");
    const model::HealthCounts counts = model::health_counts(findings);
    expect(model::health_summary(counts) == "1 risk, 1 warning, 1 info", "the summary");
    expect(model::health_summary(model::HealthCounts{}) == "Nothing found", "an empty summary");
    expect(model::health_summary(model::HealthCounts{2, 0, 3}) == "2 risks, 3 info", "levels with none left out");
}

// Every text fits its place: a title the list's name column (20
// characters), what it means two lines of the detail (80), what to do one
// line after "Fix: " (37).
void check_texts() {
    for (const model::HealthRule& rule : model::health_rules()) {
        expect(std::strlen(rule.title) <= 20, std::string("title too long: ") + rule.title);
        expect(std::strlen(rule.about) <= 80, std::string("explanation too long: ") + rule.about);
        expect(std::strlen(rule.fix) <= 37, std::string("fix too long: ") + rule.fix);
    }
    for (const model::ServiceRule& rule : model::service_rules()) {
        expect(std::strlen(rule.title) <= 20, std::string("title too long: ") + rule.title);
        expect(std::strlen(rule.note) <= 79, std::string("note too long: ") + rule.note);
        expect(std::strlen(rule.fix) <= 37, std::string("fix too long: ") + rule.fix);
    }
}

} // namespace

int main() {
    check_services();
    check_router_page_and_upnp();
    check_versions();
    check_many_ports();
    check_wifi();
    check_cable();
    check_hosts();
    check_order_and_summary();
    check_texts();
    if (failures > 0) {
        std::printf("%d health check(s) failed\n", failures);
        return 1;
    }
    std::printf("all health checks passed\n");
    return 0;
}
