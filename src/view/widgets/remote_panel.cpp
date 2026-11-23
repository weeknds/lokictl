/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_panel.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <cstdio>
#include <cstring>

namespace view::widgets {
namespace {

// The box covers the page between the title bar and the soft-key bar, so
// the soft keys (key 6 switches Remote view off) stay in sight.
constexpr int32_t kSideMargin = 8;
constexpr int32_t kVerticalMargin = 2;
constexpr int32_t kPanelWidth = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kPanelHeight =
    view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight - 2 * kVerticalMargin;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, "");
    return label;
}

// Sets the text only when it changed, so an unchanged panel draws nothing.
void set_text(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (hidden != lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// "482913" as "482 913", easier to read and to type.
std::string spaced_code(const std::string& code) {
    if (code.size() != 6) {
        return code;
    }
    return code.substr(0, 3) + " " + code.substr(3);
}

} // namespace

RemotePanel::RemotePanel(lv_obj_t* parent, view::ThemeEngine& theme_engine)
    : BaseWidgets(parent), theme_engine_(theme_engine) {}

void RemotePanel::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    core_obj_ = plain_object(parent_);
    lv_obj_set_pos(core_obj_, kSideMargin, view::kTitleBarHeight + kVerticalMargin);
    lv_obj_set_size(core_obj_, kPanelWidth, kPanelHeight);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    header_ = plain_object(core_obj_);
    lv_obj_set_size(header_, LV_PCT(100), LV_SIZE_CONTENT);
    title_ = plain_label(header_);
    lv_obj_align(title_, LV_ALIGN_LEFT_MID, 0, 0);
    time_left_ = plain_label(header_);
    lv_obj_align(time_left_, LV_ALIGN_RIGHT_MID, 0, 0);

    address_row_ = plain_object(core_obj_);
    lv_obj_set_size(address_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(address_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(address_row_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    open_ = plain_label(address_row_);
    lv_label_set_text(open_, "Open");
    url_ = plain_label(address_row_);

    code_ = plain_label(core_obj_);

    message_ = plain_label(core_obj_);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message_, LV_PCT(100));
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);

    // LVGL calls the observer once right away, which applies the current
    // theme; it then runs again after every theme change.
    lv_subject_add_observer_obj(theme_engine_.subject(), theme_observer_cb, core_obj_, this);
    hide();
}

void RemotePanel::show_code(const std::string& url, const std::string& code, int32_t seconds_left) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }

    if (seconds_left < 0) {
        seconds_left = 0;
    }
    char time_text[24];
    std::snprintf(time_text,
                  sizeof(time_text),
                  "%d:%02d left",
                  static_cast<int>(seconds_left / 60),
                  static_cast<int>(seconds_left % 60));

    set_text(time_left_, time_text);
    set_text(url_, url.c_str());
    set_text(code_, spaced_code(code).c_str());
    set_hidden(time_left_, false);
    set_hidden(address_row_, false);
    set_hidden(code_, false);
    set_hidden(message_, true);
    if (lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(core_obj_);
        lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RemotePanel::show_message(const std::string& message) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }

    set_text(message_, message.c_str());
    set_hidden(time_left_, true);
    set_hidden(address_row_, true);
    set_hidden(code_, true);
    set_hidden(message_, false);
    if (lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(core_obj_);
        lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RemotePanel::hide() {
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool RemotePanel::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) && !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void RemotePanel::apply_theme() {
    if (!core_obj_) {
        return;
    }

    const auto& theme = theme_engine_.theme();
    const auto& colors = theme.colors;
    const auto& metrics = theme.metrics;

    view::style::popup(core_obj_, theme);
    lv_obj_set_style_pad_hor(core_obj_, metrics.space_md, 0);
    lv_obj_set_style_pad_ver(core_obj_, metrics.space_sm, 0);

    view::style::caption(title_, theme, "Remote view");
    view::style::text(time_left_, theme, view::FontRole::Small, colors.muted);

    lv_obj_set_style_pad_column(address_row_, metrics.space_sm, 0);
    view::style::text(open_, theme, view::FontRole::Body);
    view::style::text(url_, theme, view::FontRole::Mono, colors.accent2);

    view::style::text(code_, theme, view::FontRole::Display, colors.accent);
    view::style::text(message_, theme, view::FontRole::Body, colors.warning);
}

void RemotePanel::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* panel = static_cast<RemotePanel*>(lv_observer_get_user_data(observer));
    if (panel) {
        panel->apply_theme();
    }
}

} // namespace view::widgets
