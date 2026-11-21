/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "toast.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <algorithm>
#include <cstdio>

namespace view::widgets {

Toast::Toast(lv_obj_t* parent, view::ThemeEngine& theme_engine, ToastConfig config)
    : BaseWidgets(parent), theme_engine_(theme_engine), config_(config) {}

Toast::~Toast() {
    if (hide_timer_) {
        lv_timer_delete(hide_timer_);
        hide_timer_ = nullptr;
    }
}

void Toast::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, config_.min_width, config_.min_height);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, -config_.bottom_offset);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);

    label_ = lv_label_create(core_obj_);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(label_, true);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_);

    // LVGL calls the observer once right away, which applies the current
    // theme; it then runs again after every theme change.
    lv_subject_add_observer_obj(theme_engine_.subject(), theme_observer_cb, core_obj_, this);
    hide();
}

void Toast::show(const std::string& message, ToastTone tone, uint32_t duration_ms) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_ || !label_) {
        return;
    }

    prefix_ = message;
    highlighted_.clear();
    suffix_.clear();
    tone_ = tone;
    scroll_ = false;
    show_current_message(duration_ms, false);
}

void Toast::show_highlighted(const std::string& prefix,
                             const std::string& highlighted,
                             const std::string& suffix,
                             ToastTone tone,
                             uint32_t duration_ms,
                             bool scroll) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_ || !label_) {
        return;
    }

    prefix_ = prefix;
    highlighted_ = highlighted;
    suffix_ = suffix;
    tone_ = tone;
    scroll_ = scroll;
    show_current_message(duration_ms, false);
}

void Toast::show_current_message(uint32_t duration_ms, bool persistent) {
    update_size();
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);

    if (hide_timer_) {
        lv_timer_pause(hide_timer_);
    }
    if (persistent) {
        return;
    }
    const uint32_t timeout = duration_ms ? duration_ms : config_.duration_ms;
    if (timeout == 0) {
        return;
    }
    if (!hide_timer_) {
        hide_timer_ = lv_timer_create(hide_timer_cb, timeout, this);
        if (!hide_timer_) {
            return;
        }
        lv_timer_set_auto_delete(hide_timer_, false);
    }
    lv_timer_set_period(hide_timer_, timeout);
    lv_timer_set_repeat_count(hide_timer_, 1);
    lv_timer_resume(hide_timer_);
    lv_timer_reset(hide_timer_);
}

void Toast::show_persistent(const std::string& message, ToastTone tone) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_ || !label_) {
        return;
    }

    prefix_ = message;
    highlighted_.clear();
    suffix_.clear();
    tone_ = tone;
    scroll_ = false;
    show_current_message(0, true);
}

void Toast::show_persistent_highlighted(const std::string& prefix,
                                        const std::string& highlighted,
                                        const std::string& suffix,
                                        ToastTone tone) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_ || !label_) {
        return;
    }

    prefix_ = prefix;
    highlighted_ = highlighted;
    suffix_ = suffix;
    tone_ = tone;
    scroll_ = false;
    show_current_message(0, true);
}

void Toast::hide() {
    if (hide_timer_) {
        lv_timer_pause(hide_timer_);
    }
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool Toast::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void Toast::update_label_text() {
    if (!label_) {
        return;
    }
    if (highlighted_.empty()) {
        lv_label_set_text(label_, (prefix_ + suffix_).c_str());
        return;
    }

    const auto& colors = theme_engine_.theme().colors;
    lv_color_t highlight_color = colors.accent;
    if (tone_ == ToastTone::Success) {
        highlight_color = colors.success;
    }
    else if (tone_ == ToastTone::Warning) {
        highlight_color = colors.warning;
    }
    else if (tone_ == ToastTone::Error) {
        highlight_color = colors.error;
    }

    // LVGL's recolour command: "#rrggbb text#".
    char color_hex[8]{};
    std::snprintf(color_hex,
                  sizeof(color_hex),
                  "%02x%02x%02x",
                  static_cast<unsigned int>(highlight_color.red),
                  static_cast<unsigned int>(highlight_color.green),
                  static_cast<unsigned int>(highlight_color.blue));
    const std::string text =
        prefix_ + "#" + color_hex + " " + highlighted_ + "#" + suffix_;
    lv_label_set_text(label_, text.c_str());
}

void Toast::update_size() {
    if (!core_obj_ || !label_ || !font_) {
        return;
    }

    const int32_t max_width = std::max(config_.min_width, config_.max_width);
    const int32_t content_max_width =
        std::max<int32_t>(1, max_width - config_.horizontal_padding * 2);
    const std::string plain_text = prefix_ + highlighted_ + suffix_;

    lv_point_t text_size{};
    lv_text_get_size(&text_size,
                     plain_text.c_str(),
                     font_,
                     0,
                     0,
                     content_max_width,
                     LV_TEXT_FLAG_NONE);

    const int32_t label_width = scroll_
                                    ? content_max_width
                                    : std::clamp<int32_t>(text_size.x, 1, content_max_width);
    const int32_t toast_width = std::clamp<int32_t>(
        label_width + config_.horizontal_padding * 2,
        config_.min_width,
        max_width);
    const int32_t text_height = scroll_ ? lv_font_get_line_height(font_) : text_size.y;
    const int32_t toast_height =
        std::max<int32_t>(config_.min_height, text_height + config_.vertical_padding * 2);

    lv_obj_set_width(label_, label_width);
    lv_obj_set_height(label_, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label_,
                           scroll_ ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_WRAP);
    lv_obj_set_style_anim_duration(label_, scroll_ ? lv_anim_speed(22) : 0, 0);
    lv_obj_set_size(core_obj_, toast_width, toast_height);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, -config_.bottom_offset);
    update_label_text();
    lv_obj_update_layout(label_);
    lv_obj_center(label_);
}

void Toast::apply_theme() {
    if (!core_obj_ || !label_) {
        return;
    }

    const auto& theme = theme_engine_.theme();
    view::style::popup(core_obj_, theme);
    // The font decides the toast's size, so the size is worked out again.
    font_ = theme.font(view::FontRole::Body);
    lv_obj_set_style_text_font(label_, font_, 0);
    lv_obj_set_style_text_color(label_, theme.colors.text, 0);
    update_size();
}

void Toast::hide_timer_cb(lv_timer_t* timer) {
    auto* toast = static_cast<Toast*>(lv_timer_get_user_data(timer));
    if (toast) {
        toast->hide();
    }
}

void Toast::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* toast = static_cast<Toast*>(lv_observer_get_user_data(observer));
    if (toast) {
        toast->apply_theme();
    }
}

} // namespace view::widgets
