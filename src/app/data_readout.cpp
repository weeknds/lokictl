/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_readout.h"

#include "data_reader.h"
#include "home_data.h"
#include "home_format.h"

#include <cstdio>

namespace app {

int run_data_readout(platform::DataSourceKind kind, uint32_t steps, const std::string& data_root) {
    // The same hand-over the app uses, but every reading is taken here, one
    // after the other: the background thread is never started.
    platform::DataReader reader(platform::make_data_source(kind, data_root));
    reader.read_now();
    for (uint32_t step = 0; step < steps; ++step) {
        reader.read_now();
    }
    const model::HomeData data = reader.latest();

    std::printf("source=%s\n", platform::data_source_name(reader.kind()));
    std::printf("sample=%s\n", data.sample ? "yes" : "no");
    std::printf("clock=%s\n", screen::home::clock_text(data.clock_minutes).c_str());
    if (data.battery_percent >= 0) {
        std::printf("battery_percent=%d\n", static_cast<int>(data.battery_percent));
    } else {
        std::printf("battery_percent=unknown\n");
    }
    std::printf("battery_state=%s\n", model::battery_state_name(data.battery_state));
    return 0;
}

} // namespace app
