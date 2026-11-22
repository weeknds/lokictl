/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "live_data_source.h"

#include "device_info.h"

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

} // namespace

LiveDataSource::LiveDataSource(std::string data_root, bool may_change_network)
    : sample_(model::sample_home_data()),
      test_folder_(!data_root.empty()),
      root_(test_folder_ ? std::filesystem::path(data_root) : std::filesystem::path("/")),
      addresses_file_(test_folder_ ? root_ / "addresses" : std::filesystem::path()),
      changes_network_(may_change_network && data_root.empty() && NetworkManagerClient::can_change_network()),
      clients_(root_, addresses_file_, test_folder_) {
    // Reads the time zone once, so localtime_r() below shows local time.
    tzset();
    // Relative on purpose: an absolute path on the right would drop the root.
    power_supply_dir_ = root_ / "sys/class/power_supply";
    if (test_folder_) {
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

void LiveDataSource::set_details_shown(bool shown) {
    details_shown_ = shown;
}

void LiveDataSource::wait_until_ready(milliseconds limit) {
    if (network_manager_) {
        network_manager_->wait_started(limit);
    }
}

bool LiveDataSource::changes_network() const {
    return changes_network_;
}

void LiveDataSource::request_mode(const ModeRequest& request) {
    if (!changes_network_) {
        pretend_.request(request);
        return;
    }
    refused_ = false;
    const std::string name = request.name.empty() ? default_hotspot_name(root_) : request.name;
    if (request.mode == model::NetMode::Router && request.restart) {
        network_manager_->restart_router(RouterRequest{kHotspotInterface, name, request.password});
    } else if (request.mode == model::NetMode::Router) {
        // Decided on a fresh look at the kernel's uplink: Router shares a
        // cable, never the Wi-Fi it is about to take over.
        if (read_network(root_, addresses_file_).kind != model::LinkKind::Cable) {
            refused_ = true;
            refusal_ = model::ModeSwitch{};
            refusal_.target = model::NetMode::Router;
            refusal_.state = model::SwitchState::Failed;
            refusal_.error = model::SwitchError::NoCable;
            return;
        }
        network_manager_->start_router(RouterRequest{kHotspotInterface, name, request.password});
    } else if (request.mode == model::NetMode::Client) {
        network_manager_->start_client();
    }
}

void LiveDataSource::leave_modes(milliseconds limit) {
    if (changes_network_) {
        network_manager_->leave(limit);
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

    // When the Status page has just opened, the battery and the network are
    // read again at once, with their details, so the page fills right away.
    const bool details_started = details_shown_ && !details_shown_read_;
    details_shown_read_ = details_shown_;
    if (!details_shown_) {
        details_ = model::StatusDetails{};
    }

    const auto now = std::chrono::steady_clock::now();
    if (!battery_read_ || details_started || now - battery_read_at_ >= kBatteryInterval) {
        battery_ = read_battery(power_supply_dir_);
        if (details_shown_) {
            read_battery_details(power_supply_dir_, details_);
            read_device_details(root_, details_);
        }
        battery_read_at_ = now;
        battery_read_ = true;
    }
    data.battery_percent = battery_.percent;
    data.battery_state = battery_.state;

    const bool network_due = !network_read_ || details_started || now - network_read_at_ >= kNetworkInterval;
    if (network_due) {
        network_ = read_network(root_, addresses_file_);
        if (details_shown_) {
            details_.ports = read_ports(root_, addresses_file_, network_.interface_name);
            details_.dns = read_dns_servers(root_);
        }
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

    // The devices on the hotspot. Read before a pretend switch is applied,
    // so a pretend Client drops them with the access point.
    const std::string& hotspot_interface = service.hotspot.interface_name;
    if (service.available && service.hotspot.active && !hotspot_interface.empty()) {
        if (test_folder_ || network_due || !clients_.has_read() || clients_.interface_name() != hotspot_interface) {
            const bool switching = network_manager_ && network_manager_->busy();
            clients_.read(hotspot_interface, !switching);
        }
        data.clients = clients_.clients();
    } else {
        clients_.clear();
    }

    if (changes_network_) {
        data.mode_switch = refused_ ? refusal_ : service.mode_switch;
    } else {
        pretend_.apply(data, service.switch_failure);
    }
    if (details_shown_) {
        data.details = details_;
        apply_network_manager(service, data.details.ports);
    }

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

    if (!battery_read_ || !network_read_ || traffic_shown_ != traffic_shown_read_ ||
        details_shown_ != details_shown_read_) {
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
        if (network_manager_ && (network_manager_->starting() || network_manager_->busy())) {
            delay = std::min(delay, kServiceStartInterval);
        }
        delay = std::min(delay, pretend_.next_delay());
    }
    return std::max(delay, kMinimumDelay);
}

} // namespace platform
