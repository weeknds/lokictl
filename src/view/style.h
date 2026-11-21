/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "theme.h"

#include "lvgl.h"

#include <cstdint>

// Shared building blocks that give LVGL objects the active theme's look.
//
// They set local style properties once, while a screen is built; nothing
// here watches for theme changes (see ThemeEngine). Objects passed in should
// carry no LVGL theme styles: create them and call lv_obj_remove_style_all()
// first, as everything created in here does.
namespace view::style {

// Screen roots: the theme's ground colour, the main text colour and the Body
// font, which child labels inherit. No border, radius or padding.
void screen(lv_obj_t* obj, const Theme& theme);

enum class Edge {
    Top,
    Bottom,
};

// A bar along a screen edge (title bar, navigation bar): the ground colour,
// a rule in the border colour and width on `rule_edge` only, and the main
// text colour. Padding is left to the caller.
void bar(lv_obj_t* obj, const Theme& theme, Edge rule_edge);

// A box that floats above a screen (toast, help panel): surface fill, a
// border in the strong border colour, the theme's corner radius and the main
// text colour. Padding is left to the caller.
void popup(lv_obj_t* obj, const Theme& theme);

// A label in the font of `role`, in the main text colour or in `color`.
void text(lv_obj_t* label, const Theme& theme, FontRole role);
void text(lv_obj_t* label, const Theme& theme, FontRole role, lv_color_t color);

// Sets `text` as a caption: the Small font in the muted colour, in capital
// letters and letter-spaced when the theme asks for that.
void caption(lv_obj_t* label, const Theme& theme, const char* text);

// A raised box: surface fill, the theme's border and corner radius and a
// small padding.
void card(lv_obj_t* obj, const Theme& theme);

// One row of a list. Labels inside inherit the row's font and colour:
// - not selected: Body font in the main text colour, no fill;
// - selected: Strong font, either filled with the selection colour (text in
//   on_selection) or outlined in the selection colour (text in selection),
//   as the theme says.
// Unselected rows may colour parts of their text themselves (an address,
// for example). Every row keeps the same border width, so text lines up
// whether a row is selected or not.
void list_row(lv_obj_t* row, const Theme& theme, bool selected);

struct Pane {
    lv_obj_t* root;    // position this one
    lv_obj_t* content; // put the pane's children in here
};

// A box with a border and a title. Themes with pane_title_in_border set the
// title (Small font, in `title_color`) into the top border line; the other
// themes show it as a normal caption inside the box, and then `title_color`
// is not used. `width` must be a fixed size; `height` may be LV_SIZE_CONTENT.
// `content` fills the box inside its padding and has no layout of its own.
Pane pane(lv_obj_t* parent,
          const Theme& theme,
          const char* title,
          int32_t width,
          int32_t height,
          lv_color_t title_color);

} // namespace view::style
