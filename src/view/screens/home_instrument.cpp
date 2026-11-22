/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The home page of the Instrument theme: a big-type gauge. It has two views
// (see HomeLayout). From the top, the router view shows:
// - a status strip with the app name, the sample mark and clock and battery;
// - the mode in the Display font with its detail line, and right of an
//   upright line three large numbers: clients, download and upload rate;
// - one chip per mode, the active one filled with the accent colour;
// - the access point with its uptime;
// - the selected client: its position in a filled tag, name, address and
//   signal.
// The link view, without an access point, keeps the strip, the chips and
// the places of the mode word and the numbers, and shows the uplink in the
// rest:
// - the mode word ("OFFLINE", muted, without a link) and under it the
//   network the uplink leads to;
// - the first number is the Wi-Fi signal in dBm, or the link speed in Mbit
//   on a cable and without a link; download and upload stay;
// - the link's interface and kind, with the Wi-Fi's link speed at the right;
// - the device's address and the router's address.
// Every label sits at a fixed position on a 14 px row step; text rows are
// never stacked by LVGL, because labels are 16-17 px tall and ten rows only
// fit the 140 px area when their boxes may overlap.

#include "base_viewmodel.h"
#include "home_format.h"
#include "home_layout.h"
#include "style.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

namespace screen {
namespace {

// Status strip along the top edge, with a rule under it.
constexpr int32_t kStripHeight = 18;
constexpr int32_t kStripInset  = 8;   // app name from the left, clock ends this far from the right

// Left column: the mode.
constexpr int32_t kLeftX          = 10;
constexpr int32_t kLeftWidth      = 196;
constexpr int32_t kModeCaptionY   = 20;
constexpr int32_t kModeBaselineY  = 63;  // the mode word stands on this line
constexpr int32_t kModeDetailY    = 66;

// The upright line between the columns.
constexpr int32_t kDividerX      = 212;
constexpr int32_t kDividerTop    = 22;
constexpr int32_t kDividerBottom = 84;  // last row of the line

// Right column: three numbers with a caption each.
constexpr int32_t kNumberX       = 220;
constexpr int32_t kNumberWidth   = 92;
constexpr int32_t kNumberHeight  = 23;
constexpr int32_t kNumberTops[]  = {21, 42, 63};
constexpr int32_t kNumberGap     = 6;   // between a number and its caption

// One chip per mode.
constexpr int32_t kChipY      = 88;
constexpr int32_t kChipWidth  = 72;
constexpr int32_t kChipHeight = 18;
constexpr int32_t kChipStep   = 76;    // 72 px chip + 4 px gap

// The access point line and the selected client's line.
constexpr int32_t kRightEnd       = 310;  // right-aligned text ends here
constexpr int32_t kApY            = 108;
constexpr int32_t kApWidth        = 200;
constexpr int32_t kClientY        = 122;
constexpr int32_t kClientHeight   = 16;   // a line of the 12 px Small font, in every theme
constexpr int32_t kTagPadding     = 3;
constexpr int32_t kNameX          = 43;
constexpr int32_t kNameWidth      = 70;
constexpr int32_t kAddressX       = 118;
constexpr int32_t kAddressWidth   = 132;  // ends before the widest signal text

// The link view's lines, in the places of the access point line and the
// selected client's line.
constexpr int32_t kLinkY             = kApY;
constexpr int32_t kLinkWidth         = kApWidth;
constexpr int32_t kLinkSpeedWidth    = 96;   // right-aligned, room for "1000 Mbit/s"
constexpr int32_t kLinkAddressY      = kClientY;
constexpr int32_t kIpAddressX        = 33;
constexpr int32_t kLinkAddressWidth  = 105;  // 15 characters, the widest IPv4 address
constexpr int32_t kRouterCaptionX    = 150;
constexpr int32_t kRouterAddressX    = kRightEnd - kLinkAddressWidth;

constexpr int32_t kContentWidth  = view::kScreenWidth;
constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;

static_assert(kChipStep * (model::kNetModeCount - 1) + kLeftX + kChipWidth == kRightEnd,
              "the four chips span x 10-310");
static_assert(kClientY + kClientHeight <= kContentHeight - 2,
              "rows 138-139, right above the soft-key bar, stay empty");
static_assert(kLeftX + kLinkWidth <= kRightEnd - kLinkSpeedWidth,
              "the link line ends before the link speed's box");
static_assert(kIpAddressX + kLinkAddressWidth < kRouterCaptionX &&
                  kRouterCaptionX < kRouterAddressX,
              "the address, the router's caption and its address do not overlap");

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

// The clock and the battery as the strip shows them, one text: "HH:MM · NN%".
std::string status_text(const model::HomeData& data) {
    return home::clock_text(data.clock_minutes) + " \u00b7 " +
           home::battery_text(data.battery_percent, data.battery_state);
}

// The texts of the link view. build() and update() both take them from
// here, so a page updated in place shows what a fresh one shows.

// Under the mode word: the network and, on Wi-Fi, its security when it is
// known ("wi-fi network" when the name is not known, "wired network",
// "no network").
std::string network_line_text(const model::HomeData& data) {
    const bool wifi = data.uplink_kind == model::LinkKind::Wifi;
    return home::join_parts({home::network_text(data), wifi ? data.wifi_security : std::string()});
}

// The first number: the signal on Wi-Fi, the link speed otherwise.
bool first_number_is_signal(const model::HomeData& data) {
    return data.uplink_kind == model::LinkKind::Wifi;
}

std::string first_number_text(const model::HomeData& data) {
    return first_number_is_signal(data) ? home::signal_number_text(data.wifi_signal_dbm)
                                        : home::speed_number_text(data.uplink_speed_mbps);
}

const char* first_caption_text(const model::HomeData& data) {
    return first_number_is_signal(data) ? "dBm" : "Mbit";
}

// The link line: "LINK wlan0 · wi-fi", "LINK --" without a link.
std::string link_line_text(const model::HomeData& data) {
    return "LINK " + home::link_text(data);
}

// Right of the link line: the Wi-Fi's link speed in words when it is known;
// on a cable the first number shows it already.
std::string link_speed_text(const model::HomeData& data) {
    if (data.uplink_kind != model::LinkKind::Wifi) {
        return "";
    }
    return home::speed_part_text(data.uplink_speed_mbps);
}

std::string address_text(const model::HomeData& data) {
    return home::text_or_unknown(data.uplink_address);
}

std::string router_address_text(const model::HomeData& data) {
    return home::text_or_unknown(data.gateway_address);
}

// Puts a number's caption right of it, on the row's bottom edge. The
// caption's translation then moves it onto the number's baseline.
void place_caption(lv_obj_t* caption, lv_obj_t* number) {
    lv_obj_align_to(caption, number, LV_ALIGN_OUT_RIGHT_BOTTOM, kNumberGap, 0);
}

class InstrumentHomeLayout final : public HomeLayout {
public:
    void build(const HomeContext& context) override;
    void select(const HomeContext& context, int32_t index) override;
    void update(const HomeContext& context) override;

private:
    void build_status_strip(const HomeContext& context);
    void build_mode(const HomeContext& context);
    void build_numbers(const HomeContext& context);
    void build_chips(const HomeContext& context);
    void build_access_point(const HomeContext& context);
    void build_client_line(const HomeContext& context);
    void build_link_line(const HomeContext& context);
    void build_address_line(const HomeContext& context);

