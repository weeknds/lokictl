/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "health_check.h"

#include "service_banner.h"

#include <algorithm>
#include <cstddef>

namespace model {
namespace {

// "a.b.c.d" as a number (host byte order); false when it is not one.
bool parse_ipv4(const std::string& text, uint32_t& out) {
    if (!is_ipv4_text(text)) {
        return false;
    }
    uint32_t number = 0;
    uint32_t part = 0;
    for (const char character : text) {
        if (character == '.') {
            number = (number << 8) | part;
            part = 0;
        } else {
            part = part * 10 + static_cast<uint32_t>(character - '0');
        }
    }
    out = (number << 8) | part;
    return true;
}

// Whether `address` may be scanned on the network `network`/`netmask`: in
// it, and not its network or broadcast address (on networks with more than
// two addresses).
bool inside(uint32_t address, uint32_t network, uint32_t netmask) {
    if ((address & netmask) != (network & netmask)) {
        return false;
    }
    const uint32_t host = address & ~netmask;
    return ~netmask <= 1 || (host != 0 && host != ~netmask);
}

// A score in tenths as text: 81 -> "8.1".
std::string score_text(int32_t tenths) {
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

// Who a device's finding names in the list, and in the detail.
std::string host_who(const HealthHost& host) {
    if (host.router) {
        return "router";
    }
    if (host.own) {
        return "this device";
    }
    return host.address;
}

std::string host_where(const HealthHost& host) {
    if (host.router) {
        return "router " + host.address;
    }
    if (host.own) {
        return "this device " + host.address;
    }
    const std::string& label = host.name.empty() ? host.kind : host.name;
    return label.empty() ? host.address : label + " " + host.address;
}

HealthFinding make_finding(HealthIssue issue, RiskLevel level, std::string title, int32_t order) {
    HealthFinding finding;
    finding.issue = issue;
    finding.level = level;
    finding.title = std::move(title);
    finding.order = order;
    return finding;
}

// "Port 23, remote login".
std::string port_fact(uint16_t port) {
    const char* plain = port_plain(port);
    return "Port " + std::to_string(port) + (*plain != '\0' ? std::string(", ") + plain : std::string());
}

bool has_port(const std::vector<uint16_t>& ports, uint16_t port) {
    return std::find(ports.begin(), ports.end(), port) != ports.end();
}

bool upnp_port(uint16_t port) {
    return port == 1900 || port == 49152;
}

} // namespace

const std::vector<HealthRule>& health_rules() {
    static const std::vector<HealthRule> rules{
        {HealthIssue::RouterPage, RiskLevel::Warning, "Plain router page",
         "A page answers on plain http; a login there would not be encrypted.",
         "Use https://, or turn plain http off."},
        {HealthIssue::Upnp, RiskLevel::Warning, "Apps can open ports",
         "UPnP is on: any app or device here can open router ports to the internet.",
         "Turn UPnP off if nothing needs it."},
        {HealthIssue::Holes, RiskLevel::Risk, "Known holes",
         "This version has known security holes that attackers can use to break in.",
         "Update the device's software."},
        {HealthIssue::Outdated, RiskLevel::Warning, "Old software",
         "This version no longer gets security fixes, so new holes stay open.",
         "Update its software or replace it."},
        {HealthIssue::Check, RiskLevel::Info, "Check for updates",
         "Holes or an old version, but its maker may fix them without a new number.",
         "Make sure it installs its updates."},
        {HealthIssue::ManyPorts, RiskLevel::Warning, "Many open ports",
         "Many services listen on this device; each one is a way in.",
         "Turn off the services you don't use."},
        {HealthIssue::OpenWifi, RiskLevel::Risk, "No Wi-Fi password",
         "Anyone nearby can join your Wi-Fi and may see what is not encrypted.",
         "Turn on WPA2 or WPA3 with a password."},
        {HealthIssue::WepWifi, RiskLevel::Risk, "Weak Wi-Fi lock",
         "WEP is very old and can be broken in minutes.",
         "Switch the router to WPA2 or WPA3."},
        {HealthIssue::WpaWifi, RiskLevel::Warning, "Old Wi-Fi lock",
         "WPA (version 1) is outdated and can be attacked.",
         "Switch the router to WPA2 or WPA3."},
        {HealthIssue::LookAlike, RiskLevel::Warning, "Look-alike network",
         "A nearby network has your Wi-Fi's name but weaker security: maybe a trap.",
         "Don't join it; check your devices."},
        {HealthIssue::SlowCable, RiskLevel::Warning, "Slow cable link",
         "The link is slow or one way at a time, often from a worn cable or port.",
         "Try another cable or switch port."},
    };
    return rules;
}

const HealthRule* health_rule(HealthIssue issue) {
    for (const HealthRule& rule : health_rules()) {
        if (rule.issue == issue) {
            return &rule;
        }
    }
    return nullptr;
}

std::string health_about(const HealthFinding& finding) {
    if (finding.issue == HealthIssue::Service) {
        const ServiceRule* rule = service_rule(finding.port);
        return rule ? capitalized(rule->note) + "." : std::string();
    }
    const HealthRule* rule = health_rule(finding.issue);
    return rule ? rule->about : "";
}

std::string health_fix(const HealthFinding& finding) {
    if (finding.issue == HealthIssue::Service) {
        const ServiceRule* rule = service_rule(finding.port);
        return rule ? rule->fix : "";
    }
    const HealthRule* rule = health_rule(finding.issue);
    return rule ? rule->fix : "";
}

const char* health_level_tag(RiskLevel level) {
    switch (level) {
        case RiskLevel::Risk:
            return "Risk";
        case RiskLevel::Warning:
            return "Warn";
        case RiskLevel::Info:
            return "Info";
    }
    return "Info";
}

std::vector<HealthHost> health_hosts(const HealthLocal& local,
                                     const std::vector<HeardDevice>& devices,
                                     uint32_t network,
                                     uint32_t netmask) {
    struct Candidate {
        uint32_t number{0};
        HealthHost host;
    };
    std::vector<Candidate> list;
    const auto find = [&list](uint32_t number) {
        return std::find_if(list.begin(), list.end(),
                            [number](const Candidate& candidate) { return candidate.number == number; });
    };
    uint32_t number = 0;
    if (parse_ipv4(local.router, number) && inside(number, network, netmask)) {
        Candidate router;
        router.number = number;
        router.host.address = local.router;
        router.host.router = true;
        list.push_back(router);
    }
    if (parse_ipv4(local.address, number) && inside(number, network, netmask) && find(number) == list.end()) {
        Candidate own;
        own.number = number;
        own.host.address = local.address;
        own.host.own = true;
        list.push_back(own);
    }
    const std::size_t fixed = list.size();
    for (const HeardDevice& device : devices) {
        if (!device.own_port.empty() || !parse_ipv4(device.address, number) || !inside(number, network, netmask)) {
            continue;
        }
        auto found = find(number);
        if (found == list.end()) {
            Candidate candidate;
            candidate.number = number;
            candidate.host.address = device.address;
            list.push_back(candidate);
            found = list.end() - 1;
        }
        HealthHost& host = found->host;
        if (host.name.empty()) {
            host.name = device.name;
        }
        if (host.kind.empty()) {
            host.kind = device.kind;
        }
        host.upnp = host.upnp || device.upnp;
        if (host.ssdp_server.empty()) {
            host.ssdp_server = device.ssdp_server;
        }
    }
    std::sort(list.begin() + static_cast<std::ptrdiff_t>(fixed), list.end(),
              [](const Candidate& left, const Candidate& right) { return left.number < right.number; });
    std::vector<HealthHost> hosts;
    hosts.reserve(std::min(list.size(), kHealthMaxHosts));
    for (Candidate& candidate : list) {
        if (hosts.size() >= kHealthMaxHosts) {
            break;
        }
        hosts.push_back(std::move(candidate.host));
    }
    return hosts;
}

std::vector<HealthFinding> host_findings(const HealthHost& host, const PortRun& scan, int32_t order) {
    std::vector<HealthFinding> findings;
    const auto add = [&](HealthFinding finding, std::string fact, uint16_t port) {
        finding.who = host_who(host);
        finding.where = host_where(host);
        finding.fact = std::move(fact);
        finding.port = port;
        finding.address = host.address;
        findings.push_back(std::move(finding));
    };

    // The open ports with a rule of their own.
    for (const uint16_t port : scan.open) {
        const ServiceRule* rule = service_rule(port);
        if (rule == nullptr || upnp_port(port)) {
            continue;
        }
        add(make_finding(HealthIssue::Service, rule->level, rule->title, order), port_fact(port), port);
    }

    // The router's settings page on plain http: its first plain web port.
    if (host.router) {
        for (const uint16_t port : scan.open) {
            if (banner_probe(port) != BannerProbe::Http) {
                continue;
            }
            const HealthRule* rule = health_rule(HealthIssue::RouterPage);
            std::string fact = port_fact(port);
            const std::string said = port_banner(scan, port);
            if (!said.empty()) {
                fact += ", " + said.substr(0, said.find_first_of(" ;/("));
            }
            add(make_finding(HealthIssue::RouterPage, rule->level, rule->title, order), std::move(fact), port);
            break;
        }
    }

    // UPnP, on the router only: every TV and speaker answers SSDP.
    if (host.router || host.kind == "router") {
        const bool open_port = has_port(scan.open, 1900) || has_port(scan.open, 49152);
        if (host.upnp || open_port) {
            const HealthRule* rule = health_rule(HealthIssue::Upnp);
            const uint16_t port = has_port(scan.open, 1900) ? 1900 : (has_port(scan.open, 49152) ? 49152 : 0);
            add(make_finding(HealthIssue::Upnp, rule->level, rule->title, order),
                host.upnp ? std::string("Answers UPnP searches") : "Port " + std::to_string(port) + " open", port);
        }
    }

    // The check against the list of known holes and outdated versions.
    for (const PortVerdict& verdict : scan.verdicts) {
        const auto worst = std::find_if(scan.findings.begin(), scan.findings.end(),
                                        [&verdict](const ServiceFinding& finding) {
                                            return finding.port == verdict.port;
                                        });
        if (worst == scan.findings.end()) {
            continue;
        }
        std::string fact = "Port " + std::to_string(verdict.port) + ", " + worst->name + " " + worst->version;
        HealthFinding finding;
        if (!verdict.vendor.empty()) {
            finding = make_finding(HealthIssue::Check, RiskLevel::Info, health_rule(HealthIssue::Check)->title, order);
            fact += ", " + verdict.vendor + " build";
        } else if (verdict.holes > 0) {
            std::string title = verdict.exploited > 0
                                    ? std::string("Hole attackers use")
                                    : std::to_string(verdict.holes) + (verdict.holes == 1 ? " known hole" : " known holes");
            finding = make_finding(HealthIssue::Holes, RiskLevel::Risk, std::move(title), order);
            if (worst->kind == FindingKind::Hole && worst->score >= 0) {
                fact += ", worst " + score_text(worst->score);
            }
        } else if (verdict.outdated) {
            finding = make_finding(HealthIssue::Outdated, RiskLevel::Warning,
                                   host.router ? "Old router software" : "Old software", order);
        } else {
            continue;
        }
        add(std::move(finding), std::move(fact), verdict.port);
    }

    if (scan.open.size() > kHealthManyPorts) {
        const HealthRule* rule = health_rule(HealthIssue::ManyPorts);
        add(make_finding(HealthIssue::ManyPorts, rule->level, rule->title, order),
            std::to_string(scan.open.size()) + " ports open", 0);
    }
    return findings;
}

std::vector<HealthFinding> local_findings(const HealthLocal& local) {
    std::vector<HealthFinding> findings;
    constexpr int32_t kLinkOrder = -1;
    const std::string wifi_where = local.wifi_name.empty() ? std::string("Wi-Fi") : "Wi-Fi " + local.wifi_name;

    // The joined Wi-Fi's own security.
    const int32_t joined = wifi_security_rank(local.wifi_security);
    if (!local.wifi_security.empty()) {
        HealthIssue issue = HealthIssue::Service;
        if (local.wifi_security == "open") {
            issue = HealthIssue::OpenWifi;
        } else if (local.wifi_security == "WEP") {
            issue = HealthIssue::WepWifi;
        } else if (local.wifi_security == "WPA" || local.wifi_security == "WPA-EAP") {
            issue = HealthIssue::WpaWifi;
        }
        if (const HealthRule* rule = health_rule(issue)) {
            HealthFinding finding = make_finding(issue, rule->level, rule->title, kLinkOrder);
            finding.who = "Wi-Fi";
            finding.where = wifi_where;
            finding.fact = issue == HealthIssue::OpenWifi ? std::string("Open, no password")
                                                          : "Uses " + local.wifi_security;
            findings.push_back(std::move(finding));
        }
    }

    // A look-alike: the same name, weak security, weaker than the joined
    // one's. The weakest one is named.
    if (!local.wifi_name.empty() && joined >= 0) {
        const WifiNetwork* weakest = nullptr;
        int32_t weakest_rank = joined;
        for (const WifiNetwork& network : local.nearby) {
            if (network.joined || network.name != local.wifi_name) {
                continue;
            }
            const int32_t rank = wifi_security_rank(network.security);
            if (rank >= 0 && rank <= 2 && rank < weakest_rank) {
                weakest = &network;
                weakest_rank = rank;
            }
        }
        if (weakest != nullptr) {
            const HealthRule* rule = health_rule(HealthIssue::LookAlike);
            HealthFinding finding = make_finding(HealthIssue::LookAlike, rule->level, rule->title, kLinkOrder);
            finding.who = "nearby";
            finding.where = "nearby " + weakest->name;
            finding.fact = capitalized(weakest->security) +
                           (weakest->channel > 0 ? ", channel " + std::to_string(weakest->channel) : std::string()) +
                           ", yours " + local.wifi_security;
            findings.push_back(std::move(finding));
        }
    }

    // The cable link.
    if (local.cable) {
        const bool slow = local.cable_mbps > 0 && local.cable_mbps <= kHealthSlowCableMbps;
        const bool half = local.cable_duplex == "half";
        if (slow || half) {
            const HealthRule* rule = health_rule(HealthIssue::SlowCable);
            HealthFinding finding = make_finding(HealthIssue::SlowCable, rule->level, rule->title, kLinkOrder);
            finding.who = local.cable_name.empty() ? std::string("cable") : local.cable_name;
            finding.where = (local.cable_name.empty() ? std::string() : local.cable_name + " ") + "cable";
            std::string fact = local.cable_mbps > 0 ? std::to_string(local.cable_mbps) + " Mbit/s" : std::string();
            if (!local.cable_duplex.empty()) {
                fact += (fact.empty() ? "" : ", ") + local.cable_duplex + " duplex";
            }
            finding.fact = std::move(fact);
            findings.push_back(std::move(finding));
        }
    }
    return findings;
}

void sort_findings(std::vector<HealthFinding>& findings) {
    std::stable_sort(findings.begin(), findings.end(), [](const HealthFinding& left, const HealthFinding& right) {
        if (left.level != right.level) {
            return left.level > right.level;
        }
        if (left.order != right.order) {
            return left.order < right.order;
        }
        if (left.issue != right.issue) {
            return left.issue < right.issue;
        }
        return left.port < right.port;
    });
    if (findings.size() > kHealthMaxFindings) {
        findings.resize(kHealthMaxFindings);
    }
}

int32_t wifi_security_rank(std::string_view security) {
    if (security == "open") {
        return 0;
    }
    if (security == "OWE" || security == "WEP") {
        return 1;
    }
    if (security == "WPA" || security == "WPA-EAP") {
        return 2;
    }
    if (security == "WPA2" || security == "WPA2-EAP") {
        return 3;
    }
    if (security == "WPA2/3") {
        return 4;
    }
    if (security == "WPA3" || security == "WPA3-EAP") {
        return 5;
    }
    return -1;
}

HealthCounts health_counts(const std::vector<HealthFinding>& findings) {
    HealthCounts counts;
    for (const HealthFinding& finding : findings) {
        switch (finding.level) {
            case RiskLevel::Risk:
                ++counts.risks;
                break;
            case RiskLevel::Warning:
                ++counts.warnings;
                break;
            case RiskLevel::Info:
                ++counts.infos;
                break;
        }
    }
    return counts;
}

std::string health_summary(const HealthCounts& counts) {
    std::string text;
    const auto part = [&text](int32_t count, const char* one, const char* many) {
        if (count <= 0) {
            return;
        }
        text += (text.empty() ? "" : ", ") + std::to_string(count) + " " + (count == 1 ? one : many);
    };
    part(counts.risks, "risk", "risks");
    part(counts.warnings, "warning", "warnings");
    part(counts.infos, "info", "info");
    return text.empty() ? std::string("Nothing found") : text;
}

std::string health_uplink(const HomeData& home) {
    return valid_interface_name(home.uplink_name) ? home.uplink_name : std::string();
}

std::string health_home_address(const HomeData& home) {
    uint32_t address = 0;
    if (!parse_ipv4(home.uplink_address, address)) {
        return {};
    }
    const uint32_t first = address >> 24;
    const uint32_t second = (address >> 16) & 0xffu;
    const bool home_network = first == 10 || (first == 172 && second >= 16 && second <= 31) ||
                              (first == 192 && second == 168);
    return home_network ? home.uplink_address : std::string();
}

HealthStart health_start(const HomeData& home, const ToolsData& tools) {
    HealthStart start;
    start.interface_name = health_uplink(home);
    const std::string address = health_home_address(home);
    if (!address.empty()) {
        start.asks = address.substr(0, address.rfind('.') + 1) + "1-254";
    }
    if (tools.health.state == ToolState::Running) {
        start.gate = HealthGate::Running;
    } else if (tool_running(tools)) {
        start.gate = HealthGate::OtherTool;
    } else if (start.interface_name.empty()) {
        start.gate = HealthGate::NoNetwork;
    } else if (!is_ipv4_text(home.uplink_address)) {
        start.gate = HealthGate::NoAddress;
    } else if (address.empty()) {
        start.gate = HealthGate::NotPrivate;
    } else {
        start.gate = HealthGate::Open;
    }
    return start;
}

std::string health_gate_text(HealthGate gate, const ToolsData& tools) {
    switch (gate) {
        case HealthGate::Open:
            return {};
        case HealthGate::Running:
            return "A health check is running";
        case HealthGate::OtherTool:
            return other_tool_text(tools);
        case HealthGate::NoNetwork:
            return "Join a network first";
        case HealthGate::NoAddress:
            return "No IPv4 address yet";
        case HealthGate::NotPrivate:
            return "Not a private home network, so the check stays off";
    }
    return {};
}

const char* other_tool_text(const ToolsData& data) {
    if (data.ping.state == ToolState::Running) {
        return "Ping is running";
    }
    if (data.dns.state == ToolState::Running) {
        return "DNS lookup is running";
    }
    if (data.trace.state == ToolState::Running) {
        return "Trace is running";
    }
    if (data.listen.state == ToolState::Running) {
        return "Discover is listening";
    }
    if (data.probe.state == ToolState::Running) {
        return "Discover is probing";
    }
    if (data.speed.state == ToolState::Running) {
        return "Speed test is running";
    }
    if (data.leak.state == ToolState::Running) {
        return "Leak test is running";
    }
    if (data.signin.state == ToolState::Running) {
        return "Sign-in check is running";
    }
    return "Port scan is running";
}

} // namespace model
