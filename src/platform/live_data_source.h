/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "battery.h"
#include "data_source.h"
#include "mode_switch.h"
#include "network.h"
#include "network_manager.h"
#include "rate_history.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace platform {

// Real readings: the local time, the battery, the uplink (see
// read_network()) and its traffic (see read_traffic_counters()), with the
// names and state the system's network service adds (see
// apply_network_manager()). The mode is Client unless LokiCTL's hotspot is
// active (Router, still without clients). Nothing is made up, so
// HomeData::sample is false (unless the system cannot tell the time; see
// read()).
//
// Switching modes: on the real root of a device build, and only when the
// source may change the network (a normal start), request_mode() and
// leave_modes() go to NetworkManagerClient's actions; Router is refused at
// once ("Plug in a cable first") unless the kernel's uplink is a cable, and
// a restart (a new name or password while Router runs) goes to
// restart_router(). The hotspot's name is the request's, or else
// default_hotspot_name().
// Everywhere else (test folders, the Mac, snapshots, --print-data) the
// switch only pretends (PretendModeSwitch), and a test folder's
// "networkmanager" file can make it fail.
class LiveDataSource final : public DataSource {
public:
    // `data_root` stands in for the root of the file system (see
    // make_data_source()); empty means the real root. With a test folder
    // the traffic is timed by the readings, not by the clock (see read()),
    // and "<data_root>/networkmanager" stands in for the network service
    // (see read_network_manager_file()). On the real root the connection to
    // the service starts here, without waiting.
    // `may_change_network`: see make_data_source().
    LiveDataSource(std::string data_root, bool may_change_network);

    DataSourceKind kind() const override;

    // Reads the clock every time. The battery files are read on the first
    // call and then again only once kBatteryInterval has passed since the
    // last battery reading, and the network files the same way with
    // kNetworkInterval; in between the last reading is used again.
    //
    // The traffic: while it is shown (set_traffic_shown()) and there is an
    // uplink, the uplink's byte counters are read on the first call and then
    // again once kTrafficInterval has passed since the last try. The rates
    // are the bytes since the reading before, times 8, over the time between
    // the two readings, in kbit/s; each new pair of rates goes into the two
    // histories. The rates are not known, and both histories empty, while
    // the traffic is not shown, without an uplink, after the first reading,
    // after a counter that cannot be read or went backwards, and after a
    // change of uplink: each of these starts the traffic afresh. In a test
    // folder the counters are read at every call, each call counts as
    // exactly kTrafficInterval after the one before, and call n reads line
    // n of the counter files (see read_traffic_counters()), so a run with
    // the same files always gives the same rates.
    //
    // The connection to the network service is read at every call: it
    // applies the changes the service has announced since and asks the
    // service nothing (see NetworkManagerClient). In a test folder the
    // stand-in file is read at every call instead.
    //
    // The Status page's details: only while they are shown
    // (set_details_shown()). The ports and the DNS servers are read with
    // the network, the battery details and the device values with the
    // battery; when the page has just opened, both are read at once. While
    // the details are not shown, HomeData::details is left unknown (empty).
    void read(model::HomeData& data) override;

    // The earliest of "just after the next minute starts", "the battery is
    // due", "the network is due" and, while the traffic is shown and there
    // is an uplink, "the traffic is due"; never less than kMinimumDelay. Right
    // away (kMinimumDelay) when the traffic or the details flag changed since
    // the last read(), so the page gets the new state at once. At most
    // kServiceStartInterval while the connection to the network service
    // starts or a switch between modes is busy, so its values follow soon;
    // at most PretendModeSwitch::next_delay() while a pretend switch runs.
    std::chrono::milliseconds next_read_delay() override;

    void set_traffic_shown(bool shown) override;
    void set_details_shown(bool shown) override;

    // Waits until the connection to the network service has started, at
    // most `limit`.
    void wait_until_ready(std::chrono::milliseconds limit) override;

    void request_mode(const ModeRequest& request) override;
    bool changes_network() const override;
    void leave_modes(std::chrono::milliseconds limit) override;

    static constexpr std::chrono::milliseconds kBatteryInterval{10000};
    static constexpr std::chrono::milliseconds kNetworkInterval{5000};
    static constexpr std::chrono::milliseconds kTrafficInterval{1000};
    // How long after the minute starts the clock is read, so the new
    // minute is surely there.
    static constexpr std::chrono::milliseconds kMinuteMargin{20};
    static constexpr std::chrono::milliseconds kMinimumDelay{20};
    static constexpr std::chrono::milliseconds kServiceStartInterval{100};

private:
    // Forgets the traffic: no counters, rates not known, empty histories.
    void forget_traffic();
    // Reads the uplink's counters and works out the rates (see read()).
    void read_traffic();

    const model::HomeData sample_; // the clock's fall-back value
    const bool test_folder_;
    std::filesystem::path root_;
    std::filesystem::path power_supply_dir_;
    // Empty on the real system; in a test folder, the file that stands in
    // for the system's list of addresses (see read_network()).
    std::filesystem::path addresses_file_;
    // The network service: the connection on the real system, its stand-in
    // file in a test folder (the other one is empty).
    std::unique_ptr<NetworkManagerClient> network_manager_;
    std::filesystem::path network_manager_file_;
    // True when switches go to the network service (see the class comment).
    const bool changes_network_;
    PretendModeSwitch pretend_;
    // A refusal decided here (no cable), shown until the next request.
    bool refused_{false};
    model::ModeSwitch refusal_;
    BatteryReading battery_;
    bool battery_read_{false};
    std::chrono::steady_clock::time_point battery_read_at_{};
    NetworkReading network_;
    bool network_read_{false};
    std::chrono::steady_clock::time_point network_read_at_{};

    // The Status page's details.
    bool details_shown_{false};      // the flag set_details_shown() gave last
    bool details_shown_read_{false}; // the flag the last read() used
    model::StatusDetails details_;   // the details read last, without the network service's names

    // The traffic.
    bool traffic_shown_{true};       // the flag set_traffic_shown() gave last
    bool traffic_shown_read_{true};  // the flag the last read() used
    bool traffic_tried_{false};      // the counters were tried since the traffic started afresh
    std::chrono::steady_clock::time_point traffic_tried_at_{};
    std::string traffic_interface_;  // the interface they were tried on
    bool has_counters_{false};       // `counters_` holds the last good reading
    TrafficCounters counters_;
    std::size_t counter_line_{0};    // test folder: the line the next reading takes
    int32_t down_kbps_{model::kRateUnknown};
    int32_t up_kbps_{model::kRateUnknown};
    model::RateHistory down_history_;
    model::RateHistory up_history_;
};

} // namespace platform
