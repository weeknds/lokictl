/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_reader.h"
#include "data_source.h"
#include "home_data.h"

#include "lvgl.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// A new hotspot password: 12 characters from a set that is easy to read and
// type (no 0/o, 1/l/i), from the system's random source.
std::string random_hotspot_password();

// The screen's side of the data layer. It owns a platform::DataReader,
// fetches the newest values from it and hands them to the view model, which
// tells the pages. It is the only place that gives the view model new
// values, and the only place that logs about the data layer: everything here
// runs on the UI thread, the background reader never draws, never logs and
// never touches the view model.
//
// In a normal run, read_first() takes the first reading before the first
// page is built, start() starts the background reader and an LVGL timer that
// looks four times a second whether a new block is there, and stop() ends
// both. Snapshot runs never start the reader: step() takes each further
// reading on the UI thread, one after the other, through the same path, so
// the same command always gives the same picture.
//
// Log lines: "data: <source> (first reading in <n> ms)" once at the start;
// "data: battery <n>% <state>" for the first reading and then only when the
// charge or the state changed ("--%" for an unknown charge, "data: battery
// unknown" when neither is known); "data: link <none|wifi|cable>, access
// point <on|off>, <n> clients" for the first reading and then only when the
// kind of link, the access point or the number of clients changed, always
// after the battery line of the same reading; "data: reader stopped" when a
// background reader that ran has ended.
//
// It also follows the current page and tells the reader whether the home
// page, the only page that shows the traffic, is on top
// (DataReader::set_traffic_shown()), so the live source reads the traffic
// once a second only while it is seen; and the same for the Status page and
// its details (DataReader::set_details_shown()). While the screen is dark
// (set_screen_dark()), neither counts as shown, whatever page is on top.
//
// Switching modes: it follows the view model's mode requests
// (BaseViewModel::request_mode(), from the Modes page) and hands each to the
// reader with the hotspot's password (VPN: with the picked WireGuard file,
// BaseViewModel::vpn_file()), so the UI thread never waits on the
// network. Without the background reader (snapshots) it takes a reading
// right after the request, so the page shows the switch's first state at
// once. Log lines: "data: mode <client|router|share|vpn>" for the first reading and
// whenever the mode changes; "data: switching to <mode>", "data: switch to
// <mode> failed: <why>" and "data: switch to <mode> done" as the switch
// moves on (never a name or the password); "data: back to Client before
// quitting" when a reader that changes the network stops while a mode other
// than Client is on or a switch runs.
//
// The hotspot's name and password: it follows the Hotspot page's requests
// (BaseViewModel::request_hotspot_rename() and
// request_new_hotspot_password()), makes the new password, gives the new
// values to the view model, has them saved (set_hotspot_saver(), only when
// the reader changes the real network) and, while Router or Share runs, asks the
// reader to start the hotspot again with them (ModeRequest::restart).
// Requests while a switch runs are ignored. Log lines: "data: hotspot
// renamed" and "data: new hotspot password" (never the values).
//
// Blocking devices on the hotspot: it keeps the list of blocked devices
// (set_blocked_devices() at start, then the Devices page's requests,
// BaseViewModel::request_block()), at most platform::kMaxBlockedDevices,
// the oldest dropped first; has it saved (set_blocked_saver(), only when
// the reader changes the real network) and hands the whole list to the
// reader with each request, also one that changes nothing, so a change the
// firewall refused is tried again. When the data layer reports a new failure
// (HomeData::block_failures), it logs the message and tells the failure
// listener. Log lines: "data: block a device (<n> blocked)", "data: unblock
// a device (<n> blocked)" (never an address) and "data: <message>".
//
// USB Wi-Fi adapters: it logs those of the first reading ("data: usb
// wi-fi: none" or "data: usb wi-fi adapter <interface> (<driver>, <usb
// id>)"), then "data: usb wi-fi adapter connected: <interface> (<driver>,
// <usb id>)" and "data: usb wi-fi adapter removed: <interface>" as they
// come and go, and tells the USB Wi-Fi listener.
//
// Wi-Fi scans: it hands the Wi-Fi page's requests
// (BaseViewModel::request_wifi_scan()) to the reader, and tells the reader
// whether that page is on top (DataReader::set_wifi_shown()), so the live
// source reads the list only then. Log lines: "data: wi-fi scan asked",
// then "data: wi-fi scan done (<n> networks)" or "data: wi-fi scan:
// <why not>" when it ends while the page is shown (never a network name).
//
// Create it after the view model and destroy it before; like
// app::ThemeController, it is not copied.
class DataController {
public:
    // The longest read_first() waits for values a source collects in the
    // background (the live source's connection to the network service).
    static constexpr std::chrono::milliseconds kFirstReadingWait{250};

