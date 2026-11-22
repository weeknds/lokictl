/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"

#include <array>
#include <cstdint>
#include <string>

namespace screen {

// The Modes page, opened from the home page (key 4). It switches the device
// between the modes:
// - one row per mode, in model::NetMode order: Client, Router, Share and
//   VPN, each with its name, a short hint and a tick on the mode that is on
//   (HomeData::mode). Share and VPN come later and are drawn switched off;
// - a status line under the rows: "Switching…" while a switch
//   runs, a plain error after one failed ("Plug in a cable first", "Not
//   allowed: permission missing", "Failed: <reason>"), the hotspot's name
//   and password while Router runs, or the current link in Client.
//
// Up and down (F/X) move the highlight and stop at the first and the last
// row. Enter or key 6 (Use) asks for the highlighted mode
// (BaseViewModel::request_mode()); Use is switched off on the mode that is
// on, on Share and VPN, and while a switch runs. Key 4 or a short ESC goes
// back. The page never waits for the switch: the data layer does it, and
// the page follows HomeData::mode and HomeData::mode_switch as they arrive.
//
// The highlight lives in the view model (modes_selection_*), where
// kModesFollowsActive means "the mode that is on", so it is kept across a
// theme switch. Rows, ticks, the status line and key 6 are restyled in
// place when the values or the highlight change; nothing is built again.
class ModesScreen : public BaseScreen {
public:
    ModesScreen(viewmodel::BaseViewModel& view_model,
                app::AssetManager& assets,
                view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    // The page's help.
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    struct Row {
        lv_obj_t* row{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* hint{nullptr};
        lv_obj_t* mark{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The row of the mode that is on.
    int32_t active_row() const;
    // The highlighted row: the view model's selection when it names a row,
    // otherwise active_row().
    int32_t selected_row() const;
    // True when Use may switch to `row` now.
    bool can_use(int32_t row) const;
    void use_selected();
    // Shows the current values and highlight: rows, ticks, the status line
    // and key 6. Touches only what changed.
    void refresh();
    void refresh_status();

    // Both call refresh(). The user data is the screen; the observers are
    // bound to the content area, so they go when the screen goes.
    static void selection_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void data_observer(lv_observer_t* observer, lv_subject_t* subject);

    std::array<Row, model::kNetModeCount> rows_{};
    lv_obj_t* status_left_{nullptr};
    lv_obj_t* status_right_{nullptr};
    lv_obj_t* status_caption_{nullptr}; // "password", left of status_right_ while it shows one
    std::string status_left_shown_;
    std::string status_right_shown_;
    int32_t status_tone_{-1};     // the left label's colour shown last; -1 before the first
    int32_t shown_selected_{-1};  // the highlighted row drawn last
    int32_t shown_active_{-1};    // the row with the tick drawn last
    int32_t shown_use_{-1};       // key 6: 1 on, 0 off, -1 before the first
};

} // namespace screen
