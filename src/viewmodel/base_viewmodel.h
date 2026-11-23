/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "tool_data.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
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
    lv_subject_t* mode_request_subject();
    model::NetMode requested_mode() const;
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

    // The Tools page and its Ping, DNS and Trace pages.
    // The highlighted row of the Tools page; starts at 0. The page shows it
    // clamped to the rows that can be opened.
    lv_subject_t* tools_selection_subject();
    int32_t tools_selection() const;
    // Stores `row`; nothing is written when it stays the same.
    void select_tools_row(int32_t row);
    // Opens the Tools page (see open_page()). The home page's key 7 uses it.
    bool open_tools();
    // Sets typing back to off, then opens the Ping, DNS or Trace page (see
    // open_page()).
    bool open_tool(model::ToolKind kind);
    // The target each tool uses, as its place in the page's list of
    // targets (the presets, then the typed target); starts at 0. The page
    // shows it clamped to the list, which can change (the router comes and
    // goes).
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
    // Asks to run `kind` on `target`, or to stop the run. The view model
    // only passes the request on: app::ToolController hands it to the tool
    // runner, and the run comes back in tools(). Counts every request, like
    // request_mode(); requested_tool_stop(), requested_tool() and
    // requested_tool_target() say what was asked.
    void request_tool_run(model::ToolKind kind, const std::string& target);
    void request_tool_stop();
    lv_subject_t* tool_request_subject();
    bool requested_tool_stop() const;
    model::ToolKind requested_tool() const;
    const std::string& requested_tool_target() const;
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
    reactive::IntSubject status_scroll_subject_;
    reactive::IntSubject devices_selection_subject_;
    reactive::IntSubject block_request_subject_;
    std::string requested_block_address_;
    bool requested_block_{false};
    reactive::IntSubject modes_selection_subject_;
    reactive::IntSubject mode_request_subject_;
    model::NetMode requested_mode_{model::NetMode::Client};
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
    model::ToolKind requested_tool_{model::ToolKind::Ping};
    std::string requested_tool_target_;
    model::ToolsData tools_;
    reactive::IntSubject tools_subject_;
    reactive::IntSubject trace_scroll_subject_;
    reactive::IntSubject wifi_scroll_subject_;
    reactive::IntSubject wifi_scan_request_subject_;
    reactive::IntSubject home_selection_subject_;
    reactive::IntSubject home_data_subject_;
    reactive::IntSubject key_test_selection_subject_;
    reactive::IntSubject key_test_picked_subject_;
    reactive::StringSubject<16> key_test_last_key_subject_;
    reactive::BoolSubject key_test_typing_subject_;
};

} // namespace viewmodel
