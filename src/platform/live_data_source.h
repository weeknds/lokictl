/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "battery.h"
#include "data_source.h"
#include "network.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace platform {

// Real readings: the local time, the battery and the uplink (see
// read_network()). The device is shown in the Client mode, without an
// access point and without clients. The rates are still made up: with an
// uplink they are the sample's, without one they are not known. So
// HomeData::sample stays true.
class LiveDataSource final : public DataSource {
public:
    // `data_root` stands in for the root of the file system (see
    // make_data_source()); empty means the real root.
    explicit LiveDataSource(std::string data_root);

    DataSourceKind kind() const override;

    // Reads the clock every time. The battery files are read on the first
    // call and then again only once kBatteryInterval has passed since the
    // last battery reading, and the network files the same way with
    // kNetworkInterval; in between the last reading is used again.
    void read(model::HomeData& data) override;

    // The earliest of "just after the next minute starts", "the battery is
    // due" and "the network is due", never less than kMinimumDelay.
    std::chrono::milliseconds next_read_delay() override;

    static constexpr std::chrono::milliseconds kBatteryInterval{10000};
    static constexpr std::chrono::milliseconds kNetworkInterval{5000};
    // How long after the minute starts the clock is read, so the new
    // minute is surely there.
    static constexpr std::chrono::milliseconds kMinuteMargin{20};
    static constexpr std::chrono::milliseconds kMinimumDelay{20};

private:
    const model::HomeData sample_; // the made-up values still used
    std::filesystem::path root_;
    std::filesystem::path power_supply_dir_;
    // Empty on the real system; in a test folder, the file that stands in
    // for the system's list of addresses (see read_network()).
    std::filesystem::path addresses_file_;
    BatteryReading battery_;
    bool battery_read_{false};
    std::chrono::steady_clock::time_point battery_read_at_{};
    NetworkReading network_;
    bool network_read_{false};
    std::chrono::steady_clock::time_point network_read_at_{};
};

} // namespace platform
