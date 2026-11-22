/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_data.h"

namespace model {
namespace {

HomeClient make_client(const char* name,
                       const char* address,
                       int32_t signal_dbm,
                       int32_t rate_kbps,
                       bool is_new) {
    HomeClient client;
    client.name = name;
    client.address = address;
    client.signal_dbm = signal_dbm;
    client.rate_kbps = rate_kbps;
    client.is_new = is_new;
    return client;
}

} // namespace

const char* net_mode_name(NetMode mode) {
    switch (mode) {
        case NetMode::Client:
            return "Client";
        case NetMode::Router:
            return "Router";
        case NetMode::Share:
            return "Share";
        case NetMode::Vpn:
            return "VPN";
    }
    return "Client";
}

const char* battery_state_name(BatteryState state) {
    switch (state) {
        case BatteryState::Unknown:
            return "unknown";
        case BatteryState::Discharging:
            return "discharging";
        case BatteryState::Charging:
            return "charging";
        case BatteryState::Idle:
            return "idle";
    }
    return "unknown";
}

bool operator==(const HomeClient& left, const HomeClient& right) {
    return left.name == right.name &&
           left.address == right.address &&
           left.signal_dbm == right.signal_dbm &&
           left.rate_kbps == right.rate_kbps &&
           left.is_new == right.is_new;
}

bool operator!=(const HomeClient& left, const HomeClient& right) {
    return !(left == right);
}

// Every field of HomeData, in the order they are declared. A new field must
// be added here too (see home_data.h).
bool operator==(const HomeData& left, const HomeData& right) {
    return left.sample == right.sample &&
           left.clock_minutes == right.clock_minutes &&
           left.battery_percent == right.battery_percent &&
           left.battery_state == right.battery_state &&
           left.mode == right.mode &&
           left.mode_detail == right.mode_detail &&
           left.uplink_name == right.uplink_name &&
           left.uplink_address == right.uplink_address &&
           left.uplink_speed_mbps == right.uplink_speed_mbps &&
           left.ap_name == right.ap_name &&
           left.ap_security == right.ap_security &&
           left.uptime_seconds == right.uptime_seconds &&
           left.down_kbps == right.down_kbps &&
           left.up_kbps == right.up_kbps &&
           left.down_history == right.down_history &&
           left.up_history == right.up_history &&
           left.clients == right.clients;
}

bool operator!=(const HomeData& left, const HomeData& right) {
    return !(left == right);
}

// These are made-up example values, so the home page has something to show.
// The data layer starts from them: the sample source hands them over as
// they are, the demo source moves some of them, and the live source keeps
// every value it does not read yet. Nothing else in the app writes sample
// values.
HomeData sample_home_data() {
    HomeData data;
    data.sample = true;
    data.clock_minutes = 7 * 60 + 46;
    data.battery_percent = 91;
    data.battery_state = BatteryState::Discharging;
    data.mode = NetMode::Router;
    data.mode_detail = "eth0 \u2192 wi-fi ap \u00b7 nat \u00b7 dhcp";
    data.uplink_name = "eth0";
    data.uplink_address = "10.0.0.12";
    data.uplink_speed_mbps = 100;
    data.ap_name = "LokiCTL-7F";
    data.ap_security = "WPA2";
    data.uptime_seconds = 751;
    data.down_kbps = 3800;
    data.up_kbps = 900;
    data.down_history = {8, 10, 14, 22, 35, 48, 60, 52, 40, 30, 38, 55,
                         72, 90, 100, 84, 66, 50, 36, 28, 34, 52, 70, 62};
    data.up_history = {8, 8, 12, 25, 12, 8, 17, 42, 17, 8, 8, 12,
                       33, 58, 25, 12, 8, 8, 17, 100, 50, 25, 12, 17};
    data.clients.push_back(make_client("phone", "10.42.0.11", -61, 400, false));
    data.clients.push_back(make_client("laptop", "10.42.0.14", -48, 2100, false));
    data.clients.push_back(make_client("tv", "10.42.0.23", -70, 1300, true));
    return data;
}

} // namespace model
