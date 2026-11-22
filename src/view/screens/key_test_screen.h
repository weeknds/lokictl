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

// The key test page (demo-keys), the third demo page: home -> counter ->
// key test. It shows what a page gets from the keyboard:
// - a list of four rows; up and down (F/X or the arrow keys) move the
//   selection and stop at the first and the last row, Enter picks the
//   selected row (marked with a tick);
// - a box with the name of the last key the page got, the picked row and
//   whether typing mode is on.
// Soft key 5 (Type) turns typing mode on: letters and keys 4-8 then arrive as
// characters, both soft keys are switched off, and a short ESC ends typing.
//
// Everything it shows lives in the view model (key_test_*), so the same
// state always gives the same picture, also after going back, opening the
// page again or a theme switch. The page draws nothing else: no counter of
// presses and nothing that depends on how the page was reached.
class KeyTestScreen : public BaseScreen {
public:
    KeyTestScreen(viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets,
                  view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    bool on_key(uint32_t key) override;
    bool on_back() override;
    bool text_entry() const override;
    view::widgets::HelpContent help() const override;

private:
    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;
    void build_list(lv_obj_t* content);
    void build_status(lv_obj_t* content);

    // Shows typing mode in the box and switches the two soft keys off while
    // the user types. The user data is the screen; the observer is bound to
    // a label of the screen, so it goes when the screen goes.
    static void typing_observer(lv_observer_t* observer, lv_subject_t* subject);
};

} // namespace screen
