/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "help_popup.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace view::widgets {
namespace {

struct HelpRow {
    const char* key;
    const char* action;
};

constexpr std::array<HelpRow, 5> kAppleRows = {{
    {"HOLD ESC / 4", "Exit"},
    {"5", "Text size"},
    {"6", "Next theme"},
    {"7", "Version info"},
    {"8", "Open counter"},
}};

// Left and right work like Z and C on this page.
constexpr std::array<HelpRow, 3> kButterRows = {{
    {"ESC / 4", "Back to home"},
    {"Z / 5", "Decrease"},
    {"C / 7", "Increase"},
}};

// The panel is as tall as its text, which depends on the theme's fonts;
// with five rows it stays well inside the 170 px screen in every theme.
constexpr int32_t kPanelWidth = view::kScreenWidth - 16;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// Width of the widest key text of both pages in `font`, so the action column
// lines up in every theme.
int32_t key_column_width(const lv_font_t* font) {
    int32_t widest = 0;
    const auto measure = [&](const char* text) {
        lv_point_t size{};
        lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        widest = std::max(widest, size.x);
    };
    for (const auto& row : kAppleRows) {
        measure(row.key);
    }
    for (const auto& row : kButterRows) {
        measure(row.key);
    }
    return widest;
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

void HelpPopup::show(model::AppPage page) {
    if (!core_obj_) {
        build();
    }
    if (!core_obj_) {
        return;
    }
    update_rows(page);
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

void HelpPopup::update_rows(model::AppPage page) {
    const bool butter = page == model::AppPage::Butter;
    lv_label_set_text(page_badge_, butter ? "COUNTER" : "HOME");
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool row_visible = butter ? i < kButterRows.size() : i < kAppleRows.size();
        if (!row_visible) {
            lv_obj_add_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(rows_[i].row, LV_OBJ_FLAG_HIDDEN);
        const auto& row = butter ? kButterRows[i] : kAppleRows[i];
        lv_label_set_text(rows_[i].key, row.key);
        lv_label_set_text(rows_[i].action, row.action);
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

    const lv_font_t* key_font = theme.font(view::FontRole::Strong);
    const int32_t key_width = key_column_width(key_font) + metrics.space_md;
    for (auto& row : rows_) {
        view::style::text(row.key, theme, view::FontRole::Strong, colors.accent);
        lv_obj_set_width(row.key, key_width);
        view::style::text(row.action, theme, view::FontRole::Body);
    }

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
