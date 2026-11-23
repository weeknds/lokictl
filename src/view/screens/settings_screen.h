/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"

#include <cstdint>

namespace screen {

// The Settings page, opened from the home page (key 6). It holds the theme
// picker and the dimming switch:
// - a heading line, "Settings" with the caption "theme" (shown as THEME,
//   lowercase in Terminal);
// - one row per theme, in table order: its name, four small squares in that
//   theme's own colours (ground, text, accent, second accent) and a tick on
//   the theme that is active;
// - the row "Dim when idle" with "on" or "off" (BaseViewModel::
//   screen_dimming());
// - the row "Remote view" with its state;
// - the row "Keep mode after quitting" with "on" or "off" (BaseViewModel::
//   keep_mode()).
// Up and down (F/X or the arrow keys) move the highlight and stop at the
// first and the last row. Enter or key 6 (Use) switches to the highlighted
// theme; Use is switched off while the highlight is on the active theme,
// because there is nothing to switch to. On the dimming row key 6 reads
// Off (or On while dimming is off), and it or Enter switches dimming
// (app::PowerController saves it); the keep-mode row works the same way
// (app::DataController saves it). Key 4 or a short ESC goes back.
//
// The highlight lives in the view model (settings_theme_selection_*), so it
// is kept across the theme switch, which builds the page again. There
// kSettingsThemeFollowsActive (-1) means "the active theme", and the row
// after the themes is the dimming row: the view model
// knows no theme types, so this screen, which knows the theme it was built
// in, turns -1 into that theme's row. A pick only asks the view model
// (request_theme()); app::ThemeController switches later. Nothing on the
// page depends on how it was reached, so a page built after a switch looks
// the same as one built at a fresh start in that theme.
class SettingsScreen : public BaseScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The row of the theme this page was built in.
    int32_t active_row() const;
    // The highlighted row: the view model's selection when it names a row,
    // otherwise active_row().
    int32_t selected_row() const;

    // Restyles the rows for the highlight and switches the Use key on or
    // off. The user data is the screen; the observer is bound to the list,
    // so it goes when the screen goes.
    static void selection_observer(lv_observer_t* observer, lv_subject_t* subject);
    // Shows "on" or "off" in the dimming row. The observer is bound to the
    // value's label.
    static void dimming_observer(lv_observer_t* observer, lv_subject_t* subject);
};

} // namespace screen
