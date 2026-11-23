/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "theme.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace screen {

// What a home layout draws with. It is made fresh for every call and holds
// references only, so a layout must not keep it.
struct HomeContext {
    lv_obj_t* content;            // 320x140, no layout of its own
    const view::Theme& theme;
    const model::HomeData& data;
    const char* app_name;
};

// One way of drawing the home page; each theme has its own (see
// HomeScreen). A layout places plain objects, labels and lines at fixed
// positions, styled from context.theme, with every value taken from
// context.data. It keeps pointers to the objects select() and update()
// change, and the text each label of update() shows; LVGL owns the
// objects, and they go when the page goes.
//
// A layout has two views: with an access point (HomeData::ap_active) the
// router view, with the access point and its clients; without one the link
// view, which shows the uplink's own details in that space. When a new
// block differs from the shown one in more than the values of
// home_values_only(), HomeScreen drops the layout and its objects and
// builds a new one, so a layout never has to change from one view to the
// other.
class HomeLayout {
public:
    virtual ~HomeLayout() = default;

    // Builds the page into context.content.
    virtual void build(const HomeContext& context) = 0;

    // Marks client `index` as the selected one. Called once right after
    // build() and again whenever the selection changes; it may only restyle,
    // move and retext objects build() made. It does nothing in a view
    // without clients.
    virtual void select(const HomeContext& context, int32_t index) = 0;

    // Shows the values of context.data. Called once right after build(),
    // when nothing has changed yet, and again whenever only the values of
    // home_values_only() changed while the page is open. It must show every
    // one of those values the layout draws, in both views, so a page
    // updated in place looks exactly like one built fresh. It may only
    // change the text and the style of objects build() made, and move them
    // (a caption may follow its number); it never creates or deletes an
    // object, and it sets a text only when that text really changed (see
    // set_text_if_changed()), so nothing is redrawn for a value that stayed
    // the same.
    virtual void update(const HomeContext& context) = 0;
};

// True when `left` and `right` list the same devices in the same order:
// the same hardware address, name, address and new mark at each place. Their
// signal, rate and time may differ.
inline bool same_clients(const std::vector<model::HomeClient>& left,
                         const std::vector<model::HomeClient>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& a = left[index];
        const auto& b = right[index];
        if (a.hardware_address != b.hardware_address || a.name != b.name || a.address != b.address ||
            a.is_new != b.is_new) {
            return false;
        }
    }
    return true;
}

// True when `next` differs from `shown`, the block a layout shows, only in
// the values update() shows in place: the clock, the battery, the uplink's
// interface, address and link speed, the router's address, the Wi-Fi
// network's name, security and signal, the router view's uptime, the two
// rates with their histories, the clients' signal, rate and time while the
// same devices are listed (same_clients()), the Status page's details
// (HomeData::details), the Wi-Fi page's list (HomeData::wifi_scan), the
// switch between modes (HomeData::mode_switch)
// and the firewall's failures (HomeData::block_failures and
// block_message); the home page shows none of the last four, nor which
// device is blocked (HomeClient::blocked, which same_clients() ignores). Every other difference (the kind of link, the access point,
// the mode, another list of clients, the sample mark, the internet state, the offline
// state and any field added later) means the page is built again. The
// internet and offline states change seldom, so a layout shows them in
// build() only. It copies `shown`, takes over exactly those
// values from `next` and compares the copy with `next`, so a new field
// counts as "build again" by itself.
inline bool home_values_only(const model::HomeData& shown, const model::HomeData& next) {
    model::HomeData copy = shown;
    copy.clock_minutes = next.clock_minutes;
    copy.battery_percent = next.battery_percent;
    copy.battery_state = next.battery_state;
    copy.uplink_name = next.uplink_name;
    copy.uplink_address = next.uplink_address;
    copy.uplink_speed_mbps = next.uplink_speed_mbps;
    copy.gateway_address = next.gateway_address;
    copy.wifi_name = next.wifi_name;
    copy.wifi_security = next.wifi_security;
    copy.wifi_signal_dbm = next.wifi_signal_dbm;
    copy.uptime_seconds = next.uptime_seconds;
    copy.down_kbps = next.down_kbps;
    copy.up_kbps = next.up_kbps;
    copy.down_history = next.down_history;
    copy.up_history = next.up_history;
    if (same_clients(shown.clients, next.clients)) {
        copy.clients = next.clients;
    }
    // The Status page's values: the home page shows none of them, so they
    // never build it again (update() then finds nothing to retext).
    copy.details = next.details;
    copy.wifi_scan = next.wifi_scan;
    copy.mode_switch = next.mode_switch;
    copy.block_failures = next.block_failures;
    copy.block_message = next.block_message;
    return copy == next;
}

// Small helpers the layouts share.

// Gives `label` the text `text`, but only when it differs from `shown`, the
// text the layout last gave it; `shown` then becomes `text`. Returns true
// when the text was set. Does nothing and returns false for a null label.
// It compares with the layout's own copy and not with the label's text,
// because LVGL rewrites the text a label holds when it ends it in dots.
inline bool set_text_if_changed(lv_obj_t* label, std::string& shown, const std::string& text) {
    if (!label || shown == text) {
        return false;
    }
    shown = text;
    lv_label_set_text(label, shown.c_str());
    return true;
}

// Gives the battery's `label` the theme's warning colour while the battery
// is low (model::battery_low()) and `normal` otherwise. `shown_low` is the
// layout's copy of what the label shows, false for a new label in `normal`;
// only a change restyles, because the text alone may stay the same ("15%"
// both idle and discharging).
inline void set_battery_color(lv_obj_t* label,
                              bool& shown_low,
                              const model::HomeData& data,
                              const view::Theme& theme,
                              lv_color_t normal) {
    const bool low = model::battery_low(data.battery_percent, data.battery_state);
    if (!label || low == shown_low) {
        return;
    }
    shown_low = low;
    lv_obj_set_style_text_color(label, low ? theme.colors.warning : normal, 0);
}

// Makes `label` one line of a fixed width: longer text ends in dots instead
// of wrapping onto a second line.
inline void fix_width(lv_obj_t* label, int32_t width, const lv_font_t* font) {
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

// How far below the top of its label a font's baseline lies.
inline int32_t baseline_from_top(const lv_font_t* font) {
    return lv_font_get_line_height(font) - font->base_line;
}

// The big-type gauge of the Instrument theme.
std::unique_ptr<HomeLayout> make_instrument_home_layout();

// The network map of the Constellation theme.
std::unique_ptr<HomeLayout> make_constellation_home_layout();

// The boxed panes of the Terminal theme.
std::unique_ptr<HomeLayout> make_terminal_home_layout();

} // namespace screen
