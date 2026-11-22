/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <cstdint>
#include <filesystem>

namespace platform {

struct BatteryReading {
    int32_t percent{model::kBatteryPercentUnknown}; // 0-100, or unknown
    model::BatteryState state{model::BatteryState::Unknown};
};

// Reads the charge and the state of the first battery in
// `power_supply_dir`, the folder that holds one sub-folder per power supply
// (on Linux that is /sys/class/power_supply). The sub-folders are taken in
// order of their names, and the first whose `type` file reads "Battery" is
// used.
// - `capacity` is the charge in per cent. Only a whole number counts; a
//   negative number, a missing file or any other text means unknown, and a
//   number above 100 counts as 100.
// - `status`: "Charging" is charging, "Discharging" is discharging, "Full"
//   and "Not charging" are idle; anything else, or no file, is unknown.
//   The state is read on its own, whatever the charge says.
// Without the folder, without a battery, or when the folder cannot be read,
// both are unknown. Never throws on a file system error.
BatteryReading read_battery(const std::filesystem::path& power_supply_dir);

} // namespace platform
