/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The home page of the Constellation theme: a network map with a panel.
// - The map (x 0-203): the internet at the top, joined by the uplink line to
//   this device in the middle, two faint rings around the device, the
//   connected clients below it, each joined to the device by a thin line,
//   and a few dim dots as stars. The selected client gets two brackets
//   around its dot and its name in the bold font.
// - The panel (x 204-319, behind a 1 px line): clock and battery, the mode,
//   the traffic, the client count and the uptime, and under a rule the
//   selected client's name, signal and address.
// Every shape and label sits at a fixed position. The links between the
// device and the clients are solid, not dashed: LVGL's software renderer
// only dashes horizontal and vertical lines.

#include "base_viewmodel.h"
#include "home_format.h"
#include "home_layout.h"
#include "style.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>

namespace screen {
namespace {

// --- The map. Shapes are centred on the device; even-sized ones on the
// point between pixels 99 and 100 (x) and 61 and 62 (y).

// The internet: a square at the top with a caption beside it.
constexpr int32_t kInternetX      = 95;
constexpr int32_t kInternetY      = 4;
constexpr int32_t kInternetSize   = 10;
constexpr int32_t kInternetLabelX = 111;
constexpr int32_t kInternetLabelY = 1;

// The uplink: a 2 px line from the square down to the device (pixels 99-100,
// rows 14-54), with the uplink's name and speed beside it.
constexpr int32_t kUplinkLineX  = 100;
constexpr int32_t kUplinkTop    = 14;
constexpr int32_t kUplinkBottom = 55;
constexpr int32_t kUplinkWidth  = 2;
constexpr int32_t kUplinkLabelX = 106;
constexpr int32_t kUplinkLabelY = 26;

// This device: a ring with a dot in it, and the access point beside it.
constexpr int32_t kDeviceX        = 93;
constexpr int32_t kDeviceY        = 55;
constexpr int32_t kDeviceSize     = 14;
constexpr int32_t kDeviceBorder   = 2;
constexpr int32_t kDeviceDotSize  = 6;
constexpr int32_t kApLabelX       = 111;
constexpr int32_t kApLabelY       = 54;
constexpr int32_t kApLabelWidth   = 91;

// Where the links to the clients start; the device ring covers this point.
constexpr int32_t kLinkStartX = 100;
constexpr int32_t kLinkStartY = 62;

// Two faint rings around the device (radius 26 and 48).
constexpr int32_t kRingCentreX2 = 2 * kDeviceX + kDeviceSize; // twice the centre
constexpr int32_t kRingCentreY2 = 2 * kDeviceY + kDeviceSize;
constexpr int32_t kRingSizes[]  = {52, 96};

// Dim dots, decoration only, placed clear of every label and line.
struct Point {
    int32_t x;
    int32_t y;
};
constexpr Point kStars[] = {{26, 34}, {58, 76}, {14, 96}, {192, 36}, {190, 88}};
constexpr int32_t kStarSize = 3;

// The client slots, as the centres of their dots, left to right. With fewer
// than three clients, kSlotUse says which slots are used.
constexpr std::size_t kSlotCount = 3;
constexpr Point kSlots[kSlotCount] = {{40, 102}, {100, 112}, {160, 102}};
constexpr std::size_t kSlotUse[kSlotCount + 1][kSlotCount] = {
    {0, 0, 0}, // no clients
    {1, 0, 0}, // one client: the middle slot
    {0, 2, 0}, // two: left and right
    {0, 1, 2}, // three: all
};
constexpr int32_t kClientDotSize  = 7;
constexpr int32_t kNameWidth      = 76;
constexpr int32_t kNameGap        = 6;  // from the dot's centre to the top of a Small name

// The brackets around the selected client's dot: 4x16, 3 px from the dot.
constexpr int32_t kBracketWidth  = 4;
constexpr int32_t kBracketHeight = 16;
constexpr int32_t kBracketGap    = 3;

static_assert(viewmodel::kHomeClientRows <= static_cast<int32_t>(kSlotCount),
              "one map slot per client row");

// lv_line keeps a pointer to its points instead of copying them, so every
// line's points live here for the whole run of the app.
static const lv_point_precise_t kUplinkPoints[] = {
    {kUplinkLineX, kUplinkTop},
    {kUplinkLineX, kUplinkBottom},
};
static const lv_point_precise_t kLinkPoints[kSlotCount][2] = {
    {{kLinkStartX, kLinkStartY}, {kSlots[0].x, kSlots[0].y}},
    {{kLinkStartX, kLinkStartY}, {kSlots[1].x, kSlots[1].y}},
    {{kLinkStartX, kLinkStartY}, {kSlots[2].x, kSlots[2].y}},
};

// --- The panel. Text runs from x 210 to 316 (15 characters of Small).
constexpr int32_t kPanelX      = 204;
constexpr int32_t kPanelWidth  = view::kScreenWidth - kPanelX;
constexpr int32_t kPanelBorder = 1;    // the line on its left
constexpr int32_t kTextX       = 210;
constexpr int32_t kTextEnd     = 316;
constexpr int32_t kTextWidth   = kTextEnd - kTextX;
constexpr int32_t kStatusY     = 1;
constexpr int32_t kBatteryWidth = 35;  // right-aligned, room for "100%"
constexpr int32_t kModeCaptionY = 15;
constexpr int32_t kModeY       = 26;
constexpr int32_t kTrafficY    = 47;
constexpr int32_t kCountY      = 61;
constexpr int32_t kUptimeY     = 75;
constexpr int32_t kRuleY       = 94;
constexpr int32_t kSelectedY   = 99;
constexpr int32_t kClientY     = 111; // the Strong name; Small text on its baseline
constexpr int32_t kClientNameWidth = 54;
constexpr int32_t kAddressY    = 125;

constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;

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

// A round, filled shape of `size` at (x, y).
lv_obj_t* dot(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, lv_color_t color) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

// A line through `points`, drawn in the parent's coordinates. lv_line keeps
// the pointer, so `points` must be one of the static arrays above.
lv_obj_t* line(lv_obj_t* parent,
               const lv_point_precise_t* points,
               uint32_t count,
               int32_t width,
               lv_color_t color) {
    auto* obj = lv_line_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, 0, 0);
    lv_line_set_points(obj, points, count);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_color(obj, color, 0);
    lv_obj_set_style_line_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(obj, false, 0);
    return obj;
}

// Fills a label's box with the ground colour, so a ring or a line that
// crosses it does not run through its text.
void ground_fill(lv_obj_t* label, const view::Theme& theme) {
    lv_obj_set_style_bg_color(label, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
}

// The panel's clock and battery texts.
std::string clock_label_text(const model::HomeData& data) {
    return home::clock_text(data.clock_minutes);
}

std::string battery_label_text(const model::HomeData& data) {
    return home::battery_text(data.battery_percent, data.battery_state);
}

class ConstellationHomeLayout final : public HomeLayout {
public:
    void build(const HomeContext& context) override;
    void select(const HomeContext& context, int32_t index) override;
    void update(const HomeContext& context) override;

private:
    void build_map(const HomeContext& context);
    void build_panel(const HomeContext& context);

    // The map's clients, left to right.
    std::array<lv_obj_t*, kSlotCount> names_{};
    std::array<std::size_t, kSlotCount> slots_{};
    int32_t client_count_{0};
    lv_obj_t* left_bracket_{nullptr};
    lv_obj_t* right_bracket_{nullptr};

    // The panel's clock and battery, and the text each shows; update()
    // changes the text only.
    lv_obj_t* clock_{nullptr};
    std::string clock_shown_;
    lv_obj_t* battery_{nullptr};
    std::string battery_shown_;

    // The panel's lines about the selected client.
    lv_obj_t* selected_caption_{nullptr};
    lv_obj_t* selected_name_{nullptr};
    lv_obj_t* selected_signal_{nullptr};
    lv_obj_t* selected_address_{nullptr};
};

void ConstellationHomeLayout::build(const HomeContext& context) {
    client_count_ = static_cast<int32_t>(std::min<std::size_t>(
        context.data.clients.size(), static_cast<std::size_t>(viewmodel::kHomeClientRows)));
    for (std::size_t index = 0; index < kSlotCount; ++index) {
        slots_[index] = kSlotUse[static_cast<std::size_t>(client_count_)][index];
    }

    build_map(context);
    build_panel(context);
}

void ConstellationHomeLayout::build_map(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    lv_obj_t* map = context.content;

    // Drawn back to front: rings, stars, lines, shapes, then labels.
    for (const int32_t size : kRingSizes) {
        auto* ring = plain_object(map);
        lv_obj_set_pos(ring, (kRingCentreX2 - size) / 2, (kRingCentreY2 - size) / 2);
        lv_obj_set_size(ring, size, size);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(ring, colors.border_subtle, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    }

    for (const auto& star : kStars) {
        dot(map, star.x - kStarSize / 2, star.y - kStarSize / 2, kStarSize, colors.border_strong);
    }

    line(map, kUplinkPoints, static_cast<uint32_t>(std::size(kUplinkPoints)), kUplinkWidth,
         colors.accent);
    for (int32_t index = 0; index < client_count_; ++index) {
        line(map, kLinkPoints[slots_[static_cast<std::size_t>(index)]], 2, 1, colors.accent2);
    }

    auto* internet = plain_object(map);
    lv_obj_set_pos(internet, kInternetX, kInternetY);
    lv_obj_set_size(internet, kInternetSize, kInternetSize);
    lv_obj_set_style_bg_color(internet, colors.background, 0);
    lv_obj_set_style_bg_opa(internet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(internet, colors.text, 0);
    lv_obj_set_style_border_width(internet, 1, 0);
    lv_obj_set_style_border_opa(internet, LV_OPA_COVER, 0);

    auto* device = plain_object(map);
    lv_obj_set_pos(device, kDeviceX, kDeviceY);
    lv_obj_set_size(device, kDeviceSize, kDeviceSize);
    lv_obj_set_style_radius(device, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(device, colors.background, 0);
    lv_obj_set_style_bg_opa(device, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(device, colors.accent, 0);
    lv_obj_set_style_border_width(device, kDeviceBorder, 0);
    lv_obj_set_style_border_opa(device, LV_OPA_COVER, 0);
    const int32_t device_dot = kDeviceX + (kDeviceSize - kDeviceDotSize) / 2;
    dot(map, device_dot, kDeviceY + (kDeviceSize - kDeviceDotSize) / 2, kDeviceDotSize, colors.accent);

    for (int32_t index = 0; index < client_count_; ++index) {
        const auto& slot = kSlots[slots_[static_cast<std::size_t>(index)]];
        dot(map, slot.x - kClientDotSize / 2, slot.y - kClientDotSize / 2, kClientDotSize, colors.text);
    }

    // The brackets: open towards the dot, placed by select().
    left_bracket_ = plain_object(map);
    right_bracket_ = plain_object(map);
    for (auto* bracket : {left_bracket_, right_bracket_}) {
        lv_obj_set_size(bracket, kBracketWidth, kBracketHeight);
        lv_obj_set_style_border_color(bracket, colors.accent, 0);
        lv_obj_set_style_border_width(bracket, 1, 0);
        lv_obj_set_style_border_opa(bracket, LV_OPA_COVER, 0);
        lv_obj_add_flag(bracket, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_border_side(
        left_bracket_,
        static_cast<lv_border_side_t>(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM),
        0);
    lv_obj_set_style_border_side(
        right_bracket_,
        static_cast<lv_border_side_t>(LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM),
        0);

    if (data.sample) {
        auto* mark = plain_label(map, "");
        view::style::caption(mark, theme, "sample data");
        lv_obj_set_pos(mark, 4, 1);
    }

    auto* internet_label = plain_label(map, "");
    view::style::caption(internet_label, theme, "internet");
    lv_obj_set_pos(internet_label, kInternetLabelX, kInternetLabelY);

    const std::string uplink =
        data.uplink_name + " \u00b7 " + std::to_string(data.uplink_speed_mbps) + "M";
    auto* uplink_label = plain_label(map, uplink.c_str());
    view::style::text(uplink_label, theme, view::FontRole::Small, colors.muted);
    ground_fill(uplink_label, theme);
    lv_obj_set_pos(uplink_label, kUplinkLabelX, kUplinkLabelY);

    const std::string access_point = "AP " + data.ap_name;
    auto* ap_label = plain_label(map, access_point.c_str());
    view::style::text(ap_label, theme, view::FontRole::Small, colors.accent);
    fix_width(ap_label, kApLabelWidth, theme.font(view::FontRole::Small));
    ground_fill(ap_label, theme);
    lv_obj_set_pos(ap_label, kApLabelX, kApLabelY);

    // Client names, centred under their dots; select() sets font and colour.
    for (int32_t index = 0; index < client_count_; ++index) {
        const auto position = static_cast<std::size_t>(index);
        const auto& slot = kSlots[slots_[position]];
        auto* name = plain_label(map, data.clients[position].name.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_x(name, slot.x - kNameWidth / 2);
        names_[position] = name;
    }
}

void ConstellationHomeLayout::build_panel(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    auto* panel = plain_object(context.content);
    lv_obj_set_pos(panel, kPanelX, 0);
    lv_obj_set_size(panel, kPanelWidth, kContentHeight);
    lv_obj_set_style_border_color(panel, colors.border, 0);
    lv_obj_set_style_border_width(panel, kPanelBorder, 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, 0);

    // LVGL places children from the inside of the left border.
    const int32_t left = kTextX - kPanelX - kPanelBorder;

    clock_shown_ = clock_label_text(data);
    clock_ = plain_label(panel, clock_shown_.c_str());
    view::style::text(clock_, theme, view::FontRole::Small, colors.muted);
    lv_obj_set_pos(clock_, left, kStatusY);

    battery_shown_ = battery_label_text(data);
    battery_ = plain_label(panel, battery_shown_.c_str());
    view::style::text(battery_, theme, view::FontRole::Small, colors.muted);
    fix_width(battery_, kBatteryWidth, small);
    lv_obj_set_style_text_align(battery_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(battery_, left + kTextWidth - kBatteryWidth, kStatusY);

    auto* mode_caption = plain_label(panel, "");
    view::style::caption(mode_caption, theme, "mode");
    lv_obj_set_pos(mode_caption, left, kModeCaptionY);

    const lv_font_t* title = theme.font(view::FontRole::Title);
    const std::string word = home::upper_text(model::net_mode_name(data.mode));
    auto* mode = plain_label(panel, word.c_str());
    view::style::text(mode, theme, view::FontRole::Title, colors.accent);
    fix_width(mode, kTextWidth, title);
    lv_obj_set_pos(mode, left, kModeY);

    const std::string traffic = "\u2193" + home::rate_text(data.down_kbps) + " \u2191" +
                                home::rate_text(data.up_kbps) + " Mb/s";
    auto* rates = plain_label(panel, traffic.c_str());
    view::style::text(rates, theme, view::FontRole::Small);
    fix_width(rates, kTextWidth, small);
    lv_obj_set_pos(rates, left, kTrafficY);

    const std::string count = "clients " + std::to_string(data.clients.size());
    auto* clients = plain_label(panel, count.c_str());
    view::style::text(clients, theme, view::FontRole::Small);
    fix_width(clients, kTextWidth, small);
    lv_obj_set_pos(clients, left, kCountY);

    const std::string uptime = "up " + home::uptime_text(data.uptime_seconds);
    auto* up = plain_label(panel, uptime.c_str());
    view::style::text(up, theme, view::FontRole::Small, colors.muted);
    fix_width(up, kTextWidth, small);
    lv_obj_set_pos(up, left, kUptimeY);

    auto* rule = plain_object(panel);
    lv_obj_set_pos(rule, left, kRuleY);
    lv_obj_set_size(rule, kTextWidth, 1);
    lv_obj_set_style_bg_color(rule, colors.border, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    // The selected client; select() fills in the text.
    selected_caption_ = plain_label(panel, "");
    view::style::caption(selected_caption_, theme, "selected");
    lv_obj_set_pos(selected_caption_, left, kSelectedY);

    selected_name_ = plain_label(panel, "");
    view::style::text(selected_name_, theme, view::FontRole::Strong);
    fix_width(selected_name_, kClientNameWidth, strong);
    lv_obj_set_pos(selected_name_, left, kClientY);

    // The signal stands on the name's baseline, at the right end.
    selected_signal_ = plain_label(panel, "");
    view::style::text(selected_signal_, theme, view::FontRole::Small);
    fix_width(selected_signal_, kTextWidth - kClientNameWidth, small);
    lv_obj_set_style_text_align(selected_signal_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(selected_signal_,
                   left + kClientNameWidth,
                   kClientY + baseline_from_top(strong) - baseline_from_top(small));

    selected_address_ = plain_label(panel, "");
    view::style::text(selected_address_, theme, view::FontRole::Small, colors.accent2);
    fix_width(selected_address_, kTextWidth, small);
    lv_obj_set_pos(selected_address_, left, kAddressY);
}

void ConstellationHomeLayout::select(const HomeContext& context, int32_t index) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& clients = context.data.clients;
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    // The clients the map was built with, but never more than the list
    // holds now, so a shorter list is never read past its end.
    const auto listed = static_cast<int32_t>(
        std::min<std::size_t>(static_cast<std::size_t>(client_count_), clients.size()));

    const std::string caption = "selected " + home::count_text(index, listed);
    view::style::caption(selected_caption_, theme, caption.c_str());

    if (listed == 0) {
        lv_obj_add_flag(left_bracket_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_bracket_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(selected_name_, "");
        lv_label_set_text(selected_signal_, "");
        lv_label_set_text(selected_address_, "");
        return;
    }
    const int32_t selected = std::clamp<int32_t>(index, 0, listed - 1);

    // The map: the selected name in the bold font, the others in Small; the
    // name keeps its baseline either way.
    for (int32_t position = 0; position < client_count_; ++position) {
        const auto slot_index = static_cast<std::size_t>(position);
        const auto& slot = kSlots[slots_[slot_index]];
        const bool marked = position == selected;
        const lv_font_t* font = marked ? strong : small;
        auto* name = names_[slot_index];
        lv_obj_set_style_text_font(name, font, 0);
        lv_obj_set_style_text_color(name, marked ? colors.text : colors.muted, 0);
        lv_obj_set_size(name, kNameWidth, lv_font_get_line_height(font));
        lv_obj_set_y(name, slot.y + kNameGap + baseline_from_top(small) - baseline_from_top(font));
    }

    const auto& slot = kSlots[slots_[static_cast<std::size_t>(selected)]];
    const int32_t dot_left = slot.x - kClientDotSize / 2;
    const int32_t dot_right = dot_left + kClientDotSize;
    const int32_t bracket_top = slot.y - kBracketHeight / 2;
    lv_obj_set_pos(left_bracket_, dot_left - kBracketGap - kBracketWidth, bracket_top);
    lv_obj_set_pos(right_bracket_, dot_right + kBracketGap, bracket_top);
    lv_obj_clear_flag(left_bracket_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(right_bracket_, LV_OBJ_FLAG_HIDDEN);

    // The panel.
    const auto& client = clients[static_cast<std::size_t>(selected)];
    lv_label_set_text(selected_name_, client.name.c_str());
    lv_label_set_text(selected_signal_, home::signal_text(client.signal_dbm).c_str());
    lv_label_set_text(selected_address_, client.address.c_str());
}

// Only the panel's clock and battery follow the values for now. The clock
// keeps its place; the battery stays right-aligned in its box.
void ConstellationHomeLayout::update(const HomeContext& context) {
    set_text_if_changed(clock_, clock_shown_, clock_label_text(context.data));
    set_text_if_changed(battery_, battery_shown_, battery_label_text(context.data));
}

} // namespace

std::unique_ptr<HomeLayout> make_constellation_home_layout() {
    return std::make_unique<ConstellationHomeLayout>();
}

} // namespace screen
