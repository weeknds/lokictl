/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Probing for the Discover page (its step 3): which addresses a probe may
// ask, reading the system's neighbour table, the questions it sends for
// kinds and names, and the pretend runner's made-up probe. Nothing here
// opens a socket; the tool runner sends and receives (tool_runner.cpp).
namespace platform {

// A probe reads the neighbour table this often while it sends its echoes,
// waits this long after the last echo for the last answers (the system
// gives up on an address after about 3 s of ARP), and waits this long after
// its last question for the answers to it (SSDP answers within its MX of
// 2 s).
constexpr uint32_t kProbeTableReadMs = 1000;
constexpr uint32_t kProbeSettleMs = 3000;
constexpr uint32_t kProbeAnswerMs = 3000;

// The system's neighbour table, readable by every user.
constexpr const char* kNeighbourTable = "/proc/net/arp";

// Whether `address` (host byte order) belongs to a private home network:
// 10/8, 172.16/12 or 192.168/16.
bool private_network_address(uint32_t address);

// The addresses one probe asks, all on one network.
struct ProbeRange {
    uint32_t network{0};              // host byte order
    int32_t prefix{0};                // 24 to 30
    std::vector<uint32_t> addresses;  // ascending, host byte order; never the network, broadcast or own ones
    std::string text;                 // "10.0.0.0/24"
};

// Works out the range of an interface with the IPv4 `address` and
// `netmask` (host byte order): its network, but never more than the /24
// around `address`, without the network and broadcast addresses and
// without any of `own` (the Cardputer's own addresses). Returns false with
// a short `reason` when `address` is not on a private network ("not a
// private network"), when the network is smaller than a /30, or when
// nothing is left to ask.
bool make_probe_range(uint32_t address,
                      uint32_t netmask,
                      const std::vector<uint32_t>& own,
                      ProbeRange& out,
                      std::string& reason);

// make_probe_range() for the interface `interface_name`, with its first
// IPv4 address and netmask and every IPv4 address of the Cardputer as
// `own`, from the system (getifaddrs()). Returns false with a short
// `reason` ("no IPv4 address on eth0") as make_probe_range() does.
bool read_probe_range(const std::string& interface_name, ProbeRange& out, std::string& reason);

// One complete entry of the neighbour table.
struct Neighbour {
    std::array<uint8_t, 6> hardware{};
    uint32_t address{0};  // host byte order
};

// Reads the text of the neighbour table (kNeighbourTable: a header line,
// then "<IPv4> <hw type> <flags> <hw address> <mask> <device>" per line)
// and keeps the complete Ethernet entries (flag 0x2) with a unicast
// hardware address whose IPv4 address is in `range`. Lines that do not
// read like that are skipped. At most model::kProbeMaxAddresses entries.
std::vector<Neighbour> parse_neighbours(std::string_view text, const ProbeRange& range);

// A multicast DNS question (RFC 6762) for `name` (labels of 1-63
// printable characters other than '.', underscores allowed, as in
// "_services._dns-sd._udp.local") and `type`, with the ID `id` and no
// flags. Empty for a name it refuses.
std::vector<uint8_t> mdns_query(std::string_view name, uint16_t type, uint16_t id);

// The service-enumeration name: an mDNS question for it makes each device
// answer with the service types it offers.
constexpr const char* kServiceEnumeration = "_services._dns-sd._udp.local";

// One SSDP search for every device and service ("ssdp:all", MX 2).
std::string ssdp_search();

// The pretend runner's made-up probe on interface `interface_name`, at
// `elapsed_ms` into the run: the sample's 10.0.0.0/24 network (the
// Cardputer at 10.0.0.12 and 10.0.0.13), timed as the live runner sends,
// with the devices of pretend_capture() (listen.h) and two quiet ones it
// never hears. The made-up neighbour table and replies go through
// parse_neighbours() and HeardDevices like real ones. `sent` is the number
// of packets the run has sent by then; `done` says the run has ended.
model::ProbeRun pretend_probe(uint32_t elapsed_ms, const std::string& interface_name, uint32_t& sent, bool& done);

} // namespace platform
