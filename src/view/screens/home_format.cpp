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

constexpr int32_t kMinutesPerDay = 24 * 60;
constexpr int32_t kSignalLimit = 999;
// Rounding adds half a step of 100 kbit/s; this keeps the sum in range.
constexpr int32_t kRateLimitKbps = 2000000000;

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
    const int32_t clamped = std::clamp<int32_t>(kbps, 0, kRateLimitKbps);
    const int32_t tenths = (clamped + 50) / 100;
    char buffer[kTextBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%d.%d",
                  static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
    return buffer;
}

std::string signal_text(int32_t dbm) {
    const int32_t clamped = std::clamp<int32_t>(dbm, -kSignalLimit, kSignalLimit);
    if (clamped < 0) {
        return std::string(kMinusSign) + std::to_string(-clamped) + " dBm";
    }
    return std::to_string(clamped) + " dBm";
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
