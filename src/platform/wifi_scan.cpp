/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_scan.h"

#include "data_source.h"
#include "home_examples.h"

#include <utility>

namespace platform {
namespace {

constexpr int32_t kMinutesPerDay = 24 * 60;
// How long before the block's clock the made-up list was scanned.
constexpr int32_t kExampleScanAgeMinutes = 7;

} // namespace

void PretendWifiScan::request() {
    if (!scanning_) {
        requested_ = true;
    }
}

void PretendWifiScan::apply(model::HomeData& data, bool shown) {
    model::WifiScan scan;
    const bool radio_off = data.uplink_kind == model::LinkKind::None &&
                           (data.offline_state == model::OfflineState::WifiOff ||
                            data.offline_state == model::OfflineState::NetworkOff);
    if (data.ap_active || radio_off) {
        requested_ = false;
        scanning_ = false;
        scan.error = data.ap_active ? model::WifiScanError::Hotspot : model::WifiScanError::WifiOff;
    } else {
        if (scanning_) {
            scanning_ = false;
            scanned_ = true;
            scanned_at_ = data.clock_minutes;
        }
        if (requested_) {
            requested_ = false;
            scanning_ = true;
        }
        scan.scanning = scanning_;
        scan.last_scan_minutes =
            scanned_ ? scanned_at_
                     : ((data.clock_minutes - kExampleScanAgeMinutes) % kMinutesPerDay + kMinutesPerDay) %
                           kMinutesPerDay;
        scan.networks = model::example_wifi_networks(data, scanned_);
        scan.found = static_cast<int32_t>(scan.networks.size());
    }
    data.wifi_scan = shown ? std::move(scan) : model::WifiScan{};
}

std::chrono::milliseconds PretendWifiScan::next_delay() const {
    if (requested_) {
        return std::chrono::milliseconds(0);
    }
    return scanning_ ? kPretendTime : DataSource::kNever;
}

} // namespace platform
