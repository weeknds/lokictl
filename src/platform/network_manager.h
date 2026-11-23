/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "mode_switch.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace platform {

// What the system's network service (NetworkManager) says, as far as the
// home page needs it. It adds names and state on top of what the kernel
// says (see read_network()); it never picks the uplink itself. It also
// says whether LokiCTL's hotspot runs, which is what makes the mode Router
// or Share (the mode comes from the service, not from the app's memory).
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

    // LokiCTL's own hotspot (the profile kHotspotProfile), while it is
    // active. Router is on exactly then, or Share when it runs on
    // kShareInterface.
    struct Hotspot {
        bool active{false};
        std::string interface_name; // for example "wlan0"
        std::string name;           // the network name (SSID) it sends
        std::string security;       // for example "WPA2"
        int32_t channel{0};         // the Wi-Fi channel it sends on; 0 when not known
        int32_t uptime_seconds{0};  // since the profile was made; 0 when not known
    };
    Hotspot hotspot;

    // The connection's own switch between modes (see
    // NetworkManagerClient::start_router()); idle in a test folder.
    model::ModeSwitch mode_switch;

    // Test folder only: how a pretend switch ends (a "fail" line).
    PretendFailure switch_failure;
};

// The name of LokiCTL's hotspot profile. It is only ever kept in memory.
constexpr const char* kHotspotProfile = "LokiCTL hotspot";

// Adds `reading` to `data`, whose uplink the kernel's values have already
// set. Does nothing when the service is not available, so every value keeps
// its "not known" value then.
// - Wi-Fi uplink: the name, security and speed of the Wi-Fi link whose
//   interface is the uplink's (a speed that is not known keeps the old one).
// - With a link: HomeData::internet is the service's internet check.
// - Without a link: HomeData::offline_state is NetworkOff when networking is
//   switched off, else Connecting while a device is being connected, else
//   WifiOff when the Wi-Fi radio is off, else Unknown (plainly no network).
// - While the hotspot is active: the mode is Router, with the hotspot's
//   name, security, channel and uptime (see show_router()); Share instead
//   when it runs on kShareInterface (see show_share()).
void apply_network_manager(const NetworkManagerReading& reading, model::HomeData& data);

// Adds `reading` to the Status page's ports (see read_ports()): each
// connected Wi-Fi port gets the name, security and speed of the Wi-Fi link
// with its interface (a speed that is not known stays unknown), and the
// port that runs the hotspot becomes an access point with the hotspot's
// name and security. Does nothing when the service is not available.
void apply_network_manager(const NetworkManagerReading& reading, std::vector<model::StatusPort>& ports);

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
//   ap <interface> <security|-> <network name ...>
//   fail permission | fail <reason ...>
// A "link" line adds a Wi-Fi link; "-" means not known, and the name is the
// rest of the line (it may hold spaces; it may be missing). An "ap" line
// says LokiCTL's hotspot is active on that interface (Router; Share on
// kShareInterface). A "fail"
// line makes a pretend switch fail: as not allowed, or with that reason. A
// line that cannot be read is skipped. A missing file gives a reading that
// is not available, like a system without the service. Never throws.
NetworkManagerReading read_network_manager_file(const std::filesystem::path& path);

// What start_router() and start_share() need.
struct RouterRequest {
    std::string interface_name; // kHotspotInterface (Router) or kShareInterface (Share)
    std::string name;           // the network name (SSID) to send
    std::string password;       // WPA2 password; never logged
    int32_t channel{0};         // the 2.4 GHz channel to send on; 0 lets the service pick
};

