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
        const int32_t value = std::min<int32_t>(values[index], 100);
        const int32_t bar_height = std::max<int32_t>(1, value * height / 100);

        auto* bar = plain_object(graph);
        lv_obj_set_pos(bar, index * (bar_width + gap), height - bar_height);
        lv_obj_set_size(bar, bar_width, bar_height);
        lv_obj_set_style_bg_color(bar, color, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
    return graph;
}

} // namespace view::widgets
