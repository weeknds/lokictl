/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace screen::home {
namespace {

// Large enough for every format below with any int32_t value, so GCC's
// -Wformat-truncation has nothing to warn about.
constexpr std::size_t kTextBufferSize = 48;

constexpr const char* kMinusSign = "\u2212";
constexpr const char* kPartSeparator = " \u00b7 ";

constexpr int32_t kMinutesPerDay = 24 * 60;
constexpr int32_t kSignalLimit = 999;
// Rounding adds half a step of 100 kbit/s; this keeps the sum in range.
constexpr int32_t kRateLimitKbps = 2000000000;
// A rate that rounds to this many tenths of a Mbit/s (100.0) or more is
// written in whole Mbit/s.
constexpr int32_t kWholeRateTenths = 1000;

// A signal clamped to -999..999, as text with the real minus sign.
std::string signal_number(int32_t dbm) {
    const int32_t clamped = std::clamp<int32_t>(dbm, -kSignalLimit, kSignalLimit);
    if (clamped < 0) {
        return std::string(kMinusSign) + std::to_string(-clamped);
    }
    return std::to_string(clamped);
}

bool speed_known(int32_t mbps) {
    return mbps > model::kSpeedUnknown;
}

} // namespace

std::string clock_text(int32_t minutes) {
    const int32_t clamped = std::clamp<int32_t>(minutes, 0, kMinutesPerDay - 1);
    char buffer[kTextBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d",
                  static_cast<int>(clamped / 60), static_cast<int>(clamped % 60));
    return buffer;
}

std::string battery_text(int32_t percent, model::BatteryState state) {
    if (percent < 0) {
        return "--%";
    }
    std::string text = std::to_string(std::clamp<int32_t>(percent, 0, 100)) + "%";
    if (state == model::BatteryState::Charging) {
        text += "+";
    }
    return text;
}

std::string uptime_text(int32_t seconds) {
    const int32_t clamped = std::max<int32_t>(seconds, 0);
    char buffer[kTextBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
                  static_cast<int>(clamped / 3600),
                  static_cast<int>(clamped / 60 % 60),
                  static_cast<int>(clamped % 60));
    return buffer;
}

std::string rate_text(int32_t kbps) {
    if (kbps < 0) {
        return kUnknownText;
    }
    const int32_t clamped = std::clamp<int32_t>(kbps, 0, kRateLimitKbps);
    const int32_t tenths = (clamped + 50) / 100;
    char buffer[kTextBufferSize];
    if (tenths >= kWholeRateTenths) {
        // From 100 Mbit/s on, whole Mbit/s: "120" is no wider than "99.9".
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>((clamped + 500) / 1000));
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%d.%d",
                  static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
    return buffer;
}

std::string rate_short_text(int32_t kbps) {
    if (kbps < 0) {
        return kUnknownText;
    }
    return rate_text(kbps) + "M";
}

std::string signal_text(int32_t dbm) {
    if (dbm == model::kSignalUnknown) {
        return kUnknownText;
    }
    return signal_number(dbm) + " dBm";
}

std::string signal_number_text(int32_t dbm) {
    if (dbm == model::kSignalUnknown) {
        return kUnknownText;
    }
    return signal_number(dbm);
}

std::string speed_text(int32_t mbps) {
    if (!speed_known(mbps)) {
        return kUnknownText;
    }
    return std::to_string(mbps) + "M";
}

std::string speed_long_text(int32_t mbps) {
    if (!speed_known(mbps)) {
        return kUnknownText;
    }
    return std::to_string(mbps) + " Mbit/s";
}

std::string speed_number_text(int32_t mbps) {
    if (!speed_known(mbps)) {
        return kUnknownText;
    }
    return std::to_string(mbps);
}

std::string speed_part_text(int32_t mbps) {
    if (!speed_known(mbps)) {
        return "";
    }
    return speed_long_text(mbps);
}

std::string text_or_unknown(std::string_view text) {
    if (text.empty()) {
        return kUnknownText;
    }
    return std::string(text);
}

std::string network_text(const model::HomeData& data) {
    switch (data.uplink_kind) {
        case model::LinkKind::Wifi:
            return data.wifi_name.empty() ? std::string("wi-fi network") : data.wifi_name;
        case model::LinkKind::Cable:
            return "wired network";
        case model::LinkKind::None:
            break;
    }
    switch (data.offline_state) {
        case model::OfflineState::Connecting:
            return "connecting";
        case model::OfflineState::WifiOff:
            return "wi-fi off";
        case model::OfflineState::NetworkOff:
            return "network off";
        case model::OfflineState::Unknown:
            break;
    }
    return "no network";
}

std::string internet_note(const model::HomeData& data) {
    if (data.uplink_kind == model::LinkKind::None) {
        return {};
    }
    switch (data.internet) {
        case model::Internet::Portal:
            return "login page";
        case model::Internet::Limited:
        case model::Internet::None:
            return "no internet";
        case model::Internet::Unknown:
        case model::Internet::Full:
            break;
    }
    return {};
}

std::string network_name_text(const model::HomeData& data) {
    switch (data.uplink_kind) {
        case model::LinkKind::Wifi:
            return text_or_unknown(data.wifi_name);
        case model::LinkKind::Cable:
            return "wired";
        case model::LinkKind::None:
            return kUnknownText;
    }
    return kUnknownText;
}

std::string link_kind_text(model::LinkKind kind) {
    switch (kind) {
        case model::LinkKind::Wifi:
            return "wi-fi";
        case model::LinkKind::Cable:
            return "cable";
        case model::LinkKind::None:
            return "";
    }
    return "";
}

std::string link_text(const model::HomeData& data) {
    return text_or_unknown(join_parts({data.uplink_name, link_kind_text(data.uplink_kind)}));
}

bool is_offline(const model::HomeData& data) {
    return !data.ap_active && data.uplink_kind == model::LinkKind::None;
}

std::string mode_word(const model::HomeData& data) {
    if (is_offline(data)) {
        return "Offline";
    }
    return model::net_mode_name(data.mode);
}

std::string join_parts(std::initializer_list<std::string> parts) {
    std::string text;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += kPartSeparator;
        }
        text += part;
    }
    return text;
}

std::string count_text(int32_t index, int32_t count) {
    if (count <= 0) {
        return "0/0";
    }
    const int32_t position = std::clamp<int32_t>(index, 0, count - 1) + 1;
    return std::to_string(position) + "/" + std::to_string(count);
}

std::string upper_text(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return result;
}

std::string lower_text(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

} // namespace screen::home
