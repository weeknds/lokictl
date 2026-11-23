/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "battery.h"
#include "data_source.h"
#include "firewall.h"
#include "hotspot_clients.h"
#include "mode_switch.h"
#include "network.h"
#include "network_manager.h"
#include "rate_history.h"
#include "status_summary.h"
#include "wifi_scan.h"
#include "wireguard.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace platform {

// Real readings: the local time, the battery, the uplink (see
// read_network()) and its traffic (see read_traffic_counters()), with the
// names and state the system's network service adds (see
// apply_network_manager()). The mode is Client unless LokiCTL's hotspot is
// active (Router, or Share on kShareInterface, with the devices on it; see
// HotspotClients). Nothing is
// made up, so
// HomeData::sample is false (unless the system cannot tell the time; see
// read()).
//
// Switching modes: on the real root of a device build, and only when the
// source may change the network (a normal start), request_mode() and
// leave_modes() go to NetworkManagerClient's actions; Router is refused at
// once ("Plug in a cable first") unless the kernel's uplink is a cable;
// Share is refused at once ("Join a Wi-Fi network first") unless
// kHotspotInterface is joined to a Wi-Fi network, and otherwise has the
// helper add kShareInterface (on this thread, waiting for it) before
// start_share() runs the hotspot there on that network's channel; Router
// while Share runs, and Share while Router runs, are refused ("Back to
// Client first"). A restart (a new name or password while Router or Share
// runs) goes to restart_router(). The hotspot's name is the request's, or
// else default_hotspot_name(). kShareInterface without an active hotspot
// on it (Share ended or failed, or a killed run left it) is removed through
// the helper once the network service has answered and no switch runs: at
// most kShareRemovalTries times, kNetworkInterval apart, never in the first
// reading.
// Everywhere else (test folders, the Mac, snapshots, --print-data) the
// switch only pretends (PretendModeSwitch), and a test folder's
// "networkmanager" file can make it fail.
//
// VPN: the WireGuard files are listed from wireguard_folder() with the
// network (in a test folder "<data_root>/wireguard", at every call); a
// normal start on the device also creates and tightens the folder there
// (prepare_wireguard_folder()). A VPN request reads the chosen file (a bad
// one is refused at once with its reason) and goes to start_vpn() on the
// device, or to the pretend switch everywhere else, with the file's
// server. While the tunnel is up, kVpnInterface's received bytes are read
// at every reading (every kVpnReplyInterval until the first reply, for at
// most kVpnReplyWait): the time since they last grew is
// VpnStatus::reply_seconds.
//
// The VPN gateway (F4.7): VPN while Router or Share runs, and Router or
// Share while the tunnel runs, first arm the kill switch through the helper
// (DeviceBlocker::arm_kill_switch(), on this thread; a failure refuses the
// switch with "kill switch: <reason>"), then start the second half. The
// kill switch then follows the user's intent: it stays while the tunnel is
// down or the hotspot restarts, and comes off only with the user's VPN Off
// (ModeRequest::vpn_off: stop_vpn() while a hotspot runs, which keeps it,
// otherwise start_client()) or Client. A start that finds a hotspot and
// the kill chain adopts the gateway (see DeviceBlocker).
//
// Blocking devices (set_blocked()) works the same way: on the device, when
// the source changes the network, a real DeviceBlocker makes LokiCTL's
// firewall table match the list (never in the first reading, which the app
// takes on the UI thread, and never while a switch runs); everywhere else
// it only pretends.
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
    // kNetworkInterval; in between the last reading is used again. In a
    // test folder the battery is read at every call, and each call counts
    // as kBatteryInterval after the one before.
    //
    // Every battery reading, on every page, goes into the battery clock
    // (model::BatteryClock), so the Status page's time left
    // (StatusDetails::battery_empty_minutes) is ready about a minute after
    // the battery starts discharging, whatever page was shown.
    //
    // Cable drops (StatusPort::drops): the first time a cable interface is
    // read connected, as the uplink on any page or as a port on the Status
    // page, its link downs (read_link_downs()) are kept; drops are the
    // downs since then. A count below the kept one (the interface came
    // back) is kept instead. In a test folder call n reads line n of the
    // files.
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
    // The USB Wi-Fi adapters (HomeData::usb_wifi, see read_usb_wifi()):
    // with the network, whatever page is shown; in a test folder at every
    // call. Their managed state comes from the network service at every
    // call (apply_usb_wifi_managed()).
    //
    // The Status page's details: only while they are shown
    // (set_details_shown()). The ports and the DNS servers are read with
    // the network, the battery details and the device values with the
    // battery; when the page has just opened, both are read at once. While
    // the details are not shown, HomeData::details is left unknown (empty).
    //
    // The Wi-Fi page's list (HomeData::wifi_scan): only while it is shown
    // (set_wifi_shown()), at every call, from the connection to the network
    // service (NetworkManagerClient::read_wifi_scan(), kHotspotInterface
    // only); in a test folder from a pretend scan (PretendWifiScan). While
    // LokiCTL's hotspot runs (also a pretend one) there is no list
    // (WifiScanError::Hotspot).
    //
    // The devices on the hotspot (HomeData::clients): only while LokiCTL's
    // hotspot is active, read with the network and at once when the hotspot
    // has just come up (in a test folder at every call); in between the
    // last reading is used again. No names are asked while a switch between
    // modes runs. When the hotspot stops, they are forgotten.
    void read(model::HomeData& data) override;

    // The earliest of "just after the next minute starts", "the battery is
    // due", "the network is due" and, while the traffic is shown and there
    // is an uplink, "the traffic is due"; never less than kMinimumDelay. Right
    // away (kMinimumDelay) when the traffic, the details or the Wi-Fi flag
    // changed since the last read(), so the page gets the new state at
    // once. At most kServiceStartInterval while the connection to the
    // network service starts or a switch between modes is busy, so its
    // values follow soon; at most kScanInterval while a Wi-Fi scan runs;
    // at most PretendModeSwitch::next_delay() while a pretend switch runs
    // and PretendWifiScan::next_delay() while a pretend scan runs.
    std::chrono::milliseconds next_read_delay() override;

    void set_traffic_shown(bool shown) override;
    void set_details_shown(bool shown) override;
    void set_wifi_shown(bool shown) override;
    // On the real root: NetworkManagerClient::request_wifi_scan() for
    // kHotspotInterface, unless LokiCTL's hotspot runs. In a test folder:
    // the pretend scan.
    void request_wifi_scan() override;

    // Waits until the connection to the network service has started, at
    // most `limit`.
    void wait_until_ready(std::chrono::milliseconds limit) override;

    void request_mode(const ModeRequest& request) override;
    void set_blocked(const std::vector<std::string>& addresses) override;
    bool changes_network() const override;
    // Removes the firewall table first (DeviceBlocker::leave()), then asks
    // the network service to go back to Client with the time that is left,
    // and last removes kShareInterface when it is there (keeping at least
    // kHelperLeaveTimeLimit for that, and giving it all the time left).
    // With the kill switch on, the table goes right after the network
    // service's answer instead (with kHelperLeaveTimeLimit kept for it), so
    // hotspot devices never go out around the tunnel. Not called when the
    // user keeps the mode (DataReader::set_keep_modes()).
    void leave_modes(std::chrono::milliseconds limit) override;

    static constexpr std::chrono::milliseconds kBatteryInterval{10000};
    static constexpr std::chrono::milliseconds kNetworkInterval{5000};
    static constexpr std::chrono::milliseconds kTrafficInterval{1000};
    // How long after the minute starts the clock is read, so the new
    // minute is surely there.
    static constexpr std::chrono::milliseconds kMinuteMargin{20};
    static constexpr std::chrono::milliseconds kMinimumDelay{20};
    static constexpr std::chrono::milliseconds kServiceStartInterval{100};
    static constexpr std::chrono::milliseconds kScanInterval{250};
    // How often the tunnel's counters are read while it is up and nothing
    // came back yet, and for how long after it came up at most.
    static constexpr std::chrono::milliseconds kVpnReplyInterval{1000};
    static constexpr std::chrono::milliseconds kVpnReplyWait{60000};
    static constexpr int kShareRemovalTries = 3;

