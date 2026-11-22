/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace platform {

// What the system's network service (NetworkManager) says, as far as the
// home page needs it. It adds names and state on top of what the kernel
// says (see read_network()); it never picks the uplink itself.
struct NetworkManagerReading {
    bool available{false};       // the service runs and answered; nothing below counts without it
    bool networking_on{true};    // networking as a whole is switched on
    bool wifi_on{true};          // the Wi-Fi radio is switched on (in software and by its switch)
    bool connecting{false};      // a managed Wi-Fi or cable device is being connected
    model::Internet internet{model::Internet::Unknown}; // the service's own internet check

    // A managed Wi-Fi device that is joined to a network as a client.
    struct WifiLink {
        std::string interface_name; // for example "wlan0"
        std::string name;           // network name (SSID); empty when not known
        std::string security;       // for example "WPA2"; empty when not known
        int32_t speed_mbps{model::kSpeedUnknown}; // the device's current bit rate
    };
    std::vector<WifiLink> wifi_links;
};

// Adds `reading` to `data`, whose uplink the kernel's values have already
// set. Does nothing when the service is not available, so every value keeps
// its "not known" value then.
// - Wi-Fi uplink: the name, security and speed of the Wi-Fi link whose
//   interface is the uplink's (a speed that is not known keeps the old one).
// - With a link: HomeData::internet is the service's internet check.
// - Without a link: HomeData::offline_state is NetworkOff when networking is
//   switched off, else Connecting while a device is being connected, else
//   WifiOff when the Wi-Fi radio is off, else Unknown (plainly no network).
void apply_network_manager(const NetworkManagerReading& reading, model::HomeData& data);

// A network name as the page may show it: every control character (below
// 0x20, and 0x7f) becomes '?', and at most 32 bytes are kept, the longest
// name a Wi-Fi network can have; a UTF-8 character that would be cut is
// dropped whole.
std::string clean_network_name(const std::string& name);

// Reads a file that stands in for the service in a test folder. Lines of
// "<key> <value>"; blank lines and lines starting with '#' are skipped:
//   running yes|no          the service runs (without this line: no)
//   networking on|off
//   wifi on|off             the Wi-Fi radio
//   connecting yes|no
//   internet unknown|none|portal|limited|full
//   link <interface> <security|-> <speed_mbps|-> <network name ...>
// A "link" line adds a Wi-Fi link; "-" means not known, and the name is the
// rest of the line (it may hold spaces; it may be missing). A line that
// cannot be read is skipped. A missing file gives a reading that is not
// available, like a system without the service. Never throws.
NetworkManagerReading read_network_manager_file(const std::filesystem::path& path);

// A connection to the system's network service that only reads. Device
// builds use libnm (APP_USE_LIBNM); other builds (the Mac simulator) get a
// stand-in that is never available.
//
// It has no thread of its own. It starts without waiting and keeps its own
// GLib main context; the service's change messages queue up there and are
// applied when read() runs, so nothing is asked again and nothing is
// polled. Only one thread at a time may use it (the data reader guarantees
// that); the thread may change between calls.
class NetworkManagerClient {
public:
    NetworkManagerClient();
    // Releases the connection and waits a short, bounded time until libnm
    // has let go of its context.
    ~NetworkManagerClient();

    NetworkManagerClient(const NetworkManagerClient&) = delete;
    NetworkManagerClient& operator=(const NetworkManagerClient&) = delete;

    // Applies the change messages that have arrived, without waiting, and
    // fills in `reading` from the client's copy of the service's state. Not
    // available while the client starts, when it failed to start and when
    // the service does not run.
    void read(NetworkManagerReading& reading);

    // True while the client is still starting.
    bool starting() const;

    // Waits until the client has started, at most `limit`. Returns at once
    // when it is not starting. For one-shot readouts only: it blocks.
    void wait_started(std::chrono::milliseconds limit);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform
