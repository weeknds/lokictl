/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_examples.h"

#include <cstddef>

namespace model {
namespace {

// The widest Wi-Fi name the tour shows: 32 characters, the longest name a
// Wi-Fi network can have.
constexpr char kWidestWifiName[] = "Harbour-Cafe-Upstairs-Guest-5GHz";
static_assert(sizeof(kWidestWifiName) - 1 == 32, "the widest Wi-Fi name has 32 characters");

// The widest IPv4 addresses the tour shows: 15 characters each.
constexpr char kWidestAddress[] = "100.100.100.200";
constexpr char kWidestRouterAddress[] = "100.100.100.100";
static_assert(sizeof(kWidestAddress) - 1 == 15 && sizeof(kWidestRouterAddress) - 1 == 15,
              "the widest addresses have 15 characters");

// The fastest link speed the tour shows, Mbit/s.
constexpr int32_t kWidestSpeedMbps = 1000;

// A block in the Client mode without any network value, with the sample's
// mark, clock and battery, so they never drift apart from the sample.
HomeData client_base() {
    const HomeData sample = sample_home_data();
    HomeData data;
    data.sample = sample.sample;
    data.clock_minutes = sample.clock_minutes;
    data.battery_percent = sample.battery_percent;
    data.battery_state = sample.battery_state;
    data.mode = NetMode::Client;
    data.ap_active = false;
    return data;
}

// The situations the tour walks through.
enum class Situation {
    Sample,
    Wifi,
    Cable,
    Offline,
};

// Which situation each stop starts from; home_tour_stop() then changes the
// values of the stops that have values of their own.
constexpr Situation kTourSituations[kHomeTourStops] = {
    Situation::Sample,  //  0
    Situation::Wifi,    //  1
    Situation::Sample,  //  2
    Situation::Cable,   //  3
    Situation::Sample,  //  4
    Situation::Offline, //  5
    Situation::Wifi,    //  6
    Situation::Cable,   //  7
    Situation::Wifi,    //  8
    Situation::Offline, //  9
    Situation::Cable,   // 10
    Situation::Offline, // 11
    Situation::Sample,  // 12
    Situation::Wifi,    // 13
    Situation::Wifi,    // 14: a weak signal and a slow link
    Situation::Wifi,    // 15: the widest texts
    Situation::Wifi,    // 16: values that are not known
    Situation::Wifi,    // 17
    Situation::Cable,   // 18
    Situation::Cable,   // 19: a fast link and the widest addresses
    Situation::Cable,   // 20: values that are not known
    Situation::Sample,  // 21
    Situation::Sample,  // 22: another uplink
    Situation::Sample,  // 23: one client left
    Situation::Sample,  // 24: no clients left
};

HomeData situation_data(Situation situation) {
    switch (situation) {
        case Situation::Sample:
            return sample_home_data();
        case Situation::Wifi:
            return wifi_home_data();
        case Situation::Cable:
            return cable_home_data();
        case Situation::Offline:
            return offline_home_data();
    }
    return sample_home_data();
}

} // namespace

HomeData wifi_home_data() {
    HomeData data = client_base();
    data.uplink_kind = LinkKind::Wifi;
    data.uplink_name = "wlan0";
    data.uplink_address = "10.20.30.44";
    data.gateway_address = "10.20.30.1";
    data.uplink_speed_mbps = 72;
    data.wifi_name = "Harbour-Cafe";
    data.wifi_security = "WPA2";
    data.wifi_signal_dbm = -57;
    data.down_kbps = 2400;
    data.up_kbps = 300;
    data.down_history = {12, 18, 30, 44, 62, 80, 100, 86, 70, 52, 40, 30,
                         24, 20, 28, 40, 56, 72, 60, 46, 34, 26, 30, 38};
    data.up_history = {8, 10, 8, 14, 22, 12, 8, 10, 30, 55, 100, 60,
                       28, 14, 8, 8, 12, 20, 12, 8, 10, 16, 10, 8};
    return data;
}

HomeData cable_home_data() {
    const HomeData sample = sample_home_data();
    HomeData data = client_base();
    data.uplink_kind = LinkKind::Cable;
    data.uplink_name = "eth0";
    data.uplink_address = "10.0.0.12";
    data.gateway_address = "10.0.0.1";
    data.uplink_speed_mbps = 100;
    data.down_kbps = 3800;
    data.up_kbps = 900;
    data.down_history = sample.down_history;
    data.up_history = sample.up_history;
    return data;
}

HomeData offline_home_data() {
    HomeData data = client_base();
    data.uplink_kind = LinkKind::None;
    data.uplink_speed_mbps = kSpeedUnknown;
    data.wifi_signal_dbm = kSignalUnknown;
    data.down_kbps = kRateUnknown;
    data.up_kbps = kRateUnknown;
    // Every text stays empty and both histories stay all 0.
    return data;
}

HomeData home_tour_stop(int index) {
    const int stop = index < 0 ? 0 : index % kHomeTourStops;
    HomeData data = situation_data(kTourSituations[static_cast<std::size_t>(stop)]);

    switch (stop) {
        case 14:
            data.wifi_signal_dbm = -83;
            data.uplink_speed_mbps = 6;
            break;
        case 15:
            data.wifi_name = kWidestWifiName;
            data.wifi_security = "WPA3";
            data.wifi_signal_dbm = -100;
            data.uplink_speed_mbps = kWidestSpeedMbps;
            data.uplink_address = kWidestAddress;
            data.gateway_address = kWidestRouterAddress;
            break;
        case 16:
            // The interface and the address stay known.
            data.wifi_name.clear();
            data.wifi_security.clear();
            data.wifi_signal_dbm = kSignalUnknown;
            data.uplink_speed_mbps = kSpeedUnknown;
            data.gateway_address.clear();
            break;
        case 19:
            data.uplink_speed_mbps = kWidestSpeedMbps;
            data.uplink_address = kWidestAddress;
            data.gateway_address = kWidestRouterAddress;
            break;
        case 20:
            // The interface stays known.
            data.uplink_speed_mbps = kSpeedUnknown;
            data.uplink_address.clear();
            data.gateway_address.clear();
            break;
        case 22:
            data.uplink_address = kWidestAddress;
            data.uplink_speed_mbps = kWidestSpeedMbps;
            break;
        case 23: {
            // Only the sample's second client is left.
            const HomeClient kept = data.clients.size() > 1 ? data.clients[1] : HomeClient{};
            data.clients.clear();
            data.clients.push_back(kept);
            break;
        }
        case 24:
            data.clients.clear();
            break;
        default:
            break;
    }
    return data;
}

} // namespace model
