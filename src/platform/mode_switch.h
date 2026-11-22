/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <chrono>
#include <string>

namespace platform {

// A switch between modes the user asked for, on its way from the Modes page
// through the data reader to the data source (DataSource::request_mode()).
struct ModeRequest {
    model::NetMode mode{model::NetMode::Client};
    // Router: the hotspot's password, 8 to 63 printable ASCII characters.
    // It is never logged and never shown in a readout.
    std::string password;
};

// How a pretend switch ends. A test folder's stand-in for NetworkManager
// sets it with a "fail" line (see read_network_manager_file()); everywhere
// else a pretend switch works.
struct PretendFailure {
    model::SwitchError error{model::SwitchError::None}; // None: the switch works
    std::string reason;                                 // with SwitchError::Failed
};

// The interface that runs the hotspot. Router only ever touches this one.
constexpr const char* kHotspotInterface = "wlan0";

// The line that explains Router on the home page, for an uplink named
// `uplink`: "eth0 -> wi-fi ap · nat · dhcp" (with an arrow).
std::string router_mode_detail(const std::string& uplink);

// Pretends to switch between Client and Router, so pages can be pictured
// and tried where nothing may or can change the network: the made-up
// sources, test folders, the Mac, snapshots and --print-data. It changes
// nothing outside this object.
//
// It counts readings, not time, so the same snapshot always gives the same
// picture. A request is taken at the next reading (apply()): a mode that is
// already on finishes at once, Router without a cable uplink fails at once
// ("Plug in a cable first"), and every other switch shows Switching until
// the reading after, which ends it as `failure` says (or with the new mode
// on). The source asks to be read again kPretendTime after a switch starts
// (next_delay()), so in a window it takes about a second.
//
// Once a pretend switch has finished, the pretend mode replaces the
// source's own: Router shows the made-up hotspot of the sample
// (model::sample_home_data()) on top of the source's values, Client shows
// no access point and no clients. Share and VPN are never switched to: a
// request for them is ignored.
class PretendModeSwitch {
public:
    static constexpr std::chrono::milliseconds kPretendTime{1000};

    // Takes the request at the next apply(). A request while a switch runs
    // is ignored.
    void request(model::NetMode mode);

    // Puts the pretend mode and the switch's state into `data`, which the
    // source has just filled, and moves the switch on by one reading.
    void apply(model::HomeData& data, const PretendFailure& failure);

    // How long the source may wait before the next reading because of the
    // switch: no wait while a request waits, kPretendTime while a switch
    // runs, DataSource::kNever otherwise.
    std::chrono::milliseconds next_delay() const;

private:
    bool requested_{false};
    model::NetMode requested_mode_{model::NetMode::Client};
    bool switching_{false};
    bool has_mode_{false};           // a pretend switch finished: mode_ replaces the source's mode
    model::NetMode mode_{model::NetMode::Client};
    model::ModeSwitch status_;
};

// Shows Router in `data`: the mode, the access point `name` with
// `security`, `uptime_seconds` and the line that explains it. The uplink
// and the clients are left as they are.
void show_router(model::HomeData& data, const std::string& name, const std::string& security,
                 int32_t uptime_seconds);

// Shows Client in `data`: no access point, no clients, no uptime.
void show_client(model::HomeData& data);

} // namespace platform
