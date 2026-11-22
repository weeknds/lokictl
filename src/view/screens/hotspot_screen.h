/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <cstdint>
#include <string>

namespace screen {

// The Hotspot page, opened from the Modes page (key 7). It has no title
// bar, so the join code can be big:
// - at the left, while Router runs, a QR code a phone camera scans to join
//   the hotspot (join_text()), black on white with a white quiet zone in
//   every theme; otherwise a box that says the code shows while Router
//   runs;
// - at the right, the hotspot's name (the running one from
//   HomeData::ap_name while Router runs, else the one Router will use),
//   its password (always the view model's), the channel and the security,
//   and a line with the state: "Applying…" while a switch runs (no code
//   then), an error after one failed, the question after the first press
//   of key 6, or how long the hotspot has run.
//
// Key 5 (Name) starts typing mode with the current name: printable
// characters are added (at most 32), Backspace takes one away, Enter saves
// a valid name (BaseViewModel::request_hotspot_rename()), or puts the
// default name back when the name is empty, and a short ESC cancels. Key 6 (Pass) asks first; a second press makes a new password
// (BaseViewModel::request_new_hotspot_password()), any other key cancels.
// Keys 5 and 6 are switched off while a switch runs. Key 4 or a short ESC
// goes back. The page never waits: app::DataController saves the change
// and starts the hotspot again if it runs.
//
// Typing and the name typed so far live in the view model, like the key
// test page's typing.
class HotspotScreen : public BaseScreen {
public:
    HotspotScreen(viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets,
                  view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    // The page's help.
    static view::widgets::HelpContent help_content();

    // The text of the join code: "WIFI:T:WPA;S:<name>;P:<password>;;", with
    // '\', ';', ',', ':' and '"' in the name and the password escaped by a
    // backslash.
    static std::string join_text(const std::string& name, const std::string& password);

    bool on_key(uint32_t key) override;
    bool on_back() override;
    bool text_entry() const override;
    view::widgets::HelpContent help() const override;

private:
    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // True while Router runs with its hotspot.
    bool router_on() const;
    // True while a switch between modes runs.
    bool switching() const;
    // The name the page shows and the code holds.
    std::string shown_name() const;

    void start_rename();
    void press_new_password();
    // Ends the question of key 6, if it was asked.
    void cancel_question();

    // Shows the current values: the code or its box, the texts, the state
    // line, the typing panel and the soft keys. Touches only what changed.
    void refresh();
    void refresh_keys();
    void refresh_code(bool on, bool busy, const std::string& name);
    void refresh_typing();

    // Each calls refresh(). The user data is the screen; the observers are
    // bound to the content area, so they go when the screen goes.
    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* main_panel_{nullptr};   // the code and the values
    lv_obj_t* code_{nullptr};         // the QR code
    lv_obj_t* code_box_{nullptr};     // in place of the code while Router is off
    lv_obj_t* code_note_{nullptr};    // the box's text
    lv_obj_t* state_{nullptr};        // "on" / "off" right of the heading
    lv_obj_t* name_{nullptr};
    lv_obj_t* password_{nullptr};
    lv_obj_t* details_{nullptr};      // channel, security and time on
    lv_obj_t* message_{nullptr};
    lv_obj_t* typing_panel_{nullptr}; // the rename panel
    lv_obj_t* draft_{nullptr};        // the name typed so far, with a cursor
    lv_obj_t* count_{nullptr};        // characters used and the keys
    lv_obj_t* typing_note_{nullptr};  // a warning or why the name is not taken

    std::string code_text_shown_;     // the text the code holds; never logged
    bool code_ok_{false};             // the code was made from code_text_shown_
    std::string state_shown_;
    std::string name_shown_;
    std::string password_shown_;
    std::string details_shown_;
    std::string message_shown_;
    std::string draft_shown_;
    std::string count_shown_;
    std::string typing_note_shown_;
    int32_t state_tone_{-1};
    int32_t details_tone_{-1};
    int32_t message_tone_{-1};
    int32_t typing_note_tone_{-1};
    int32_t shown_typing_{-1};        // 1 typing, 0 not, -1 before the first
    int32_t shown_keys_{-1};          // what refresh_keys() set last
    bool question_{false};            // key 6 was pressed once
    bool draft_invalid_{false};       // Enter was pressed on a name that is not taken
};

} // namespace screen
