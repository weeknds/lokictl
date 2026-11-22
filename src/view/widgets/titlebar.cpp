/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "titlebar.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "asset_manager.h"
#include "home_data.h"
#include "style.h"
#include "theme.h"
#include "theme_engine.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

// Fixed widths keep the status icons from moving when the text changes.
// Every theme's Mono font fits "100%" and "21:11" in these.
constexpr int32_t kIconWidth    = 18;
constexpr int32_t kPercentWidth = 40;
constexpr int32_t kTimeWidth    = 42;

// The Wi-Fi icon's steps, by received signal in dBm.
constexpr int32_t kSignalHighDbm   = -60;
constexpr int32_t kSignalMediumDbm = -75;

constexpr int32_t kMinutesPerHour = 60;
constexpr int32_t kMinutesPerDay  = 24 * kMinutesPerHour;

// A signal that is not known shows the icon without bars.
const char* wifi_icon(int32_t signal_dbm) {
    if (signal_dbm == model::kSignalUnknown) {
        return ICON_WIFI_NONE;
    }
    if (signal_dbm >= kSignalHighDbm) {
        return ICON_WIFI_HIGH;
    }
    if (signal_dbm >= kSignalMediumDbm) {
        return ICON_WIFI_MEDIUM;
    }
    return ICON_WIFI_LOW;
}

const char* battery_icon(int32_t capacity_percent, model::BatteryState state) {
    if (state == model::BatteryState::Charging) {
        return ICON_BAT_CHARGING;
    }
    if (capacity_percent >= 95) {
        return ICON_BAT_FULL;
    }
    if (capacity_percent >= 70) {
        return ICON_BAT_HIGH;
    }
    if (capacity_percent >= 35) {
        return ICON_BAT_MEDIUM;
    }
    if (capacity_percent >= 10) {
        return ICON_BAT_LOW;
    }
    return ICON_BAT_EMPTY;
}

void configure_label(lv_obj_t* label,
                     const view::Theme& theme,
                     const lv_font_t* font,
                     int32_t width) {
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, theme.colors.text, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

// Changes the text only when it differs, so a value that stayed the same
// draws nothing.
void set_text_if_changed(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

// Changes the flag only when it differs, for the same reason.
void set_visible(lv_obj_t* obj, bool visible) {
    const bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (visible && hidden) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else if (!visible && !hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

TitleBar::TitleBar(lv_obj_t* parent,
                   viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   view::ThemeEngine& theme_engine)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets), theme_engine_(theme_engine) {}

// The observer goes with the bar's object, which the base class deletes.
TitleBar::~TitleBar() = default;

void TitleBar::build() {
    if (core_obj_) {
        return;
    }

    const auto& theme = theme_engine_.theme();

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kTitleBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    view::style::bar(core_obj_, theme, view::style::Edge::Bottom);

    auto* title = lv_label_create(core_obj_);
    lv_label_bind_text(title, view_model_.title_subject(), nullptr);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 142);
    view::style::text(title, theme, view::FontRole::Title);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    auto* status = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, 160, LV_PCT(100));
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 3, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    // Without the icon font the labels show the font's empty box rather than
    // nothing, which makes the missing file easy to notice.
    const lv_font_t* icon_font = assets_.load_font("Phosphor-Fill.ttf", 16);
    if (!icon_font) {
        icon_font = theme.font(view::FontRole::Body);
    }
    const lv_font_t* status_font = theme.font(view::FontRole::Mono);

    wifi_label_ = lv_label_create(status);
    configure_label(wifi_label_, theme, icon_font, kIconWidth);
    ethernet_label_ = lv_label_create(status);
    configure_label(ethernet_label_, theme, icon_font, kIconWidth);
    lv_label_set_text(ethernet_label_, ICON_ETHERNET);
    battery_icon_label_ = lv_label_create(status);
    configure_label(battery_icon_label_, theme, icon_font, kIconWidth);
    battery_percent_label_ = lv_label_create(status);
    configure_label(battery_percent_label_, theme, status_font, kPercentWidth);
    time_label_ = lv_label_create(status);
    configure_label(time_label_, theme, status_font, kTimeWidth);

    // LVGL calls the observer once right away, which shows the values.
    lv_subject_add_observer_obj(view_model_.home_data_subject(), data_observer, core_obj_, this);
}

void TitleBar::refresh() {
    const model::HomeData& data = view_model_.home();

    const bool wifi = data.uplink_kind == model::LinkKind::Wifi;
    if (wifi) {
        set_text_if_changed(wifi_label_, wifi_icon(data.wifi_signal_dbm));
    }
    set_visible(wifi_label_, wifi);
    set_visible(ethernet_label_, data.uplink_kind == model::LinkKind::Cable);

    const bool battery_known = data.battery_percent >= 0;
    if (battery_known) {
        const int32_t percent = std::min<int32_t>(data.battery_percent, 100);
        set_text_if_changed(battery_icon_label_, battery_icon(percent, data.battery_state));
        std::array<char, 8> text{};
        std::snprintf(text.data(), text.size(), "%d%%", static_cast<int>(percent));
        set_text_if_changed(battery_percent_label_, text.data());
    }
    set_visible(battery_icon_label_, battery_known);
    set_visible(battery_percent_label_, battery_known);

    // A low battery while discharging takes the warning colour; only a
    // change restyles, so an unchanged value draws nothing.
    const bool low = model::battery_low(data.battery_percent, data.battery_state);
    if (low != battery_low_shown_) {
        battery_low_shown_ = low;
        const auto& colors = theme_engine_.theme().colors;
        const lv_color_t color = low ? colors.warning : colors.text;
        lv_obj_set_style_text_color(battery_icon_label_, color, 0);
        lv_obj_set_style_text_color(battery_percent_label_, color, 0);
    }

    const int32_t minutes = std::clamp<int32_t>(data.clock_minutes, 0, kMinutesPerDay - 1);
    std::array<char, 8> clock{};
    std::snprintf(clock.data(),
                  clock.size(),
                  "%02d:%02d",
                  static_cast<int>(minutes / kMinutesPerHour),
                  static_cast<int>(minutes % kMinutesPerHour));
    set_text_if_changed(time_label_, clock.data());
}

void TitleBar::data_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* title_bar = static_cast<TitleBar*>(lv_observer_get_user_data(observer));
    if (title_bar && title_bar->time_label_) {
        title_bar->refresh();
    }
}

} // namespace view::widgets
