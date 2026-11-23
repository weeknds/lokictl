/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "command_bar_widget.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <cstdint>

namespace view::widgets {
namespace {

constexpr int32_t kCursorWidth = 2;
constexpr const char* kHint = "help, TAB completes, up recalls";

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

} // namespace

CommandBar::CommandBar(lv_obj_t* parent, view::ThemeEngine& theme_engine)
    : BaseWidgets(parent), theme_engine_(theme_engine) {}

void CommandBar::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    // A column at the bottom edge: the message line (only when there is a
    // message), then the input line over the soft-key bar. It takes clicks,
    // so the simulator's mouse can't press the soft keys underneath.
    core_obj_ = plain_object(parent_);
    lv_obj_set_size(core_obj_, view::kScreenWidth, LV_SIZE_CONTENT);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);

    message_box_ = plain_object(core_obj_);
    lv_obj_set_size(message_box_, LV_PCT(100), LV_SIZE_CONTENT);
    message_ = lv_label_create(message_box_);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(message_, LV_PCT(100));
    lv_obj_add_flag(message_box_, LV_OBJ_FLAG_HIDDEN);

    input_row_ = plain_object(core_obj_);
    lv_obj_set_size(input_row_, LV_PCT(100), view::kSoftKeyBarHeight);
    lv_obj_set_flex_flow(input_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    prompt_ = lv_label_create(input_row_);
    lv_label_set_text(prompt_, ":");

    line_ = lv_label_create(input_row_);
    lv_label_set_text(line_, "");

    cursor_ = plain_object(input_row_);
    lv_obj_set_width(cursor_, kCursorWidth);

    hint_ = lv_label_create(input_row_);
    lv_label_set_text(hint_, kHint);
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(hint_, 1);

    // LVGL calls the observer once right away, which applies the current
    // theme; it then runs again after every theme change.
    lv_subject_add_observer_obj(theme_engine_.subject(), theme_observer_cb, core_obj_, this);
    hide();
}

void CommandBar::show() {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }

    set_line("");
    set_message("", CommandBarTone::Choices);
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void CommandBar::hide() {
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool CommandBar::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void CommandBar::set_line(const std::string& line) {
    if (!line_) {
        return;
    }

    lv_label_set_text(line_, line.c_str());
    // The hint only fills an empty line.
    if (line.empty()) {
        lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    }
}

void CommandBar::set_message(const std::string& message, CommandBarTone tone) {
    if (!message_) {
        return;
    }

    tone_ = tone;
    lv_label_set_text(message_, message.c_str());
    if (message.empty()) {
        lv_obj_add_flag(message_box_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(message_box_, LV_OBJ_FLAG_HIDDEN);
    }
    apply_theme();
}

// Both lines look like the soft-key bar they cover (the ground colour with a
// rule on top); the input line's rule is in the accent colour, so an open
// bar stands out from the soft keys.
void CommandBar::apply_theme() {
    if (!input_row_) {
        return;
    }

    const auto& theme = theme_engine_.theme();
    const auto& colors = theme.colors;
    const auto& metrics = theme.metrics;
    const lv_font_t* line_font = theme.font(view::FontRole::Mono);

    view::style::bar(message_box_, theme, view::style::Edge::Top);
    lv_obj_set_style_pad_hor(message_box_, metrics.space_md, 0);
    lv_obj_set_style_pad_ver(message_box_, metrics.space_sm, 0);
    view::style::text(message_,
                      theme,
                      view::FontRole::Small,
                      tone_ == CommandBarTone::Error ? colors.error : colors.muted);

    view::style::bar(input_row_, theme, view::style::Edge::Top);
    lv_obj_set_style_border_color(input_row_, colors.accent, 0);
    lv_obj_set_style_pad_hor(input_row_, metrics.space_md, 0);
    lv_obj_set_style_pad_column(input_row_, 1, 0);

    view::style::text(prompt_, theme, view::FontRole::Mono, colors.accent);
    lv_obj_set_style_pad_right(prompt_, metrics.space_sm, 0);
    view::style::text(line_, theme, view::FontRole::Mono);

    lv_obj_set_height(cursor_, lv_font_get_line_height(line_font));
    lv_obj_set_style_bg_color(cursor_, colors.accent, 0);
    lv_obj_set_style_bg_opa(cursor_, LV_OPA_COVER, 0);

    view::style::text(hint_, theme, view::FontRole::Small, colors.muted);
    lv_obj_set_style_pad_left(hint_, metrics.space_sm, 0);
}

void CommandBar::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* bar = static_cast<CommandBar*>(lv_observer_get_user_data(observer));
    if (bar) {
        bar->apply_theme();
    }
}

} // namespace view::widgets
