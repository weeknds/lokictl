/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The home page of the Terminal theme: three boxed panes, with their titles
// set into the top border line. It has two views (see HomeLayout). The
// router view shows:
// - net (top left): a bar graph of the recent download and upload rates with
//   the current rate at the right end of each, then the uplink with its
//   address and link speed; the clock sits in the border.
// - mode (top right): the active mode with a dot, the access point and the
//   uptime; the battery sits in the border.
// - clients (bottom): a header row and one row per client (address, name,
//   signal, rate and a "new" mark); the selected row is filled. The sample
//   mark sits in the border.
// The link view, without an access point, keeps the panes and their places:
// - net: the same rows; without a link the uplink's row says "no network".
// - mode: the mode ("offline", muted and without the dot, when there is no
//   link), the access point "off" and the kind of link the device is on.
// - link (bottom, titled "no network" without a link): the network, the
//   Wi-Fi signal and security, the device's address and link speed, and the
//   router's address, as labels and values in two columns.
// The panes are Compact (see view::style::pane()): content starts 14 px under
// a pane's top and its rows step 14 px, or 15 px for the filled table rows.
// Every label sits at a fixed position; nothing is stacked by LVGL.

#include "bar_graph.h"
#include "base_viewmodel.h"
#include "home_format.h"
#include "home_layout.h"
#include "style.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace screen {
namespace {

// The three panes, as screen positions. They leave 2 px at the left and
// right edges and between the two top panes; the clients pane's border ends
// on row 136, so rows 137-139 above the soft-key bar stay empty.
constexpr int32_t kNetX           = 2;
constexpr int32_t kNetY           = 0;
constexpr int32_t kNetWidth       = 196;
constexpr int32_t kNetHeight      = 61;
constexpr int32_t kModeX          = 200;
constexpr int32_t kModeY          = 0;
constexpr int32_t kModeWidth      = 118;
constexpr int32_t kModeHeight     = 61;
constexpr int32_t kClientsX       = 2;
constexpr int32_t kClientsY       = 61;
constexpr int32_t kClientsWidth   = 316;
constexpr int32_t kClientsHeight  = 76;

// Rows inside a Compact pane, by the top of their labels.
constexpr int32_t kRowStep = 14;

// The net pane: an arrow, a bar graph and a rate per traffic row.
constexpr int32_t kGraphX         = 12;
constexpr int32_t kGraphBarWidth  = 4;
constexpr int32_t kGraphGap       = 1;
constexpr int32_t kGraphHeight    = 12;
constexpr int32_t kGraphTop       = 2;   // the bars end on row 13 of their text row
constexpr int32_t kRateWidth      = 42;  // right-aligned, room for "100.0M"
constexpr int32_t kInterfaceWidth = 35;  // 5 characters; a longer name ends in dots
constexpr int32_t kUplinkAddressX = 40;
constexpr int32_t kUplinkAddressWidth = 105; // 15 characters, the widest IPv4 address
constexpr int32_t kSpeedWidth     = 40;  // right-aligned, room for "1000M"

// The mode pane.
constexpr int32_t kModeDotSize    = 6;
constexpr int32_t kModeDotGap     = 5;
constexpr int32_t kModeValueX     = 21;
constexpr int32_t kModeValueWidth = 87;

// The clients table, in the content of its pane. The header labels sit 1 px
// above the content's top (their first row holds no ink), so the descender
// of "signal" keeps one empty row above the first client row's fill; the
// content has no other spare pixel. The only ink this puts in content row 1
// is the dot of the i in "signal", which lies clear of the pane's title and
// note, so nothing covers it.
constexpr int32_t kHeaderY        = -1;
constexpr int32_t kTableTop       = 14;
constexpr int32_t kTableRowStep   = 15;
constexpr int32_t kTableRowHeight = 15;
constexpr int32_t kAddressX       = 4;
constexpr int32_t kAddressWidth   = 86;
constexpr int32_t kNameX          = 95;
constexpr int32_t kNameWidth      = 80;
constexpr int32_t kSignalEnd      = 232; // right-aligned columns end here
constexpr int32_t kSignalWidth    = 56;
constexpr int32_t kTableRateEnd   = 270;
constexpr int32_t kTableRateWidth = 36;
constexpr int32_t kNewX           = 277;

// The link pane of the link view, in the content of its pane, which is 306
// px wide (the 316 px pane less a 1 px border and 4 px padding each side).
// Rows step 14 px from the content's top; a label and its value, twice.
constexpr int32_t kLinkContentWidth = 306;
constexpr int32_t kLinkLabelX       = 4;
constexpr int32_t kLinkValueX       = 60;
constexpr int32_t kLinkValueWidth   = 110;  // ends before the second column
constexpr int32_t kLinkLabel2X      = 176;
constexpr int32_t kLinkValue2X      = 218;
constexpr int32_t kLinkValue2Width  = kLinkContentWidth - kLinkValue2X;
constexpr int32_t kLinkNameWidth    = kLinkContentWidth - kLinkValueX; // a 32-character name fits

constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;

static_assert(kClientsY + kClientsHeight <= kContentHeight - 3,
              "rows 137-139, right above the soft-key bar, stay empty");
static_assert(kNetX + kNetWidth < kModeX && kModeX + kModeWidth <= view::kScreenWidth - 2,
              "the top panes sit side by side");
static_assert(kInterfaceWidth < kUplinkAddressX, "the interface ends before the address");
static_assert(kLinkValueX + kLinkValueWidth < kLinkLabel2X && kLinkLabel2X < kLinkValue2X,
              "the link pane's columns do not overlap");
static_assert(kNameX + kNameWidth <= kSignalEnd - kSignalWidth &&
                  kSignalEnd < kTableRateEnd - kTableRateWidth && kTableRateEnd < kNewX,
              "table columns do not overlap");

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    return label;
}

