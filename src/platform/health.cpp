/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "health.h"

#include "port_scan.h"
#include "probe.h"

#include <algorithm>
#include <fstream>

namespace platform {
namespace {

// The first line of a small system file; empty when it cannot be read.
std::string read_line(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return {};
    }
    return line;
}

bool parse_ipv4(const std::string& text, uint32_t& out) {
    if (!model::is_ipv4_text(text)) {
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

// A run long enough for every made-up tool to have ended.
constexpr uint32_t kPretendEndMs = 10u * 60u * 1000u;

} // namespace

void read_cable(const std::string& interface_name, model::HealthLocal& local) {
    if (!local.cable || !model::valid_interface_name(interface_name)) {
        return;
    }
    const std::string folder = "/sys/class/net/" + interface_name + "/";
    const std::string speed = read_line(folder + "speed");
    if (!speed.empty() && speed.size() <= 6 && std::all_of(speed.begin(), speed.end(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        const int32_t mbps = std::stoi(speed);
        if (mbps > 0) {
            local.cable_mbps = mbps;
        }
    }
    const std::string duplex = read_line(folder + "duplex");
    if (duplex == "full" || duplex == "half") {
        local.cable_duplex = duplex;
    }
}

bool parse_network(const std::string& text, uint32_t& network, uint32_t& netmask) {
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos || slash + 1 >= text.size() || text.size() - slash > 3) {
        return false;
    }
    uint32_t address = 0;
    if (!parse_ipv4(text.substr(0, slash), address)) {
        return false;
    }
    const std::string bits = text.substr(slash + 1);
    if (!std::all_of(bits.begin(), bits.end(), [](char character) { return character >= '0' && character <= '9'; })) {
        return false;
    }
    const int prefix = std::stoi(bits);
    if (prefix < 1 || prefix > 32) {
        return false;
    }
    netmask = prefix == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> prefix);
    network = address & netmask;
    return true;
}

void add_ssdp_version(const model::HealthHost& host, model::PortRun& scan, const std::string& list_path) {
    if (host.ssdp_server.empty() || scan.state != model::ToolState::Done || !model::port_banner(scan, 1900).empty()) {
        return;
    }
    const auto at = std::lower_bound(scan.banners.begin(), scan.banners.end(), uint16_t{1900},
                                     [](const model::PortBanner& banner, uint16_t port) { return banner.port < port; });
    scan.banners.insert(at, model::PortBanner{1900, host.ssdp_server});
    check_port_services(scan, list_path);
}

PretendHealth plan_pretend_health(const std::string& interface_name,
                                  model::HealthLocal local,
                                  const std::vector<model::HeardDevice>& known,
                                  const std::string& list_path) {
    PretendHealth plan;
    if (local.cable && local.cable_duplex.empty()) {
        local.cable_duplex = "half";
    }
    model::HealthRun& start = plan.start;
    start.state = model::ToolState::Running;
    start.interface_name = interface_name;
    start.findings = model::local_findings(local);
    model::sort_findings(start.findings);

    bool done = false;
    const model::ProbeRun probe = pretend_probe(kPretendEndMs, interface_name, plan.sweep_sent, done);
    model::HealthRun& swept = plan.swept;
    swept = start;
    swept.network = probe.network;
    swept.total = probe.total;
    swept.asked = probe.total;
    if (probe.state == model::ToolState::Failed) {
        swept.state = model::ToolState::Failed;
        swept.error = probe.error;
        swept.reason = probe.reason;
        return plan;
    }
    swept.step = model::HealthStep::Ports;
    uint32_t network = 0;
    uint32_t netmask = 0;
    if (!parse_network(probe.network, network, netmask)) {
        swept.hosts = 0;
        return plan;
    }
    std::vector<model::HeardDevice> devices = probe.devices;
    devices.insert(devices.end(), known.begin(), known.end());
    const std::vector<model::HealthHost> hosts = model::health_hosts(local, devices, network, netmask);
    swept.hosts = static_cast<int32_t>(hosts.size());
    for (std::size_t index = 0; index < hosts.size(); ++index) {
        uint32_t sent = 0;
        bool scanned = false;
        model::PortRun scan = pretend_port_scan(kPretendEndMs, hosts[index].address, sent, scanned);
        std::vector<model::HealthFinding> findings;
        if (scan.state == model::ToolState::Done) {
            check_port_services(scan, list_path);
            add_ssdp_version(hosts[index], scan, list_path);
            findings = model::host_findings(hosts[index], scan, static_cast<int32_t>(index));
        }
        plan.found.push_back(std::move(findings));
        plan.host_sent.push_back(sent);
    }
    return plan;
}

model::HealthRun pretend_health(const PretendHealth& plan, uint32_t elapsed_ms, uint32_t& sent, bool& done) {
    done = false;
    if (elapsed_ms < kPretendHealthSweepMs) {
        model::HealthRun run = plan.start;
        run.network = plan.swept.network;
        run.total = plan.swept.total;
        run.asked = static_cast<int32_t>(static_cast<uint64_t>(run.total) * elapsed_ms / kPretendHealthSweepMs);
        run.elapsed_s = static_cast<int32_t>(elapsed_ms / 1000);
        sent = static_cast<uint32_t>(static_cast<uint64_t>(plan.sweep_sent) * elapsed_ms / kPretendHealthSweepMs);
        return run;
    }
    model::HealthRun run = plan.swept;
    run.elapsed_s = static_cast<int32_t>(elapsed_ms / 1000);
    sent = plan.sweep_sent;
    if (run.state == model::ToolState::Failed) {
        done = true;
        return run;
    }
    const uint32_t into = elapsed_ms - kPretendHealthSweepMs;
    const std::size_t finished = std::min<std::size_t>(into / kPretendHealthHostMs, plan.found.size());
    for (std::size_t index = 0; index < finished; ++index) {
        run.findings.insert(run.findings.end(), plan.found[index].begin(), plan.found[index].end());
        sent += plan.host_sent[index];
    }
    model::sort_findings(run.findings);
    run.scanned = static_cast<int32_t>(finished);
    if (finished == plan.found.size()) {
        run.state = model::ToolState::Done;
        done = true;
    }
    return run;
}

} // namespace platform