private:
    // The drops of the cable `name` since its first reading (see read()),
    // with the link downs as the kernel counts them in `downs`;
    // kDetailUnknown when they cannot be read.
    int32_t cable_drops(const std::string& name, std::size_t line, int32_t& downs);
    std::map<std::string, uint64_t> link_downs_start_; // per cable interface, at its first reading
    std::size_t link_line_{0};                          // test folder: the line the next reading takes

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
    // A switch refused here, before the network service is asked.
    void refuse(model::NetMode target, model::SwitchError error, std::string reason = {});
    // request_mode() for VPN.
    void request_vpn(const ModeRequest& request);
    // Lists the WireGuard files (and, on the device, prepares the folder).
    void read_vpn_files();
    // Puts the time since the tunnel last received data into `data`.
    void read_vpn_reply(model::HomeData& data, std::chrono::steady_clock::time_point now);
    // Arms the kill switch for a switch to `target`; on failure the switch
    // is refused with the reason and false comes back.
    bool arm_kill_switch(model::NetMode target);
    // True while kShareInterface exists.
    bool share_interface_present() const;
    std::string hotspot_interface_;  // where LokiCTL's hotspot ran at the last reading; empty when off
    bool vpn_active_{false};         // LokiCTL's tunnel was being set up or up at the last reading
    std::filesystem::path vpn_folder_; // see wireguard_folder(); empty when not known
    std::vector<std::string> vpn_files_; // read with the network
    bool vpn_counted_{false};        // vpn_received_ holds a reading of the tunnel's received bytes
    uint64_t vpn_received_{0};
    bool vpn_replied_{false};        // data came in through the tunnel since it went up
    bool vpn_waiting_{false};        // the tunnel is up and nothing came back yet (see kVpnReplyWait)
    bool vpn_up_{false};             // the tunnel was up at the last reading
    std::chrono::steady_clock::time_point vpn_up_at_{}; // when it was first seen up
    std::chrono::steady_clock::time_point vpn_reply_at_{}; // when the received bytes last grew
    std::size_t vpn_line_{0};        // test folder: the counter line the next reading takes
    std::chrono::steady_clock::duration vpn_test_clock_{}; // test folder: kTrafficInterval per reading
    int share_removals_{0};          // tries to remove kShareInterface since it was last needed
    std::chrono::steady_clock::time_point share_removal_at_{};
    BatteryReading battery_;
    model::BatteryClock battery_clock_;
    std::chrono::steady_clock::duration battery_test_clock_{}; // test folder: kBatteryInterval per reading
    bool battery_read_{false};
    std::chrono::steady_clock::time_point battery_read_at_{};
    NetworkReading network_;
    std::vector<model::UsbWifiAdapter> usb_wifi_; // read with the network
    bool network_read_{false};
    std::chrono::steady_clock::time_point network_read_at_{};
    HotspotClients clients_;
    DeviceBlocker blocker_;
    bool first_read_done_{false}; // the first reading (on the UI thread) is over

    // The Status page's details.
    bool details_shown_{false};      // the flag set_details_shown() gave last
    bool details_shown_read_{false}; // the flag the last read() used
    model::StatusDetails details_;   // the details read last, without the network service's names

    // The Wi-Fi page's list.
    bool wifi_shown_{false};         // the flag set_wifi_shown() gave last
    bool wifi_shown_read_{false};    // the flag the last read() used
    PretendWifiScan pretend_scan_;   // test folder only

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