// A label in the Small font at a fixed position.
lv_obj_t* small_label(lv_obj_t* parent,
                      const view::Theme& theme,
                      const char* text,
                      lv_color_t color,
                      int32_t x,
                      int32_t y) {
    auto* label = plain_label(parent, text);
    view::style::text(label, theme, view::FontRole::Small, color);
    lv_obj_set_pos(label, x, y);
    return label;
}

// Makes `label` a box of `width` whose text ends at its right edge.
void align_text_right(lv_obj_t* label, int32_t width, const lv_font_t* font) {
    fix_width(label, width, font);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
}

// The texts of the two border notes: the clock (net pane) and the battery
// (mode pane).
std::string clock_note_text(const model::HomeData& data) {
    return home::clock_text(data.clock_minutes);
}

std::string battery_note_text(const model::HomeData& data) {
    return home::battery_text(data.battery_percent, data.battery_state);
}

// The uplink's row of the net pane: interface, address and link speed;
// "--" for each one that is not known.
std::string interface_text(const model::HomeData& data) {
    return home::text_or_unknown(data.uplink_name);
}

std::string uplink_address_text(const model::HomeData& data) {
    return home::text_or_unknown(data.uplink_address);
}

std::string uplink_speed_text(const model::HomeData& data) {
    return home::speed_text(data.uplink_speed_mbps);
}

// The values of the link pane; "--" for each one that is not known.
std::string network_value_text(const model::HomeData& data) {
    return home::network_name_text(data);
}

std::string signal_value_text(const model::HomeData& data) {
    return home::signal_text(data.wifi_signal_dbm);
}

std::string security_value_text(const model::HomeData& data) {
    return home::text_or_unknown(data.wifi_security);
}

std::string address_value_text(const model::HomeData& data) {
    return home::text_or_unknown(data.uplink_address);
}

std::string speed_value_text(const model::HomeData& data) {
    return home::speed_long_text(data.uplink_speed_mbps);
}

std::string router_value_text(const model::HomeData& data) {
    return home::text_or_unknown(data.gateway_address);
}