    // True when build() drew the link view: no access point, no clients.
    bool link_view_{false};

    // Clock and battery, one label in the strip, and the text it shows;
    // update() changes the text only.
    lv_obj_t* status_{nullptr};
    std::string status_shown_;

    // The download and upload numbers, in both views, their captions and
    // the text each number shows. When a number changes, update() moves its
    // caption with it.
    lv_obj_t* down_number_{nullptr};
    std::string down_number_shown_;
    lv_obj_t* down_caption_{nullptr};
    lv_obj_t* up_number_{nullptr};
    std::string up_number_shown_;
    lv_obj_t* up_caption_{nullptr};

    // The router view's selected client's line; select() changes their
    // text only. They stay null in the link view.
    lv_obj_t* tag_{nullptr};
    lv_obj_t* name_{nullptr};
    lv_obj_t* address_{nullptr};
    lv_obj_t* signal_{nullptr};

    // The link view's labels that show values update() follows, and the
    // text each shows. They stay null in the router view. When the first
    // number changes, update() moves its caption with it.
    lv_obj_t* network_{nullptr};
    std::string network_shown_;
    lv_obj_t* first_number_{nullptr};
    std::string first_number_shown_;
    lv_obj_t* first_caption_{nullptr};
    lv_obj_t* link_{nullptr};
    std::string link_shown_;
    lv_obj_t* link_speed_{nullptr};
    std::string link_speed_shown_;
    lv_obj_t* link_address_{nullptr};
    std::string link_address_shown_;
    lv_obj_t* router_address_{nullptr};
    std::string router_address_shown_;
};

void InstrumentHomeLayout::build(const HomeContext& context) {
    link_view_ = !context.data.ap_active;

    build_status_strip(context);
    build_mode(context);
    build_numbers(context);
    build_chips(context);
    if (link_view_) {
        build_link_line(context);
        build_address_line(context);
    } else {
        build_access_point(context);
        build_client_line(context);
    }
}

void InstrumentHomeLayout::build_status_strip(const HomeContext& context) {
    const auto& theme = context.theme;

    auto* strip = plain_object(context.content);
    lv_obj_set_pos(strip, 0, 0);
    lv_obj_set_size(strip, kContentWidth, kStripHeight);
    lv_obj_set_style_border_color(strip, theme.colors.border, 0);
    lv_obj_set_style_border_width(strip, theme.metrics.border_width, 0);
    lv_obj_set_style_border_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);

