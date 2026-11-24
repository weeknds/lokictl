/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace platform {

// A switch between modes the user asked for, on its way from the Modes page
// through the data reader to the data source (DataSource::request_mode()).
struct ModeRequest {
    model::NetMode mode{model::NetMode::Client};
    // Router and Share: the hotspot's password, 8 to 63 printable ASCII
    // characters. It is never logged and never shown in a readout.
    std::string password;
    // Router and Share: the hotspot's network name (SSID), 1 to 32 bytes;
    // empty means the default name (see default_hotspot_name()).
    std::string name;
    // Router and Share only: the hotspot already runs in that mode and
    // starts again with `name` and `password` (after a rename or a new
    // password). A restart while that mode is not on does nothing.
    bool restart{false};
    // VPN: the WireGuard file to make the tunnel from, by name without
    // ".conf" (one of model::VpnStatus::files).
    std::string vpn_file;
    // VPN, for a pretend switch only: the server's host. The data source
    // fills it in from the file (never a page); empty shows a made-up one.
    std::string vpn_server;
    // VPN only: turn the tunnel off (the user's VPN Off) instead of on. A
    // hotspot that runs stays on, and the kill switch comes off with the
    // tunnel; without a hotspot it is the same as Client.
    bool vpn_off{false};
};

// How a pretend switch ends. A test folder's stand-in for NetworkManager
// sets it with a "fail" line (see read_network_manager_file()); everywhere
// else a pretend switch works.
struct PretendFailure {
    model::SwitchError error{model::SwitchError::None}; // None: the switch works
    std::string reason;                                 // with SwitchError::Failed
};

// The built-in Wi-Fi: Router runs the hotspot on it, and Share keeps it
// joined to its network. Router only ever touches this one.
constexpr const char* kHotspotInterface = "wlan0";

// Share's own interface: a second, virtual interface of the built-in
// Wi-Fi that runs the hotspot on kHotspotInterface's channel. Only the
// helper adds and removes it (see add_share_interface()).
constexpr const char* kShareInterface = "lokiap0";

// Laptop link's cable port: the laptop plugged in here gets an address
// from LokiCTL's in-memory profile on it (model::kLaptopLinkAddress). Laptop
// link only ever touches this one.
constexpr const char* kLaptopInterface = "eth0";

// The hotspot's name until the user renames it: "LokiCTL-" and the last
// four hex digits of kHotspotInterface's hardware address, in capitals,
// read from "<root>/sys/class/net/<interface>/address" ("LokiCTL-0000" when
// it cannot be read). Never throws.
std::string default_hotspot_name(const std::filesystem::path& root);

// The line that explains Router and Share on the home page, for an uplink
// named `uplink`: "eth0 -> wi-fi ap · nat · dhcp" (with an arrow).
std::string router_mode_detail(const std::string& uplink);

// The line that explains Laptop link on the home page:
// "laptop on eth0 · 10.42.0.1".
std::string laptop_mode_detail();

// Pretends to switch between Client, Router, Share, VPN and Laptop link, so pages can
// be pictured and tried where nothing may or can change the network: the
// made-up sources, test folders, the Mac, snapshots and --print-data. It
// changes nothing outside this object.
//
// It counts readings, not time, so the same snapshot always gives the same
// picture. A request is taken at the next reading (apply()): a mode that is
// already on finishes at once (VPN: the tunnel is on; VPN off: neither the
// tunnel nor the kill switch is on), Router without a cable uplink fails at
// once ("Plug in a cable first"), Share without a Wi-Fi uplink too ("Join a
// Wi-Fi network first"), Router while Share is on and Share while Router
// is on too ("Back to Client first"), Laptop link fails at once as
// model::laptop_link_refusal() says, Router, Share and VPN while Laptop
// link is on fail at once too ("Back to Client first"), a restart while
// its mode is not on is ignored, and every other switch (a restart too)
// shows Switching until the reading after, which ends it as `failure` says
// (or with the new mode on). The source asks to be read again kPretendTime after a switch starts
// (next_delay()), so in a window it takes about a second.
//
// The hotspot and the tunnel are pretended apart, as on the device: VPN
// while Router or Share is on, and Router or Share while VPN is on, make
// the VPN gateway, with the kill switch on. Once a pretend switch has
// finished, what it set replaces the source's own: Router and Share show
// the made-up hotspot of the sample (model::sample_home_data()) on top of
// the source's values, under the name the request carried (when it carried
// one); Laptop link shows the mode with its line (see show_laptop()); VPN
// shows the tunnel on (see show_vpn()) with the request's file and server;
// VPN off takes the tunnel and the kill switch away and keeps
// the hotspot; Client shows no access point, no clients, no tunnel and no
// kill switch. The password is never kept.
class PretendModeSwitch {
public:
    static constexpr std::chrono::milliseconds kPretendTime{1000};

    // Takes the request at the next apply(). A request while a switch runs
    // is ignored.
    void request(const ModeRequest& request);

    // Puts the pretend mode and the switch's state into `data`, which the
    // source has just filled, and moves the switch on by one reading.
    void apply(model::HomeData& data, const PretendFailure& failure);

    // How long the source may wait before the next reading because of the
    // switch: no wait while a request waits, kPretendTime while a switch
    // runs, DataSource::kNever otherwise.
    std::chrono::milliseconds next_delay() const;

private:
    // Puts what finished switches set on top of `data`.
    void show_mode(model::HomeData& data) const;
    // Decides how the waiting request starts, from `data` as shown now.
    void start_request(const model::HomeData& data);

    bool requested_{false};
    model::NetMode requested_mode_{model::NetMode::Client};
    std::string requested_name_;
    bool requested_restart_{false};
    bool requested_vpn_off_{false};
    model::VpnStatus requested_vpn_; // VPN: the file and server the request carried
    bool switching_{false};
    bool switching_vpn_off_{false};
    bool switching_kill_{false};     // the running switch ends with the kill switch on
    std::string switching_name_;     // the name the running switch carries
    model::VpnStatus switching_vpn_; // ... the file and server
    // What finished switches set; each replaces the source's own once set.
    bool has_hotspot_{false};
    // Router, Share or Laptop: that mode runs; Client: none of them.
    model::NetMode hotspot_mode_{model::NetMode::Client};
    std::string name_;               // the hotspot's name; empty keeps the shown one
    bool has_vpn_{false};
    model::VpnStatus vpn_;           // state Off: no tunnel
    bool has_kill_{false};
    bool kill_{false};
    model::ModeSwitch status_;
};

// Shows Router in `data`: the mode, the access point `name` with
// `security`, `channel` (0 when not known), `uptime_seconds` and the line
// that explains it. The uplink and the clients are left as they are.
void show_router(model::HomeData& data, const std::string& name, const std::string& security,
                 int32_t channel, int32_t uptime_seconds);

// Shows Share in `data`: the same values as show_router(), in the mode
// Share.
void show_share(model::HomeData& data, const std::string& name, const std::string& security,
                int32_t channel, int32_t uptime_seconds);

// Shows Laptop link in `data`: the mode and the line that explains it.
// The uplink is left as it is (the Wi-Fi), and there is no access point.
void show_laptop(model::HomeData& data);

// Shows Client in `data`: no access point (no name, security or channel),
// no clients, no uptime, no tunnel (the VPN's files stay).
void show_client(model::HomeData& data);

// The line that explains VPN on the home page, for a tunnel to `server`:
// "wireguard -> <server>" (with an arrow).
std::string vpn_mode_detail(const std::string& server);

// Shows VPN in `data`: the tunnel's state, file, server and reply time
// from `tunnel` (its file list and kill switch are not used), and the mode
// VPN unless a hotspot runs. The access point and the clients are left as
// they are.
void show_vpn(model::HomeData& data, const model::VpnStatus& tunnel);

// The line that explains the VPN gateway (a hotspot mode with the tunnel
// up) on the home page: "ap -> wireguard -> <server>" (with arrows).
std::string gateway_mode_detail(const std::string& server);

// While a hotspot runs: the gateway's line on the home page (see
// gateway_mode_detail()) when the tunnel is up, or "vpn down · devices
// blocked" when the kill switch is on without it. Anything else is left as
// it is.
void show_gateway(model::HomeData& data);

} // namespace platform
