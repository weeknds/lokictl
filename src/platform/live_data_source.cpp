/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "live_data_source.h"

#include <algorithm>
#include <ctime>
#include <limits>
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

// `bytes` sent or received in `elapsed_ms` as kbit/s (one kbit/s is one bit
// per millisecond), rounded; at most the largest int32_t. `elapsed_ms` is
// above 0.
int32_t rate_kbps(uint64_t bytes, int64_t elapsed_ms) {
    constexpr auto kMaxRate = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    const auto ms = static_cast<uint64_t>(elapsed_ms);
    if (bytes > (std::numeric_limits<uint64_t>::max() - ms / 2) / 8) {
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(std::min((bytes * 8 + ms / 2) / ms, kMaxRate));
}

} // namespace

LiveDataSource::LiveDataSource(std::string data_root)
    : sample_(model::sample_home_data()),
      test_folder_(!data_root.empty()) {
    // Reads the time zone once, so localtime_r() below shows local time.
    tzset();
    root_ = test_folder_ ? std::filesystem::path(std::move(data_root)) : std::filesystem::path("/");
    // Relative on purpose: an absolute path on the right would drop the root.
    power_supply_dir_ = root_ / "sys/class/power_supply";
    if (test_folder_) {
        addresses_file_ = root_ / "addresses";
        network_manager_file_ = root_ / "networkmanager";
    } else {
        network_manager_ = std::make_unique<NetworkManagerClient>();
    }
}

DataSourceKind LiveDataSource::kind() const {
    return DataSourceKind::Live;
}

void LiveDataSource::set_traffic_shown(bool shown) {
    traffic_shown_ = shown;
}

void LiveDataSource::wait_until_ready(milliseconds limit) {
    if (network_manager_) {
        network_manager_->wait_started(limit);
    }
}

void LiveDataSource::read(model::HomeData& data) {
    // The Client mode, no access point, no clients, every network value not
    // known.
    data = model::HomeData{};
    data.mode = model::NetMode::Client;
    data.ap_active = false;

    // If the system cannot tell the time, the clock keeps the sample's value
    // and the page carries the sample mark, so nobody takes it for the time.
    int32_t minutes = 0;
    const bool clock_known = read_local_clock(minutes);
    data.clock_minutes = clock_known ? minutes : sample_.clock_minutes;
    data.sample = !clock_known;

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

    NetworkManagerReading service;
    if (network_manager_) {
        network_manager_->read(service);
    } else {
        service = read_network_manager_file(network_manager_file_);
    }
    apply_network_manager(service, data);

    traffic_shown_read_ = traffic_shown_;
    if (network_.kind == model::LinkKind::None || !traffic_shown_) {
        forget_traffic();
    } else if (test_folder_ || !traffic_tried_ || traffic_interface_ != network_.interface_name ||
               now - traffic_tried_at_ >= kTrafficInterval) {
        read_traffic();
    }
    data.down_kbps = down_kbps_;
    data.up_kbps = up_kbps_;
    data.down_history = down_history_.shares();
    data.up_history = up_history_.shares();
}

void LiveDataSource::forget_traffic() {
    traffic_tried_ = false;
    traffic_interface_.clear();
    has_counters_ = false;
    down_kbps_ = model::kRateUnknown;
    up_kbps_ = model::kRateUnknown;
    down_history_.clear();
    up_history_.clear();
}

void LiveDataSource::read_traffic() {
    // Taken right at the counters, after the other files, so the time
    // between two readings is the time between the two counter reads.
    const auto now = std::chrono::steady_clock::now();
    const bool same_uplink = traffic_tried_ && traffic_interface_ == network_.interface_name;
    const int64_t elapsed_ms =
        test_folder_ ? kTrafficInterval.count()
                     : std::chrono::duration_cast<milliseconds>(now - traffic_tried_at_).count();

    TrafficCounters reading;
    const std::size_t line = test_folder_ ? counter_line_++ : 0;
    const bool read_ok = read_traffic_counters(root_, network_.interface_name, line, reading);

    // A rate needs two good readings of the same uplink, in order, with time
    // between them. Anything else starts the traffic afresh, so a counter
    // that went backwards (the interface came up again) never shows a huge
    // number.
    if (read_ok && has_counters_ && same_uplink && elapsed_ms > 0 &&
        reading.received >= counters_.received && reading.sent >= counters_.sent) {
        down_kbps_ = rate_kbps(reading.received - counters_.received, elapsed_ms);
        up_kbps_ = rate_kbps(reading.sent - counters_.sent, elapsed_ms);
        down_history_.push(down_kbps_);
        up_history_.push(up_kbps_);
    } else {
        down_kbps_ = model::kRateUnknown;
        up_kbps_ = model::kRateUnknown;
        down_history_.clear();
        up_history_.clear();
    }

    has_counters_ = read_ok;
    if (read_ok) {
        counters_ = reading;
    }
    traffic_tried_ = true;
    traffic_tried_at_ = now;
    traffic_interface_ = network_.interface_name;
}

// Worked out from the current time at every call: the reader then wakes at
// most about nineteen times a minute (once for the minute, every 5 s for the
// network and every 10 s for the battery; those two often fall together),
// and once a second more while the home page shows the traffic; every
// 100 ms only while the connection to the network service starts (well under
// a second on the device). A wake-up
// that comes early, or a jump of the wall clock, corrects itself at the
// next call. The minute follows the wall clock, while the battery, the
// network, the traffic and the waiting follow the steady clock, which never
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

    if (!battery_read_ || !network_read_ || traffic_shown_ != traffic_shown_read_) {
        delay = milliseconds(0);
    } else {
        const auto now = std::chrono::steady_clock::now();
        const auto battery_due = battery_read_at_ + kBatteryInterval;
        const auto network_due = network_read_at_ + kNetworkInterval;
        delay = std::min(delay, ceil<milliseconds>(battery_due - now));
        delay = std::min(delay, ceil<milliseconds>(network_due - now));
        if (traffic_shown_ && network_.kind != model::LinkKind::None) {
            const auto traffic_due = traffic_tried_ ? traffic_tried_at_ + kTrafficInterval : now;
            delay = std::min(delay, ceil<milliseconds>(traffic_due - now));
        }
        if (network_manager_ && network_manager_->starting()) {
            delay = std::min(delay, kServiceStartInterval);
        }
    }
    return std::max(delay, kMinimumDelay);
}

} // namespace platform
