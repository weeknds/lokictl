/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The home page of the Terminal theme: three boxed panes, with their titles
// set into the top border line.
// - net (top left): a bar graph of the recent download and upload rates with
//   the current rate at the right end of each, then the uplink with its
//   address and link speed; the clock sits in the border.
// - mode (top right): the active mode with a dot, the access point and the
//   uptime; the battery sits in the border.
// - clients (bottom): a header row and one row per client (address, name,
//   signal, rate and a "new" mark); the selected row is filled. The sample
//   mark sits in the border.
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
constexpr int32_t kUplinkAddressX = 40;
constexpr int32_t kUplinkAddressWidth = 104;
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

constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;

static_assert(kClientsY + kClientsHeight <= kContentHeight - 3,
              "rows 137-139, right above the soft-key bar, stay empty");
static_assert(kNetX + kNetWidth < kModeX && kModeX + kModeWidth <= view::kScreenWidth - 2,
              "the top panes sit side by side");
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

class TerminalHomeLayout final : public HomeLayout {
public:
    void build(const HomeContext& context) override;
    void select(const HomeContext& context, int32_t index) override;

private:
    void build_net(const HomeContext& context);
    void build_mode(const HomeContext& context);
    void build_clients(const HomeContext& context);

    // One traffic row of the net pane.
    void build_traffic_row(const HomeContext& context,
                           lv_obj_t* parent,
                           int32_t y,
                           const char* arrow,
                           const uint8_t* history,
                           int32_t kbps,
                           lv_color_t color);

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
};

void TerminalHomeLayout::build(const HomeContext& context) {
    build_net(context);
    build_mode(context);
    build_clients(context);
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

    const std::string rate = home::rate_text(kbps) + "M";
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
    view::style::pane_note(pane, theme, home::clock_text(data.clock_minutes).c_str(),
                           theme.colors.muted);

    build_traffic_row(context, pane.content, 0, "\u2193", data.down_history.data(), data.down_kbps,
                      theme.colors.accent2);
    build_traffic_row(context, pane.content, kRowStep, "\u2191", data.up_history.data(),
                      data.up_kbps, theme.colors.selection);

    const int32_t uplink_y = 2 * kRowStep;
    small_label(pane.content, theme, data.uplink_name.c_str(), theme.colors.text, 0, uplink_y);

    auto* address = small_label(pane.content, theme, data.uplink_address.c_str(),
                                theme.colors.accent2, kUplinkAddressX, uplink_y);
    fix_width(address, kUplinkAddressWidth, small);

    const std::string speed = std::to_string(data.uplink_speed_mbps) + "M";
    auto* link = plain_label(pane.content, speed.c_str());
    view::style::text(link, theme, view::FontRole::Small);
    align_text_right(link, kSpeedWidth, small);
    lv_obj_align(link, LV_ALIGN_TOP_RIGHT, 0, uplink_y);
}

void TerminalHomeLayout::build_mode(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    const auto pane = view::style::pane(context.content, theme, "mode", kModeWidth, kModeHeight,
                                        theme.colors.accent, view::style::PaneDensity::Compact);
    lv_obj_set_pos(pane.root, kModeX, kModeY);
    view::style::pane_note(pane, theme, home::percent_text(data.battery_percent).c_str(),
                           theme.colors.muted);

    // The mode in bold with a round dot after it, drawn as a shape.
    const std::string word = home::lower_text(model::net_mode_name(data.mode));
    auto* mode = plain_label(pane.content, word.c_str());
    view::style::text(mode, theme, view::FontRole::Strong, theme.colors.accent);
    lv_obj_set_pos(mode, 0, 0);

    auto* dot = plain_object(pane.content);
    lv_obj_set_size(dot, kModeDotSize, kModeDotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, theme.colors.accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align_to(dot, mode, LV_ALIGN_OUT_RIGHT_MID, kModeDotGap, 0);

    small_label(pane.content, theme, "ap", theme.colors.muted, 0, kRowStep);
    auto* access_point = small_label(pane.content, theme, data.ap_name.c_str(), theme.colors.text,
                                     kModeValueX, kRowStep);
    fix_width(access_point, kModeValueWidth, small);

    small_label(pane.content, theme, "up", theme.colors.muted, 0, 2 * kRowStep);
    small_label(pane.content, theme, home::uptime_text(data.uptime_seconds).c_str(),
                theme.colors.text, kModeValueX, 2 * kRowStep);
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

        const std::string client_rate = home::rate_text(client.rate_kbps) + "M";
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

void TerminalHomeLayout::select(const HomeContext& context, int32_t index) {
    const auto& colors = context.theme.colors;

    for (int32_t row_index = 0; row_index < row_count_; ++row_index) {
        const auto& slot = rows_[static_cast<std::size_t>(row_index)];
        const bool selected = row_index == index;

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

} // namespace

std::unique_ptr<HomeLayout> make_terminal_home_layout() {
    return std::make_unique<TerminalHomeLayout>();
}

} // namespace screen