// The round dot after the mode word, drawn as a shape.
void mode_dot(lv_obj_t* parent, lv_obj_t* mode, const view::Theme& theme) {
    auto* dot = plain_object(parent);
    lv_obj_set_size(dot, kModeDotSize, kModeDotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, theme.colors.accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align_to(dot, mode, LV_ALIGN_OUT_RIGHT_MID, kModeDotGap, 0);
}

class TerminalHomeLayout final : public HomeLayout {
public:
    void build(const HomeContext& context) override;
    void select(const HomeContext& context, int32_t index) override;
    void update(const HomeContext& context) override;

private:
    void build_net(const HomeContext& context);
    void build_mode(const HomeContext& context);
    void build_link_mode(const HomeContext& context, lv_obj_t* parent);
    void build_clients(const HomeContext& context);
    void build_link(const HomeContext& context);

    // One row of the link pane: a muted label and, from kLinkValueX, its
    // value in a box of `width`. Returns the value's label.
    lv_obj_t* link_row(const HomeContext& context,
                       lv_obj_t* parent,
                       int32_t y,
                       const char* label,
                       const std::string& value,
                       lv_color_t color,
                       int32_t width);

    // The same for the second column, from kLinkLabel2X.
    lv_obj_t* link_row_second(const HomeContext& context,
                              lv_obj_t* parent,
                              int32_t y,
                              const char* label,
                              const std::string& value);

    // One traffic row of the net pane.
    void build_traffic_row(const HomeContext& context,
                           lv_obj_t* parent,
                           int32_t y,
                           const char* arrow,
                           const uint8_t* history,
                           int32_t kbps,
                           lv_color_t color);

    // The clock in the net pane's border and the battery in the mode
    // pane's, and the text each shows; update() changes the text only.
    lv_obj_t* clock_note_{nullptr};
    std::string clock_shown_;
    lv_obj_t* battery_note_{nullptr};
    std::string battery_shown_;

    // The uplink's interface, address and link speed in the net pane, and
    // the text each shows; update() changes the text only.
    lv_obj_t* interface_{nullptr};
    std::string interface_shown_;
    lv_obj_t* uplink_address_{nullptr};
    std::string uplink_address_shown_;
    lv_obj_t* uplink_speed_{nullptr};
    std::string uplink_speed_shown_;

    // One row of the clients table; select() restyles them.
    struct ClientRow {
        lv_obj_t* row{nullptr};
        lv_obj_t* address{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* signal{nullptr};
        lv_obj_t* rate{nullptr};
        lv_obj_t* fresh{nullptr}; // the "new" mark, or nullptr
    };
    std::array<ClientRow, viewmodel::kHomeClientRows> rows_{};
    int32_t row_count_{0};

    // True when build() drew the link view: no access point, no clients.
    bool link_view_{false};

    // The link pane's values that update() follows, and the text each
    // shows. They stay null in the router view, and so do the rows the link
    // pane leaves out for a kind of link.
    lv_obj_t* network_{nullptr};
    std::string network_shown_;
    lv_obj_t* signal_{nullptr};
    std::string signal_shown_;
    lv_obj_t* security_{nullptr};
    std::string security_shown_;
    lv_obj_t* address_{nullptr};
    std::string address_shown_;
    lv_obj_t* speed_{nullptr};
    std::string speed_shown_;
    lv_obj_t* router_{nullptr};
    std::string router_shown_;
};

void TerminalHomeLayout::build(const HomeContext& context) {
    link_view_ = !context.data.ap_active;

    build_net(context);
    build_mode(context);
    if (link_view_) {
        build_link(context);
    } else {
        build_clients(context);
    }
}

void TerminalHomeLayout::build_traffic_row(const HomeContext& context,
                                           lv_obj_t* parent,
                                           int32_t y,
                                           const char* arrow,
                                           const uint8_t* history,
                                           int32_t kbps,
                                           lv_color_t color) {
    const auto& theme = context.theme;

    small_label(parent, theme, arrow, color, 0, y);

    auto* graph = view::widgets::build_bar_graph(
        parent, history, model::kHomeHistoryLength, kGraphBarWidth, kGraphGap, kGraphHeight, color);
    lv_obj_set_pos(graph, kGraphX, y + kGraphTop);

    const std::string rate = home::rate_short_text(kbps);
    auto* value = plain_label(parent, rate.c_str());
    view::style::text(value, theme, view::FontRole::Small, color);
    align_text_right(value, kRateWidth, theme.font(view::FontRole::Small));
    lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, y);
}

void TerminalHomeLayout::build_net(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    const auto pane = view::style::pane(context.content, theme, "net", kNetWidth, kNetHeight,
                                        theme.colors.accent2, view::style::PaneDensity::Compact);
    lv_obj_set_pos(pane.root, kNetX, kNetY);
    clock_shown_ = clock_note_text(data);
    clock_note_ = view::style::pane_note(pane, theme, clock_shown_.c_str(), theme.colors.muted);

    build_traffic_row(context, pane.content, 0, "\u2193", data.down_history.data(), data.down_kbps,
                      theme.colors.accent2);
    build_traffic_row(context, pane.content, kRowStep, "\u2191", data.up_history.data(),
                      data.up_kbps, theme.colors.selection);

    const int32_t uplink_y = 2 * kRowStep;
    if (home::is_offline(data)) {
        // Without any network the row says so, in the muted colour.
        small_label(pane.content, theme, home::network_text(data).c_str(), theme.colors.muted, 0,
                    uplink_y);
        return;
    }

    interface_shown_ = interface_text(data);
    interface_ = small_label(pane.content, theme, interface_shown_.c_str(), theme.colors.text, 0,
                             uplink_y);
    fix_width(interface_, kInterfaceWidth, small);

    uplink_address_shown_ = uplink_address_text(data);
    uplink_address_ = small_label(pane.content, theme, uplink_address_shown_.c_str(),
                                  theme.colors.accent2, kUplinkAddressX, uplink_y);
    fix_width(uplink_address_, kUplinkAddressWidth, small);

    uplink_speed_shown_ = uplink_speed_text(data);
    uplink_speed_ = plain_label(pane.content, uplink_speed_shown_.c_str());
    view::style::text(uplink_speed_, theme, view::FontRole::Small);
    align_text_right(uplink_speed_, kSpeedWidth, small);
    lv_obj_align(uplink_speed_, LV_ALIGN_TOP_RIGHT, 0, uplink_y);
}

void TerminalHomeLayout::build_mode(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    const auto pane = view::style::pane(context.content, theme, "mode", kModeWidth, kModeHeight,
                                        theme.colors.accent, view::style::PaneDensity::Compact);
    lv_obj_set_pos(pane.root, kModeX, kModeY);
    battery_shown_ = battery_note_text(data);
    battery_note_ = view::style::pane_note(pane, theme, battery_shown_.c_str(), theme.colors.muted);
    if (link_view_) {
        build_link_mode(context, pane.content);
        return;
    }

    // The mode in bold with a round dot after it.
    const std::string word = home::lower_text(model::net_mode_name(data.mode));
    auto* mode = plain_label(pane.content, word.c_str());
    view::style::text(mode, theme, view::FontRole::Strong, theme.colors.accent);
    lv_obj_set_pos(mode, 0, 0);
    mode_dot(pane.content, mode, theme);

    small_label(pane.content, theme, "ap", theme.colors.muted, 0, kRowStep);
    auto* access_point = small_label(pane.content, theme, data.ap_name.c_str(), theme.colors.text,
                                     kModeValueX, kRowStep);
    fix_width(access_point, kModeValueWidth, small);

    small_label(pane.content, theme, "up", theme.colors.muted, 0, 2 * kRowStep);
    small_label(pane.content, theme, home::uptime_text(data.uptime_seconds).c_str(),
                theme.colors.text, kModeValueX, 2 * kRowStep);
}

// The mode pane of the link view: the mode, the access point "off" and the
// kind of link ("on wi-fi", "on cable", "on --" without a link). Without
// any network the mode word reads "offline", muted and without the dot.
void TerminalHomeLayout::build_link_mode(const HomeContext& context, lv_obj_t* parent) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const bool offline = home::is_offline(data);

    const std::string word = home::lower_text(home::mode_word(data));
    auto* mode = plain_label(parent, word.c_str());
    view::style::text(mode, theme, view::FontRole::Strong, offline ? colors.muted : colors.accent);
    lv_obj_set_pos(mode, 0, 0);
    if (!offline) {
        mode_dot(parent, mode, theme);
    }

    small_label(parent, theme, "ap", colors.muted, 0, kRowStep);
    small_label(parent, theme, "off", colors.text, kModeValueX, kRowStep);

    const std::string kind = home::text_or_unknown(home::link_kind_text(data.uplink_kind));
    small_label(parent, theme, "on", colors.muted, 0, 2 * kRowStep);
    small_label(parent, theme, kind.c_str(), colors.text, kModeValueX, 2 * kRowStep);
}

