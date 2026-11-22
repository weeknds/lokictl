/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "butter_screen.h"

#include "style.h"
#include "theme.h"

namespace screen {

ButterScreen::ButterScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

view::widgets::SoftKeySet ButterScreen::soft_keys() {
    // The view model outlives every screen. Z/C and the arrow keys reach
    // keys 5 and 7 on this page as well.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.toggle_page(); }},
        {"Less", [&view_model] { view_model.decrement_counter(); }},
        {},
        {"More", [&view_model] { view_model.increment_counter(); }},
        {},
    }};
}

void ButterScreen::build_content(lv_obj_t* content) {
    auto* page_label = lv_label_create(content);
    lv_label_bind_text(page_label, view_model().counter_subject(), "Counter: %d");
    view::style::text(page_label, theme(), view::FontRole::Title);
    lv_obj_center(page_label);
}

} // namespace screen
