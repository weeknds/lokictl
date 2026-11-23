/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

// How many values each traffic history holds, oldest first.
constexpr std::size_t kHomeHistoryLength = 24;

// What the device does with its network connections.
enum class NetMode {
    Client = 0, // joins a Wi-Fi network like a phone
    Router,     // shares its wired uplink through its own access point
    Share,      // shares the Wi-Fi network it joined through its own access point
    Vpn,        // sends its traffic through a VPN tunnel
};
constexpr int kNetModeCount = 4;

// The name of a mode as shown on screen: "Client", "Router", "Share" or
// "VPN". Pages write it in capitals or lowercase as their theme asks.
const char* net_mode_name(NetMode mode);

// True for the modes that run LokiCTL's hotspot: Router and Share.
bool is_hotspot_mode(NetMode mode);

// What the switch between modes is doing (see HomeData::mode_switch).
enum class SwitchState {
    Idle = 0,  // nothing asked for, or the last switch finished
    Switching, // a switch is under way
    Failed,    // the last switch failed; ModeSwitch::error says why
};

// The name of a switch state in lowercase: "idle", "switching" or
// "failed". Used by the log and the --print-data readout.
const char* switch_state_name(SwitchState state);

// Why the last switch failed.
enum class SwitchError {
    None = 0,
    NoCable,     // Router needs a cable that leads to the internet
    NoWifi,      // Share needs the built-in Wi-Fi joined to a network
    ClientFirst, // Router and Share go through Client: the other one runs
    NotAllowed,  // the system refused: the permission rule is missing
    Failed,      // anything else; ModeSwitch::reason says what
};

// The last switch between modes the user asked for, and where it stands.
// The mode the device is in is HomeData::mode; this only says what a
// switch is doing.
struct ModeSwitch {
    SwitchState state{SwitchState::Idle};
    NetMode target{NetMode::Client};    // the mode asked for last
    SwitchError error{SwitchError::None};
    std::string reason;                 // with SwitchError::Failed: a short reason
};

bool operator==(const ModeSwitch& left, const ModeSwitch& right);
bool operator!=(const ModeSwitch& left, const ModeSwitch& right);

// What the battery is doing, as the system reports it.
enum class BatteryState {
    Unknown = 0, // not reported, or a report the app does not know
    Discharging, // the device runs on the battery
    Charging,    // on the charger, and the battery is charging
    Idle,        // on the charger, not charging (full, or held by the charger)
};

// The name of a battery state in lowercase: "unknown", "discharging",
// "charging" or "idle". Used by the log and the --print-data readout.
const char* battery_state_name(BatteryState state);

// HomeData::battery_percent when the charge is not known.
constexpr int32_t kBatteryPercentUnknown = -1;

// The charges at which the battery counts as low and as very low.
constexpr int32_t kBatteryLowPercent = 15;
constexpr int32_t kBatteryVeryLowPercent = 5;

// True while the device runs on the battery (Discharging) and the charge is
// known and at most kBatteryLowPercent. Only the charge in per cent counts.
// The pages show the battery text in the warning colour then.
bool battery_low(int32_t percent, BatteryState state);
// The same, at most kBatteryVeryLowPercent.
bool battery_very_low(int32_t percent, BatteryState state);
// True on the charger: Charging or Idle.
bool on_charger(BatteryState state);

// What kind of link the uplink is.
enum class LinkKind {
    None = 0, // no link at all: no cable, no Wi-Fi network joined
    Wifi,     // joined to a Wi-Fi network
    Cable,    // a network cable (Ethernet)
};

// The name of a kind of link in lowercase: "none", "wifi" or "cable". Used
// by the log and the --print-data readout.
const char* link_kind_name(LinkKind kind);

// Whether the internet can be reached through the uplink, as the system's
// network service (NetworkManager) checks it by itself.
enum class Internet {
    Unknown = 0, // not known: no network service, no link, or not checked yet
    None,        // no network at all
    Portal,      // a login page stands in the way (a hotspot that wants a sign-in)
    Limited,     // the local network answers, the internet does not
    Full,        // the internet answers
};

// The name of an internet state in lowercase: "unknown", "none", "portal",
// "limited" or "full". Used by the --print-data readout.
const char* internet_name(Internet internet);

