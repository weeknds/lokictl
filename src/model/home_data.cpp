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

const char* link_kind_name(LinkKind kind) {
    switch (kind) {
        case LinkKind::None:
            return "none";
        case LinkKind::Wifi:
            return "wifi";
        case LinkKind::Cable:
            return "cable";
    }
    return "none";
}

const char* internet_name(Internet internet) {
    switch (internet) {
        case Internet::Unknown:
            return "unknown";
        case Internet::None:
            return "none";
        case Internet::Portal:
            return "portal";
        case Internet::Limited:
            return "limited";
        case Internet::Full:
            return "full";
    }
    return "unknown";
}

const char* offline_state_name(OfflineState state) {
    switch (state) {
        case OfflineState::Unknown:
            return "unknown";
        case OfflineState::Connecting:
            return "connecting";
        case OfflineState::WifiOff:
            return "wifi-off";
        case OfflineState::NetworkOff:
            return "network-off";
    }
    return "unknown";
}

bool battery_low(int32_t percent, BatteryState state) {
    return state == BatteryState::Discharging && percent >= 0 && percent <= kBatteryLowPercent;
}

bool battery_very_low(int32_t percent, BatteryState state) {
    return state == BatteryState::Discharging && percent >= 0 && percent <= kBatteryVeryLowPercent;
}

bool on_charger(BatteryState state) {
    return state == BatteryState::Charging || state == BatteryState::Idle;
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

const char* port_state_name(PortState state) {
    switch (state) {
        case PortState::Unknown:
            return "unknown";
        case PortState::Off:
            return "off";
        case PortState::NoLink:
            return "no-link";
        case PortState::Connected:
            return "connected";
        case PortState::AccessPoint:
            return "access-point";
    }
    return "unknown";
}

// Every field, in the order they are declared.
bool operator==(const StatusPort& left, const StatusPort& right) {
    return left.kind == right.kind &&
           left.name == right.name &&
           left.state == right.state &&
           left.uplink == right.uplink &&
           left.network == right.network &&
           left.security == right.security &&
           left.signal_dbm == right.signal_dbm &&
           left.speed_mbps == right.speed_mbps &&
           left.ipv4 == right.ipv4 &&
           left.ipv6 == right.ipv6 &&
           left.ipv6_link == right.ipv6_link &&
           left.router == right.router &&
           left.hardware_address == right.hardware_address;
}

bool operator!=(const StatusPort& left, const StatusPort& right) {
    return !(left == right);
}

// Every field, in the order they are declared.
bool operator==(const StatusDetails& left, const StatusDetails& right) {
    return left.ports == right.ports &&
           left.dns == right.dns &&
           left.battery_voltage_mv == right.battery_voltage_mv &&
           left.battery_current_ma == right.battery_current_ma &&
           left.battery_temp_tenths == right.battery_temp_tenths &&
           left.battery_health == right.battery_health &&
           left.battery_cycles == right.battery_cycles &&
           left.battery_charge_mah == right.battery_charge_mah &&
           left.battery_full_mah == right.battery_full_mah &&
           left.battery_empty_minutes == right.battery_empty_minutes &&
           left.battery_full_minutes == right.battery_full_minutes &&
           left.cpu_temp_tenths == right.cpu_temp_tenths &&
           left.memory_free_mb == right.memory_free_mb &&
           left.memory_total_mb == right.memory_total_mb &&
           left.uptime_seconds == right.uptime_seconds;
}

bool operator!=(const StatusDetails& left, const StatusDetails& right) {
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
           left.uplink_kind == right.uplink_kind &&
           left.uplink_name == right.uplink_name &&
           left.uplink_address == right.uplink_address &&
           left.gateway_address == right.gateway_address &&
           left.uplink_speed_mbps == right.uplink_speed_mbps &&
           left.wifi_name == right.wifi_name &&
           left.wifi_security == right.wifi_security &&
           left.wifi_signal_dbm == right.wifi_signal_dbm &&
           left.internet == right.internet &&
           left.offline_state == right.offline_state &&
           left.ap_active == right.ap_active &&
           left.ap_name == right.ap_name &&
           left.ap_security == right.ap_security &&
           left.uptime_seconds == right.uptime_seconds &&
           left.down_kbps == right.down_kbps &&
           left.up_kbps == right.up_kbps &&
           left.down_history == right.down_history &&
           left.up_history == right.up_history &&
           left.clients == right.clients &&
           left.details == right.details;
}

bool operator!=(const HomeData& left, const HomeData& right) {
    return !(left == right);
}

// These are made-up example values, so the home page has something to show.
// The data layer starts from them: the sample source hands them over as
// they are, the demo source moves some of them, and the live source falls
// back to their clock if the system cannot tell the time. The other
// made-up situations, in
// home_examples.cpp, are the only other place in the app that writes
// made-up values.
HomeData sample_home_data() {
    HomeData data;
    data.sample = true;
    data.clock_minutes = 7 * 60 + 46;
    data.battery_percent = 91;
    data.battery_state = BatteryState::Discharging;
    data.mode = NetMode::Router;
    data.mode_detail = "eth0 \u2192 wi-fi ap \u00b7 nat \u00b7 dhcp";
    data.uplink_kind = LinkKind::Cable;
    data.uplink_name = "eth0";
    data.uplink_address = "10.0.0.12";
    data.gateway_address = "10.0.0.1";
    data.uplink_speed_mbps = 100;
    data.ap_active = true;
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