    auto* name = plain_label(strip, "");
    view::style::caption(name, theme, context.app_name);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, kStripInset, 0);

    // The middle of the strip: the sample mark, or else a note when the
    // internet can not be reached, in the warning colour.
    const std::string note = home::internet_note(context.data);
    if (context.data.sample) {
        auto* mark = plain_label(strip, "");
        view::style::caption(mark, theme, "sample data");
        lv_obj_align(mark, LV_ALIGN_CENTER, 0, 0);
    } else if (!note.empty()) {
        auto* mark = plain_label(strip, "");
        view::style::caption(mark, theme, note.c_str());
        lv_obj_set_style_text_color(mark, theme.colors.warning, 0);
        lv_obj_align(mark, LV_ALIGN_CENTER, 0, 0);
    }

    // Right-aligned: a wider text grows to the left, its right end stays
    // kStripInset from the edge.
    status_shown_ = status_text(context.data);
    status_ = plain_label(strip, status_shown_.c_str());
    view::style::text(status_, theme, view::FontRole::Small);
    lv_obj_align(status_, LV_ALIGN_RIGHT_MID, -kStripInset, 0);
}

void InstrumentHomeLayout::build_mode(const HomeContext& context) {
    const auto& theme = context.theme;

    auto* caption = plain_label(context.content, "");
    view::style::caption(caption, theme, "mode");
    lv_obj_set_pos(caption, kLeftX, kModeCaptionY);

    // "OFFLINE" without any network, in the muted colour: a calm state, not
    // an alarm.
    const lv_font_t* display = theme.font(view::FontRole::Display);
    const std::string word = home::upper_text(home::mode_word(context.data));
    auto* mode = plain_label(context.content, word.c_str());
    view::style::text(mode, theme, view::FontRole::Display,
                      home::is_offline(context.data) ? theme.colors.muted : theme.colors.text);
    fix_width(mode, kLeftWidth, display);
    lv_obj_set_pos(mode, kLeftX, kModeBaselineY - baseline_from_top(display));

    // The router view explains the mode here; the link view names the
    // network instead, and update() follows it.
    const std::string detail_text =
        link_view_ ? network_line_text(context.data) : context.data.mode_detail;
    auto* detail = plain_label(context.content, detail_text.c_str());
    view::style::text(detail, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(detail, kLeftWidth, theme.font(view::FontRole::Small));
    lv_obj_set_pos(detail, kLeftX, kModeDetailY);
    if (link_view_) {
        network_ = detail;
        network_shown_ = detail_text;
    }

    auto* divider = plain_object(context.content);
    lv_obj_set_pos(divider, kDividerX, kDividerTop);
    lv_obj_set_size(divider, 1, kDividerBottom - kDividerTop + 1);
    lv_obj_set_style_bg_color(divider, theme.colors.border, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
}

void InstrumentHomeLayout::build_numbers(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;

    // The router view counts the clients first; the link view shows the
    // signal or the link speed there.
    const auto client_count = static_cast<int32_t>(std::min<std::size_t>(data.clients.size(), 99));
    const std::string first = link_view_ ? first_number_text(data)
                                         : (client_count < 10 ? "0" : "") + std::to_string(client_count);
    const char* first_caption = link_view_ ? first_caption_text(data) : "clients";
    const std::string down = home::rate_text(data.down_kbps);
    const std::string up = home::rate_text(data.up_kbps);

    struct NumberRow {
        const std::string& value;
        const char* caption;
    };
    const NumberRow rows[] = {
        {first, first_caption},
        {down, "Mb/s \u2193"},
        {up, "Mb/s \u2191"},
    };

    // Number and caption stand on the row's bottom edge; the caption is then
    // moved so both share one baseline although the fonts differ.
    const int32_t caption_shift = theme.font(view::FontRole::Small)->base_line -
                                  theme.font(view::FontRole::Title)->base_line;
    for (std::size_t index = 0; index < std::size(rows); ++index) {
        auto* row = plain_object(context.content);
        lv_obj_set_pos(row, kNumberX, kNumberTops[index]);
        lv_obj_set_size(row, kNumberWidth, kNumberHeight);

        auto* number = plain_label(row, rows[index].value.c_str());
        view::style::text(number, theme, view::FontRole::Title);
        lv_obj_align(number, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        auto* caption = plain_label(row, "");
        view::style::caption(caption, theme, rows[index].caption);
        place_caption(caption, number);
        lv_obj_set_style_translate_y(caption, caption_shift, 0);

        if (index == 0 && link_view_) {
            first_number_ = number;
            first_number_shown_ = rows[index].value;
            first_caption_ = caption;
        } else if (index == 1) {
            down_number_ = number;
            down_number_shown_ = rows[index].value;
            down_caption_ = caption;
        } else if (index == 2) {
            up_number_ = number;
            up_number_shown_ = rows[index].value;
            up_caption_ = caption;
        }
    }
}

void InstrumentHomeLayout::build_chips(const HomeContext& context) {
    const auto& theme = context.theme;

    for (int32_t index = 0; index < model::kNetModeCount; ++index) {
        const auto mode = static_cast<model::NetMode>(index);
        const bool active = mode == context.data.mode;

        auto* chip = plain_object(context.content);
        lv_obj_set_pos(chip, kLeftX + index * kChipStep, kChipY);
        lv_obj_set_size(chip, kChipWidth, kChipHeight);
        lv_obj_set_style_border_color(chip, theme.colors.border, 0);
        lv_obj_set_style_border_width(chip, theme.metrics.border_width, 0);
        lv_obj_set_style_border_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_side(chip, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_radius(chip, theme.metrics.radius, 0);
        if (active) {
            lv_obj_set_style_bg_color(chip, theme.colors.accent, 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        }

        auto* name = plain_label(chip, "");
        view::style::caption(name, theme, model::net_mode_name(mode));
        lv_obj_set_style_text_color(name, active ? theme.colors.on_accent : theme.colors.text, 0);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, 0);
    }
}

void InstrumentHomeLayout::build_access_point(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;

    const std::string access_point = "AP " + data.ap_name + " \u00b7 " + data.ap_security;
    auto* left = plain_label(context.content, access_point.c_str());
    view::style::text(left, theme, view::FontRole::Small);
    fix_width(left, kApWidth, theme.font(view::FontRole::Small));
    lv_obj_set_pos(left, kLeftX, kApY);

    const std::string uptime = "UP " + home::uptime_text(data.uptime_seconds);
    auto* right = plain_label(context.content, uptime.c_str());
    view::style::text(right, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, kRightEnd - kContentWidth, kApY);
}

void InstrumentHomeLayout::build_client_line(const HomeContext& context) {
    const auto& theme = context.theme;
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    // The tag: the position of the selected client, dark on a filled box.
    tag_ = plain_label(context.content, "");
    view::style::text(tag_, theme, view::FontRole::Small, theme.colors.background);
    lv_obj_set_style_bg_color(tag_, theme.colors.text, 0);
    lv_obj_set_style_bg_opa(tag_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(tag_, kTagPadding, 0);
    lv_obj_set_pos(tag_, kLeftX, kClientY);

    // The name is set in the Strong font, which is taller than Small; it is
    // moved so that its baseline matches the Small labels of the row.
    name_ = plain_label(context.content, "");
    view::style::text(name_, theme, view::FontRole::Strong);
    fix_width(name_, kNameWidth, strong);
    lv_obj_set_pos(name_, kNameX, kClientY + baseline_from_top(small) - baseline_from_top(strong));

    address_ = plain_label(context.content, "");
    view::style::text(address_, theme, view::FontRole::Small, theme.colors.accent2);
    fix_width(address_, kAddressWidth, small);
    lv_obj_set_pos(address_, kAddressX, kClientY);

    signal_ = plain_label(context.content, "");
    view::style::text(signal_, theme, view::FontRole::Small);
    lv_obj_align(signal_, LV_ALIGN_TOP_RIGHT, kRightEnd - kContentWidth, kClientY);
}

void InstrumentHomeLayout::build_link_line(const HomeContext& context) {
    const auto& theme = context.theme;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    link_shown_ = link_line_text(context.data);
    link_ = plain_label(context.content, link_shown_.c_str());
    view::style::text(link_, theme, view::FontRole::Small);
    fix_width(link_, kLinkWidth, small);
    lv_obj_set_pos(link_, kLeftX, kLinkY);

    // Always made, also while it is empty, so update() can fill it.
    link_speed_shown_ = link_speed_text(context.data);
    link_speed_ = plain_label(context.content, link_speed_shown_.c_str());
    view::style::text(link_speed_, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(link_speed_, kLinkSpeedWidth, small);
    lv_obj_set_style_text_align(link_speed_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(link_speed_, kRightEnd - kLinkSpeedWidth, kLinkY);
}

void InstrumentHomeLayout::build_address_line(const HomeContext& context) {
    const auto& theme = context.theme;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    auto* ip_caption = plain_label(context.content, "");
    view::style::caption(ip_caption, theme, "ip");
    lv_obj_set_pos(ip_caption, kLeftX, kLinkAddressY);

    link_address_shown_ = address_text(context.data);
    link_address_ = plain_label(context.content, link_address_shown_.c_str());
    view::style::text(link_address_, theme, view::FontRole::Small, theme.colors.accent2);
    fix_width(link_address_, kLinkAddressWidth, small);
    lv_obj_set_pos(link_address_, kIpAddressX, kLinkAddressY);

    auto* router_caption = plain_label(context.content, "");
    view::style::caption(router_caption, theme, "router");
    lv_obj_set_pos(router_caption, kRouterCaptionX, kLinkAddressY);

    router_address_shown_ = router_address_text(context.data);
    router_address_ = plain_label(context.content, router_address_shown_.c_str());
    view::style::text(router_address_, theme, view::FontRole::Small, theme.colors.accent2);
    fix_width(router_address_, kLinkAddressWidth, small);
    lv_obj_set_pos(router_address_, kRouterAddressX, kLinkAddressY);
}

void InstrumentHomeLayout::select(const HomeContext& context, int32_t index) {
    // The link view has no clients to mark.
    if (link_view_) {
        return;
    }

    const auto& clients = context.data.clients;
    const auto shown = static_cast<int32_t>(
        std::min<std::size_t>(clients.size(), static_cast<std::size_t>(viewmodel::kHomeClientRows)));

    lv_label_set_text(tag_, home::count_text(index, shown).c_str());
    if (shown == 0) {
        lv_label_set_text(name_, "");
        lv_label_set_text(address_, "");
        lv_label_set_text(signal_, "");
        return;
    }

    const auto& client = clients[static_cast<std::size_t>(std::clamp<int32_t>(index, 0, shown - 1))];
    lv_label_set_text(name_, client.name.c_str());
    lv_label_set_text(address_, client.address.c_str());
    lv_label_set_text(signal_, home::signal_text(client.signal_dbm).c_str());
}

// The strip's clock and battery and the download and upload numbers follow
// the values in both views. The strip's label keeps its alignment, so LVGL
// places a text of another width again at the same right end; a rate's
// caption moves with its number's width. In the link view the network line,
// the first number, the link line, the link speed and both addresses follow
// too; each keeps its box, and the first number's caption moves with the
// number's width. The labels of the other view are null, and
// set_text_if_changed() skips them.
void InstrumentHomeLayout::update(const HomeContext& context) {
    const auto& data = context.data;
    set_text_if_changed(status_, status_shown_, status_text(data));
    if (set_text_if_changed(down_number_, down_number_shown_, home::rate_text(data.down_kbps))) {
        place_caption(down_caption_, down_number_);
    }
    if (set_text_if_changed(up_number_, up_number_shown_, home::rate_text(data.up_kbps))) {
        place_caption(up_caption_, up_number_);
    }
    set_text_if_changed(network_, network_shown_, network_line_text(data));
    if (set_text_if_changed(first_number_, first_number_shown_, first_number_text(data))) {
        place_caption(first_caption_, first_number_);
    }
    set_text_if_changed(link_, link_shown_, link_line_text(data));
    set_text_if_changed(link_speed_, link_speed_shown_, link_speed_text(data));
    set_text_if_changed(link_address_, link_address_shown_, address_text(data));
    set_text_if_changed(router_address_, router_address_shown_, router_address_text(data));
}

} // namespace

std::unique_ptr<HomeLayout> make_instrument_home_layout() {
    return std::make_unique<InstrumentHomeLayout>();
}

} // namespace screen
