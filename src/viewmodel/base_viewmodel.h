/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace viewmodel {

// The number of rows in the list of the key test page (demo-keys).
constexpr int32_t kKeyTestRowCount = 4;

// The highlighted row of the Settings page while it follows the active
// theme, whichever that is.
constexpr int32_t kSettingsThemeFollowsActive = -1;

// The home page shows at most this many clients; the selection never goes
// past the last of them.
constexpr int32_t kHomeClientRows = 3;

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
    reactive::IntSubject home_selection_subject_;
    reactive::IntSubject home_data_subject_;
    reactive::IntSubject key_test_selection_subject_;
    reactive::IntSubject key_test_picked_subject_;
    reactive::StringSubject<16> key_test_last_key_subject_;
    reactive::BoolSubject key_test_typing_subject_;
};

} // namespace viewmodel
