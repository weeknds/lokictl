/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The home page of the Constellation theme: a network map with a panel. It
// has two views (see HomeLayout). The router view shows:
// - The map (x 0-203): the internet at the top, joined by the uplink line to
//   this device in the middle, two faint rings around the device, the
//   connected clients below it, each joined to the device by a thin line,
//   and a few dim dots as stars. The selected client gets two brackets
//   around its dot and its name in the bold font.
// - The panel (x 204-319, behind a 1 px line): clock and battery, the mode,
//   the traffic, the client count and the uptime, and under a rule the
//   selected client's name, signal and address.
// The link view, without an access point, shows:
// - The map: a chain down the middle, from the internet through the router
//   and the uplink to this device, with the link's interface and kind left
//   of the uplink, two faint rings around the device, a few stars, and
//   under it the network's name and its security and link speed. Without
//   a link only the internet and a muted device are left, and "NO NETWORK"
//   under them.
// - The panel: clock and battery, the mode ("OFFLINE", muted, without a
//   link), the traffic, the Wi-Fi signal or the link speed, and under a rule
//   the device's address and the router's address.
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
// The star on the uplink label's row, right of its text. A wide uplink text
// ("eth0 · 1000M") would run into it, so it is hidden while the text leaves
// less than kStarClearance px before it.
constexpr std::size_t kUplinkStar = 3;
constexpr int32_t kStarClearance = 3;

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

// --- The map of the link view: a chain down the middle (pixels 99-100).

// A 1 px muted line from the internet down to the router (rows 14-28).
constexpr int32_t kRouterLinkX      = 100;
constexpr int32_t kRouterLinkTop    = 14;
constexpr int32_t kRouterLinkBottom = 28;  // last row

// The router: a small ring with a caption beside it.
constexpr int32_t kRouterX      = 95;
constexpr int32_t kRouterY      = 29;
constexpr int32_t kRouterSize   = 10;
constexpr int32_t kRouterLabelX = 111;
constexpr int32_t kRouterLabelY = 26;

// The uplink: a 2 px line from the router down to the device (pixels
// 99-100, rows 39-54), with the link's interface and kind left of it,
// right-aligned in a box from x 1 to x 93.
constexpr int32_t kLinkLineX      = 99;
constexpr int32_t kLinkLineTop    = 39;
constexpr int32_t kLinkLineBottom = 54;  // last row
constexpr int32_t kLinkLabelX     = 1;
constexpr int32_t kLinkLabelY     = 39;
constexpr int32_t kLinkLabelWidth = 93;

// This device keeps its place; its caption sits right of it.
constexpr int32_t kDeviceLabelX = 111;
constexpr int32_t kDeviceLabelY = 54;

// Two faint rings around the device (radius 26 and 40); the outer one ends
// above the network's name.
constexpr int32_t kLinkRingSizes[] = {52, 80};

// Dim dots, placed clear of every label and line, and below row 13.
constexpr Point kLinkStars[] = {{22, 26}, {12, 72}, {46, 92}, {178, 22}, {170, 88}};

// Under the map, centred on x 100: the network in the bold font and a
// line on its security and speed; without a link "NO NETWORK".
constexpr int32_t kNetworkX      = 4;
constexpr int32_t kNetworkWidth  = 192;
constexpr int32_t kNetworkY      = 106;
constexpr int32_t kDetailsY      = 122;
constexpr int32_t kNoNetworkY    = 104;

static_assert(kRouterLinkBottom + 1 == kRouterY && kRouterY + kRouterSize == kLinkLineTop &&
                  kLinkLineBottom + 1 == kDeviceY,
              "the chain's parts meet without a gap");
static_assert(kLinkLabelX + kLinkLabelWidth < kLinkLineX, "the link's label ends left of the uplink");
static_assert(kNetworkX + kNetworkWidth / 2 == kUplinkLineX, "the network's lines are centred on the chain");
static_assert((kRingCentreY2 + kLinkRingSizes[1]) / 2 < kNetworkY,
              "the outer ring ends above the network's name");

static_assert(kUplinkStar < std::size(kStars) && kStars[kUplinkStar].x > kUplinkLabelX &&
                  kStars[kUplinkStar].y > kUplinkLabelY &&
                  kStars[kUplinkStar].y < kUplinkLabelY + 16,
              "the uplink's star sits on the uplink label's row, right of its start");
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