    // Starts following the current page at once.
    DataController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::DataSource> source);
    // Calls stop() and stops following the current page.
    ~DataController();

    DataController(const DataController&) = delete;
    DataController& operator=(const DataController&) = delete;

    // Takes the first reading on the calling thread, hands it to the view
    // model and logs how long it took, then the first battery and link
    // lines. Before it, it waits up to kFirstReadingWait for values the
    // source collects in the background (DataReader::wait_until_ready()),
    // and the logged time includes that wait. Call it
    // once, before the first page is built, so the page never shows values
    // that are about to change.
    void read_first();

    // For a normal run: starts the background reader and the timer that
    // fetches its values. Does nothing while the reader already runs.
    void start();

    // For runs without the background reader (snapshots): takes one further
    // reading on the calling thread and hands it over at once. Does nothing
    // while the background reader runs.
    void step();

    // Deletes the timer, stops the background reader and waits for it to
    // end. Logs "data: reader stopped" when the reader ran. Safe to call
    // more than once, and without start().
    void stop();

    // Makes the hotspot's new passwords: when the Hotspot page asks for
    // one, and when Router or Share is asked for while the view model holds none
    // and the reader changes the real network. Empty (the default):
    // random_hotspot_password(). Snapshots set a fixed one.
    void set_password_generator(std::function<std::string()> generator);

    // Saves a changed name or password (a value that is not set stays as
    // it is in the settings file). Called on the UI thread, and only when
    // the reader changes the real network; empty: nothing is saved.
    using HotspotSaver =
        std::function<void(const std::optional<std::string>& name, const std::optional<std::string>& password)>;
    void set_hotspot_saver(HotspotSaver saver);

    // The blocked devices at start (the saved list, already checked). Call
    // it before read_first(); the reader gets the list with the first
    // reading.
    void set_blocked_devices(const std::vector<std::string>& addresses);

    // Saves the list of blocked devices after each change. Called on the UI
    // thread, and only when the reader changes the real network; empty:
    // nothing is saved.
    using BlockedSaver = std::function<void(const std::vector<std::string>& addresses)>;
    void set_blocked_saver(BlockedSaver saver);

    // Saves the "Keep mode after quitting" setting after each change the
    // user makes (not the value set at start). Called on the UI thread.
    // Empty: nothing is saved.
    using KeepModeSaver = std::function<void(bool keep)>;
    void set_keep_mode_saver(KeepModeSaver saver);

    // Told the message of each new failure to block or unblock a device
    // ("Block failed: helper not installed"), on the UI thread. Empty:
    // only the log says it.
    using BlockFailureListener = std::function<void(const std::string& message)>;
    void set_block_failure_listener(BlockFailureListener listener);

    // Told about each USB Wi-Fi adapter that is plugged in (`connected`) or
    // removed after the first reading, with its interface name, on the UI
    // thread. Adapters there at the first reading are only logged. Empty:
    // only the log says it.
    using UsbWifiListener = std::function<void(const std::string& interface_name, bool connected)>;
    void set_usb_wifi_listener(UsbWifiListener listener);

    // Tells the controller whether the screen is dark (app::PowerController,
    // F3.7). While it is, the reader is told that neither the traffic nor
    // the Status page's details are shown, so the live source stops reading
    // them; when the screen wakes, the page on top counts again at once.
    void set_screen_dark(bool dark);

private:
    static void poll_timer_cb(lv_timer_t* timer);
    // Tells the reader whether the page on top shows the traffic or the
    // details. The user data is the controller.
    static void page_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    // Hands a mode request of the view model to the reader. The user data
    // is the controller.
    static void mode_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    // Hands the "Keep mode after quitting" setting to the reader and saves
    // a change. The user data is the controller.
    static void keep_mode_cb(lv_observer_t* observer, lv_subject_t* subject);
    // Handles a change the Hotspot page asked for. The user data is the
    // controller.
    static void hotspot_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    // Handles a block or unblock the Devices page asked for. The user data
    // is the controller.
    static void block_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    // Hands a Wi-Fi scan request of the view model to the reader. The user
    // data is the controller.
    static void wifi_scan_request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void log_wifi_scan(const model::WifiScan& scan);
    void request_mode(model::NetMode mode);
    void change_blocked();
    void change_hotspot();
    // Hands `request` to the reader; without the background reader it is
    // taken with a reading right away.
    void send(const platform::ModeRequest& request);
    std::string new_password() const;
    void save_hotspot(const std::optional<std::string>& name, const std::optional<std::string>& password);
    void log_mode(const model::HomeData& data);
    // Hands the reader the traffic and details flags for the page on top
    // and the screen's state.
    void update_shown();

    // Fetches the newest block from the reader, if one arrived since the
    // last time, and hands it over. Returns false when nothing new was there;
    // nothing else happens then.
    bool poll();
    // Gives `data` to the view model, and logs the battery line when the
    // charge or the state differs from the one logged last, then the link
    // line when the kind of link, the access point or the number of clients
    // does.
    void deliver(const model::HomeData& data);
    void log_battery(int32_t percent, model::BatteryState state);
    void log_link(model::LinkKind kind, bool ap_active, std::size_t clients);
    // Logs the USB Wi-Fi adapters of the first reading, then each one
    // plugged in or removed (told apart by UsbWifiAdapter::device), and
    // tells the listener.
    void check_usb_wifi(const std::vector<model::UsbWifiAdapter>& adapters);

    viewmodel::BaseViewModel& view_model_;
    platform::DataReader reader_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* mode_observer_{nullptr};
    int32_t mode_requests_seen_{0};  // the view model's request count handed on last
    lv_observer_t* hotspot_observer_{nullptr};
    int32_t hotspot_requests_seen_{0};
    std::function<std::string()> password_generator_;
    HotspotSaver hotspot_saver_;
    lv_observer_t* block_observer_{nullptr};
    int32_t block_requests_seen_{0};
    lv_observer_t* wifi_scan_observer_{nullptr};
    int32_t wifi_scan_requests_seen_{0};
    bool scan_logged_{false};           // a scan was asked for and its end is not logged yet
    bool scan_seen_running_{false};     // a block showed that scan under way
    std::vector<std::string> blocked_;  // the devices to block, oldest first
    BlockedSaver blocked_saver_;
    lv_observer_t* keep_mode_observer_{nullptr};
    bool keep_mode_seen_{false};        // the setting's first value was handed on
    KeepModeSaver keep_mode_saver_;
    BlockFailureListener block_failure_listener_;
    int32_t block_failures_seen_{0};    // the data layer's failure count handled last
    bool mode_logged_{false};
    model::NetMode logged_mode_{model::NetMode::Client};
    model::SwitchState logged_switch_{model::SwitchState::Idle};
    bool screen_dark_{false};
    model::HomeData block_;  // the block fetched last; kept to reuse its memory
    bool battery_logged_{false};
    int32_t logged_percent_{model::kBatteryPercentUnknown};
    model::BatteryState logged_state_{model::BatteryState::Unknown};
    bool link_logged_{false};
    model::LinkKind logged_kind_{model::LinkKind::None};
    bool logged_ap_active_{false};
    std::size_t logged_clients_{0};
    UsbWifiListener usb_wifi_listener_;
    bool usb_wifi_logged_{false};
    std::vector<model::UsbWifiAdapter> usb_wifi_;  // the adapters handed over last
};

} // namespace app