// Why there is no link, as the system's network service tells it. It only
// means something while the uplink is LinkKind::None.
enum class OfflineState {
    Unknown = 0, // not known, or nothing special: plainly no network
    Connecting,  // a Wi-Fi or cable connection is being set up
    WifiOff,     // the Wi-Fi radio is switched off
    NetworkOff,  // networking as a whole is switched off
};

// The name of an offline state in lowercase: "unknown", "connecting",
// "wifi-off" or "network-off". Used by the --print-data readout.
const char* offline_state_name(OfflineState state);

// HomeData::wifi_signal_dbm when the signal is not known. A received signal
// is always below 0 dBm, so 0 can never be a real reading.
constexpr int32_t kSignalUnknown = 0;

// HomeData::uplink_speed_mbps when the link speed is not known.
constexpr int32_t kSpeedUnknown = 0;

// HomeData::down_kbps and up_kbps when the rate is not known. Any negative
// rate counts as not known.
constexpr int32_t kRateUnknown = -1;

// HomeClient::connected_seconds when the time is not known.
constexpr int32_t kConnectedUnknown = -1;

// A device counts as new while it has been on the access point for less
// than this (HomeClient::is_new).
constexpr int32_t kClientNewSeconds = 5 * 60;

// One device connected to the access point. A text that is not known is
// empty; a number has the "not known" value noted beside it.
struct HomeClient {
    std::string name;             // host name, for example "printer"; empty when not known
    std::string address;          // IPv4 address as text; empty when not known
    std::string hardware_address; // "02:00:00:00:00:01"; empty when not known
    int32_t signal_dbm{kSignalUnknown}; // received signal strength, dBm (negative)
    int32_t rate_kbps{kRateUnknown};    // current traffic, both ways together, kbit/s
    int32_t connected_seconds{kConnectedUnknown}; // time on the access point, seconds
    bool is_new{false};           // joined less than kClientNewSeconds ago
    bool blocked{false};          // LokiCTL's firewall blocks it (see platform::DeviceBlocker)
};

// Every field is compared. A new field must be added here too: the data
// layer hands a block over only when it differs from the last one.
bool operator==(const HomeClient& left, const HomeClient& right);
bool operator!=(const HomeClient& left, const HomeClient& right);

// A number of the Status page that is not known. Every number of
// StatusPort and StatusDetails below uses it, because 0 and negative
// numbers are real readings there (a current of 0 mA, a current of -480 mA).
constexpr int32_t kDetailUnknown = INT32_MIN;

// What a network port is doing, as the Status page shows it.
enum class PortState {
    Unknown = 0,  // not known
    Off,          // the interface is switched off (not up)
    NoLink,       // up, but no cable plugged in, or no Wi-Fi network joined
    Connected,    // a cable plugged in, or a Wi-Fi network joined
    AccessPoint,  // the port runs the device's own access point
};

// The name of a port state in lowercase: "unknown", "off", "no-link",
// "connected" or "access-point". Used by the --print-data readout.
const char* port_state_name(PortState state);

// One network port (a Wi-Fi or a cable interface) on the Status page. A
// text that is not known is empty; a number is kDetailUnknown.
struct StatusPort {
    LinkKind kind{LinkKind::None};   // Wifi or Cable
    std::string name;                // interface, for example "wlan0"
    PortState state{PortState::Unknown};
    bool uplink{false};              // this port carries the traffic (see HomeData::uplink_name)
    std::string network;             // Wi-Fi: the network joined, or the access point's name
    std::string security;            // Wi-Fi: that network's security, for example "WPA2"
    int32_t signal_dbm{kDetailUnknown}; // Wi-Fi: received signal, dBm
    int32_t speed_mbps{kDetailUnknown}; // link speed, Mbit/s
    // A connected cable only (see platform::LiveDataSource):
    std::string duplex;              // "full" or "half"; empty when not known
    int32_t drops{kDetailUnknown};   // times the link went down since LokiCTL started
    int32_t link_downs{kDetailUnknown}; // times it went down since the interface came up, as the kernel counts them
    std::string ipv4;                // first IPv4 address with its prefix, "10.0.0.12/24"
    std::string ipv6;                // first IPv6 address that is not link-local, without prefix
    std::string ipv6_link;           // the link-local IPv6 address (fe80::...), without prefix
    std::string router;              // IPv4 address of this port's default router
    std::string hardware_address;    // "02:00:00:00:00:01"
};

