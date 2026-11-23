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

// The longest health text kept.
constexpr std::size_t kMaxHealthText = 24;

// The folder of the first battery in `power_supply_dir` (see
// read_battery()), or false when there is none.
bool find_battery(const std::filesystem::path& power_supply_dir, std::filesystem::path& battery_dir) {
    // The names of the sub-folders first, so they can be taken in order.
    // The entries are usually links to the supplies' own folders, so they
    // are not filtered by their type: a name without a readable `type`
    // file is simply passed over below. The iterator is stepped with
    // increment() and an error code, because a range-for would throw.
    std::error_code error;
    std::filesystem::directory_iterator entry(power_supply_dir, error);
    if (error) {
        return false;
    }
    std::vector<std::string> names;
    const std::filesystem::directory_iterator end;
    while (entry != end) {
        names.push_back(entry->path().filename().string());
        entry.increment(error);
        if (error) {
            return false;
        }
    }
    std::sort(names.begin(), names.end());

    for (const auto& name : names) {
        const std::filesystem::path supply_dir = power_supply_dir / name;
        std::string type;
        if (read_first_line(supply_dir / "type", type) && type == "Battery") {
            battery_dir = supply_dir;
            return true;
        }
    }
    return false;
}

// A whole number from `file` in the battery's folder, or false.
bool read_number(const std::filesystem::path& battery_dir, const char* file, int32_t& value) {
    std::string text;
    return read_first_line(battery_dir / file, text) && parse_whole_number(text, value);
}

// A number in millionths (µV, µA, µAh) in thousandths (mV, mA, mAh),
// rounded half away from zero; unknown when it cannot be read.
int32_t read_thousandths(const std::filesystem::path& battery_dir, const char* file) {
    int32_t value = 0;
    if (!read_number(battery_dir, file, value)) {
        return model::kDetailUnknown;
    }
    return (value >= 0 ? value + 500 : value - 500) / 1000;
}

// Seconds (0 or more) in whole minutes; unknown when it cannot be read.
int32_t read_minutes(const std::filesystem::path& battery_dir, const char* file) {
    int32_t seconds = 0;
    if (!read_number(battery_dir, file, seconds) || seconds < 0) {
        return model::kDetailUnknown;
    }
    return seconds / 60;
}

} // namespace

BatteryReading read_battery(const std::filesystem::path& power_supply_dir) {
    BatteryReading reading;
    std::filesystem::path battery_dir;
    if (find_battery(power_supply_dir, battery_dir)) {
        reading.percent = read_capacity(battery_dir);
        reading.state = read_state(battery_dir);
        if (reading.state == model::BatteryState::Discharging) {
            reading.current_ma = read_thousandths(battery_dir, "current_now");
            reading.charge_mah = read_thousandths(battery_dir, "charge_now");
            if (reading.charge_mah == model::kDetailUnknown && reading.percent >= 0) {
                const int32_t full = read_thousandths(battery_dir, "charge_full");
                if (full != model::kDetailUnknown && full > 0) {
                    reading.charge_mah = static_cast<int32_t>(static_cast<int64_t>(full) * reading.percent / 100);
                }
            }
            reading.empty_minutes = read_minutes(battery_dir, "time_to_empty_now");
        }
    }
    return reading;
}

void read_battery_details(const std::filesystem::path& power_supply_dir, model::StatusDetails& details) {
    details.battery_voltage_mv = model::kDetailUnknown;
    details.battery_current_ma = model::kDetailUnknown;
    details.battery_temp_tenths = model::kDetailUnknown;
    details.battery_health.clear();
    details.battery_cycles = model::kDetailUnknown;
    details.battery_charge_mah = model::kDetailUnknown;
    details.battery_full_mah = model::kDetailUnknown;
    details.battery_empty_minutes = model::kDetailUnknown;
    details.battery_full_minutes = model::kDetailUnknown;

    std::filesystem::path battery_dir;
    if (!find_battery(power_supply_dir, battery_dir)) {
        return;
    }
    details.battery_voltage_mv = read_thousandths(battery_dir, "voltage_now");
    details.battery_current_ma = read_thousandths(battery_dir, "current_now");
    details.battery_charge_mah = read_thousandths(battery_dir, "charge_now");
    details.battery_full_mah = read_thousandths(battery_dir, "charge_full");
    int32_t value = 0;
    if (read_number(battery_dir, "temp", value)) {
        details.battery_temp_tenths = value;
    }
    if (read_number(battery_dir, "cycle_count", value) && value >= 0) {
        details.battery_cycles = value;
    }
    details.battery_full_minutes = read_minutes(battery_dir, "time_to_full_now");

    std::string health;
    if (read_first_line(battery_dir / "health", health) && !health.empty() &&
        health.size() <= kMaxHealthText &&
        std::all_of(health.begin(), health.end(), [](unsigned char ch) { return ch >= 0x20 && ch < 0x7f; })) {
        details.battery_health = health;
    }
}

} // namespace platform
