/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "battery.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace platform {
namespace {

constexpr int32_t kFullPercent = 100;
// A charge has at most this many digits; anything longer is not a charge.
constexpr std::size_t kMaxDigits = 9;

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

// The first line of a small text file, without the white space around it.
// Returns false if the file cannot be opened or read.
bool read_first_line(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }
    value = trim(line);
    return true;
}

// A whole number: an optional minus sign followed by 1 to kMaxDigits digits
// and nothing else. Returns false for any other text.
bool parse_whole_number(const std::string& text, int32_t& value) {
    std::size_t index = 0;
    const bool negative = !text.empty() && text[0] == '-';
    if (negative) {
        index = 1;
    }
    const std::size_t digits = text.size() - index;
    if (digits == 0 || digits > kMaxDigits) {
        return false;
    }
    int32_t result = 0;
    for (; index < text.size(); ++index) {
        const char character = text[index];
        if (character < '0' || character > '9') {
            return false;
        }
        result = result * 10 + (character - '0');
    }
    value = negative ? -result : result;
    return true;
}

int32_t read_capacity(const std::filesystem::path& battery_dir) {
    std::string text;
    int32_t percent = 0;
    if (!read_first_line(battery_dir / "capacity", text) || !parse_whole_number(text, percent) ||
        percent < 0) {
        return model::kBatteryPercentUnknown;
    }
    return std::min(percent, kFullPercent);
}

model::BatteryState read_state(const std::filesystem::path& battery_dir) {
    std::string text;
    if (!read_first_line(battery_dir / "status", text)) {
        return model::BatteryState::Unknown;
    }
    if (text == "Charging") {
        return model::BatteryState::Charging;
    }
    if (text == "Discharging") {
        return model::BatteryState::Discharging;
    }
    if (text == "Full" || text == "Not charging") {
        return model::BatteryState::Idle;
    }
    return model::BatteryState::Unknown;
}

} // namespace

BatteryReading read_battery(const std::filesystem::path& power_supply_dir) {
    BatteryReading reading;

    // The names of the sub-folders first, so they can be taken in order.
    // The entries are usually links to the supplies' own folders, so they
    // are not filtered by their type: a name without a readable `type`
    // file is simply passed over below. The iterator is stepped with
    // increment() and an error code, because a range-for would throw.
    std::error_code error;
    std::filesystem::directory_iterator entry(power_supply_dir, error);
    if (error) {
        return reading;
    }
    std::vector<std::string> names;
    const std::filesystem::directory_iterator end;
    while (entry != end) {
        names.push_back(entry->path().filename().string());
        entry.increment(error);
        if (error) {
            return reading;
        }
    }
    std::sort(names.begin(), names.end());

    for (const auto& name : names) {
        const std::filesystem::path supply_dir = power_supply_dir / name;
        std::string type;
        if (!read_first_line(supply_dir / "type", type) || type != "Battery") {
            continue;
        }
        reading.percent = read_capacity(supply_dir);
        reading.state = read_state(supply_dir);
        return reading;
    }
    return reading;
}

} // namespace platform