void TerminalHomeLayout::build_clients(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    const std::string title = "clients " + std::to_string(data.clients.size());
    const auto pane = view::style::pane(context.content, theme, title.c_str(), kClientsWidth,
                                        kClientsHeight, theme.colors.selection,
                                        view::style::PaneDensity::Compact);
    lv_obj_set_pos(pane.root, kClientsX, kClientsY);
    if (data.sample) {
        view::style::pane_note(pane, theme, "sample data", theme.colors.warning);
    }

    // The header row.
    auto* address = plain_label(pane.content, "");
    view::style::caption(address, theme, "address");
    lv_obj_set_pos(address, kAddressX, kHeaderY);

    auto* name = plain_label(pane.content, "");
    view::style::caption(name, theme, "name");
    lv_obj_set_pos(name, kNameX, kHeaderY);

    auto* signal = plain_label(pane.content, "");
    view::style::caption(signal, theme, "signal");
    align_text_right(signal, kSignalWidth, small);
    lv_obj_set_pos(signal, kSignalEnd - kSignalWidth, kHeaderY);

    auto* rate = plain_label(pane.content, "");
    view::style::caption(rate, theme, "rate");
    align_text_right(rate, kTableRateWidth, small);
    lv_obj_set_pos(rate, kTableRateEnd - kTableRateWidth, kHeaderY);

    // One row per client shown. Colours are set by select().
    row_count_ = static_cast<int32_t>(
        std::min<std::size_t>(data.clients.size(), static_cast<std::size_t>(viewmodel::kHomeClientRows)));
    for (int32_t index = 0; index < row_count_; ++index) {
        const auto& client = data.clients[static_cast<std::size_t>(index)];
        auto& slot = rows_[static_cast<std::size_t>(index)];

        slot.row = plain_object(pane.content);
        lv_obj_set_pos(slot.row, 0, kTableTop + index * kTableRowStep);
        lv_obj_set_size(slot.row, LV_PCT(100), kTableRowHeight);
        lv_obj_set_style_text_font(slot.row, small, 0);

        slot.address = plain_label(slot.row, client.address.c_str());
        fix_width(slot.address, kAddressWidth, small);
        lv_obj_set_pos(slot.address, kAddressX, 0);

        slot.name = plain_label(slot.row, client.name.c_str());
        fix_width(slot.name, kNameWidth, small);
        lv_obj_set_pos(slot.name, kNameX, 0);

        slot.signal = plain_label(slot.row, home::signal_text(client.signal_dbm).c_str());
        align_text_right(slot.signal, kSignalWidth, small);
        lv_obj_set_pos(slot.signal, kSignalEnd - kSignalWidth, 0);

        const std::string client_rate = home::rate_short_text(client.rate_kbps);
        slot.rate = plain_label(slot.row, client_rate.c_str());
        align_text_right(slot.rate, kTableRateWidth, small);
        lv_obj_set_pos(slot.rate, kTableRateEnd - kTableRateWidth, 0);

        slot.fresh = nullptr;
        if (client.is_new) {
            slot.fresh = plain_label(slot.row, "new");
            lv_obj_set_pos(slot.fresh, kNewX, 0);
        }
    }
}

