/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <cstdint>
#include <filesystem>
#include <string>

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
//   the file stands in for it: one line per address, "<interface> <IPv4>".
// Without any uplink every value is not known. Never throws on a file
// system error.
NetworkReading read_network(const std::filesystem::path& root,
                            const std::filesystem::path& addresses_file);

} // namespace platform
