/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "theme.h"

#include "lvgl.h"

#include <cstdint>
#include <memory>

namespace screen {

// What a home layout draws with. It is made fresh for every call and holds
// references only, so a layout must not keep it.
struct HomeContext {
    lv_obj_t* content;            // 320x140, no layout of its own
    const view::Theme& theme;
    const model::HomeData& data;
    const char* app_name;
};

// One way of drawing the home page; each theme has its own (see
// HomeScreen). A layout places plain objects, labels and lines at fixed
// positions, styled from context.theme, with every value taken from
// context.data. It keeps pointers to the objects select() changes; LVGL owns
// the objects, and they go when the page goes.
class HomeLayout {
public:
    virtual ~HomeLayout() = default;

    // Builds the page into context.content.
    virtual void build(const HomeContext& context) = 0;

    // Marks client `index` as the selected one. Called once right after
    // build() and again whenever the selection changes; it may only restyle,
    // move and retext objects build() made.
    virtual void select(const HomeContext& context, int32_t index) = 0;
};

// Small helpers the layouts share.

// Makes `label` one line of a fixed width: longer text ends in dots instead
// of wrapping onto a second line.
inline void fix_width(lv_obj_t* label, int32_t width, const lv_font_t* font) {
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

// How far below the top of its label a font's baseline lies.
inline int32_t baseline_from_top(const lv_font_t* font) {
    return lv_font_get_line_height(font) - font->base_line;
}

// The big-type gauge of the Instrument theme.
std::unique_ptr<HomeLayout> make_instrument_home_layout();

// The network map of the Constellation theme.
std::unique_ptr<HomeLayout> make_constellation_home_layout();

// The boxed panes of the Terminal theme.
std::unique_ptr<HomeLayout> make_terminal_home_layout();

} // namespace screen