lv_obj_t* TerminalHomeLayout::link_row(const HomeContext& context,
                                       lv_obj_t* parent,
                                       int32_t y,
                                       const char* label,
                                       const std::string& value,
                                       lv_color_t color,
                                       int32_t width) {
    const auto& theme = context.theme;
    small_label(parent, theme, label, theme.colors.muted, kLinkLabelX, y);
    auto* value_label = small_label(parent, theme, value.c_str(), color, kLinkValueX, y);
    fix_width(value_label, width, theme.font(view::FontRole::Small));
    return value_label;
}

lv_obj_t* TerminalHomeLayout::link_row_second(const HomeContext& context,
                                              lv_obj_t* parent,
                                              int32_t y,
                                              const char* label,
                                              const std::string& value) {
    const auto& theme = context.theme;
    small_label(parent, theme, label, theme.colors.muted, kLinkLabel2X, y);
    auto* value_label = small_label(parent, theme, value.c_str(), theme.colors.text, kLinkValue2X, y);
    fix_width(value_label, kLinkValue2Width, theme.font(view::FontRole::Small));
    return value_label;
}

// The link pane, in the clients pane's place: the network, on Wi-Fi its
// signal and security, the device's address with the link speed beside it,
// and the router's address. Without a link it is titled "no network" and
// shows the network, the address and the router as "--".
void TerminalHomeLayout::build_link(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const bool wifi = data.uplink_kind == model::LinkKind::Wifi;
    const bool linked = data.uplink_kind != model::LinkKind::None;

    const std::string title = home::is_offline(data) ? home::network_text(data) : std::string("link");
    const auto pane = view::style::pane(context.content, theme, title.c_str(), kClientsWidth,
                                        kClientsHeight, colors.selection,
                                        view::style::PaneDensity::Compact);
    lv_obj_set_pos(pane.root, kClientsX, kClientsY);
    if (data.sample) {
        view::style::pane_note(pane, theme, "sample data", colors.warning);
    }

    int32_t y = 0;
    network_shown_ = network_value_text(data);
    network_ = link_row(context, pane.content, y, "network", network_shown_, colors.text,
                        kLinkNameWidth);
    y += kRowStep;

    if (wifi) {
        signal_shown_ = signal_value_text(data);
        signal_ = link_row(context, pane.content, y, "signal", signal_shown_, colors.text,
                           kLinkValueWidth);
        security_shown_ = security_value_text(data);
        security_ = link_row_second(context, pane.content, y, "auth", security_shown_);
        y += kRowStep;
    }

    address_shown_ = address_value_text(data);
    address_ = link_row(context, pane.content, y, "address", address_shown_, colors.accent2,
                        kLinkValueWidth);
    if (linked) {
        speed_shown_ = speed_value_text(data);
        speed_ = link_row_second(context, pane.content, y, "speed", speed_shown_);
    }
    y += kRowStep;

    router_shown_ = router_value_text(data);
    router_ = link_row(context, pane.content, y, "router", router_shown_, colors.accent2,
                       kLinkValueWidth);
}

