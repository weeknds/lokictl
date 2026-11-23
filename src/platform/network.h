/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace platform {

// What the kernel says about the link that carries the traffic (the
// "uplink"). A value that is not known keeps the "not known" value of
// model::HomeData: an empty text, model::kSpeedUnknown or
// model::kSignalUnknown.
struct NetworkReading {
    model::LinkKind kind{model::LinkKind::None}; // None when there is no uplink
    std::string interface_name;                  // for example "wlan0"
    std::string address;                         // IPv4 address of the interface
    std::string gateway;                         // IPv4 address of the router
    int32_t speed_mbps{model::kSpeedUnknown};    // link speed, cable only
    int32_t signal_dbm{model::kSignalUnknown};   // received signal, Wi-Fi only
};

// Reads the uplink from the files the kernel offers to every user. It only
// reads: it never changes an interface, never starts a scan and never
// writes a file.
//
// `root` stands in for the root of the file system: "<root>/proc/net/route"
// is read in place of "/proc/net/route", and so on.
// - The uplink is the default route (destination and mask 0, flag "up")
//   with the lowest metric in proc/net/route whose interface reads 1 in
//   sys/class/net/<name>/carrier. It is Wi-Fi when that interface has a
//   "wireless" folder, a cable when its "type" is 1 (Ethernet); any other
//   interface (the loopback, a tunnel) is skipped. An interface that does
//   not carry the default route is never the uplink. On a tie of metrics the
//   route listed first wins.
// - The router is the gateway of that route.
// - The signal is the level of the interface in proc/net/wireless, in dBm
//   (-255 to -1; anything else is not known).
// - The speed is sys/class/net/<name>/speed for a cable (a read error, or 0
//   or less, is not known). A Wi-Fi network's speed is not read here.
// - The address is the interface's first IPv4 address. The system keeps that
//   list in no file, so with `addresses_file` empty it comes from the
//   system (getifaddrs()). With a file named, the system is not asked and
//   the file stands in for it: one line per address, "<interface> <IPv4>"
//   or "<interface> <IPv4>/<prefix>" (the prefix is not used here).
// Without any uplink every value is not known. Never throws on a file
// system error.
NetworkReading read_network(const std::filesystem::path& root,
                            const std::filesystem::path& addresses_file);

// The most network ports read_ports() lists.
constexpr std::size_t kMaxPorts = 4;

// The network ports for the Status page, read like read_network() from the
// files the kernel offers to every user (and getifaddrs(), or
// `addresses_file` in its place); it only reads.
// - A port is an interface in sys/class/net whose "type" is 1 (Ethernet)
//   and that has a "device" entry (real hardware, so not the loopback, a
//   bridge or a tunnel); Wi-Fi when it has a "wireless" folder, otherwise a
//   cable. Ports are listed by name, at most kMaxPorts.
// - State: Off when "flags" says the interface is not up; NoLink when
//   "carrier" is not 1 (or cannot be read); Connected otherwise; Unknown
//   when "flags" cannot be read. The port named `uplink_name` is marked as
//   the uplink.
// - Only a connected port gets its signal (Wi-Fi, proc/net/wireless), its
//   speed and duplex (cable, "speed" and "duplex", which counts only when it
//   reads "full" or "half") and its router (the gateway of its own default
//   route with the lowest metric in proc/net/route).
// - IPv4: the port's first IPv4 address with its prefix, "10.0.0.12/24"
//   (a line of `addresses_file` without a prefix gives the address alone).
// - IPv6, from proc/net/if_inet6: the first address that is not link-local
//   and the first link-local one, without prefix; tentative addresses and
//   addresses that failed their check are skipped.
// - The hardware address is "address", when it reads like one.
// The Wi-Fi name, security and speed are not read here (see
// apply_network_manager()). Never throws on a file system error.
std::vector<model::StatusPort> read_ports(const std::filesystem::path& root,
                                          const std::filesystem::path& addresses_file,
                                          const std::string& uplink_name);

// The most DNS servers read_dns_servers() lists, as many as the system's
// resolver uses.
constexpr std::size_t kMaxDnsServers = 3;

// The DNS servers in "<root>/etc/resolv.conf": the address of every
// "nameserver" line that holds an IPv4 or IPv6 address, in order, at most
// kMaxDnsServers. Empty without the file. Never throws.
std::vector<std::string> read_dns_servers(const std::filesystem::path& root);

// How many bytes an interface has received and sent since it came up, as
// the kernel counts them.
struct TrafficCounters {
    uint64_t received{0}; // statistics/rx_bytes
    uint64_t sent{0};     // statistics/tx_bytes
};

// Reads "<root>/sys/class/net/<interface_name>/statistics/rx_bytes" and
// "tx_bytes", which every user may read. Like read_network() it only reads.
// `line` picks the line of each file, counting from 0: the kernel's files
// hold one number, so the app passes 0; a test folder may hold several
// numbers, one per line, and a `line` past the last takes the last one.
// Returns false, and leaves `counters` alone, when the name could not be an
// interface's, or either file is missing, empty or holds anything but a
// whole number of 1 to 20 digits that fits in 64 bits. Never throws on a
// file system error.
bool read_traffic_counters(const std::filesystem::path& root,
                           const std::string& interface_name,
                           std::size_t line,
                           TrafficCounters& counters);

// How many times the link of `interface_name` went down since the
// interface came up: "<root>/sys/class/net/<name>/carrier_down_count", or,
// without that file, (carrier_changes + 1) / 2 (the kernel counts a fall
// first, then each rise and fall). `line` as for read_traffic_counters().
// Returns false, and leaves `downs` alone, when the name could not be an
// interface's or neither file holds a whole number. Never throws.
bool read_link_downs(const std::filesystem::path& root,
                     const std::string& interface_name,
                     std::size_t line,
                     uint64_t& downs);

// The first IPv4 address of `interface_name` as text, from the system
// (getifaddrs()), or from `addresses_file` when one is named (see
// read_network()). Empty when it has none. Never throws.
std::string interface_address(const std::filesystem::path& addresses_file,
                              const std::string& interface_name);

// `bytes` sent or received in `elapsed_ms` as kbit/s (one kbit/s is one bit
// per millisecond), rounded; at most the largest int32_t. `elapsed_ms` must
// be above 0.
int32_t rate_kbps(uint64_t bytes, int64_t elapsed_ms);

} // namespace platform
