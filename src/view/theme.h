/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace view {

constexpr int kScreenWidth    = 320;
constexpr int kScreenHeight   = 170;
constexpr int kTitleBarHeight = 30;
constexpr int kNavBarHeight   = 30;

// The three looks the app ships with. The values are positions in the theme
// table in theme.cpp and are stored in ThemeEngine::subject().
enum class ThemeId {
    Instrument = 0,
    Constellation,
    Terminal,
};
constexpr int kThemeCount = 3;

// The theme used when neither the command line nor a saved setting picks
// one. This is the single place to change the default theme.
constexpr ThemeId kDefaultThemeId = ThemeId::Terminal;

// What a piece of text is for. Every theme maps each role to a font file and
// a size; screens ask for a role, never for a file.
enum class FontRole {
    Display = 0, // big numbers and single words
    Title,       // headings and important values
    Body,        // normal interface text
    Strong,      // emphasised interface text, same size as Body
    Mono,        // data: addresses, counters, tables
    Small,       // captions, hints, secondary details
};
constexpr int kFontRoleCount = 6;

// No text on the 320x170 screen may be smaller than this.
constexpr uint16_t kMinTextSizePx = 12;

struct FontSpec {
    const char* file; // a file name from bundled_fonts.h
    uint16_t size_px;
};

// Colours as 0xRRGGBB. The first ten are the semantic colours every screen
// uses; the last five cover borders and selections.
struct ThemeColorSpec {
    uint32_t background;    // screen ground
    uint32_t surface;       // raised areas: cards, popups, toasts
    uint32_t text;          // main text
    uint32_t muted;         // secondary text, disabled things
    uint32_t accent;        // primary highlight
    uint32_t accent2;       // links and addresses
    uint32_t border;        // rules, pane and card borders
    uint32_t success;
    uint32_t warning;
    uint32_t error;
    uint32_t border_strong; // popups and other things that must stand out
    uint32_t border_subtle; // faint grid lines and dividers
    uint32_t on_accent;     // text on an accent fill
    uint32_t selection;     // the selected item
    uint32_t on_selection;  // text on a filled selection
};

struct ThemeMetrics {
    int16_t space_xs;
    int16_t space_sm;
    int16_t space_md;
    int16_t space_lg;
    int16_t radius;
    int16_t border_width;
    int16_t caption_letter_space;
    bool dark;                 // dark ground: dark simulator shell and LVGL dark variant
    bool pane_title_in_border; // pane titles sit on the pane's top border line
    bool uppercase_captions;   // captions are written in capital letters
    bool selection_filled;     // selected items are filled; otherwise outlined
};

// One theme as compiled into the app.
struct ThemeSpec {
    ThemeId id;
    const char* name;         // lowercase, as typed on the command line
    const char* display_name; // as shown to the user
    ThemeColorSpec colors;
    FontSpec fonts[kFontRoleCount]; // indexed by FontRole
    ThemeMetrics metrics;
};

const ThemeSpec& theme_spec(ThemeId id);
// "instrument", "constellation" or "terminal"; nullptr for any other name.
const ThemeSpec* find_theme(std::string_view name);
// The theme names joined with ", ", for help and error messages.
std::string theme_names();

// The same colours as ThemeColorSpec, ready for LVGL.
struct ThemeColors {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t text;
    lv_color_t muted;
    lv_color_t accent;
    lv_color_t accent2;
    lv_color_t border;
    lv_color_t success;
    lv_color_t warning;
    lv_color_t error;
    lv_color_t border_strong;
    lv_color_t border_subtle;
    lv_color_t on_accent;
    lv_color_t selection;
    lv_color_t on_selection;
};

ThemeColors to_colors(const ThemeColorSpec& spec);

// The active theme with its fonts loaded; see ThemeEngine.
struct Theme {
    ThemeId id;
    const char* name;
    const char* display_name;
    ThemeColors colors;
    ThemeMetrics metrics;
    const lv_font_t* fonts[kFontRoleCount]; // indexed by FontRole, never nullptr

    const lv_font_t* font(FontRole role) const;
};

} // namespace view