// The panel's rows of the link view.
constexpr int32_t kLinkValueY      = 61;  // the signal, or the link speed
constexpr int32_t kLinkRuleY       = 80;
constexpr int32_t kIpCaptionY      = 85;
constexpr int32_t kIpAddressY      = 97;
constexpr int32_t kRouterCaptionY  = 111;
constexpr int32_t kRouterAddressY  = 123;

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

// The caption beside the internet's square: "internet", or the note when
// the internet can not be reached ("no internet", "login page"), then in
// the warning colour.
void internet_caption(lv_obj_t* map, const view::Theme& theme, const model::HomeData& data) {
    const std::string note = home::internet_note(data);
    auto* label = plain_label(map, "");
    view::style::caption(label, theme, note.empty() ? "internet" : note.c_str());
    if (!note.empty()) {
        lv_obj_set_style_text_color(label, theme.colors.warning, 0);
    }
    lv_obj_set_pos(label, kInternetLabelX, kInternetLabelY);
}

// A filled rectangle from (x, top) to (x + width - 1, bottom), for a
// straight upright line of the map.
lv_obj_t* upright(lv_obj_t* parent, int32_t x, int32_t top, int32_t bottom, int32_t width,
                  lv_color_t color) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, top);
    lv_obj_set_size(obj, width, bottom - top + 1);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

