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

LiveDataSource::LiveDataSource(std::string data_root) {
    // Reads the time zone once, so localtime_r() below shows local time.
    tzset();
    const std::filesystem::path root = data_root.empty() ? std::filesystem::path("/")
                                                         : std::filesystem::path(std::move(data_root));
    // Relative on purpose: an absolute path on the right would drop the root.
    power_supply_dir_ = root / "sys/class/power_supply";
}

DataSourceKind LiveDataSource::kind() const {
    return DataSourceKind::Live;
}

void LiveDataSource::read(model::HomeData& data) {
    data = model::sample_home_data();

    // If the system cannot tell the time, the clock keeps the sample's value;
    // the page still carries the sample mark.
    int32_t minutes = 0;
    if (read_local_clock(minutes)) {
        data.clock_minutes = minutes;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!battery_read_ || now - battery_read_at_ >= kBatteryInterval) {
        battery_ = read_battery(power_supply_dir_);
        battery_read_at_ = now;
        battery_read_ = true;
    }
    data.battery_percent = battery_.percent;
    data.battery_state = battery_.state;
}

// Worked out from the current time at every call: the reader then wakes
// about seven times a minute (once for the minute, six times for the
// battery), and a wake-up that comes early, or a jump of the wall clock,
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

    if (!battery_read_) {
        delay = milliseconds(0);
    } else {
        const auto battery_due = battery_read_at_ + kBatteryInterval;
        delay = std::min(delay, ceil<milliseconds>(battery_due - std::chrono::steady_clock::now()));
    }
    return std::max(delay, kMinimumDelay);
}

} // namespace platform
