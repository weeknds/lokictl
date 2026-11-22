/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <filesystem>

namespace platform {

// Reads the Status page's device values into `details`, from files every
// user may read. `root` stands in for the root of the file system, as for
// read_network(). Every value that cannot be read becomes
// model::kDetailUnknown.
// - Processor temperature: the first sys/class/thermal/thermal_zone* (by
//   name) whose "type" is "cpu-thermal"; its "temp" (thousandths of a
//   degree) in tenths, rounded.
// - Memory: MemAvailable and MemTotal of proc/meminfo (kB) in whole MiB.
// - Time since start: the whole seconds of the first number in
//   proc/uptime.
// It only reads. Never throws on a file system error.
void read_device_details(const std::filesystem::path& root, model::StatusDetails& details);

} // namespace platform
