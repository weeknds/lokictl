/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <string>
#include <string_view>
#include <vector>

namespace view {

constexpr int kScreenWidth    = 320;
constexpr int kScreenHeight   = 170;
constexpr int kTitleBarHeight = 30;
constexpr int kNavBarHeight   = 30;

struct ThemePalette {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t bar;
    lv_color_t button;
    lv_color_t border;
    lv_color_t text;
    lv_color_t text_disabled;
    lv_color_t primary;
    lv_color_t info;
    lv_color_t success;
    lv_color_t warning;
    lv_color_t error;
};

ThemePalette palette(bool dark_mode);
void apply_lvgl_theme(lv_display_t* display, bool dark_mode, const lv_font_t* font = LV_FONT_DEFAULT);

// Theme names accepted on the command line. For now a theme is just the
// light or dark mode; the first entry is the default.
struct ThemeChoice {
    const char* name;
    bool dark_mode;
};

const std::vector<ThemeChoice>& theme_choices();
const ThemeChoice* find_theme_choice(std::string_view name);
// The theme names joined with ", ", for help and error messages.
std::string theme_choice_names();

} // namespace view
