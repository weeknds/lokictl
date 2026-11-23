/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "status_rows.h"

#include "home_format.h"

#include <cstdio>
#include <cstdlib>

namespace screen::status {
namespace {

using home::kUnknownText;

// Large enough for every format below with any int32_t value.
constexpr std::size_t kTextBufferSize = 48;

constexpr const char* kMinusSign = "−";
constexpr const char* kDegrees = "°C";
constexpr const char* kUplinkMark = " · uplink";
constexpr const char* kUsbMark = " · usb";

bool known(int32_t value) {
    return value != model::kDetailUnknown;
}

std::string number_or_unknown(int32_t value) {
    return known(value) ? std::to_string(value) : std::string(kUnknownText);
}

// A whole number in tenths as "31.4", with the real minus sign.
std::string tenths_text(int32_t tenths) {
    const int64_t magnitude = std::llabs(static_cast<int64_t>(tenths));
    char buffer[kTextBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%lld.%lld",
                  static_cast<long long>(magnitude / 10), static_cast<long long>(magnitude % 10));
    return (tenths < 0 ? std::string(kMinusSign) : std::string()) + buffer;
}

std::string port_state_text(const model::StatusPort& port) {
    switch (port.state) {
        case model::PortState::Connected:
            return "connected";
        case model::PortState::AccessPoint:
            return "access point";
        case model::PortState::NoLink:
            return port.kind == model::LinkKind::Wifi ? "not joined" : "no cable";
        case model::PortState::Off:
            return "off";
        case model::PortState::Unknown:
            break;
    }
    return kUnknownText;
}

NoteTone port_state_tone(model::PortState state) {
    return state == model::PortState::Connected || state == model::PortState::AccessPoint ? NoteTone::Good
                                                                                          : NoteTone::Quiet;
}

std::string signal_detail_text(int32_t dbm) {
    return known(dbm) ? home::signal_text(dbm) : std::string(kUnknownText);
}

std::string speed_detail_text(int32_t mbps) {
    return known(mbps) && mbps > 0 ? home::speed_long_text(mbps) : std::string(kUnknownText);
}

void add_heading(std::vector<StatusRow>& rows, std::string label, std::string note, NoteTone tone) {
    rows.push_back({true, std::move(label), std::move(note), tone});
}

void add_row(std::vector<StatusRow>& rows, std::string label, const std::string& value) {
    rows.push_back({false, std::move(label), home::text_or_unknown(value), NoteTone::Plain});
}

// A value in one row, or in two when it is longer than kValueChars bytes:
// the first row ends after the last `break_after` character that fits (an
// IPv6 address breaks after a colon), or else at kValueChars, never inside
// a UTF-8 character; the second row has no label and ends in dots when the
// rest is still too long.
void add_long_rows(std::vector<StatusRow>& rows, const char* label, const std::string& text, char break_after) {
    if (text.size() <= kValueChars) {
        add_row(rows, label, text);
        return;
    }
    std::size_t cut = break_after != '\0' ? text.rfind(break_after, kValueChars - 1) : std::string::npos;
    cut = cut == std::string::npos || cut == 0 ? kValueChars : cut + 1;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    add_row(rows, label, text.substr(0, cut));
    add_row(rows, "", text.substr(cut));
}

void add_port(std::vector<StatusRow>& rows, const model::StatusPort& port, bool usb) {
    std::string heading = home::link_kind_text(port.kind) + " " + port.name;
    if (port.uplink) {
        heading += kUplinkMark;
    }
    if (usb) {
        heading += kUsbMark;
    }
    add_heading(rows, heading, port_state_text(port), port_state_tone(port.state));

    const bool active = port.state == model::PortState::Connected || port.state == model::PortState::AccessPoint;
    if (active) {
        if (port.kind == model::LinkKind::Wifi) {
            add_long_rows(rows, "network", port.network, '\0');
            add_row(rows, "security", port.security);
            add_row(rows, "signal", signal_detail_text(port.signal_dbm));
        }
        add_row(rows, "speed", speed_detail_text(port.speed_mbps));
        add_row(rows, "IPv4", port.ipv4);
        add_long_rows(rows, "IPv6", port.ipv6, ':');
        add_long_rows(rows, "link-local", port.ipv6_link, ':');
        add_row(rows, "router", port.router);
    }
    add_row(rows, "hardware", port.hardware_address);
}

bool is_usb_wifi(const model::HomeData& data, const std::string& interface_name) {
    for (const model::UsbWifiAdapter& adapter : data.usb_wifi) {
        if (adapter.interface_name == interface_name) {
            return true;
        }
    }
    return false;
}

std::string managed_text(model::Managed managed) {
    switch (managed) {
        case model::Managed::Yes:
            return "yes";
        case model::Managed::No:
            return "no";
        case model::Managed::Unknown:
            break;
    }
    return kUnknownText;
}

// LokiCTL needs no USB Wi-Fi adapter, so "none" is a normal state and says
// so.
void add_usb_wifi(std::vector<StatusRow>& rows, const std::vector<model::UsbWifiAdapter>& adapters) {
    if (adapters.empty()) {
        add_heading(rows, "usb wi-fi", "none", NoteTone::Quiet);
        add_row(rows, "adapter", "none (optional)");
        add_row(rows, "", "built-in Wi-Fi is used");
        return;
    }
    const std::size_t count = adapters.size();
    add_heading(rows,
                "usb wi-fi",
                std::to_string(count) + (count == 1 ? " adapter" : " adapters"),
                NoteTone::Plain);
    for (const model::UsbWifiAdapter& adapter : adapters) {
        add_row(rows, "interface", adapter.interface_name);
        add_long_rows(rows, "name", adapter.name, ' ');
        add_row(rows, "driver", adapter.driver);
        add_row(rows, "usb id", adapter.usb_id);
        add_row(rows, "managed", managed_text(adapter.managed));
    }
}

// The tunnel's file, server and last reply while it runs;
// otherwise how many WireGuard files there are to use. The kill switch of
// the VPN gateway while it is on; with the tunnel down that is a warning
// (hotspot devices get nothing).
void add_vpn(std::vector<StatusRow>& rows, const model::VpnStatus& vpn) {
    const bool down = model::vpn_gateway_down(vpn);
    const NoteTone tone = down                                       ? NoteTone::Warning
                          : vpn.state == model::VpnState::On         ? NoteTone::Good
                          : vpn.state == model::VpnState::Connecting ? NoteTone::Plain
                                                                     : NoteTone::Quiet;
    add_heading(rows, "vpn", down && vpn.state == model::VpnState::Off ? "down" : model::vpn_state_name(vpn.state),
                tone);
    if (vpn.kill_switch) {
        add_row(rows, "kill switch", down ? "devices blocked" : "on");
    }
    if (vpn.state == model::VpnState::Off) {
        const std::size_t count = vpn.files.size();
        add_row(rows, "files", count == 0 ? std::string("none") : std::to_string(count));
        return;
    }
    add_long_rows(rows, "file", vpn.file, ' ');
    add_long_rows(rows, "server", vpn.server, '.');
    if (vpn.state == model::VpnState::On) {
        add_row(rows, "reply", vpn.reply_seconds < 0 ? std::string("none yet")
                                                     : std::to_string(vpn.reply_seconds) + " s ago");
    }
}

std::string battery_state_text(model::BatteryState state) {
    switch (state) {
        case model::BatteryState::Discharging:
            return "discharging";
        case model::BatteryState::Charging:
            return "charging";
        case model::BatteryState::Idle:
            return "on the charger";
        case model::BatteryState::Unknown:
            break;
    }
    return kUnknownText;
}

std::string time_left_text(const model::HomeData& data) {
    const model::StatusDetails& details = data.details;
    if (data.battery_state == model::BatteryState::Discharging && known(details.battery_empty_minutes)) {
        return duration_text(details.battery_empty_minutes);
    }
    if (data.battery_state == model::BatteryState::Charging && known(details.battery_full_minutes)) {
        return duration_text(details.battery_full_minutes) + " to full";
    }
    return kUnknownText;
}

std::string charge_text(const model::StatusDetails& details) {
    if (!known(details.battery_charge_mah) && !known(details.battery_full_mah)) {
        return kUnknownText;
    }
    return number_or_unknown(details.battery_charge_mah) + " / " + number_or_unknown(details.battery_full_mah) +
           " mAh";
}

std::string memory_text(const model::StatusDetails& details) {
    if (!known(details.memory_free_mb)) {
        return kUnknownText;
    }
    std::string text = std::to_string(details.memory_free_mb);
    if (known(details.memory_total_mb)) {
        text += " of " + std::to_string(details.memory_total_mb);
    }
    return text + " MB free";
}

} // namespace

std::string voltage_text(int32_t millivolts) {
    if (!known(millivolts) || millivolts < 0) {
        return kUnknownText;
    }
    const int32_t hundredths = (millivolts + 5) / 10;
    char buffer[kTextBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%d.%02d V",
                  static_cast<int>(hundredths / 100), static_cast<int>(hundredths % 100));
    return buffer;
}

std::string current_text(int32_t milliamps) {
    if (!known(milliamps)) {
        return kUnknownText;
    }
    const int64_t magnitude = std::llabs(static_cast<int64_t>(milliamps));
    return (milliamps < 0 ? std::string(kMinusSign) : std::string()) + std::to_string(magnitude) + " mA";
}

std::string temperature_text(int32_t tenths) {
    if (!known(tenths)) {
        return kUnknownText;
    }
    return tenths_text(tenths) + " " + kDegrees;
}

std::string duration_text(int32_t minutes) {
    if (!known(minutes) || minutes < 0) {
        return kUnknownText;
    }
    const int32_t hours = minutes / 60;
    if (hours == 0) {
        return std::to_string(minutes) + " min";
    }
    return std::to_string(hours) + " h " + std::to_string(minutes % 60) + " min";
}

std::string uptime_long_text(int32_t seconds) {
    if (!known(seconds) || seconds < 0) {
        return kUnknownText;
    }
    const int32_t days = seconds / 86400;
    const int32_t minutes = seconds / 60;
    if (days == 0) {
        return duration_text(minutes);
    }
    return std::to_string(days) + " d " + std::to_string(minutes / 60 % 24) + " h " +
           std::to_string(minutes % 60) + " min";
}

std::vector<StatusRow> status_rows(const model::HomeData& data) {
    const model::StatusDetails& details = data.details;
    std::vector<StatusRow> rows;
    rows.reserve(40);

    if (details.ports.empty()) {
        add_heading(rows, "network", "", NoteTone::Quiet);
        add_row(rows, "ports", "");
    }
    for (const model::StatusPort& port : details.ports) {
        add_port(rows, port, is_usb_wifi(data, port.name));
    }
    add_vpn(rows, data.vpn);
    add_usb_wifi(rows, data.usb_wifi);

    add_heading(rows, "dns", "", NoteTone::Plain);
    if (details.dns.empty()) {
        add_row(rows, "server", "");
    }
    for (const std::string& server : details.dns) {
        add_long_rows(rows, "server", server, ':');
    }

    add_heading(rows,
                "battery",
                home::battery_text(data.battery_percent, data.battery_state),
                model::battery_low(data.battery_percent, data.battery_state) ? NoteTone::Warning
                                                                              : NoteTone::Plain);
    add_row(rows, "state", battery_state_text(data.battery_state));
    add_row(rows, "voltage", voltage_text(details.battery_voltage_mv));
    add_row(rows, "current", current_text(details.battery_current_ma));
    add_row(rows, "temp", temperature_text(details.battery_temp_tenths));
    add_row(rows, "health", details.battery_health);
    add_row(rows, "cycles", number_or_unknown(details.battery_cycles));
    add_row(rows, "charge", charge_text(details));
    add_row(rows, "time left", time_left_text(data));

    add_heading(rows, "device", "", NoteTone::Plain);
    add_row(rows, "processor", temperature_text(details.cpu_temp_tenths));
    add_row(rows, "memory", memory_text(details));
    add_row(rows, "uptime", uptime_long_text(details.uptime_seconds));
    return rows;
}

} // namespace screen::status
