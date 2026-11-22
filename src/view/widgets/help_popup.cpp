/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "help_popup.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include "logger.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace view::widgets {
namespace {

// The panel is as tall as its text, which depends on the theme's fonts;
// with kHelpRowMax rows it stays well inside the 170 px screen in every
// theme.
constexpr int32_t kPanelWidth = view::kScreenWidth - 16;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

int32_t text_width(const std::string& text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

} // namespace

HelpPopup::HelpPopup(lv_obj_t* parent, view::ThemeEngine& theme_engine)
    : BaseWidgets(parent), theme_engine_(theme_engine) {}

void HelpPopup::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    // The dimmed backdrop over the whole screen.
    core_obj_ = plain_object(parent_);
    lv_obj_set_size(core_obj_, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(core_obj_, backdrop_clicked_cb, LV_EVENT_CLICKED, this);

    panel_ = plain_object(core_obj_);
    lv_obj_set_size(panel_, kPanelWidth, LV_SIZE_CONTENT);
    lv_obj_center(panel_);
    lv_obj_set_flex_flow(panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_CLICKABLE);

    header_ = plain_object(panel_);
    lv_obj_set_size(header_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    title_ = lv_label_create(header_);
    lv_label_set_text(title_, "Keyboard help");

    page_badge_ = lv_label_create(header_);

    rows_box_ = plain_object(panel_);
    lv_obj_set_size(rows_box_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rows_box_, LV_FLEX_FLOW_COLUMN);

    for (auto& widgets : rows_) {
        widgets.row = plain_object(rows_box_);
        lv_obj_set_size(widgets.row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(widgets.row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(widgets.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        widgets.key = lv_label_create(widgets.row);
        lv_obj_set_style_text_align(widgets.key, LV_TEXT_ALIGN_LEFT, 0);

        widgets.action = lv_label_create(widgets.row);
        lv_label_set_long_mode(widgets.action, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(widgets.action, 1);
    }

    footer_ = lv_label_create(panel_);
    lv_label_set_text(footer_, "HELP / ESC  Close   PRSC  Screenshot");
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(footer_, LV_PCT(100));

    // LVGL calls the observer once right away, which applies the current
    // theme; it then runs again after every theme change.
    lv_subject_add_observer_obj(theme_engine_.subject(), theme_observer_cb, core_obj_, this);
    hide();
}

void HelpPopup::show(const HelpContent& content) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }

    content_ = content;
    if (content_.rows.size() > kHelpRowMax) {
        LOG_WARN("help \"{}\": {} rows, only the first {} fit; the rest are left out",
                 content_.badge,
                 content_.rows.size(),
                 kHelpRowMax);
        content_.rows.resize(kHelpRowMax);
    }

    update_rows();
    update_key_column();
    lv_obj_move_foreground(core_obj_);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::hide() {
    if (core_obj_ && lv_obj_is_valid(core_obj_)) {
        lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool HelpPopup::visible() const {
    return core_obj_ && lv_obj_is_valid(core_obj_) &&
           !lv_obj_has_flag(core_obj_, LV_OBJ_FLAG_HIDDEN);
}

void HelpPopup::update_rows() {
    if (content_.badge.empty()) {
        lv_label_set_text(page_badge_, "");
        lv_obj_add_flag(page_badge_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_label_set_text(page_badge_, content_.badge.c_str());
        lv_obj_remove_flag(page_badge_, LV_OBJ_FLAG_HIDDEN);
    }

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (i >= content_.rows.size()) {
            lv_obj_add_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(rows_[i].key, content_.rows[i].keys.c_str());
        lv_label_set_text(rows_[i].action, content_.rows[i].action.c_str());
    }
}

// The key column is as wide as the widest key text shown, in the Strong font
// the keys are drawn in, plus a gap before the action column. Called again
// after a theme change, because the width depends on the theme's font.
void HelpPopup::update_key_column() {
    if (!panel_) {
        return;
    }

    const auto& theme = theme_engine_.theme();
    const lv_font_t* key_font = theme.font(view::FontRole::Strong);
    int32_t widest = 0;
    for (const auto& row : content_.rows) {
        widest = std::max(widest, text_width(row.keys, key_font));
    }

    const int32_t key_width = widest + theme.metrics.space_md;
    for (auto& row : rows_) {
        lv_obj_set_width(row.key, key_width);
    }
}

// Sets every colour, font and spacing from the active theme. The sizes that
// depend on font metrics (the key column, and through the flex layout every
// row and the panel height) follow from here as well.
void HelpPopup::apply_theme() {
    if (!panel_) {
        return;
    }

    const auto& theme = theme_engine_.theme();
    const auto& colors = theme.colors;
    const auto& metrics = theme.metrics;

    lv_obj_set_style_bg_color(core_obj_, colors.background, 0);
    lv_obj_set_style_bg_opa(core_obj_, LV_OPA_70, 0);

    view::style::popup(panel_, theme);
    lv_obj_set_style_pad_hor(panel_, metrics.space_lg, 0);
    lv_obj_set_style_pad_ver(panel_, metrics.space_md, 0);
    lv_obj_set_style_pad_row(panel_, metrics.space_sm, 0);

    view::style::text(title_, theme, view::FontRole::Title);

    view::style::text(page_badge_, theme, view::FontRole::Small, colors.accent);
    lv_obj_set_style_border_color(page_badge_, colors.accent, 0);
    lv_obj_set_style_border_width(page_badge_, metrics.border_width, 0);
    lv_obj_set_style_border_opa(page_badge_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(page_badge_, metrics.radius, 0);
    lv_obj_set_style_pad_hor(page_badge_, metrics.space_sm, 0);
    lv_obj_set_style_pad_ver(page_badge_, 0, 0);

    for (auto& row : rows_) {
        view::style::text(row.key, theme, view::FontRole::Strong, colors.accent);
        view::style::text(row.action, theme, view::FontRole::Body);
    }
    update_key_column();

    view::style::text(footer_, theme, view::FontRole::Small, colors.muted);
}

void HelpPopup::backdrop_clicked_cb(lv_event_t* event) {
    auto* popup = static_cast<HelpPopup*>(lv_event_get_user_data(event));
    if (popup && lv_event_get_target(event) == popup->core_obj_) {
        popup->hide();
    }
}

void HelpPopup::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* popup = static_cast<HelpPopup*>(lv_observer_get_user_data(observer));
    if (popup) {
        popup->apply_theme();
    }
}

} // namespace view::widgets
