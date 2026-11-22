/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace view::widgets {

// A small bar graph built from plain objects: one filled rectangle per value,
// left to right, standing on the bottom edge. Graphs are drawn as shapes
// because no bundled font has braille characters.
//
// Returns a plain container, count * (bar_width + gap) - gap wide and
// `height` tall, to be placed by the caller. Each value is a share of the
// full height (0-100; larger values count as 100). A bar is never less than
// 1 px tall, so the graph always shows its floor. The caller passes the
// colour; nothing in here looks at the theme. The number of bars is fixed
// once built; update_bar_graph() changes their heights.
lv_obj_t* build_bar_graph(lv_obj_t* parent,
                          const uint8_t* values,
                          std::size_t count,
                          int32_t bar_width,
                          int32_t gap,
                          int32_t height,
                          lv_color_t color);

// Shows new values on a graph build_bar_graph() made: each bar takes the
// height and place a graph built fresh with `values` would give it. Only the
// bars whose height changed are touched, so values that stayed the same
// redraw nothing. It never adds or deletes a bar: values past the graph's
// bars are ignored, and bars past `count` keep their height. Does nothing
// for a null graph or null values.
void update_bar_graph(lv_obj_t* graph, const uint8_t* values, std::size_t count);

} // namespace view::widgets