bool operator==(const StatusPort& left, const StatusPort& right);
bool operator!=(const StatusPort& left, const StatusPort& right);

// The extra values of the Status page. The live source reads them only
// while that page is on top; otherwise everything here is unknown (empty).
struct StatusDetails {
    std::vector<StatusPort> ports;   // in the order they are shown
    std::vector<std::string> dns;    // DNS servers, in the system's order
    int32_t battery_voltage_mv{kDetailUnknown};  // mV
    int32_t battery_current_ma{kDetailUnknown};  // mA, negative while discharging
    int32_t battery_temp_tenths{kDetailUnknown}; // tenths of a degree Celsius
    std::string battery_health;                  // as the battery reports it, for example "Good"
    int32_t battery_cycles{kDetailUnknown};      // charge cycles
    int32_t battery_charge_mah{kDetailUnknown};  // charge now, mAh
    int32_t battery_full_mah{kDetailUnknown};    // charge when full, mAh
    // Until empty while discharging, at the use of about the last minute
    // (model::BatteryClock); not known while battery_estimating.
    int32_t battery_empty_minutes{kDetailUnknown};
    bool battery_estimating{false};              // on battery for less than about a minute
    int32_t battery_full_minutes{kDetailUnknown};  // until full, while charging
    int32_t cpu_temp_tenths{kDetailUnknown};     // processor, tenths of a degree Celsius
    int32_t memory_free_mb{kDetailUnknown};      // memory still available, MiB
    int32_t memory_total_mb{kDetailUnknown};     // all memory, MiB
    int32_t uptime_seconds{kDetailUnknown};      // time since the device started
};

bool operator==(const StatusDetails& left, const StatusDetails& right);
bool operator!=(const StatusDetails& left, const StatusDetails& right);

// The Wi-Fi page: the networks near the built-in Wi-Fi.

// A scan list keeps at most this many networks, the strongest;
// WifiScan::found counts them all.
constexpr std::size_t kWifiMaxNetworks = 32;

// WifiScan::last_scan_minutes when the time is not known.
constexpr int32_t kClockUnknown = -1;

// One network the built-in Wi-Fi can hear, as the system's network service
// lists it. It is one access point, so a network with two of them is
// listed twice.
struct WifiNetwork {
    std::string name;     // network name (SSID); empty for a hidden network
    std::string security; // as the home page writes it: "WPA2", "WPA2/3", "open"
    int32_t strength{0};  // signal, 0-100 per cent, as the service gives it
    int32_t channel{0};   // Wi-Fi channel; 0 when not known
    bool joined{false};   // the built-in Wi-Fi is joined to this access point
};

bool operator==(const WifiNetwork& left, const WifiNetwork& right);
bool operator!=(const WifiNetwork& left, const WifiNetwork& right);

// Why the Wi-Fi page has no list, or what went wrong with the last scan.
enum class WifiScanError {
    None = 0,
    NoService,  // no network service (NetworkManager) to ask
    NoDevice,   // the built-in Wi-Fi is missing or not managed by the service
    WifiOff,    // the Wi-Fi radio is switched off
    Hotspot,    // LokiCTL's hotspot runs on the Wi-Fi (Router or Share)
    // The last three are about the last scan; the list is still shown.
    NotAllowed, // the scan was refused for lack of permission
    TimedOut,   // no scan result within the time limit
    Refused,    // the service refused the scan; WifiScan::reason says why
};

// What the Wi-Fi page shows. The live source fills it only while that page
// is on top; otherwise it is empty (every value unknown).
struct WifiScan {
    bool scanning{false};                     // a scan the user asked for is under way
    WifiScanError error{WifiScanError::None};
    std::string reason;                       // with WifiScanError::Refused
    int32_t last_scan_minutes{kClockUnknown}; // local time of day of the last finished scan, minutes since midnight
    int32_t found{0};                         // networks listed, also those not kept
    std::vector<WifiNetwork> networks;        // strongest first, at most kWifiMaxNetworks
};

