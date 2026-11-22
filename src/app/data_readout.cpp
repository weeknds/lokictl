/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_readout.h"

#include "data_reader.h"
#include "home_data.h"
#include "home_format.h"
#include "live_data_source.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

namespace app {
namespace {

// How long the readout waits at most for the network service.
constexpr std::chrono::milliseconds kServiceWait{2000};

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

// One name=value line with a traffic history: its shares, oldest first,
// separated by commas.
void print_history(const char* name, const std::array<uint8_t, model::kHomeHistoryLength>& history) {
    std::string values;
    for (const uint8_t share : history) {
        if (!values.empty()) {
            values += ',';
        }
        values += std::to_string(share);
    }
    std::printf("%s=%s\n", name, values.c_str());
}

// A Status page number as text, or `unknown`.
std::string detail_text(int32_t value) {
    return value == model::kDetailUnknown ? std::string("unknown") : std::to_string(value);
}

// A text, or `unknown` when it is empty.
std::string text_or_unknown(const std::string& value) {
    return value.empty() ? std::string("unknown") : value;
}

// One line per port: its fields as name=value pairs in a fixed order,
// separated by spaces, with the network name last (it may hold spaces).
void print_port(const model::StatusPort& port) {
    std::printf("port=%s kind=%s state=%s uplink=%s ipv4=%s ipv6=%s ipv6_link=%s router=%s "
                "speed_mbps=%s signal_dbm=%s hardware=%s security=%s network=%s\n",
                port.name.c_str(),
                model::link_kind_name(port.kind),
                model::port_state_name(port.state),
                port.uplink ? "yes" : "no",
                text_or_unknown(port.ipv4).c_str(),
                text_or_unknown(port.ipv6).c_str(),
                text_or_unknown(port.ipv6_link).c_str(),
                text_or_unknown(port.router).c_str(),
                detail_text(port.speed_mbps).c_str(),
                detail_text(port.signal_dbm).c_str(),
                text_or_unknown(port.hardware_address).c_str(),
                text_or_unknown(port.security).c_str(),
                text_or_unknown(port.network).c_str());
}

void print_details(const model::StatusDetails& details) {
    std::printf("ports=%zu\n", details.ports.size());
    for (const model::StatusPort& port : details.ports) {
        print_port(port);
    }
    std::string servers;
    for (const std::string& server : details.dns) {
        if (!servers.empty()) {
            servers += ',';
        }
        servers += server;
    }
    print_text("dns", servers);
    std::printf("battery_voltage_mv=%s\n", detail_text(details.battery_voltage_mv).c_str());
    std::printf("battery_current_ma=%s\n", detail_text(details.battery_current_ma).c_str());
    std::printf("battery_temp_tenths=%s\n", detail_text(details.battery_temp_tenths).c_str());
    print_text("battery_health", details.battery_health);
    std::printf("battery_cycles=%s\n", detail_text(details.battery_cycles).c_str());
    std::printf("battery_charge_mah=%s\n", detail_text(details.battery_charge_mah).c_str());
    std::printf("battery_full_mah=%s\n", detail_text(details.battery_full_mah).c_str());
    std::printf("battery_empty_min=%s\n", detail_text(details.battery_empty_minutes).c_str());
    std::printf("battery_full_min=%s\n", detail_text(details.battery_full_minutes).c_str());
    std::printf("cpu_temp_tenths=%s\n", detail_text(details.cpu_temp_tenths).c_str());
    std::printf("memory_free_mb=%s\n", detail_text(details.memory_free_mb).c_str());
    std::printf("memory_total_mb=%s\n", detail_text(details.memory_total_mb).c_str());
    std::printf("uptime_s=%s\n", detail_text(details.uptime_seconds).c_str());
}

} // namespace

int run_data_readout(platform::DataSourceKind kind, uint32_t steps, const std::string& data_root) {
    // The same hand-over the app uses, but every reading is taken here, one
    // after the other: the background thread is never started. The live
    // source's connection to the network service starts in the background,
    // so the readout first waits for it (a short, bounded time).
    platform::DataReader reader(platform::make_data_source(kind, data_root));
    // The readout prints the Status page's details too, so the live source
    // reads them as if that page were open.
    reader.set_details_shown(true);
    reader.wait_until_ready(kServiceWait);
    reader.read_now();
    // A rate needs two readings of the byte counters. On the real system
    // they must be apart in time, so with a link the readout waits for the
    // next traffic reading and takes it; a test folder needs no wait.
    if (kind == platform::DataSourceKind::Live && data_root.empty() &&
        reader.latest().uplink_kind != model::LinkKind::None) {
        std::this_thread::sleep_for(platform::LiveDataSource::kTrafficInterval);
        reader.read_now();
    }
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
    print_number("ap_channel", data.ap_channel, data.ap_channel > 0);
    std::printf("clients=%zu\n", data.clients.size());
    print_number("down_kbps", data.down_kbps, data.down_kbps >= 0);
    print_number("up_kbps", data.up_kbps, data.up_kbps >= 0);
    print_history("down_history", data.down_history);
    print_history("up_history", data.up_history);
    std::printf("internet=%s\n", model::internet_name(data.internet));
    std::printf("state=%s\n", model::offline_state_name(data.offline_state));
    print_details(data.details);
    return 0;
}

} // namespace app
