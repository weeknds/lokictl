/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "battery.h"
#include "data_source.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace platform {

// Real readings. So far these are the local time and the battery; every
// other value is still the sample's, and HomeData::sample stays true.
class LiveDataSource final : public DataSource {
public:
    // `data_root` stands in for the root of the file system (see
    // make_data_source()); empty means the real root.
    explicit LiveDataSource(std::string data_root);

    DataSourceKind kind() const override;

    // Reads the clock every time. The battery files are read on the first
    // call and then again only once kBatteryInterval has passed since the
    // last battery reading; in between the last reading is used again.
    void read(model::HomeData& data) override;

    // The earlier of "just after the next minute starts" and "the battery
    // is due", never less than kMinimumDelay.
    std::chrono::milliseconds next_read_delay() override;

    static constexpr std::chrono::milliseconds kBatteryInterval{10000};
    // How long after the minute starts the clock is read, so the new
    // minute is surely there.
    static constexpr std::chrono::milliseconds kMinuteMargin{20};
    static constexpr std::chrono::milliseconds kMinimumDelay{20};

private:
    std::filesystem::path power_supply_dir_;
    BatteryReading battery_;
    bool battery_read_{false};
    std::chrono::steady_clock::time_point battery_read_at_{};
};

} // namespace platform
