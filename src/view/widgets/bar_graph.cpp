/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "bar_graph.h"

#include <algorithm>

namespace view::widgets {
namespace {

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// The height of a bar for `value` in a graph `height` tall.
int32_t bar_height(uint8_t value, int32_t height) {
    return std::max<int32_t>(1, std::min<int32_t>(value, 100) * height / 100);
}

} // namespace

lv_obj_t* build_bar_graph(lv_obj_t* parent,
                          const uint8_t* values,
                          std::size_t count,
                          int32_t bar_width,
                          int32_t gap,
                          int32_t height,
                          lv_color_t color) {
    bar_width = std::max<int32_t>(bar_width, 1);
    gap = std::max<int32_t>(gap, 0);
    height = std::max<int32_t>(height, 1);
    if (values == nullptr) {
        count = 0;
    }

    const auto bars = static_cast<int32_t>(count);
    auto* graph = plain_object(parent);
    lv_obj_set_size(graph, bars > 0 ? bars * (bar_width + gap) - gap : 0, height);

    for (int32_t index = 0; index < bars; ++index) {
        const int32_t bar = bar_height(values[index], height);

        auto* bar_object = plain_object(graph);
        lv_obj_set_pos(bar_object, index * (bar_width + gap), height - bar);
        lv_obj_set_size(bar_object, bar_width, bar);
        lv_obj_set_style_bg_color(bar_object, color, 0);
        lv_obj_set_style_bg_opa(bar_object, LV_OPA_COVER, 0);
    }
    return graph;
}

void update_bar_graph(lv_obj_t* graph, const uint8_t* values, std::size_t count) {
    if (graph == nullptr || values == nullptr) {
        return;
    }
    // The heights build_bar_graph() set are styles, so they read back at once,
    // also before LVGL has laid the graph out.
    const int32_t height = lv_obj_get_style_height(graph, LV_PART_MAIN);
    const auto bars = std::min<std::size_t>(count, lv_obj_get_child_count(graph));
    for (std::size_t index = 0; index < bars; ++index) {
        auto* bar_object = lv_obj_get_child(graph, static_cast<int32_t>(index));
        const int32_t bar = bar_height(values[index], height);
        if (lv_obj_get_style_height(bar_object, LV_PART_MAIN) == bar) {
            continue;
        }
        lv_obj_set_y(bar_object, height - bar);
        lv_obj_set_height(bar_object, bar);
    }
}

} // namespace view::widgets
