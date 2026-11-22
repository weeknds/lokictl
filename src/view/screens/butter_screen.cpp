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
    // The view model outlives every screen. A short ESC goes back as well;
    // Z/C and the arrow keys reach the page as left and right (on_key()).
    // Key 8 opens the key test page on top of this one.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Less", [&view_model] { view_model.decrement_counter(); }},
        {},
        {"More", [&view_model] { view_model.increment_counter(); }},
        {"Keys", [&view_model] { view_model.open_page(model::AppPage::KeyTest); }},
    }};
}

// Left counts down and right counts up, like keys 5 and 7.
bool ButterScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_LEFT:
            view_model().decrement_counter();
            return true;
        case LV_KEY_RIGHT:
            view_model().increment_counter();
            return true;
        default:
            return false;
    }
}

view::widgets::HelpContent ButterScreen::help() const {
    return {"COUNTER",
            {
                {"ESC / 4", "Back"},
                {"Z / 5", "Decrease"},
                {"C / 7", "Increase"},
                {"8", "Key test"},
            }};
}

void ButterScreen::build_content(lv_obj_t* content) {
    auto* page_label = lv_label_create(content);
    lv_label_bind_text(page_label, view_model().counter_subject(), "Counter: %d");
    view::style::text(page_label, theme(), view::FontRole::Title);
    lv_obj_center(page_label);
}

} // namespace screen