// A ring of `size` at (x, y): a 1 px border, filled with the ground colour,
// so what lies behind it does not show through.
lv_obj_t* ring(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, int32_t border,
               lv_color_t color, const view::Theme& theme) {
    auto* obj = plain_object(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

// The panel's clock and battery texts.
std::string clock_label_text(const model::HomeData& data) {
    return home::clock_text(data.clock_minutes);
}

std::string battery_label_text(const model::HomeData& data) {
    return home::battery_text(data.battery_percent, data.battery_state);
}

// The uplink beside its line in the router view: interface and link speed,
// "eth0 · 100M"; "--" for either one when it is not known.
std::string uplink_label_text(const model::HomeData& data) {
    return home::join_parts(
        {home::text_or_unknown(data.uplink_name), home::speed_text(data.uplink_speed_mbps)});
}

// The panel's traffic line, in both views: "↓3.8 ↑0.9 Mb/s"; "--" for a
// rate that is not known. When that is wider than the panel's text in
// `font`, as two rates of 10 Mbit/s or more are, each number takes the
// unit's short form instead: "↓12.3M ↑10.2M".
std::string traffic_label_text(const model::HomeData& data, const lv_font_t* font) {
    std::string text = "\u2193" + home::rate_text(data.down_kbps) + " \u2191" +
                       home::rate_text(data.up_kbps) + " Mb/s";
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= kTextWidth) {
        return text;
    }
    return "\u2193" + home::rate_short_text(data.down_kbps) + " \u2191" +
           home::rate_short_text(data.up_kbps);
}

// The texts of the link view. build() and update() both take them from
// here, so a page updated in place shows what a fresh one shows.

// Under the map: the network's name, "wi-fi network" when it is not known,
// "wired network" on a cable.
std::string network_label_text(const model::HomeData& data) {
    return home::network_text(data);
}

// Under the network's name: on Wi-Fi the security and the link speed, on a
// cable the link speed; a part that is not known is left out.
std::string details_label_text(const model::HomeData& data) {
    const bool wifi = data.uplink_kind == model::LinkKind::Wifi;
    return home::join_parts(
        {wifi ? data.wifi_security : std::string(), home::speed_part_text(data.uplink_speed_mbps)});
}

// The panel's row under the traffic: the signal on Wi-Fi, "signal −57 dBm";
// the link speed otherwise, "link 100M"; "--" when it is not known.
std::string link_value_text(const model::HomeData& data) {
    if (data.uplink_kind == model::LinkKind::Wifi) {
        return "signal " + home::signal_text(data.wifi_signal_dbm);
    }
    return "link " + home::speed_text(data.uplink_speed_mbps);
}

std::string address_text(const model::HomeData& data) {
    return home::text_or_unknown(data.uplink_address);
}

std::string router_address_text(const model::HomeData& data) {
    return home::text_or_unknown(data.gateway_address);
}

class ConstellationHomeLayout final : public HomeLayout {
public:
    void build(const HomeContext& context) override;
    void select(const HomeContext& context, int32_t index) override;
    void update(const HomeContext& context) override;

private:
    void build_map(const HomeContext& context);
    void build_link_map(const HomeContext& context);
    void build_panel(const HomeContext& context);
    void build_client_rows(const HomeContext& context, lv_obj_t* panel);
    void build_link_rows(const HomeContext& context, lv_obj_t* panel);

    // Hides the star on the uplink label's row while the uplink's text
    // would come too close to it, and shows it otherwise.
    void show_uplink_star(const view::Theme& theme);

    // The map's clients, left to right.
    std::array<lv_obj_t*, kSlotCount> names_{};
    std::array<std::size_t, kSlotCount> slots_{};
    int32_t client_count_{0};
    lv_obj_t* left_bracket_{nullptr};
    lv_obj_t* right_bracket_{nullptr};

    // The uplink beside its line, and the text it shows; update() changes
    // the text, and shows or hides the star right of it.
    lv_obj_t* uplink_{nullptr};
    std::string uplink_shown_;
    lv_obj_t* uplink_star_{nullptr};

    // The panel's clock, battery and traffic line, and the text each shows;
    // update() changes the text only.
    lv_obj_t* clock_{nullptr};
    std::string clock_shown_;
    lv_obj_t* battery_{nullptr};
    std::string battery_shown_;
    bool battery_low_shown_{false};
    lv_obj_t* traffic_{nullptr};
    std::string traffic_shown_;

    // The panel's lines about the selected client.
    lv_obj_t* selected_caption_{nullptr};
    lv_obj_t* selected_name_{nullptr};
    lv_obj_t* selected_signal_{nullptr};
    lv_obj_t* selected_address_{nullptr};

    // True when build() drew the link view: no access point, no clients.
    bool link_view_{false};

    // The link view's labels that show values update() follows, and the
    // text each shows. They stay null in the router view, and so do those
    // of a part the link view leaves out without a link.
    lv_obj_t* link_label_{nullptr};
    std::string link_label_shown_;
    lv_obj_t* network_{nullptr};
    std::string network_shown_;
    lv_obj_t* details_{nullptr};
    std::string details_shown_;
    lv_obj_t* link_value_{nullptr};
    std::string link_value_shown_;
    lv_obj_t* address_{nullptr};
    std::string address_shown_;
    lv_obj_t* router_address_{nullptr};
    std::string router_address_shown_;
};

void ConstellationHomeLayout::build(const HomeContext& context) {
    link_view_ = !context.data.ap_active;
    if (link_view_) {
        build_link_map(context);
        build_panel(context);
        return;
    }

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

    for (std::size_t index = 0; index < std::size(kStars); ++index) {
        const auto& star = kStars[index];
        auto* obj = dot(map, star.x - kStarSize / 2, star.y - kStarSize / 2, kStarSize,
                        colors.border_strong);
        if (index == kUplinkStar) {
            uplink_star_ = obj;
        }
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

    internet_caption(map, theme, data);

    // Not a fixed box: a wider text grows to the right, into free space.
    uplink_shown_ = uplink_label_text(data);
    uplink_ = plain_label(map, uplink_shown_.c_str());
    view::style::text(uplink_, theme, view::FontRole::Small, colors.muted);
    ground_fill(uplink_, theme);
    lv_obj_set_pos(uplink_, kUplinkLabelX, kUplinkLabelY);
    show_uplink_star(theme);

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

void ConstellationHomeLayout::build_link_map(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);
    lv_obj_t* map = context.content;
    const bool linked = data.uplink_kind != model::LinkKind::None;

    // Drawn back to front: rings, stars, lines, shapes, then labels.
    for (const int32_t size : kLinkRingSizes) {
        auto* obj = plain_object(map);
        lv_obj_set_pos(obj, (kRingCentreX2 - size) / 2, (kRingCentreY2 - size) / 2);
        lv_obj_set_size(obj, size, size);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(obj, colors.border_subtle, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    }

    for (const auto& star : kLinkStars) {
        dot(map, star.x - kStarSize / 2, star.y - kStarSize / 2, kStarSize, colors.border_strong);
    }

    if (linked) {
        upright(map, kRouterLinkX, kRouterLinkTop, kRouterLinkBottom, 1, colors.muted);
        upright(map, kLinkLineX, kLinkLineTop, kLinkLineBottom, kUplinkWidth, colors.accent);
    }

    // The internet, as in the router view.
    auto* internet = plain_object(map);
    lv_obj_set_pos(internet, kInternetX, kInternetY);
    lv_obj_set_size(internet, kInternetSize, kInternetSize);
    lv_obj_set_style_bg_color(internet, colors.background, 0);
    lv_obj_set_style_bg_opa(internet, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(internet, colors.text, 0);
    lv_obj_set_style_border_width(internet, 1, 0);
    lv_obj_set_style_border_opa(internet, LV_OPA_COVER, 0);

    if (linked) {
        ring(map, kRouterX, kRouterY, kRouterSize, 1, colors.text, theme);
    }

    // This device, where the router view has it; muted without a link.
    const lv_color_t device_color = linked ? colors.accent : colors.muted;
    ring(map, kDeviceX, kDeviceY, kDeviceSize, kDeviceBorder, device_color, theme);
    const int32_t device_dot = kDeviceX + (kDeviceSize - kDeviceDotSize) / 2;
    dot(map, device_dot, kDeviceY + (kDeviceSize - kDeviceDotSize) / 2, kDeviceDotSize, device_color);

    if (data.sample) {
        auto* mark = plain_label(map, "");
        view::style::caption(mark, theme, "sample data");
        lv_obj_set_pos(mark, 4, 1);
    }

    internet_caption(map, theme, data);

    if (linked) {
        auto* router_label = plain_label(map, "");
        view::style::caption(router_label, theme, "router");
        ground_fill(router_label, theme);
        lv_obj_set_pos(router_label, kRouterLabelX, kRouterLabelY);

        // Right-aligned against the uplink, in a fixed box; a text too long
        // for it (a long interface name) ends in dots.
        link_label_shown_ = home::link_text(data);
        link_label_ = plain_label(map, link_label_shown_.c_str());
        view::style::text(link_label_, theme, view::FontRole::Small, colors.muted);
        fix_width(link_label_, kLinkLabelWidth, small);
        lv_obj_set_style_text_align(link_label_, LV_TEXT_ALIGN_RIGHT, 0);
        ground_fill(link_label_, theme);
        lv_obj_set_pos(link_label_, kLinkLabelX, kLinkLabelY);
    }

    auto* device_label = plain_label(map, "");
    view::style::caption(device_label, theme, "this device");
    ground_fill(device_label, theme);
    lv_obj_set_pos(device_label, kDeviceLabelX, kDeviceLabelY);

    if (!linked) {
        const std::string words = home::upper_text(home::network_text(data));
        auto* none = plain_label(map, words.c_str());
        view::style::text(none, theme, view::FontRole::Title);
        fix_width(none, kNetworkWidth, theme.font(view::FontRole::Title));
        lv_obj_set_style_text_align(none, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(none, kNetworkX, kNoNetworkY);
        return;
    }

    network_shown_ = network_label_text(data);
    network_ = plain_label(map, network_shown_.c_str());
    view::style::text(network_, theme, view::FontRole::Strong);
    fix_width(network_, kNetworkWidth, theme.font(view::FontRole::Strong));
    lv_obj_set_style_text_align(network_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(network_, kNetworkX, kNetworkY);

    // Always made, also while it is empty, so update() can fill it.
    details_shown_ = details_label_text(data);
    details_ = plain_label(map, details_shown_.c_str());
    view::style::text(details_, theme, view::FontRole::Small, colors.muted);
    fix_width(details_, kNetworkWidth, small);
    lv_obj_set_style_text_align(details_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(details_, kNetworkX, kDetailsY);
}

void ConstellationHomeLayout::show_uplink_star(const view::Theme& theme) {
    if (!uplink_star_) {
        return;
    }
    lv_point_t size{};
    lv_text_get_size(&size, uplink_shown_.c_str(), theme.font(view::FontRole::Small), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t star_left = kStars[kUplinkStar].x - kStarSize / 2;
    if (kUplinkLabelX + size.x + kStarClearance <= star_left) {
        lv_obj_clear_flag(uplink_star_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(uplink_star_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ConstellationHomeLayout::build_panel(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);

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
    set_battery_color(battery_, battery_low_shown_, data, theme, colors.muted);

    auto* mode_caption = plain_label(panel, "");
    view::style::caption(mode_caption, theme, "mode");
    lv_obj_set_pos(mode_caption, left, kModeCaptionY);

    // "OFFLINE" without any network, in the muted colour: a calm state, not
    // an alarm.
    const lv_font_t* title = theme.font(view::FontRole::Title);
    const std::string word = home::upper_text(home::mode_word(data));
    auto* mode = plain_label(panel, word.c_str());
    view::style::text(mode, theme, view::FontRole::Title,
                      home::is_offline(data) ? colors.muted : colors.accent);
    fix_width(mode, kTextWidth, title);
    lv_obj_set_pos(mode, left, kModeY);

    traffic_shown_ = traffic_label_text(data, small);
    traffic_ = plain_label(panel, traffic_shown_.c_str());
    view::style::text(traffic_, theme, view::FontRole::Small);
    fix_width(traffic_, kTextWidth, small);
    lv_obj_set_pos(traffic_, left, kTrafficY);

    if (link_view_) {
        build_link_rows(context, panel);
    } else {
        build_client_rows(context, panel);
    }
}

// The router view's panel under the traffic: the client count, the uptime
// and the selected client.
void ConstellationHomeLayout::build_client_rows(const HomeContext& context, lv_obj_t* panel) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);
    const int32_t left = kTextX - kPanelX - kPanelBorder;

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

// The link view's panel under the traffic: the signal or the link speed,
// and under a rule the device's address and the router's address.
void ConstellationHomeLayout::build_link_rows(const HomeContext& context, lv_obj_t* panel) {
    const auto& theme = context.theme;
    const auto& colors = theme.colors;
    const auto& data = context.data;
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const int32_t left = kTextX - kPanelX - kPanelBorder;

    link_value_shown_ = link_value_text(data);
    link_value_ = plain_label(panel, link_value_shown_.c_str());
    view::style::text(link_value_, theme, view::FontRole::Small);
    fix_width(link_value_, kTextWidth, small);
    lv_obj_set_pos(link_value_, left, kLinkValueY);

    auto* rule = plain_object(panel);
    lv_obj_set_pos(rule, left, kLinkRuleY);
    lv_obj_set_size(rule, kTextWidth, 1);
    lv_obj_set_style_bg_color(rule, colors.border, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    auto* ip_caption = plain_label(panel, "");
    view::style::caption(ip_caption, theme, "ip");
    lv_obj_set_pos(ip_caption, left, kIpCaptionY);

    address_shown_ = address_text(data);
    address_ = plain_label(panel, address_shown_.c_str());
    view::style::text(address_, theme, view::FontRole::Small, colors.accent2);
    fix_width(address_, kTextWidth, small);
    lv_obj_set_pos(address_, left, kIpAddressY);

    auto* router_caption = plain_label(panel, "");
    view::style::caption(router_caption, theme, "router");
    lv_obj_set_pos(router_caption, left, kRouterCaptionY);

    router_address_shown_ = router_address_text(data);
    router_address_ = plain_label(panel, router_address_shown_.c_str());
    view::style::text(router_address_, theme, view::FontRole::Small, colors.accent2);
    fix_width(router_address_, kTextWidth, small);
    lv_obj_set_pos(router_address_, left, kRouterAddressY);
}

void ConstellationHomeLayout::select(const HomeContext& context, int32_t index) {
    // The link view has no clients to mark.
    if (link_view_) {
        return;
    }

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

// The panel's clock, battery and traffic line follow the values in both
// views. The clock and the traffic line keep their places; the battery
// stays right-aligned in its box. In the
// router view the uplink beside its line follows too: its text grows to the
// right, and the star beside it gives way to a wide text. In the link view
// the link's label, the network's name and details, the signal or link
// speed and both addresses follow, each in its fixed box. The labels the
// built view does not have are null, and set_text_if_changed() skips them.
void ConstellationHomeLayout::update(const HomeContext& context) {
    const auto& data = context.data;
    set_text_if_changed(clock_, clock_shown_, clock_label_text(data));
    set_text_if_changed(battery_, battery_shown_, battery_label_text(data));
    set_battery_color(battery_, battery_low_shown_, data, context.theme, context.theme.colors.muted);
    set_text_if_changed(traffic_, traffic_shown_,
                        traffic_label_text(data, context.theme.font(view::FontRole::Small)));
    if (set_text_if_changed(uplink_, uplink_shown_, uplink_label_text(data))) {
        show_uplink_star(context.theme);
    }
    set_text_if_changed(link_label_, link_label_shown_, home::link_text(data));
    set_text_if_changed(network_, network_shown_, network_label_text(data));
    set_text_if_changed(details_, details_shown_, details_label_text(data));
    set_text_if_changed(link_value_, link_value_shown_, link_value_text(data));
    set_text_if_changed(address_, address_shown_, address_text(data));
    set_text_if_changed(router_address_, router_address_shown_, router_address_text(data));
}

} // namespace

std::unique_ptr<HomeLayout> make_constellation_home_layout() {
    return std::make_unique<ConstellationHomeLayout>();
}

} // namespace screen
