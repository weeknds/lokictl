/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace platform::remote {

// One IPv4 address of a network interface. The numbers are in host byte
// order.
struct InterfaceAddress {
    std::string interface_name; // for example "wlan0"
    uint32_t address{0};
    uint32_t netmask{0};
};

// True for the private IPv4 ranges: 10/8, 172.16/12 and 192.168/16.
bool private_address(uint32_t address);

// True when `peer` lies in the subnet of `own` (its address and netmask).
bool in_subnet(uint32_t peer, const InterfaceAddress& own);

// True when the subnets of `a` and `b` share any address.
bool subnets_overlap(const InterfaceAddress& a, const InterfaceAddress& b);

// Picks the address of the hotspot side of Remote view from `addresses`
// (every IPv4 address of every interface, as system_addresses() lists
// them). The hotspot side is only ever on `hotspot_interface`, the
// interface LokiCTL's own hotspot runs on, and only when:
// - `hotspot_interface` is not empty and is not the uplink's interface;
// - it has an IPv4 address that is private, with a netmask;
// - that address's subnet does not overlap the uplink's subnet, so every
//   peer belongs to exactly one side.
// The first address of that interface that passes wins. Returns false,
// leaving `out` as it was, when there is none.
bool pick_hotspot_address(const std::vector<InterfaceAddress>& addresses,
                          std::string_view hotspot_interface,
                          const InterfaceAddress& uplink,
                          InterfaceAddress& out);

// Every IPv4 address of every interface, from the system (getifaddrs()),
// in the system's order. Empty when the system could not be asked.
std::vector<InterfaceAddress> system_addresses();

// "10.42.0.1" for an address in host byte order; empty on an error.
std::string address_text(uint32_t address);

} // namespace platform::remote
