/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "map_parts.h"

#include <initializer_list>

namespace view::widgets {
namespace {

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

} // namespace

lv_obj_t* map_dot(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, lv_color_t color) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t* map_line(lv_obj_t* parent,
                   const lv_point_precise_t* points,
                   uint32_t count,
                   int32_t width,
                   lv_color_t color) {
    auto* obj = lv_line_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, 0, 0);
    lv_line_set_points(obj, points, count);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_color(obj, color, 0);
    lv_obj_set_style_line_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(obj, false, 0);
    return obj;
}

lv_obj_t* map_upright(lv_obj_t* parent, int32_t x, int32_t top, int32_t bottom, int32_t width, lv_color_t color) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, top);
    lv_obj_set_size(obj, width, bottom - top + 1);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t* map_ring(lv_obj_t* parent,
                   int32_t x,
                   int32_t y,
                   int32_t size,
                   int32_t border,
                   lv_color_t color,
                   const Theme& theme) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t* map_orbit(lv_obj_t* parent, int32_t centre_x2, int32_t centre_y2, int32_t size, const Theme& theme) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, (centre_x2 - size) / 2, (centre_y2 - size) / 2);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(obj, theme.colors.border_subtle, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t* map_internet(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, const Theme& theme) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_bg_color(obj, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, theme.colors.text, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t* map_device(lv_obj_t* parent,
                     int32_t x,
                     int32_t y,
                     int32_t size,
                     int32_t border,
                     int32_t dot_size,
                     lv_color_t color,
                     const Theme& theme) {
    auto* ring = map_ring(parent, x, y, size, border, color, theme);
    map_dot(parent, x + (size - dot_size) / 2, y + (size - dot_size) / 2, dot_size, color);
    return ring;
}

lv_obj_t* map_star(lv_obj_t* parent, int32_t x, int32_t y, const Theme& theme) {
    return map_dot(parent, x - kMapStarSize / 2, y - kMapStarSize / 2, kMapStarSize, theme.colors.border_strong);
}

void map_ground_fill(lv_obj_t* label, const Theme& theme) {
    lv_obj_set_style_bg_color(label, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
}

MapBrackets make_map_brackets(lv_obj_t* parent, int32_t height, const Theme& theme) {
    MapBrackets brackets{plain_object(parent), plain_object(parent)};
    for (auto* bracket : {brackets.left, brackets.right}) {
        lv_obj_set_size(bracket, kMapBracketWidth, height);
        lv_obj_set_style_border_color(bracket, theme.colors.accent, 0);
        lv_obj_set_style_border_width(bracket, 1, 0);
        lv_obj_set_style_border_opa(bracket, LV_OPA_COVER, 0);
        lv_obj_add_flag(bracket, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_border_side(
        brackets.left,
        static_cast<lv_border_side_t>(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM),
        0);
    lv_obj_set_style_border_side(
        brackets.right,
        static_cast<lv_border_side_t>(LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM),
        0);
    return brackets;
}

void show_map_brackets(const MapBrackets& brackets, int32_t node_left, int32_t node_right, int32_t top, int32_t height) {
    if (!brackets.left || !brackets.right) {
        return;
    }
    lv_obj_set_height(brackets.left, height);
    lv_obj_set_height(brackets.right, height);
    lv_obj_set_pos(brackets.left, node_left - kMapBracketGap - kMapBracketWidth, top);
    lv_obj_set_pos(brackets.right, node_right + kMapBracketGap, top);
    lv_obj_clear_flag(brackets.left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(brackets.right, LV_OBJ_FLAG_HIDDEN);
}

void hide_map_brackets(const MapBrackets& brackets) {
    if (!brackets.left || !brackets.right) {
        return;
    }
    lv_obj_add_flag(brackets.left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(brackets.right, LV_OBJ_FLAG_HIDDEN);
}

} // namespace view::widgets
