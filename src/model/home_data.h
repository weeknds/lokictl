/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

// How many values each traffic history holds, oldest first.
constexpr std::size_t kHomeHistoryLength = 24;

// What the device does with its network connections.
enum class NetMode {
    Client = 0, // joins a Wi-Fi network like a phone
    Router,     // shares its wired uplink through its own access point
    Share,      // shares another Wi-Fi network through its own access point
    Vpn,        // sends its traffic through a VPN tunnel
};
constexpr int kNetModeCount = 4;

// The name of a mode as shown on screen: "Client", "Router", "Share" or
// "VPN". Pages write it in capitals or lowercase as their theme asks.
const char* net_mode_name(NetMode mode);

// One device connected to the access point.
struct HomeClient {
    std::string name;    // host name, for example "printer"
    std::string address; // IPv4 address as text
    int32_t signal_dbm;  // received signal strength, dBm (negative)
    int32_t rate_kbps;   // current traffic, kbit/s
    bool is_new;         // connected since the user last looked
};

// Everything the home page shows. Numbers are stored as numbers in the units
// noted on each field; the page turns them into text.
struct HomeData {
    bool sample{true};                 // true while the values are made up
    int32_t clock_minutes{0};          // time of day, minutes since midnight
    int32_t battery_percent{0};        // battery charge, per cent (0-100)
    NetMode mode{NetMode::Client};     // the active mode
    std::string mode_detail;           // one line on what the mode does
    std::string uplink_name;           // interface of the uplink, for example "eth0"
    std::string uplink_address;        // IPv4 address of the uplink as text
    int32_t uplink_speed_mbps{0};      // link speed of the uplink, Mbit/s
    std::string ap_name;               // network name (SSID) of the access point
    std::string ap_security;           // security of the access point, for example "WPA2"
    int32_t uptime_seconds{0};         // time since the mode started, seconds
    int32_t down_kbps{0};              // current download rate, kbit/s
    int32_t up_kbps{0};                // current upload rate, kbit/s
    // The two traffic histories, oldest first. Each value is a share of
    // that history's own recent peak (0-100, the peak is 100), not of a
    // fixed rate, so each graph fills its own height however busy its
    // direction is; the current rates above give the real numbers.
    std::array<uint8_t, kHomeHistoryLength> down_history{}; // download rate history
    std::array<uint8_t, kHomeHistoryLength> up_history{};   // upload rate history
    std::vector<HomeClient> clients;   // connected devices, in the order they are shown
};

// Made-up example values for the home page, used until the data layer reads
// the real ones. HomeData::sample is true, so the page marks them as sample
// data.
HomeData sample_home_data();

} // namespace model
