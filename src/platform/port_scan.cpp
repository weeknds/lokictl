/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "port_scan.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstddef>

namespace platform {
namespace {

// "a.b.c.d" as a number (host byte order); false when it is not one.
bool parse_address(const std::string& text, uint32_t& out) {
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

constexpr uint32_t address_of(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

constexpr uint32_t kPretendNetmask = 0xFFFFFF00U;

// The Cardputer's made-up addresses: the sample's uplink, its hotspot, and
// the Wi-Fi example's uplink.
const std::vector<OwnNetwork>& pretend_networks() {
    static const std::vector<OwnNetwork> networks{
        {address_of(10, 0, 0, 12), kPretendNetmask},
        {address_of(10, 42, 0, 1), kPretendNetmask},
        {address_of(10, 20, 30, 44), kPretendNetmask},
    };
    return networks;
}

// The made-up open ports of each kind of device, and the ports that never
// answer.
const std::vector<uint16_t> kRouterOpen{53, 80, 443, 1900};
const std::vector<uint16_t> kRouterSilent{23, 7547};
const std::vector<uint16_t> kOwnOpen{22};
const std::vector<uint16_t> kPrinterOpen{80, 443, 515, 631, 9100};
const std::vector<uint16_t> kMediaOpen{7000, 8008, 8009, 8443};
const std::vector<uint16_t> kStorageOpen{22, 139, 445, 5000, 32400};
const std::vector<uint16_t> kDeviceSilent{3389};

bool contains(const std::vector<uint16_t>& list, uint16_t port) {
    return std::find(list.begin(), list.end(), port) != list.end();
}

} // namespace

std::vector<OwnNetwork> read_own_networks() {
    std::vector<OwnNetwork> networks;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return networks;
    }
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET || entry->ifa_netmask == nullptr ||
            entry->ifa_netmask->sa_family != AF_INET || (entry->ifa_flags & IFF_LOOPBACK) != 0 ||
            (entry->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        OwnNetwork network;
        network.address = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
        network.netmask = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask)->sin_addr.s_addr);
        networks.push_back(network);
    }
    ::freeifaddrs(list);
    return networks;
}

bool port_scan_allowed(uint32_t target, const std::vector<OwnNetwork>& networks, std::string& reason) {
    for (const OwnNetwork& network : networks) {
        if (target == network.address) {
            return true;  // the Cardputer itself
        }
    }
    for (const OwnNetwork& network : networks) {
        if (network.netmask == 0 || (target & network.netmask) != (network.address & network.netmask)) {
            continue;
        }
        const uint32_t host = target & ~network.netmask;
        // A /31 or /32 has no network or broadcast address.
        if (~network.netmask > 1 && (host == 0 || host == ~network.netmask)) {
            reason = "a network or broadcast address";
            return false;
        }
        return true;
    }
    reason = "not on the Cardputer's networks";
    return false;
}

model::PortRun pretend_port_scan(uint32_t elapsed_ms, const std::string& target, uint32_t& sent, bool& done) {
    model::PortRun run;
    run.state = model::ToolState::Running;
    run.target = target;
    const std::vector<model::CommonPort>& ports = model::common_ports();
    run.total = static_cast<int32_t>(ports.size());
    uint32_t address = 0;
    if (!parse_address(target, address) || !port_scan_allowed(address, pretend_networks(), run.reason)) {
        run.state = model::ToolState::Failed;
        run.error = model::ToolError::Failed;
        if (run.reason.empty()) {
            run.reason = "not a valid address";
        }
        done = true;
        return run;
    }

    const std::vector<uint16_t>* open = &kRouterOpen;
    const std::vector<uint16_t>* silent = &kDeviceSilent;
    const bool own = std::any_of(pretend_networks().begin(), pretend_networks().end(),
                                 [address](const OwnNetwork& network) { return network.address == address; });
    if (own) {
        open = &kOwnOpen;
    } else if ((address & 0xFFU) == 1) {
        silent = &kRouterSilent;
    } else {
        switch ((address & 0xFFU) % 3) {
            case 0:
                open = &kPrinterOpen;
                break;
            case 1:
                open = &kMediaOpen;
                break;
            default:
                open = &kStorageOpen;
                break;
        }
    }

    uint32_t end = 0;
    for (std::size_t index = 0; index < ports.size(); ++index) {
        const uint16_t port = ports[index].port;
        const uint32_t sent_at = static_cast<uint32_t>(index) * static_cast<uint32_t>(model::kPortIntervalMs);
        const bool quiet = contains(*silent, port);
        const uint32_t settled_at = sent_at + (quiet ? static_cast<uint32_t>(model::kPortTimeoutMs) : 2u);
        end = std::max(end, settled_at);
        if (elapsed_ms < sent_at) {
            continue;
        }
        ++sent;
        ++run.asked;
        if (elapsed_ms < settled_at) {
            continue;
        }
        if (quiet) {
            ++run.silent;
        } else if (contains(*open, port)) {
            run.open.push_back(port);
        } else {
            ++run.closed;
        }
    }
    if (elapsed_ms >= end) {
        run.state = model::ToolState::Done;
        done = true;
    }
    return run;
}

} // namespace platform
