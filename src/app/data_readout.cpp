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
#include <string>

namespace app {
namespace {

// One name=value line; `value` is printed as it is, or `unknown` when it
// is empty.
void print_text(const char* name, const std::string& value) {
    std::printf("%s=%s\n", name, value.empty() ? "unknown" : value.c_str());
}

// One name=value line with a whole number, or `unknown` when `known` is
// false.
void print_number(const char* name, int32_t value, bool known) {
    if (known) {
        std::printf("%s=%d\n", name, static_cast<int>(value));
    } else {
        std::printf("%s=unknown\n", name);
    }
}

} // namespace

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

    // The network, in a fixed order.
    std::printf("mode=%s\n", screen::home::lower_text(model::net_mode_name(data.mode)).c_str());
    std::printf("link=%s\n", model::link_kind_name(data.uplink_kind));
    print_text("interface", data.uplink_name);
    print_text("address", data.uplink_address);
    print_text("router", data.gateway_address);
    print_number("speed_mbps", data.uplink_speed_mbps, data.uplink_speed_mbps > model::kSpeedUnknown);
    print_text("network", data.wifi_name);
    print_text("security", data.wifi_security);
    print_number("signal_dbm", data.wifi_signal_dbm, data.wifi_signal_dbm != model::kSignalUnknown);
    std::printf("access_point=%s\n", data.ap_active ? "yes" : "no");
    std::printf("clients=%zu\n", data.clients.size());
    print_number("down_kbps", data.down_kbps, data.down_kbps >= 0);
    print_number("up_kbps", data.up_kbps, data.up_kbps >= 0);
    return 0;
}

} // namespace app
