/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "theme.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <string_view>

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

// A bar along a screen edge (title bar, soft-key bar): the ground colour,
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

// How much room a pane leaves between its border and its content.
// - Normal: the theme's usual padding.
// - Compact: for dense pages where rows sit on a fixed grid. In themes with
//   pane_title_in_border the content starts 2 px higher than Normal, so its
//   first two rows lie under the bottom of the title label, and the bottom
//   padding is space_xs; the side padding is unchanged. In the other themes
//   the padding is space_xs all round. In title-in-border themes, keep the
//   first two rows of a Compact pane's content free of ink: the title and a
//   pane note are filled with the ground colour and cover them.
enum class PaneDensity {
    Normal,
    Compact,
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
          lv_color_t title_color,
          PaneDensity density = PaneDensity::Normal);

// A second label on a pane's top line, at its right end, for a short value
// such as a clock or a count. In themes with pane_title_in_border it sits in
// the top border line like the title: Small font in `color`, filled with the
// ground colour, as far from the right edge as the title is from the left.
// In the other themes it sits in the box's top right corner, in the Small
// font and in `color`, and takes no part in the box's layout. Returns the
// label, so the caller can change its text later.
lv_obj_t* pane_note(const Pane& pane, const Theme& theme, const char* text, lv_color_t color);

// How one key of the soft-key bar is drawn; see widgets::SoftKeyBar.
enum class SoftKeyState {
    Normal,   // a key that does something
    Primary,  // the page's main key
    Disabled, // shown, but switched off for now
};

struct SoftKeyLook {
    bool box_filled;   // false: the box is an outline of the theme's border width
    lv_color_t box;    // fill or outline colour; also the colour of the line to the key
    lv_color_t number; // the key number inside the box
    lv_color_t label;  // the word
    int32_t radius;    // box corners
};

// The look of a soft key, derived from tokens every theme already has, so a
// new theme needs no code here:
// - the number box is filled when the theme fills its selection
//   (selection_filled), otherwise it is an outline of border_width;
// - corners use the theme's radius, but never more than 2 px;
// - Normal: the box is `text` on a light theme and `muted` on a dark one; the
//   number is `background` on a filled box and the box colour on an outline;
//   the word is `text`;
// - Primary: the box is `accent`; the number is `on_accent` on a filled box
//   and `accent` on an outline; the word is `accent` on a dark theme and
//   `text` on a light one, where accent-coloured text would be hard to read;
// - Disabled: the box is `border_subtle` when filled and `border` when
//   outlined; number and word are `muted`.
SoftKeyLook soft_key(const Theme& theme, SoftKeyState state);

// The word as the theme writes it: capitals when the theme writes captions in
// capitals (uppercase_captions), otherwise lowercase. ASCII letters only;
// other bytes are kept.
std::string soft_key_text(const Theme& theme, std::string_view text);

} // namespace view::style
