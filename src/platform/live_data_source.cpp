/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "live_data_source.h"

#include <algorithm>
#include <ctime>
#include <time.h>
#include <utility>

namespace platform {
namespace {

using std::chrono::milliseconds;

constexpr int32_t kMinutesPerHour = 60;

// The time of day on the local clock, in minutes since midnight. Returns
// false if the system cannot tell.
bool read_local_clock(int32_t& minutes) {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
    if (now == static_cast<std::time_t>(-1) || localtime_r(&now, &local_time) == nullptr) {
        return false;
    }
    minutes = local_time.tm_hour * kMinutesPerHour + local_time.tm_min;
    return true;
}

} // namespace

LiveDataSource::LiveDataSource(std::string data_root)
    : sample_(model::sample_home_data()) {
    // Reads the time zone once, so localtime_r() below shows local time.
    tzset();
    const bool test_folder = !data_root.empty();
    root_ = test_folder ? std::filesystem::path(std::move(data_root)) : std::filesystem::path("/");
    // Relative on purpose: an absolute path on the right would drop the root.
    power_supply_dir_ = root_ / "sys/class/power_supply";
    if (test_folder) {
        addresses_file_ = root_ / "addresses";
    }
}

DataSourceKind LiveDataSource::kind() const {
    return DataSourceKind::Live;
}

void LiveDataSource::read(model::HomeData& data) {
    // The Client mode, no access point, no clients, every network value not
    // known; the mark stays on while the rates are made up.
    data = model::HomeData{};
    data.sample = true;
    data.mode = model::NetMode::Client;
    data.ap_active = false;

    // If the system cannot tell the time, the clock keeps the sample's value;
    // the page still carries the sample mark.
    int32_t minutes = 0;
    data.clock_minutes = read_local_clock(minutes) ? minutes : sample_.clock_minutes;

    const auto now = std::chrono::steady_clock::now();
    if (!battery_read_ || now - battery_read_at_ >= kBatteryInterval) {
        battery_ = read_battery(power_supply_dir_);
        battery_read_at_ = now;
        battery_read_ = true;
    }
    data.battery_percent = battery_.percent;
    data.battery_state = battery_.state;

    if (!network_read_ || now - network_read_at_ >= kNetworkInterval) {
        network_ = read_network(root_, addresses_file_);
        network_read_at_ = now;
        network_read_ = true;
    }
    data.uplink_kind = network_.kind;
    data.uplink_name = network_.interface_name;
    data.uplink_address = network_.address;
    data.gateway_address = network_.gateway;
    data.uplink_speed_mbps = network_.speed_mbps;
    data.wifi_signal_dbm = network_.signal_dbm;

    if (network_.kind == model::LinkKind::None) {
        // No traffic without a link; both histories stay all 0.
        data.down_kbps = model::kRateUnknown;
        data.up_kbps = model::kRateUnknown;
    } else {
        // Still made up (real rates come later).
        data.down_kbps = sample_.down_kbps;
        data.up_kbps = sample_.up_kbps;
        data.down_history = sample_.down_history;
        data.up_history = sample_.up_history;
    }
}

// Worked out from the current time at every call: the reader then wakes at
// most about nineteen times a minute (once for the minute, every 5 s for the
// network and every 10 s for the battery; those two often fall together),
// and a wake-up that comes early, or a jump of the wall clock,
// corrects itself at the next call. The minute follows the wall clock,
// while the battery and the waiting follow the steady clock, which never
// jumps.
milliseconds LiveDataSource::next_read_delay() {
    using std::chrono::ceil;
    using std::chrono::minutes;

    const auto since_epoch = std::chrono::duration_cast<milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    milliseconds into_minute = since_epoch % minutes(1);
    if (into_minute < milliseconds(0)) {
        into_minute += minutes(1);
    }
    milliseconds delay = minutes(1) - into_minute + kMinuteMargin;

    if (!battery_read_ || !network_read_) {
        delay = milliseconds(0);
    } else {
        const auto now = std::chrono::steady_clock::now();
        const auto battery_due = battery_read_at_ + kBatteryInterval;
        const auto network_due = network_read_at_ + kNetworkInterval;
        delay = std::min(delay, ceil<milliseconds>(battery_due - now));
        delay = std::min(delay, ceil<milliseconds>(network_due - now));
    }
    return std::max(delay, kMinimumDelay);
}

} // namespace platform
