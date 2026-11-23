/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_mark.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

namespace view::widgets {
namespace {

// The tag's place, the same on every page. It covers the title bar's app
// name (its ink lies within x 9 to 76, y 7 to 19 in every theme) and ends
// above y 22, where the Home pages' content starts.
constexpr int32_t kLeft = 3;
constexpr int32_t kTop = 3;
constexpr int32_t kWidth = 81;
constexpr int32_t kHeight = 19;

} // namespace

RemoteMark::RemoteMark(lv_obj_t* parent, view::ThemeEngine& theme_engine)
    : BaseWidgets(parent), theme_engine_(theme_engine) {}

void RemoteMark::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(core_obj_, kLeft, kTop);
    lv_obj_set_size(core_obj_, kWidth, kHeight);

    label_ = lv_label_create(core_obj_);
    lv_obj_remove_style_all(label_);
    lv_obj_center(label_);

    // LVGL calls the observer once right away, which applies the current
    // theme; it then runs again after every theme change.
    lv_subject_add_observer_obj(theme_engine_.subject(), theme_observer_cb, core_obj_, this);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void RemoteMark::show() {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_ || !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void RemoteMark::hide() {
    if (core_obj_ && lv_obj_is_valid(core_obj_) && !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool RemoteMark::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) && !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void RemoteMark::apply_theme() {
    if (!core_obj_ || !label_) {
        return;
    }

    const auto& theme = theme_engine_.theme();
    const auto& colors = theme.colors;
    const auto& metrics = theme.metrics;

    // Filled with the accent in themes that fill their selection, otherwise
    // outlined in it like their selection. Either way the ground under the
    // tag is covered.
    const bool filled = metrics.selection_filled;
    lv_obj_set_style_bg_color(core_obj_, filled ? colors.accent : colors.background, 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(core_obj_, colors.accent, 0);
    lv_obj_set_style_border_width(core_obj_, filled ? 0 : metrics.border_width, 0);
    lv_obj_set_style_border_opa(core_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(core_obj_, metrics.radius, 0);

    view::style::text(label_, theme, view::FontRole::Strong, filled ? colors.on_accent : colors.accent);
    lv_label_set_text(label_, view::style::soft_key_text(theme, "Remote").c_str());
    lv_obj_center(label_);
}

void RemoteMark::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* mark = static_cast<RemoteMark*>(lv_observer_get_user_data(observer));
    if (mark) {
        mark->apply_theme();
    }
}

} // namespace view::widgets
