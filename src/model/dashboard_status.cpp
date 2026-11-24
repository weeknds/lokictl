/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dashboard_status.h"

#include "report.h"

#include <cstdint>

namespace model {
namespace {

const char* mode_id(NetMode mode) {
    switch (mode) {
        case NetMode::Client: return "client";
        case NetMode::Router: return "router";
        case NetMode::Share: return "share";
        case NetMode::Vpn: return "vpn";
    }
    return "client";
}

const char* flag(bool value) {
    return value ? "true" : "false";
}

// A number, or null when it is negative (not known).
std::string number_or_null(int32_t value) {
    return value < 0 ? std::string("null") : std::to_string(value);
}

} // namespace

std::string dashboard_status_json(const HomeData& home) {
    const bool wifi = home.uplink_kind == LinkKind::Wifi;
    const bool hotspot = home.ap_active && is_hotspot_mode(home.mode);
    std::string out;
    out.reserve(384);
    out += "{\"v\":1,\"sample\":";
    out += flag(home.sample);
    out += ",\"cardputer\":{\"address\":";
    out += json_text(home.uplink_address);
    out += ",\"battery\":";
    out += number_or_null(home.battery_percent);
    out += ",\"charging\":";
    out += flag(home.battery_state == BatteryState::Charging);
    out += "},\"network\":{\"name\":";
    out += json_text(wifi ? home.wifi_name : std::string());
    out += ",\"link\":\"";
    out += link_kind_name(home.uplink_kind);
    out += "\",\"internet\":\"";
    out += internet_name(home.internet);
    out += "\"},\"mode\":{\"id\":\"";
    out += mode_id(home.mode);
    out += "\",\"name\":";
    out += json_text(net_mode_name(home.mode));
    out += ",\"hotspot\":";
    out += flag(hotspot);
    out += ",\"hotspot_devices\":";
    out += std::to_string(hotspot ? home.clients.size() : 0);
    out += ",\"vpn\":\"";
    out += vpn_state_name(home.vpn.state);
    out += "\",\"kill_switch\":";
    out += flag(home.vpn.kill_switch);
    out += "},\"traffic\":{\"down_kbps\":";
    out += number_or_null(home.down_kbps);
    out += ",\"up_kbps\":";
    out += number_or_null(home.up_kbps);
    out += "}}\n";
    return out;
}

} // namespace model
