/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <chrono>

namespace platform {

// Pretends to scan for Wi-Fi networks, so the Wi-Fi page can be pictured
// and tried where there is no network service to ask: the made-up sources,
// test folders, snapshots. It sends nothing and changes nothing outside
// this object.
//
// Like PretendModeSwitch it counts readings, not time, so the same snapshot
// always gives the same picture. Before the first scan the list is the
// short one of model::example_wifi_networks() with a last-scan time seven
// minutes before the block's clock. A request is taken at the next reading
// (apply()), which shows the scan under way; the reading after ends it with
// the whole list and the block's clock as the last-scan time. The source
// asks to be read again kPretendTime after a scan starts (next_delay()), so
// in a window it takes about a second.
//
// While LokiCTL's hotspot runs (HomeData::ap_active) there is no list and a
// request is dropped (WifiScanError::Hotspot); without a link and with the
// Wi-Fi or all networking switched off, there is none either
// (WifiScanError::WifiOff).
class PretendWifiScan {
public:
    static constexpr std::chrono::milliseconds kPretendTime{1000};

    // Takes the request at the next apply(). A request while a scan runs is
    // ignored.
    void request();

    // Moves the scan on by one reading and, when `shown`, puts the list
    // into data.wifi_scan; otherwise leaves it empty. `data` is the block
    // the source has just filled, with the mode already applied.
    void apply(model::HomeData& data, bool shown);

    // How long the source may wait before the next reading because of the
    // scan: no wait while a request waits, kPretendTime while a scan runs,
    // DataSource::kNever otherwise.
    std::chrono::milliseconds next_delay() const;

private:
    bool requested_{false};
    bool scanning_{false};
    bool scanned_{false};
    int32_t scanned_at_{model::kClockUnknown};
};

} // namespace platform