bool operator==(const WifiScan& left, const WifiScan& right);
bool operator!=(const WifiScan& left, const WifiScan& right);

// Sorts `networks` strongest first (on equal strength the joined one, then
// by name) and keeps the first kWifiMaxNetworks.
void sort_wifi_networks(std::vector<WifiNetwork>& networks);

// Whether the system's network service (NetworkManager) manages a USB
// Wi-Fi adapter.
enum class Managed {
    Unknown = 0, // no network service to ask
    Yes,
    No,
};

// The name of a managed state in lowercase: "unknown", "yes" or "no". Used
// by the --print-data readout.
const char* managed_name(Managed managed);

// One USB Wi-Fi adapter (a Wi-Fi stick) that is plugged in. LokiCTL only
// shows it: it never changes it, and works the same without one.
// A text that is not known is empty.
struct UsbWifiAdapter {
    std::string interface_name; // its interface, for example "wlan1"
    std::string driver;         // kernel driver, for example "ath9k_htc"
    std::string usb_id;         // USB vendor:product, for example "0cf3:9271"
    std::string name;           // as the stick names itself, for example "ATHEROS USB2.0 WLAN"
    Managed managed{Managed::Unknown};
    std::string device;         // where it is plugged in (a system path); tells adapters apart, never shown
    // Every interface on the stick, interface_name among them, sorted (a
    // second one such as "wlan1mon"); never the survey's listening one.
    std::vector<std::string> interfaces;
};

bool operator==(const UsbWifiAdapter& left, const UsbWifiAdapter& right);
bool operator!=(const UsbWifiAdapter& left, const UsbWifiAdapter& right);

// What LokiCTL's VPN tunnel is doing (see VpnStatus).
enum class VpnState {
    Off = 0,    // no tunnel
    Connecting, // the system's network service is setting the tunnel up
    On,         // the tunnel is up (WireGuard needs no answer for that: see VpnStatus::reply_seconds)
};

// The name of a VPN state in lowercase: "off", "connecting" or "on". Used
// by the pages, the log and the --print-data readout.
const char* vpn_state_name(VpnState state);

// VpnStatus::reply_seconds while nothing has come back through the tunnel.
constexpr int32_t kVpnReplyNone = -1;

// The most WireGuard files the VPN row offers.
constexpr std::size_t kVpnMaxFiles = 16;

// The VPN: the WireGuard files LokiCTL can use and the tunnel made from one
// of them. Only the files' names and the server's host are here: keys and
// the rest of a file never leave the data layer. A text that is not known
// is empty.
struct VpnStatus {
    // The files in LokiCTL's WireGuard folder, by name without ".conf",
    // sorted; at most kVpnMaxFiles. Filled whatever the tunnel does.
    std::vector<std::string> files;
    VpnState state{VpnState::Off};
    // The values below only mean something while the state is not Off.
    std::string file;    // the file the tunnel was made from, without ".conf"
    std::string server;  // the server's host (first peer's endpoint), without the port
    // Seconds since data last came in through the tunnel (WireGuard's
    // handshake time needs privileges the app does not have), or
    // kVpnReplyNone before anything came back.
    int32_t reply_seconds{kVpnReplyNone};
    // The kill switch of the VPN gateway (a hotspot mode and the VPN
    // together): hotspot devices may only go out through the tunnel, so
    // while it is down they get nothing. It follows what the user asked
    // for, not the tunnel's state, so it can be on with the state Off (the
    // tunnel dropped). Off whenever the user has not run both together.
    bool kill_switch{false};
};

// True while the VPN gateway's kill switch is on but the tunnel is not up:
// hotspot devices are cut off until the VPN is on again or turned off.
bool vpn_gateway_down(const VpnStatus& vpn);

bool operator==(const VpnStatus& left, const VpnStatus& right);
bool operator!=(const VpnStatus& left, const VpnStatus& right);

