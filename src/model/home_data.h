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

// What the battery is doing, as the system reports it.
enum class BatteryState {
    Unknown = 0, // not reported, or a report the app does not know
    Discharging, // the device runs on the battery
    Charging,    // on the charger, and the battery is charging
    Idle,        // on the charger, not charging (full, or held by the charger)
};

// The name of a battery state in lowercase: "unknown", "discharging",
// "charging" or "idle". Used by the log and the --print-data readout.
const char* battery_state_name(BatteryState state);

// HomeData::battery_percent when the charge is not known.
constexpr int32_t kBatteryPercentUnknown = -1;

// What kind of link the uplink is.
enum class LinkKind {
    None = 0, // no link at all: no cable, no Wi-Fi network joined
    Wifi,     // joined to a Wi-Fi network
    Cable,    // a network cable (Ethernet)
};

// The name of a kind of link in lowercase: "none", "wifi" or "cable". Used
// by the log and the --print-data readout.
const char* link_kind_name(LinkKind kind);

// HomeData::wifi_signal_dbm when the signal is not known. A received signal
// is always below 0 dBm, so 0 can never be a real reading.
constexpr int32_t kSignalUnknown = 0;

// HomeData::uplink_speed_mbps when the link speed is not known.
constexpr int32_t kSpeedUnknown = 0;

// HomeData::down_kbps and up_kbps when the rate is not known. Any negative
// rate counts as not known.
constexpr int32_t kRateUnknown = -1;

// One device connected to the access point.
struct HomeClient {
    std::string name;    // host name, for example "printer"
    std::string address; // IPv4 address as text
    int32_t signal_dbm;  // received signal strength, dBm (negative)
    int32_t rate_kbps;   // current traffic, kbit/s
    bool is_new;         // connected since the user last looked
};

// Every field is compared. A new field must be added here too: the data
// layer hands a block over only when it differs from the last one.
bool operator==(const HomeClient& left, const HomeClient& right);
bool operator!=(const HomeClient& left, const HomeClient& right);

// Everything the home page shows. Numbers are stored as numbers in the units
// noted on each field; the page turns them into text. A value that is not
// known has a value of its own (an empty text, or one of the constants
// above), and the page shows it as "--" instead of an old or a made-up
// number. Each field sits on one line.
struct HomeData {
    bool sample{true};                 // true while the values are made up
    int32_t clock_minutes{0};          // time of day, minutes since midnight
    // Battery charge, per cent (0-100), or kBatteryPercentUnknown (-1) when
    // it is not known. Unknown is never shown as 0.
    int32_t battery_percent{kBatteryPercentUnknown};
    BatteryState battery_state{BatteryState::Unknown}; // what the battery is doing
    NetMode mode{NetMode::Client};     // the active mode
    std::string mode_detail;           // one line on what the mode does
    LinkKind uplink_kind{LinkKind::None}; // what kind of link the uplink is
    // The uplink's values. An empty text, kSpeedUnknown and kSignalUnknown
    // mean "not known"; the Wi-Fi values are empty when the uplink is not
    // a Wi-Fi network.
    std::string uplink_name;           // interface of the uplink, for example "eth0"
    std::string uplink_address;        // IPv4 address of the uplink as text
    std::string gateway_address;       // IPv4 address of the router the uplink leads to
    int32_t uplink_speed_mbps{kSpeedUnknown}; // link speed of the uplink, Mbit/s
    std::string wifi_name;             // name (SSID) of the Wi-Fi network the uplink joined
    std::string wifi_security;         // security of that network, for example "WPA2"
    int32_t wifi_signal_dbm{kSignalUnknown}; // received signal of that network, dBm (negative)
    bool ap_active{false};             // true while the device runs its own access point
    // The access point's values, the uptime and the clients only mean
    // something while ap_active is true.
    std::string ap_name;               // network name (SSID) of the access point
    std::string ap_security;           // security of the access point, for example "WPA2"
    int32_t uptime_seconds{0};         // time since the mode started, seconds
    // The current rates, kbit/s; kRateUnknown (or any negative value) when
    // not known.
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

// Every field is compared. A new field must be added here too: the data
// layer hands a block over only when it differs from the last one, so a
// field left out here would never reach the screen on its own.
bool operator==(const HomeData& left, const HomeData& right);
bool operator!=(const HomeData& left, const HomeData& right);

// Made-up example values for the home page: a router that shares its
// cable uplink through its own access point, with three clients. They are
// the `sample` data source as they are, the starting point of the `demo`
// source, and the `live` source's clock when the system cannot tell the
// time.
// HomeData::sample is true, so the page marks them as sample data. The
// other made-up situations are in home_examples.h.
HomeData sample_home_data();

} // namespace model