// A connection to the system's network service. Device builds use libnm
// (APP_USE_LIBNM); other builds (the Mac simulator) get a stand-in that is
// never available and changes nothing (can_change_network() is false).
//
// It reads, and only the action calls below change anything:
// start_router(), start_share(), restart_router(), start_client() and
// leave(). They add, update, activate and delete only the in-memory
// profile kHotspotProfile, and activate only the Wi-Fi profile its
// interface was on before, on that interface (Router only). They never wait:
// each sends its requests and returns, and the answers are handled by
// later read() calls (leave() waits, on the way out only). The only other
// request it sends is request_wifi_scan(), which asks for a Wi-Fi scan and
// changes no setting.
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

    // The channel of the Wi-Fi network `interface_name` is joined to as a
    // client, from the client's copy of the service's state; 0 when it is
    // not joined or the channel is not known. Applies the change messages
    // that have arrived first, without waiting.
    int32_t wifi_channel(const std::string& interface_name);

    // Waits until the client has started, at most `limit`. Returns at once
    // when it is not starting. For one-shot readouts only: it blocks.
    void wait_started(std::chrono::milliseconds limit);

    // True when this build can change the network (libnm); the stand-in
    // returns false, and its action calls do nothing.
    static bool can_change_network();

    // How long a switch may take before it counts as failed and is undone.
    static constexpr std::chrono::milliseconds kSwitchLimit{20000};

    // Actions. Each is ignored while a switch runs.
    //
    // Router: adds kHotspotProfile in memory (autoconnect off): an access
    // point on `request.interface_name`, 2.4 GHz, WPA2-PSK, IPv4 shared,
    // IPv6 off, and activates it there. The UUID of the Wi-Fi profile the
    // interface was on and the time it was made go into the profile's own
    // user data (lokictl.restore, lokictl.started). A leftover profile of
    // LokiCTL's that is not active is deleted first. The switch ends when
    // the hotspot is active; when it fails, or takes longer than
    // kSwitchLimit, it is undone as by start_client().
    void start_router(const RouterRequest& request);
    // Share: like start_router(), on `request.interface_name`
    // (kShareInterface, which the caller has just added) and on
    // `request.channel`, without a Wi-Fi profile to bring back. The
    // profile is added once the service shows that interface as a Wi-Fi
    // device that is ready (read() checks); the switch runs from the call
    // on, with the same limit. Nothing is sent while another hotspot of
    // LokiCTL's is active.
    void start_share(const RouterRequest& request);
    // Router or Share again, with a new name or password: while LokiCTL's hotspot
    // is active, its profile is built afresh from `request` (same UUID, the
    // same lokictl.restore, a new lokictl.started), updated in memory only
    // and activated again on `request.interface_name`, so the interface
    // goes straight from the old hotspot to the new one. A request without
    // a channel keeps the profile's. Devices on the
    // hotspot must join again. The switch ends when the hotspot is active;
    // when it fails, or takes longer than kSwitchLimit, it is undone as by
    // start_client(). Nothing is sent while the hotspot is not active.
    void restart_router(const RouterRequest& request);
    // Client: deletes every profile of LokiCTL's hotspot, then activates
    // the Wi-Fi profile recorded in it, if there is one (otherwise the
    // service's own autoconnect picks). The switch ends when the service
    // has answered both. Nothing is sent when there is no such profile.
    void start_client();
    // True while an action waits for an answer or a switch runs; the
    // reader then reads every 100 ms.
    bool busy() const;
    // On the way out: if a profile of LokiCTL's hotspot exists or is being
    // added, waits for the add to be answered, then switches back to Client
    // as start_client() does and waits until the service has answered, at
    // most `limit` in all. Sends nothing in plain Client. It blocks.
    void leave(std::chrono::milliseconds limit);

    // Wi-Fi scan. A scan changes no setting, so it is not one of the
    // actions above: it never counts as busy() and never holds up a
    // switch.
    //
    // How long a scan may take before it counts as having no result.
    static constexpr std::chrono::milliseconds kScanLimit{15000};
    // Asks the service to scan on `interface_name` (a managed Wi-Fi
    // device), without waiting. Ignored while a scan or a switch runs,
    // while LokiCTL's hotspot is active and when there is no such device.
    // The scan ends when the device's last-scan time changes, or after
    // kScanLimit (WifiScanError::TimedOut); a refusal ends it at once
    // (NotAllowed, or Refused with the service's reason).
    void request_wifi_scan(const std::string& interface_name);
    // True from request_wifi_scan() until that scan has ended.
    bool scanning() const;
    // Fills in `scan` from the client's copy of the service's state, after
    // applying the change messages that have arrived (without waiting): the
    // access points `interface_name` hears (copied, sorted and capped with
    // model::sort_wifi_networks()), the time of its last finished scan and
    // how the scan the user asked for stands. No list without the service
    // (NoService), without that managed Wi-Fi device (NoDevice), with the
    // Wi-Fi radio off (WifiOff) or while LokiCTL's hotspot is active
    // (Hotspot).
    void read_wifi_scan(const std::string& interface_name, model::WifiScan& scan);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform
