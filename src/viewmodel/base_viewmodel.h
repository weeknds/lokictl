/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "tool_data.h"
#include "trap_data.h"
#include "watch_data.h"

#include "lvgl.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace viewmodel {

// The number of rows in the list of the key test page (demo-keys).
constexpr int32_t kKeyTestRowCount = 4;

// The highlighted row of the Settings page while it follows the active
// theme, whichever that is.
constexpr int32_t kSettingsThemeFollowsActive = -1;

// The home page shows at most this many clients; the selection never goes
// past the last of them.
constexpr int32_t kHomeClientRows = 3;

// The highlighted row of the Modes page while it follows the active mode.
constexpr int32_t kModesFollowsActive = -1;

// The Map page's nodes, as map_selection() names them, in stepping order:
// the route to the internet, the router, the network devices' slots (slot
// s is kMapFirstSlot + s), the switch between the router and this device,
// this device, and the hotspot's clients (client i of the home page's list
// is kMapFirstClient + i, at most kHomeClientRows of them). A node keeps
// its number while the page is open, so the selection stays on it when
// other nodes come and go.
constexpr int32_t kMapSlots = 6;
constexpr int32_t kMapRoute = 0;
constexpr int32_t kMapRouter = 1;
constexpr int32_t kMapFirstSlot = 2;
constexpr int32_t kMapSwitch = kMapFirstSlot + kMapSlots;
constexpr int32_t kMapDevice = kMapSwitch + 1;
constexpr int32_t kMapFirstClient = kMapDevice + 1;

// A change of the hotspot the Hotspot page asks for (see
// BaseViewModel::request_hotspot_rename()).
enum class HotspotChange {
    Rename,      // a new name, BaseViewModel::requested_hotspot_name()
    NewPassword, // a new random password
};

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* counter_subject();
    lv_subject_t* quit_requested_subject();
    // Counts how often a theme was asked for. It is a counter, because LVGL
    // only tells observers about a value that changed, and the same theme
    // may be asked for twice. requested_theme() says which one.
    lv_subject_t* theme_request_subject();
    // The theme asked for last, as its row number (its position in the
    // theme table); -1 before the first request.
    int32_t requested_theme() const;

    // The path of open pages (see model::BaseModel). Only the top page is
    // shown; current_page_subject() publishes it, and the app builds that
    // page later, never inside the caller's callback.
    model::AppPage current_page() const;
    std::size_t page_depth() const;
    // Makes `page` the only page in the path; used at startup and by
    // snapshots.
    void show_page(model::AppPage page);
    // Opens `page` on top, or goes back to it when it is already further
    // down the path. Returns false when nothing changed (already on top, or
    // the path is full).
    bool open_page(model::AppPage page);
    // Goes back one page. Returns false on the first page.
    bool go_back();
    void increment_counter();
    void decrement_counter();
    void request_quit();
    // Asks for the theme in table row `row`. The view model only passes the
    // request on: the app applies it (app::ThemeController), and
    // view::ThemeEngine::subject() publishes the theme that is active.
    void request_theme(int32_t row);

    // The Settings page's theme list.
    // The highlighted row, or kSettingsThemeFollowsActive while the
    // highlight is on the active theme; starts as
    // kSettingsThemeFollowsActive.
    lv_subject_t* settings_theme_selection_subject();
    int32_t settings_theme_selection() const;
    void select_settings_theme(int32_t row);
    // Sets the highlight back to kSettingsThemeFollowsActive, then opens the
    // Settings page (see open_page()). Every way into Settings uses it, so
    // the highlight always starts on the active theme.
    bool open_settings();

    // Whether the screen dims when no key was pressed for a while (the
    // Settings page's "Dim when idle" row); starts as true. The app sets the
    // saved choice at start; app::PowerController follows the subject and
    // saves every later change. Nothing is written when it stays the same.
    lv_subject_t* screen_dimming_subject();
    bool screen_dimming() const;
    void set_screen_dimming(bool on);

    // Whether the network mode is kept when LokiCTL quits (the Settings
    // page's "Keep mode after quitting" row); starts as false. The app sets
    // the saved choice at start; app::DataController follows the subject,
    // tells the data reader and saves every later change. Nothing is
    // written when it stays the same.
    lv_subject_t* keep_mode_subject();
    bool keep_mode() const;
    void set_keep_mode(bool on);

    // The Status page.
    // The first row it shows, counted from the top of its list; starts at
    // 0. The page shows it clamped to the rows there are (the view model
    // does not know how many rows the values make), so a value past the end
    // shows the last rows.
    lv_subject_t* status_scroll_subject();
    int32_t status_scroll() const;
    // Stores `row` (0 or more) as the first row shown; nothing is written
    // when it stays the same.
    void set_status_scroll(int32_t row);
    // Sets the first row back to 0, then opens the Status page (see
    // open_page()). The home page's key 8 uses it, so the page always opens
    // at the top.
    bool open_status();

    // The Devices page.
    // The highlighted device, as its place in home().clients; starts at 0.
    // The page shows it clamped to the devices there are (the list changes
    // while the page is open), like the Status page's first row.
    lv_subject_t* devices_selection_subject();
    int32_t devices_selection() const;
    // Stores `index` (0 or more); nothing is written when it stays the same.
    void set_devices_selection(int32_t index);
    // Puts the highlight on the home page's selected client, then opens the
    // Devices page (see open_page()). The home page's Enter uses it.
    bool open_devices();
    // Asks to block (`block` true) or unblock the device with the hardware
    // address `address`. The view model only passes the request on:
    // app::DataController keeps the list, saves it and hands it to the data
    // layer, and each client's state comes back in home().clients
    // (HomeClient::blocked). Counts every request, like request_mode();
    // requested_block_address() and requested_block() say what was asked.
    void request_block(const std::string& address, bool block);
    lv_subject_t* block_request_subject();
    const std::string& requested_block_address() const;
    bool requested_block() const;

    // The Modes page.
    // The highlighted row (one row per model::NetMode, in its order), or
    // kModesFollowsActive while the highlight is on the active mode; starts
    // as kModesFollowsActive.
    lv_subject_t* modes_selection_subject();
    int32_t modes_selection() const;
    void select_mode_row(int32_t row);
    // Sets the highlight back to kModesFollowsActive, then opens the Modes
    // page (see open_page()). The home page's key 4 uses it.
    bool open_modes();
    // Asks for a switch to `mode`. The view model only passes the request
    // on: app::DataController hands it to the data layer, and the switch's
    // state comes back in home().mode_switch. Counts every request, like
    // theme_request_subject(); requested_mode() says which one.
    void request_mode(model::NetMode mode);
    // Asks to turn the VPN off (a request for model::NetMode::Vpn with
    // requested_vpn_off() true): the tunnel and the kill switch go, a
    // hotspot stays. Counted like request_mode().
    void request_vpn_off();
    lv_subject_t* mode_request_subject();
    model::NetMode requested_mode() const;
    bool requested_vpn_off() const;
    // The WireGuard file the VPN row offers: the one picked by name when it
    // is still among home().vpn.files, otherwise the first one; empty when
    // there is none. A VPN request uses it. The pick is kept across a
    // theme switch.
    std::string vpn_file() const;
    // Picks the file `step` places after (or before) vpn_file() in
    // home().vpn.files, going round at either end. Notifies
    // vpn_file_subject() when the pick changes.
    void step_vpn_file(int32_t step);
    lv_subject_t* vpn_file_subject();
    // The hotspot's password, for the Modes and Hotspot pages to show. The
    // app sets it (from the settings file on the device, a made-up one in
    // the simulator and in snapshots); empty when there is none yet. It
    // never goes to the log.
    const std::string& hotspot_password() const;
    void set_hotspot_password(const std::string& password);
    // The hotspot's name, the one Router and Share start with: the saved name, or
    // the default one on the device, a made-up one in the simulator and in
    // snapshots. Set by the app.
    const std::string& hotspot_name() const;
    void set_hotspot_name(const std::string& name);
    // The name the hotspot has until the user renames it, and gets back
    // when a rename leaves the name empty. Set by the app at start.
    const std::string& default_hotspot_name() const;
    void set_default_hotspot_name(const std::string& name);
    // Counts every change of the hotspot's name or password made with the
    // two setters above, so the pages that show them follow. Starts at 0.
    lv_subject_t* hotspot_subject();

    // The Hotspot page.
    // Sets typing back to off, then opens the page (see open_page()). The
    // Modes page's key 7 uses it.
    bool open_hotspot();
    // Asks for a new name (an empty one means the default name) or a new
    // random password. The view model only
    // passes the request on: app::DataController makes the password, saves
    // the change, sets the new values with the setters above and starts
    // the hotspot again if it runs. Counts every request, like
    // request_mode(); requested_hotspot_change() and
    // requested_hotspot_name() say what was asked.
    void request_hotspot_rename(const std::string& name);
    void request_new_hotspot_password();
    lv_subject_t* hotspot_request_subject();
    HotspotChange requested_hotspot_change() const;
    const std::string& requested_hotspot_name() const;
    // True while the page is in typing mode (renaming); the name typed so
    // far is hotspot_draft(). Kept here like the key test page's typing.
    lv_subject_t* hotspot_typing_subject();
    bool hotspot_typing() const;
    void set_hotspot_typing(bool typing);
    const std::string& hotspot_draft() const;
    void set_hotspot_draft(const std::string& draft);

    // The Tools page and its Ping, DNS, Trace, Ports and Speed pages.
    // The highlighted row of the Tools page; starts at 0. The page shows it
    // clamped to the rows that can be opened.
    lv_subject_t* tools_selection_subject();
    int32_t tools_selection() const;
    // Stores `row`; nothing is written when it stays the same.
    void select_tools_row(int32_t row);
    // Opens the Tools page (see open_page()). The home page's key 7 uses it.
    bool open_tools();
    // Sets typing back to off, then opens the Ping, DNS, Trace, Ports or
    // Speed page (see open_page()).
    bool open_tool(model::ToolKind kind);
    // The speed test's server (model::valid_speed_server()); starts as
    // model::kSpeedServerDefault. The app sets it from the settings file
    // at start; nothing else changes it.
    const std::string& speed_server() const;
    void set_speed_server(const std::string& server);
    // The target each tool uses, as its place in the page's list of
    // targets (the presets, then the typed target); starts at 0. The page
    // shows it clamped to the list, which can change (the router comes and
    // goes). kToolTargetTyped picks the typed target, which is always last.
    static constexpr int32_t kToolTargetTyped = std::numeric_limits<int32_t>::max();
    int32_t tool_target(model::ToolKind kind) const;
    void set_tool_target(model::ToolKind kind, int32_t index);
    // The target typed for each tool; empty while none was typed.
    const std::string& tool_typed_target(model::ToolKind kind) const;
    void set_tool_typed_target(model::ToolKind kind, const std::string& target);
    // Counts every change made with the four setters above, so the pages
    // follow. Starts at 0.
    lv_subject_t* tool_target_subject();
    // True while a tool's page is in typing mode; the target typed
    // so far is tool_draft(). Kept here like the hotspot's rename.
    lv_subject_t* tool_typing_subject();
    bool tool_typing() const;
    void set_tool_typing(bool typing);
    const std::string& tool_draft() const;
    void set_tool_draft(const std::string& draft);
    // Asks to run `kind` on `target`, to stop the run, or to save the
    // latest listen run as a PCAP file. The view model only passes the
    // request on: app::ToolController hands it to the tool runner, and the
    // run comes back in tools(). Counts every request, like request_mode();
    // requested_capture_save(), requested_tool_stop(), requested_tool() and
    // requested_tool_target() say what was asked.
    void request_tool_run(model::ToolKind kind, const std::string& target);
    void request_tool_stop();
    void request_capture_save();
    lv_subject_t* tool_request_subject();
    bool requested_capture_save() const;
    bool requested_tool_stop() const;
    model::ToolKind requested_tool() const;
    const std::string& requested_tool_target() const;
    // The command bar's way to start a tool: after opening its page, it asks
    // here, and the page (the Ping, DNS, Trace, Ports or Speed page, or
    // Discover for Listen and Probe) takes the request once with
    // take_tool_start() and does what its own key's first press does.
    // tool_start_subject() counts every request. A page change (open_page(),
    // go_back(), show_page()) drops a request not taken yet.
    void request_tool_start(model::ToolKind kind);
    // True once for a request of `kind`, which is then forgotten; false
    // (and the request kept) when none waits or it is for another kind.
    bool take_tool_start(model::ToolKind kind);
    lv_subject_t* tool_start_subject();
    // The latest run of each tool and the Sent count. Only ever set on the
    // UI thread, by app::ToolController. set_tools() returns false and tells
    // nobody when `data` equals what is held; otherwise it stores it and
    // tools_subject() counts one more change.
    const model::ToolsData& tools() const;
    bool set_tools(const model::ToolsData& data);
    lv_subject_t* tools_subject();
    // The Trace page's first hop row shown, or kTraceFollow (the start
    // value) to follow the newest hops. The page clamps it to the hops.
    static constexpr int32_t kTraceFollow = -1;
    lv_subject_t* trace_scroll_subject();
    int32_t trace_scroll() const;
    // Stores `row`; nothing is written when it stays the same.
    void set_trace_scroll(int32_t row);
    // The Ports page's host detail: kPortsDetailClosed (the start value)
    // while it is not shown, else its first row shown (the page clamps it
    // to the rows). open_tool() closes it.
    static constexpr int32_t kPortsDetailClosed = -1;
    lv_subject_t* ports_detail_subject();
    int32_t ports_detail() const;
    // Stores `row`; nothing is written when it stays the same.
    void set_ports_detail(int32_t row);
    // Over the host detail, one finding of the check against the list of
    // known holes at a time: kPortsFindingClosed (the start value) while
    // none is shown, else its place in PortRun::findings (the page clamps
    // it). open_tool() closes it.
    static constexpr int32_t kPortsFindingClosed = -1;
    lv_subject_t* ports_finding_subject();
    int32_t ports_finding() const;
    // Stores `index`; nothing is written when it stays the same.
    void set_ports_finding(int32_t index);

    // The Wi-Fi page.
    // Sets the first row shown back to 0, then opens the page (see
    // open_page()). The Tools page's Wi-Fi row uses it.
    bool open_wifi();
    // The first network row it shows; starts at 0. The page clamps it to
    // the networks there are.
    lv_subject_t* wifi_scroll_subject();
    int32_t wifi_scroll() const;
    // Stores `row` (0 or more); nothing is written when it stays the same.
    void set_wifi_scroll(int32_t row);
    // Asks for a fresh Wi-Fi scan. The view model only passes the request
    // on: app::DataController hands it to the data layer, and the list
    // comes back in home().wifi_scan. Counts every request, like
    // request_mode().
    void request_wifi_scan();
    lv_subject_t* wifi_scan_request_subject();

    // The Discover page (home key 5). Listening runs as a tool
    // (model::ToolKind::Listen, requested with request_tool_run() on the
    // uplink interface), so its run comes back in tools().listen.
    // Sets the first device shown back to 0, then opens the page (see
    // open_page()).
    bool open_discover();
    // The first device it shows; starts at 0. The page clamps it to the
    // devices there are.
    lv_subject_t* discover_scroll_subject();
    int32_t discover_scroll() const;
    // Stores `row` (0 or more); nothing is written when it stays the same.
    void set_discover_scroll(int32_t row);

    // The Watch page (Tools key 8, or `watch`). Watch runs on its own
    // (app::WatchController), next to any tool, and comes back in watch().
    // Puts the selection on the newest alert and closes the detail, then
    // opens the page (see open_page()).
    bool open_watch();
    // What Watch shows: its run and the known devices. Only ever set on the
    // UI thread, by app::WatchController. set_watch() returns false and
    // tells nobody when `data` equals what is held; otherwise it stores it
    // and watch_subject() counts one more change.
    const model::WatchData& watch() const;
    bool set_watch(const model::WatchData& data);
    lv_subject_t* watch_subject();
    // Asks to start Watch on the uplink, to stop it, or to mark the device
    // with the hardware address `hardware` as known. The view model only
    // passes the request on: app::WatchController carries it out. Counts
    // every request; requested_watch() and requested_watch_hardware() say
    // what was asked.
    enum class WatchRequest { Start, Stop, MarkKnown };
    void request_watch(WatchRequest request, const std::string& hardware = {});
    lv_subject_t* watch_request_subject();
    WatchRequest requested_watch() const;
    const std::string& requested_watch_hardware() const;
    // The selected alert (0 is the newest); the page clamps it to the
    // alerts there are.
    lv_subject_t* watch_selection_subject();
    int32_t watch_selection() const;
    // Stores `row` (0 or more); nothing is written when it stays the same.
    void set_watch_selection(int32_t row);
    // True while the selected alert's detail is shown.
    lv_subject_t* watch_detail_subject();
    bool watch_detail() const;
    void set_watch_detail(bool open);

    // The Trap page (Watch key 8, or `trap`): fake services that catch
    // whoever connects. The trap runs on its own (app::TrapController), and
    // its run comes back in trap(). Puts the selection on the newest
    // connection and closes the detail, then opens the page.
    bool open_trap();
    // Only ever set on the UI thread, by app::TrapController; set_trap()
    // returns false and tells nobody when `run` equals what is held.
    const model::TrapRun& trap() const;
    bool set_trap(const model::TrapRun& run);
    lv_subject_t* trap_subject();
    // Asks to set the trap or to take it away; app::TrapController carries
    // it out. Counts every request.
    enum class TrapRequest { Start, Stop };
    void request_trap(TrapRequest request);
    lv_subject_t* trap_request_subject();
    TrapRequest requested_trap() const;
    // The selected connection (0 is the newest) and whether its detail is
    // shown.
    lv_subject_t* trap_selection_subject();
    int32_t trap_selection() const;
    void set_trap_selection(int32_t row);
    lv_subject_t* trap_detail_subject();
    bool trap_detail() const;
    void set_trap_detail(bool open);

    // The Health page (Tools key 7, or `health`). The health check runs as
    // a tool (model::ToolKind::Health, requested with request_tool_run() on
    // the uplink interface), so its run comes back in tools().health.
    // Puts the selection on the first finding and closes the detail, then
    // opens the page (see open_page()).
    bool open_health();
    // The selected finding (0 is the worst); the page clamps it to the
    // findings there are.
    lv_subject_t* health_selection_subject();
    int32_t health_selection() const;
    // Stores `row` (0 or more); nothing is written when it stays the same.
    void set_health_selection(int32_t row);
    // True while the selected finding's detail is shown.
    lv_subject_t* health_detail_subject();
    bool health_detail() const;
    void set_health_detail(bool open);

    // The Map page (Discover key 5).
    // Puts the selection back on the router and empties the slots, then
    // opens the page (see open_page()).
    bool open_map();
    // The selected node (kMapRouter and the other kMap* numbers); starts at
    // kMapRouter. The page shows it moved to a node that is there.
    lv_subject_t* map_selection_subject();
    int32_t map_selection() const;
    // Stores `node`; nothing is written when it stays the same.
    void set_map_selection(int32_t node);
    // The hardware address of the network device in each slot; empty for a
    // free slot. The page fills them as devices are first seen (see
    // screen::MapScreen). Kept here, like the selection, so a theme switch
    // keeps every device in its place.
    const std::array<std::string, kMapSlots>& map_slots() const;
    void set_map_slots(const std::array<std::string, kMapSlots>& slots);

    // The home page.
    // The values it shows (see model::HomeData). The page reads them while
    // it is built, and they change while it is open: the data layer hands
    // in new values with set_home_data(), and home_data_subject() tells the
    // page.
    const model::HomeData& home() const;
    // Replaces the values with `data`. Only ever called on the UI thread, by
    // app::DataController. Returns false and changes nothing, and tells
    // nobody, when `data` equals the values already held. Otherwise it
    // stores them; if the new block has fewer clients than the selection's
    // place, the selection is pulled back to the last client shown (at most
    // kHomeClientRows; 0 without clients); then home_data_subject() counts
    // one more change. Returns true then.
    bool set_home_data(const model::HomeData& data);
    // Counts every change of home(). It is a counter, like
    // theme_request_subject(), so every new block reaches the observers;
    // home() says what the values are. Starts at 0.
    lv_subject_t* home_data_subject();
    // The index of the selected client in home().clients; starts at 0.
    lv_subject_t* home_selection_subject();
    int32_t home_selection() const;
    // Moves the selection by `delta` clients. It stops at the first and the
    // last client shown (at most kHomeClientRows); it does not wrap around.
    // Nothing changes, and nothing is written, when the selection would stay
    // where it is or there are no clients.
    void home_select(int32_t delta);

    // What the key test page (demo-keys) shows. It lives here rather than in
    // the screen, so it is kept while the app runs: after going back,
    // opening the page again and a theme switch.
    // The selected row, 0 to kKeyTestRowCount - 1; starts at 0.
    lv_subject_t* key_test_selection_subject();
    // The picked row, or -1 while none has been picked.
    lv_subject_t* key_test_picked_subject();
    // The name of the last key the page got; "-" before the first.
    lv_subject_t* key_test_last_key_subject();
    // True while the page is in typing mode.
    lv_subject_t* key_test_typing_subject();
    bool key_test_typing() const;
    // Moves the selection by `delta` rows. It stops at the first and the
    // last row; it does not wrap around.
    void key_test_move(int32_t delta);
    // Picks the selected row.
    void key_test_pick();
    // Records `name` as the last key (at most 15 characters are kept).
    void key_test_note_key(const char* name);
    void key_test_set_typing(bool typing);

private:
    void publish_all();

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::IntSubject counter_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject theme_request_subject_;
    int32_t requested_theme_{-1};
    reactive::IntSubject settings_theme_selection_subject_;
    reactive::BoolSubject screen_dimming_subject_;
    reactive::BoolSubject keep_mode_subject_;
    reactive::IntSubject status_scroll_subject_;
    reactive::IntSubject devices_selection_subject_;
    reactive::IntSubject block_request_subject_;
    std::string requested_block_address_;
    bool requested_block_{false};
    reactive::IntSubject modes_selection_subject_;
    reactive::IntSubject mode_request_subject_;
    model::NetMode requested_mode_{model::NetMode::Client};
    bool requested_vpn_off_{false};
    std::string vpn_file_;                // the picked file, by name; empty for the first one
    reactive::IntSubject vpn_file_subject_;
    std::string hotspot_password_;
    std::string hotspot_name_;
    std::string default_hotspot_name_;
    reactive::IntSubject hotspot_subject_;
    reactive::IntSubject hotspot_request_subject_;
    HotspotChange requested_hotspot_change_{HotspotChange::Rename};
    std::string requested_hotspot_name_;
    reactive::BoolSubject hotspot_typing_subject_;
    std::string hotspot_draft_;
    reactive::IntSubject tools_selection_subject_;
    int32_t tool_targets_[model::kToolKindCount]{};          // by model::ToolKind
    std::string tool_typed_targets_[model::kToolKindCount];  // by model::ToolKind
    reactive::IntSubject tool_target_subject_;
    reactive::BoolSubject tool_typing_subject_;
    std::string tool_draft_;
    reactive::IntSubject tool_request_subject_;
    bool requested_tool_stop_{false};
    bool requested_capture_save_{false};
    model::ToolKind requested_tool_{model::ToolKind::Ping};
    std::string requested_tool_target_;
    reactive::IntSubject tool_start_subject_;
    bool tool_start_waiting_{false};
    model::ToolKind tool_start_kind_{model::ToolKind::Ping};
    std::string speed_server_{model::kSpeedServerDefault};
    model::ToolsData tools_;
    reactive::IntSubject tools_subject_;
    reactive::IntSubject trace_scroll_subject_;
    reactive::IntSubject ports_detail_subject_;
    reactive::IntSubject ports_finding_subject_;
    reactive::IntSubject wifi_scroll_subject_;
    reactive::IntSubject wifi_scan_request_subject_;
    reactive::IntSubject discover_scroll_subject_;
    model::WatchData watch_;
    reactive::IntSubject watch_subject_;
    reactive::IntSubject watch_request_subject_;
    WatchRequest requested_watch_{WatchRequest::Start};
    std::string requested_watch_hardware_;
    reactive::IntSubject watch_selection_subject_;
    reactive::BoolSubject watch_detail_subject_;
    model::TrapRun trap_;
    reactive::IntSubject trap_subject_;
    reactive::IntSubject trap_request_subject_;
    TrapRequest requested_trap_{TrapRequest::Start};
    reactive::IntSubject trap_selection_subject_;
    reactive::BoolSubject trap_detail_subject_;
    reactive::IntSubject health_selection_subject_;
    reactive::BoolSubject health_detail_subject_;
    reactive::IntSubject map_selection_subject_;
    std::array<std::string, kMapSlots> map_slots_;
    reactive::IntSubject home_selection_subject_;
    reactive::IntSubject home_data_subject_;
    reactive::IntSubject key_test_selection_subject_;
    reactive::IntSubject key_test_picked_subject_;
    reactive::StringSubject<16> key_test_last_key_subject_;
    reactive::BoolSubject key_test_typing_subject_;
};

} // namespace viewmodel
