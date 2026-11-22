/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "mode_switch.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

// Where the values of the home page come from. A source fills in a whole
// model::HomeData block; platform::DataReader asks it for values and hands
// them to the screen. Sources know nothing about the screen and never draw.
namespace platform {

enum class DataSourceKind {
    Sample,  // the fixed made-up router values; they never change
    Demo,    // the made-up values, moving on a fixed script (for the simulator and tests)
    Live,    // real readings of the device
    Wifi,    // made-up and fixed: joined to a Wi-Fi network, no access point
    Cable,   // made-up and fixed: on a network cable, no access point
    Offline, // made-up and fixed: no network at all
    Tour,    // walks through the made-up situations, one stop per reading
};

// The name of a source as the --data option takes it: "sample", "demo",
// "live", "wifi", "cable", "offline" or "tour".
const char* data_source_name(DataSourceKind kind);

// Looks a source up by its exact (lowercase) name. Returns false and leaves
// `kind` alone for any other name.
bool find_data_source(std::string_view name, DataSourceKind& kind);

// Every source name, separated by commas, in the order of the help text:
// "sample, demo, live, wifi, cable, offline, tour".
std::string data_source_names();

class DataSource {
public:
    // next_read_delay() of a source whose values never change.
    static constexpr std::chrono::milliseconds kNever = std::chrono::milliseconds::max();

    virtual ~DataSource() = default;

    virtual DataSourceKind kind() const = 0;

    // Fills in every value of `data` as it is now. Only one thread at a
    // time calls a source.
    virtual void read(model::HomeData& data) = 0;

    // How long from now until the values can have changed, so the reader
    // can sleep until then. kNever when they never change.
    virtual std::chrono::milliseconds next_read_delay() = 0;

    // Whether a page that shows the traffic is on screen. The reader hands
    // the flag over before it asks the source anything, on the same thread
    // as read(). A source that reads the traffic only while it is shown
    // (the live one) uses it; every other source keeps this default, which
    // ignores it. Until the first call it counts as shown.
    virtual void set_traffic_shown(bool /*shown*/) {}

    // Whether the Status page, the only page that shows HomeData::details,
    // is on screen. Handed over like set_traffic_shown(). A source that
    // reads the details only while they are shown (the live one) uses it;
    // the made-up sources always fill them and keep this default. Until the
    // first call it counts as not shown.
    virtual void set_details_shown(bool /*shown*/) {}

    // Waits until values that a source collects in the background after it
    // was made (the live source's connection to the network service) are
    // there, at most `limit`. It blocks, so it is only called before the
    // first read(), with a short limit (the app's first reading, a one-shot
    // readout). Every other source keeps this default, which returns at
    // once.
    virtual void wait_until_ready(std::chrono::milliseconds /*limit*/) {}

    // A switch between modes the user asked for (see ModeRequest). Called
    // on the thread that calls read(), right before a reading, which then
    // shows the switch's first state in HomeData::mode_switch. It never
    // waits. Only the live source on the device, in a normal start, changes
    // the network (changes_network()); every other source pretends (see
    // PretendModeSwitch), and this default ignores the request.
    virtual void request_mode(const ModeRequest& /*request*/) {}

    // True when request_mode() and leave_modes() change the real network.
    virtual bool changes_network() const {
        return false;
    }

    // On the way out: switches back to Client if this source's switches
    // left the device in another mode, and waits at most `limit` for the
    // system to accept that. Called once, on the reader's thread, after
    // its last reading. A source that changes nothing keeps this default,
    // which does nothing.
    virtual void leave_modes(std::chrono::milliseconds /*limit*/) {}
};

// Creates a source. `data_root` only matters for the live source: it is a
// folder that stands in for the root of the file system, so that
// "<data_root>/sys/..." is read in place of "/sys/...". Empty means the
// real root. `may_change_network` is true only for a normal start: then the
// live source on the real root of a device build switches modes for real;
// every other source, and every snapshot and readout, only pretends (see
// PretendModeSwitch). The made-up sources get the pretend switch here.
std::unique_ptr<DataSource> make_data_source(DataSourceKind kind,
                                             const std::string& data_root,
                                             bool may_change_network = false);

} // namespace platform
