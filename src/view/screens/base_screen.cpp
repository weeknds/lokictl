/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_screen.h"

#include "asset_manager.h"
#include "style.h"
#include "theme.h"
#include "theme_engine.h"

namespace screen {

BaseScreen::BaseScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine)
    : view_model_(view_model), assets_(assets), theme_engine_(theme_engine) {}

BaseScreen::~BaseScreen() {
    title_bar_.reset();
    soft_key_bar_.reset();

    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
}

void BaseScreen::init(Layout layout) {
    if (root_) {
        return;
    }

    root_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_size(root_, view::kScreenWidth, view::kScreenHeight);
    view::style::screen(root_, theme());

    if (layout == Layout::FullScreen) {
        content_ = lv_obj_create(root_);
        lv_obj_remove_style_all(content_);
        lv_obj_set_size(content_, view::kScreenWidth, view::kScreenHeight);
        lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, 0);
    }
    else {
        title_bar_ = std::make_unique<view::widgets::TitleBar>(root_, view_model_, assets_, theme_engine_);
        title_bar_->build();

        soft_key_bar_ = std::make_unique<view::widgets::SoftKeyBar>(
            root_, theme_engine_, soft_keys(), view::widgets::SoftKeyBar::KeyInput::Bound);
        soft_key_bar_->build();

        content_ = lv_obj_create(root_);
        lv_obj_remove_style_all(content_);
        lv_obj_set_size(content_, LV_PCT(100), view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight);
        lv_obj_align(content_, LV_ALIGN_TOP_MID, 0, view::kTitleBarHeight);
    }

    // Transparent: the screen root draws the ground.
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);

    build_content(content_);
}

lv_obj_t* BaseScreen::root() const {
    return root_;
}

view::widgets::SoftKeySet BaseScreen::soft_keys() {
    return {};
}

view::widgets::SoftKeyBar* BaseScreen::soft_key_bar() {
    return soft_key_bar_.get();
}

viewmodel::BaseViewModel& BaseScreen::view_model() {
    return view_model_;
}

app::AssetManager& BaseScreen::assets() {
    return assets_;
}

const view::Theme& BaseScreen::theme() const {
    return theme_engine_.theme();
}

view::ThemeEngine& BaseScreen::theme_engine() {
    return theme_engine_;
}

} // namespace screen
