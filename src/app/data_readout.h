/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_source.h"

#include <cstdint>
#include <string>

namespace app {

// --print-data: takes the first reading from the source and then `steps`
// further ones, one after the other on this thread, and prints the newest
// values as name=value lines on standard output. The first five lines are,
// in this order:
//   source=<sample|demo|live>
//   sample=<yes|no>
//   clock=<HH:MM>
//   battery_percent=<0-100|unknown>
//   battery_state=<charging|discharging|idle|unknown>
// It starts no screen, no logger and no background thread, never reads or
// writes the settings file, and prints nothing else. `data_root` is handed
// to the source (see platform::make_data_source()). Returns the process
// exit code.
int run_data_readout(platform::DataSourceKind kind, uint32_t steps, const std::string& data_root);

} // namespace app
