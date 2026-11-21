/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"

namespace screen {

// Developer page that shows the active theme on one screen: every colour
// token, every font role with the symbols the themes use, a card, a list
// with a selected row and a pane with a title. It is opened by name only
// (snapshot --page dev-theme) and is not reachable from the normal UI.
class DevThemeScreen : public BaseScreen {
public:
    DevThemeScreen(viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;
    void build_swatches(lv_obj_t* content);
    void build_font_scale(lv_obj_t* content);
    void build_samples(lv_obj_t* content);
    void build_symbol_check(lv_obj_t* content);
};

} // namespace screen