// Everything the home page shows. Numbers are stored as numbers in the units
// noted on each field; the page turns them into text. A value that is not
// known has a value of its own (an empty text, or one of the constants
// above), and the page shows it as "--" instead of an old or a made-up
// number. Each field sits on one line.
struct HomeData {
    bool sample{true};                 // true while the values are made up
    int32_t clock_minutes{0};          // time of day, minutes since midnight
    // Battery charge, per cent (0-100), or kBatteryPercentUnknown (-1) when
    // it is not known. Unknown is never shown as 0.
    int32_t battery_percent{kBatteryPercentUnknown};
    BatteryState battery_state{BatteryState::Unknown}; // what the battery is doing
    NetMode mode{NetMode::Client};     // the active mode
    std::string mode_detail;           // one line on what the mode does
    LinkKind uplink_kind{LinkKind::None}; // what kind of link the uplink is
    // The uplink's values. An empty text, kSpeedUnknown and kSignalUnknown
    // mean "not known"; the Wi-Fi values are empty when the uplink is not
    // a Wi-Fi network.
    std::string uplink_name;           // interface of the uplink, for example "eth0"
    std::string uplink_address;        // IPv4 address of the uplink as text
    std::string gateway_address;       // IPv4 address of the router the uplink leads to
    int32_t uplink_speed_mbps{kSpeedUnknown}; // link speed of the uplink, Mbit/s
    std::string wifi_name;             // name (SSID) of the Wi-Fi network the uplink joined
    std::string wifi_security;         // security of that network, for example "WPA2"
    int32_t wifi_signal_dbm{kSignalUnknown}; // received signal of that network, dBm (negative)
    Internet internet{Internet::Unknown}; // whether the internet answers; Unknown without a link
    // NetworkManager's internet check is set up and on, so it can notice a
    // sign-in page (Internet::Portal). True when NetworkManager is not
    // there to ask.
    bool internet_check{true};
    OfflineState offline_state{OfflineState::Unknown}; // why there is no link; Unknown with one
    bool ap_active{false};             // true while the device runs its own access point
    // The access point's values, the uptime and the clients only mean
    // something while ap_active is true.
    std::string ap_name;               // network name (SSID) of the access point
    std::string ap_security;           // security of the access point, for example "WPA2"
    int32_t ap_channel{0};             // Wi-Fi channel of the access point; 0 when not known
    int32_t uptime_seconds{0};         // time since the mode started, seconds
    ModeSwitch mode_switch;            // the last switch between modes (see ModeSwitch)
    // The current rates, kbit/s; kRateUnknown (or any negative value) when
    // not known.
    int32_t down_kbps{0};              // current download rate, kbit/s
    int32_t up_kbps{0};                // current upload rate, kbit/s
    // The two traffic histories, oldest first. Each value is a share of
    // that history's own recent peak (0-100, the peak is 100), not of a
    // fixed rate, so each graph fills its own height however busy its
    // direction is; the current rates above give the real numbers.
    std::array<uint8_t, kHomeHistoryLength> down_history{}; // download rate history
    std::array<uint8_t, kHomeHistoryLength> up_history{};   // upload rate history
    std::vector<HomeClient> clients;   // connected devices, in the order they are shown
    // Blocking devices on the hotspot (see platform::DeviceBlocker): how
    // many changes of the firewall failed since the start, and what the
    // last failure says ("Block failed: helper not installed"; empty before
    // the first). The count tells a new failure from an old one.
    int32_t block_failures{0};
    std::string block_message;
    StatusDetails details;             // the Status page's extra values (see StatusDetails)
    WifiScan wifi_scan;                // the Wi-Fi page's list (see WifiScan)
    std::vector<UsbWifiAdapter> usb_wifi; // USB Wi-Fi adapters plugged in, by interface name; read on every page
    VpnStatus vpn;                     // the VPN's files and tunnel (see VpnStatus)
};

// Every field is compared. A new field must be added here too: the data
// layer hands a block over only when it differs from the last one, so a
// field left out here would never reach the screen on its own.
bool operator==(const HomeData& left, const HomeData& right);
bool operator!=(const HomeData& left, const HomeData& right);

// Made-up example values for the home page: a router that shares its
// cable uplink through its own access point, with three clients. They are
// the `sample` data source as they are, the starting point of the `demo`
// source, and the `live` source's clock when the system cannot tell the
// time.
// HomeData::sample is true, so the page marks them as sample data. The
// other made-up situations are in home_examples.h.
HomeData sample_home_data();

} // namespace model
