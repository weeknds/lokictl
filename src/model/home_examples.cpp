/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_examples.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

// The made-up battery behind example_status_details(): its charge when
// full, and the current it draws or takes in.
constexpr int32_t kExampleBatteryFullMah = 1750;
constexpr int32_t kExampleDischargeMa = 480;
constexpr int32_t kExampleChargeMa = 650;

// A signal or a speed of HomeData as a Status page number: its own "not
// known" value becomes kDetailUnknown.
int32_t detail_signal(int32_t dbm) {
    return dbm == kSignalUnknown ? kDetailUnknown : dbm;
}

int32_t detail_speed(int32_t mbps) {
    return mbps > kSpeedUnknown ? mbps : kDetailUnknown;
}

// An IPv4 address of a made-up block with the prefix every example network
// has (/24); empty stays empty.
std::string with_prefix(const std::string& address) {
    return address.empty() ? std::string() : address + "/24";
}

} // namespace

StatusDetails example_status_details(const HomeData& data) {
    StatusDetails details;

    // The ports in name order, as the live reader lists them.
    StatusPort cable;
    cable.kind = LinkKind::Cable;
    cable.name = "eth0";
    cable.hardware_address = "02:4c:4b:00:00:01";
    StatusPort wifi;
    wifi.kind = LinkKind::Wifi;
    wifi.name = "wlan0";
    wifi.hardware_address = "02:4c:4b:00:00:02";

    if (data.uplink_kind == LinkKind::Cable) {
        cable.state = PortState::Connected;
        cable.uplink = true;
        cable.speed_mbps = detail_speed(data.uplink_speed_mbps);
        cable.ipv4 = with_prefix(data.uplink_address);
        cable.ipv6 = "2001:db8:a::12";
        cable.ipv6_link = "fe80::4c:4bff:fe00:1";
        cable.router = data.gateway_address;
    } else {
        cable.state = PortState::NoLink;
    }

    if (data.uplink_kind == LinkKind::Wifi) {
        wifi.state = PortState::Connected;
        wifi.uplink = true;
        wifi.network = data.wifi_name;
        wifi.security = data.wifi_security;
        wifi.signal_dbm = detail_signal(data.wifi_signal_dbm);
        wifi.speed_mbps = detail_speed(data.uplink_speed_mbps);
        wifi.ipv4 = with_prefix(data.uplink_address);
        // Long enough to go on in a second row on the page.
        wifi.ipv6 = "2001:db8:85a3:42:1000:8a2e:370:7334";
        wifi.ipv6_link = "fe80::4c:4bff:fe00:2";
        wifi.router = data.gateway_address;
    } else if (data.ap_active) {
        wifi.state = PortState::AccessPoint;
        wifi.network = data.ap_name;
        wifi.security = data.ap_security;
        wifi.ipv4 = "10.42.0.1/24";
        wifi.ipv6_link = "fe80::4c:4bff:fe00:2";
    } else {
        wifi.state = PortState::NoLink;
    }

    details.ports.push_back(cable);
    details.ports.push_back(wifi);
    if (data.uplink_kind != LinkKind::None && !data.gateway_address.empty()) {
        details.dns.push_back(data.gateway_address);
    }

    if (data.battery_percent >= 0) {
        const int32_t percent = data.battery_percent > 100 ? 100 : data.battery_percent;
        const int32_t charge = kExampleBatteryFullMah * percent / 100;
        details.battery_charge_mah = charge;
        details.battery_full_mah = kExampleBatteryFullMah;
        details.battery_voltage_mv = 3500 + 7 * percent;
        switch (data.battery_state) {
            case BatteryState::Discharging:
                details.battery_current_ma = -kExampleDischargeMa;
                details.battery_empty_minutes = charge * 60 / kExampleDischargeMa;
                break;
            case BatteryState::Charging:
                details.battery_current_ma = kExampleChargeMa;
                details.battery_full_minutes = (kExampleBatteryFullMah - charge) * 60 / kExampleChargeMa;
                break;
            case BatteryState::Idle:
                details.battery_current_ma = 0;
                break;
            case BatteryState::Unknown:
                break;
        }
    }
    details.battery_temp_tenths = 314;
    details.battery_health = "Good";
    details.battery_cycles = 7;
    details.cpu_temp_tenths = 523;
    details.memory_free_mb = 212;
    details.memory_total_mb = 446;
    details.uptime_seconds = 2 * 86400 + 5 * 3600 + 17 * 60;
    return details;
}

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

std::vector<WifiNetwork> example_wifi_networks(const HomeData& data, bool scanned) {
    struct Entry {
        const char* name;
        const char* security;
        int32_t strength;
        int32_t channel;
    };
    // The first one is wifi_home_data()'s network. The names are invented.
    constexpr Entry kEntries[] = {
        {"Harbour-Cafe", "WPA2", 72, 6},
        {"Lighthouse-5", "WPA2/3", 64, 11},
        {"DIRECT-7F-Printer", "WPA2", 51, 6},
        {"", "WPA2", 38, 1},
        {"Pier 4 Library", "open", 29, 1},
        {"Old Boathouse Guest Network 2G", "WPA", 17, 11},
        {"Fishmarket-IoT", "WEP", 6, 13},
    };
    constexpr std::size_t kShortList = 3;
    const bool joined_here = data.uplink_kind == LinkKind::Wifi;
    std::vector<WifiNetwork> networks;
    for (const Entry& entry : kEntries) {
        if (!scanned && networks.size() >= kShortList) {
            break;
        }
        WifiNetwork network;
        network.name = entry.name;
        network.security = entry.security;
        network.strength = entry.strength;
        network.channel = entry.channel;
        network.joined = joined_here && !network.name.empty() && network.name == data.wifi_name;
        networks.push_back(network);
    }
    return networks;
}

} // namespace model
