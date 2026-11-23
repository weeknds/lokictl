/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "theme.h"

#include "lvgl.h"

#include <cstdint>

namespace view::widgets {

// The pieces a network map is drawn with: dots, rings, lines, the internet's
// square, a switch's box, this device, stars and the selection's brackets. Each one is a
// plain object (or an lv_line) placed at a fixed position in its parent's
// coordinates, coloured from the theme or from a colour the caller takes
// from the theme. The Constellation home page and the Map page draw with
// them. The order in which they are made is the order in which they are
// drawn, back to front.

// The size of a star.
constexpr int32_t kMapStarSize = 3;
// The brackets around a selected node: this wide, this far from the node.
constexpr int32_t kMapBracketWidth = 4;
constexpr int32_t kMapBracketGap = 3;

// A round, filled shape of `size` at (x, y).
lv_obj_t* map_dot(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, lv_color_t color);

// A line through `points`, drawn in the parent's coordinates, without
// rounded ends. lv_line keeps the pointer, so `points` must live as long as
// the line (a static array).
lv_obj_t* map_line(lv_obj_t* parent,
                   const lv_point_precise_t* points,
                   uint32_t count,
                   int32_t width,
                   lv_color_t color);

// A filled rectangle from (x, top) to (x + width - 1, bottom), for a
// straight upright line.
lv_obj_t* map_upright(lv_obj_t* parent, int32_t x, int32_t top, int32_t bottom, int32_t width, lv_color_t color);

// A ring of `size` at (x, y) with a `border` px border, filled with the
// ground colour, so what lies behind it does not show through.
lv_obj_t* map_ring(lv_obj_t* parent,
                   int32_t x,
                   int32_t y,
                   int32_t size,
                   int32_t border,
                   lv_color_t color,
                   const Theme& theme);

// A faint ring of `size` around a centre given as twice its coordinates
// (so it can lie between two pixels): a 1 px border in the subtle border
// colour and nothing inside, so a smaller ring inside it still shows.
lv_obj_t* map_orbit(lv_obj_t* parent, int32_t centre_x2, int32_t centre_y2, int32_t size, const Theme& theme);

// The internet: a square of `size` at (x, y), outlined in the text colour
// and filled with the ground colour.
lv_obj_t* map_internet(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, const Theme& theme);

// A switch: a box `width` by `height` at (x, y), outlined in the text
// colour and filled with the ground colour.
lv_obj_t* map_switch(lv_obj_t* parent, int32_t x, int32_t y, int32_t width, int32_t height, const Theme& theme);

// This device: a ring of `size` at (x, y) with a `border` px border and a
// dot of `dot_size` in its middle, both in `color`. Returns the ring.
lv_obj_t* map_device(lv_obj_t* parent,
                     int32_t x,
                     int32_t y,
                     int32_t size,
                     int32_t border,
                     int32_t dot_size,
                     lv_color_t color,
                     const Theme& theme);

// A dim dot centred on (x, y), decoration only.
lv_obj_t* map_star(lv_obj_t* parent, int32_t x, int32_t y, const Theme& theme);

// Fills a label's box with the ground colour, so a ring or a line that
// crosses it does not run through its text.
void map_ground_fill(lv_obj_t* label, const Theme& theme);

// The two brackets that mark the selected node, open towards it, in the
// accent colour. They start hidden.
struct MapBrackets {
    lv_obj_t* left{nullptr};
    lv_obj_t* right{nullptr};
};

MapBrackets make_map_brackets(lv_obj_t* parent, int32_t height, const Theme& theme);

// Shows the brackets around a node that spans x from `node_left` up to (not
// including) `node_right`: kMapBracketGap px from either side, from row
// `top` down, `height` tall.
void show_map_brackets(const MapBrackets& brackets, int32_t node_left, int32_t node_right, int32_t top, int32_t height);

void hide_map_brackets(const MapBrackets& brackets);

} // namespace view::widgets
