/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "trap_data.h"

#include <algorithm>
#include <cstdio>

namespace model {
namespace {

constexpr int64_t kMinuteMs = 60000;

} // namespace

const std::array<TrapService, kTrapServiceCount>& trap_services() {
    // The low ports and their listen ports are also in
    // src/helper/lokictl_helper.c (kTrapPorts); keep the two the same.
    static const std::array<TrapService, kTrapServiceCount> services{{
        {21, 30021, "files", "File transfer (FTP)", TrapGreeting::Ftp},
        {23, 30023, "login", "Remote login (Telnet)", TrapGreeting::Telnet},
        {80, 30080, "web", "Web page (HTTP)", TrapGreeting::Web},
        {445, 30445, "sharing", "File sharing (SMB)", TrapGreeting::None},
        {3389, 3389, "desktop", "Remote desktop (RDP)", TrapGreeting::None},
        {8080, 8080, "web", "Web page (HTTP)", TrapGreeting::Web},
    }};
    return services;
}

const TrapService* trap_service(uint16_t port) {
    for (const TrapService& service : trap_services()) {
        if (service.port == port || service.listen_port == port) {
            return &service;
        }
    }
    return nullptr;
}

bool operator==(const TrapPort& left, const TrapPort& right) {
    return left.port == right.port && left.state == right.state;
}

bool operator!=(const TrapPort& left, const TrapPort& right) {
    return !(left == right);
}

bool operator==(const TrapHit& left, const TrapHit& right) {
    return left.time == right.time && left.address == right.address && left.source_port == right.source_port &&
           left.port == right.port && left.first_bytes == right.first_bytes;
}

bool operator!=(const TrapHit& left, const TrapHit& right) {
    return !(left == right);
}

bool operator==(const TrapRun& left, const TrapRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.helper_problem == right.helper_problem && left.elapsed_s == right.elapsed_s &&
           left.ports == right.ports && left.raised == right.raised && left.dropped == right.dropped &&
           left.hits == right.hits;
}

bool operator!=(const TrapRun& left, const TrapRun& right) {
    return !(left == right);
}

int32_t trap_open_ports(const TrapRun& run) {
    return static_cast<int32_t>(std::count_if(
        run.ports.begin(), run.ports.end(), [](const TrapPort& port) { return port.state == TrapPortState::Open; }));
}

std::string trap_shown_text(const uint8_t* bytes, std::size_t size) {
    std::string text;
    bool space = false;
    for (std::size_t index = 0; index < size && bytes && text.size() < kTrapShownMax; ++index) {
        const uint8_t byte = bytes[index];
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
            space = !text.empty();
            continue;
        }
        if (space) {
            text += ' ';
            space = false;
            if (text.size() >= kTrapShownMax) {
                break;
            }
        }
        text += (byte > 0x20 && byte < 0x7f) ? static_cast<char>(byte) : '.';
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::vector<uint16_t> parse_listening_ports(const std::string& text) {
    std::vector<uint16_t> ports;
    std::size_t start = text.find('\n');  // after the column names
    while (start != std::string::npos && start < text.size()) {
        ++start;
        std::size_t end = text.find('\n', start);
        const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        unsigned int port = 0;
        unsigned int state = 0;
        // "  0: 0100007F:0016 00000000:0000 0A ..."
        if (std::sscanf(line.c_str(), " %*u: %*[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x", &port, &state) == 2 &&
            state == 0x0A /* listening */ && port > 0 && port <= 0xffff) {
            ports.push_back(static_cast<uint16_t>(port));
        }
        start = end;
    }
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

bool TrapLimiter::allow(uint32_t source, int64_t now_ms) {
    recent_.erase(std::remove_if(recent_.begin(),
                                 recent_.end(),
                                 [now_ms](const Entry& entry) { return now_ms - entry.at_ms >= kMinuteMs; }),
                  recent_.end());
    if (static_cast<int32_t>(recent_.size()) >= kTrapPerMinute) {
        return false;
    }
    const auto from_source = std::count_if(
        recent_.begin(), recent_.end(), [source](const Entry& entry) { return entry.source == source; });
    if (from_source >= kTrapPerSourcePerMinute) {
        return false;
    }
    recent_.push_back({source, now_ms});
    return true;
}

} // namespace model
