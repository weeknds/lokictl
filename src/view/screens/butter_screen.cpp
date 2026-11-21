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

void ButterScreen::build_content(lv_obj_t* content) {
    auto* page_label = lv_label_create(content);
    lv_label_bind_text(page_label, view_model().counter_subject(), "Counter: %d");
    view::style::text(page_label, theme(), view::FontRole::Title);
    lv_obj_center(page_label);
}

} // namespace screen