void TerminalHomeLayout::select(const HomeContext& context, int32_t index) {
    const auto& colors = context.theme.colors;
    // The link view has no clients to mark.
    if (link_view_ || row_count_ == 0) {
        return;
    }
    // Like the other layouts: an index past the rows marks the last row.
    const int32_t marked = std::clamp<int32_t>(index, 0, row_count_ - 1);

    for (int32_t row_index = 0; row_index < row_count_; ++row_index) {
        const auto& slot = rows_[static_cast<std::size_t>(row_index)];
        const bool selected = row_index == marked;

        // Only colours change: every row keeps its height and every label
        // its place, selected or not. This layout is Terminal's own, and
        // Terminal fills its selection, so the selected row is filled and
        // its text drawn in the colour that goes on the fill.
        lv_obj_set_style_bg_color(slot.row, colors.selection, 0);
        lv_obj_set_style_bg_opa(slot.row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        const lv_color_t mark = colors.on_selection;
        lv_obj_set_style_text_color(slot.address, selected ? mark : colors.accent2, 0);
        lv_obj_set_style_text_color(slot.name, selected ? mark : colors.text, 0);
        lv_obj_set_style_text_color(slot.signal, selected ? mark : colors.text, 0);
        lv_obj_set_style_text_color(slot.rate, selected ? mark : colors.text, 0);
        if (slot.fresh) {
            lv_obj_set_style_text_color(slot.fresh, selected ? mark : colors.accent, 0);
        }
    }
}

// The two border notes and the uplink's row of the net pane follow the
// values. Each note keeps its alignment to its pane's right end, so a wider
// text grows to the left and the clock stays where it is. The interface and
// the address keep their fixed boxes, and the link speed stays right-aligned
// in its own. In the link view the link pane's values follow too, each in
// its fixed box. The labels the built view does not have are null, and
// set_text_if_changed() skips them.
void TerminalHomeLayout::update(const HomeContext& context) {
    const auto& data = context.data;
    set_text_if_changed(clock_note_, clock_shown_, clock_note_text(data));
    set_text_if_changed(battery_note_, battery_shown_, battery_note_text(data));
    set_text_if_changed(interface_, interface_shown_, interface_text(data));
    set_text_if_changed(uplink_address_, uplink_address_shown_, uplink_address_text(data));
    set_text_if_changed(uplink_speed_, uplink_speed_shown_, uplink_speed_text(data));
    set_text_if_changed(network_, network_shown_, network_value_text(data));
    set_text_if_changed(signal_, signal_shown_, signal_value_text(data));
    set_text_if_changed(security_, security_shown_, security_value_text(data));
    set_text_if_changed(address_, address_shown_, address_value_text(data));
    set_text_if_changed(speed_, speed_shown_, speed_value_text(data));
    set_text_if_changed(router_, router_shown_, router_value_text(data));
}

} // namespace

std::unique_ptr<HomeLayout> make_terminal_home_layout() {
    return std::make_unique<TerminalHomeLayout>();
}

} // namespace screen
