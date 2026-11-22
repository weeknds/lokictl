/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dev_softkeys_screen.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace screen {
namespace {

// Rows in the 320x110 content area, which starts at y 30 on the screen. The
// sample bars' top edges land on y 50 and y 100; the page's own bar is at
// y 140.
constexpr int32_t kWidestCaptionY  = 4;
constexpr int32_t kWidestBarY      = 20;
constexpr int32_t kChangedCaptionY = 54;
constexpr int32_t kChangedBarY     = 70;

using view::widgets::SoftKey;
using view::widgets::SoftKeySet;

// The page's own keys. They do nothing: the page only shows them.
SoftKeySet page_keys() {
    return {{
        {"Modes"},
        {"Scan"},
        {"Tools", {}, true},
        {"Pcap", {}, false, false},
        {},
    }};
}

// The widest case: five 5-letter words, with a main key and a switched-off
// one.
SoftKeySet widest_keys() {
    return {{
        {"Modes"},
        {"Pause"},
        {"Tools", {}, true},
        {"Block", {}, false, false},
        {"Close"},
    }};
}

// Different words, states and a different unused key, so that every key of
// the changed bar has something to undo.
SoftKeySet first_keys() {
    return {{
        {"One"},
        {"Two", {}, true},
        {},
        {"Four"},
        {"Five", {}, false, false},
    }};
}

lv_obj_t* caption(lv_obj_t* parent, const view::Theme& theme, const char* text, int32_t y) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    view::style::caption(label, theme, text);
    lv_obj_set_pos(label, theme.metrics.space_md, y);
    return label;
}

} // namespace

DevSoftKeysScreen::DevSoftKeysScreen(viewmodel::BaseViewModel& view_model,
                                     app::AssetManager& assets,
                                     view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

DevSoftKeysScreen::~DevSoftKeysScreen() {
    widest_bar_.reset();
    changed_bar_.reset();
}

bool DevSoftKeysScreen::handles(model::AppPage page) {
    return page == model::AppPage::DevSoftKeys;
}

view::widgets::SoftKeySet DevSoftKeysScreen::soft_keys() {
    return page_keys();
}

void DevSoftKeysScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    using KeyInput = view::widgets::SoftKeyBar::KeyInput;

    // Each caption is created before its bar, so the bar's ground covers the
    // caption's last pixel rows where the two meet.
    caption(content, theme, "5 letters each", kWidestCaptionY);
    widest_bar_ = std::make_unique<view::widgets::SoftKeyBar>(
        content, theme_engine(), widest_keys(), KeyInput::None);
    widest_bar_->build();
    lv_obj_align(widest_bar_->root(), LV_ALIGN_TOP_LEFT, 0, kWidestBarY);

    caption(content, theme, "changed after build", kChangedCaptionY);
    changed_bar_ = std::make_unique<view::widgets::SoftKeyBar>(
        content, theme_engine(), first_keys(), KeyInput::None);
    changed_bar_->build();
    lv_obj_align(changed_bar_->root(), LV_ALIGN_TOP_LEFT, 0, kChangedBarY);

    const SoftKeySet keys = page_keys();
    for (std::size_t index = 0; index < keys.size(); ++index) {
        changed_bar_->set_key(index, keys[index]);
    }
}

} // namespace screen
