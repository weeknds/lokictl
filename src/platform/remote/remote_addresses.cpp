/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote/remote_addresses.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <utility>

namespace platform::remote {

bool private_address(uint32_t address) {
    return (address & 0xFF000000U) == 0x0A000000U || (address & 0xFFF00000U) == 0xAC100000U ||
           (address & 0xFFFF0000U) == 0xC0A80000U;
}

bool in_subnet(uint32_t peer, const InterfaceAddress& own) {
    return (peer & own.netmask) == (own.address & own.netmask);
}

bool subnets_overlap(const InterfaceAddress& a, const InterfaceAddress& b) {
    // Two subnets share an address exactly when they agree on the bits of
    // the shorter netmask.
    const uint32_t mask = a.netmask & b.netmask;
    return (a.address & mask) == (b.address & mask);
}

bool pick_hotspot_address(const std::vector<InterfaceAddress>& addresses,
                          std::string_view hotspot_interface,
                          const InterfaceAddress& uplink,
                          InterfaceAddress& out) {
    if (hotspot_interface.empty() || hotspot_interface == uplink.interface_name) {
        return false;
    }
    for (const InterfaceAddress& entry : addresses) {
        if (entry.interface_name != hotspot_interface || entry.netmask == 0 || !private_address(entry.address) ||
            subnets_overlap(entry, uplink)) {
            continue;
        }
        out = entry;
        return true;
    }
    return false;
}

std::vector<InterfaceAddress> system_addresses() {
    std::vector<InterfaceAddress> addresses;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return addresses;
    }
    for (const ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || !entry->ifa_netmask || entry->ifa_addr->sa_family != AF_INET || !entry->ifa_name) {
            continue;
        }
        sockaddr_in own{};
        sockaddr_in mask{};
        std::memcpy(&own, entry->ifa_addr, sizeof(own));
        std::memcpy(&mask, entry->ifa_netmask, sizeof(mask));
        InterfaceAddress value;
        value.interface_name = entry->ifa_name;
        value.address = ntohl(own.sin_addr.s_addr);
        value.netmask = ntohl(mask.sin_addr.s_addr);
        addresses.push_back(std::move(value));
    }
    ::freeifaddrs(list);
    return addresses;
}

std::string address_text(uint32_t address) {
    in_addr value{};
    value.s_addr = htonl(address);
    char text[INET_ADDRSTRLEN] = {};
    if (!::inet_ntop(AF_INET, &value, text, sizeof(text))) {
        return {};
    }
    return text;
}

} // namespace platform::remote
