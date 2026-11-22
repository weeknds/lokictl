/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The home page of the Instrument theme: a big-type gauge. From the top:
// - a status strip with the app name, the sample mark and clock and battery;
// - the mode in the Display font with its detail line, and right of an
//   upright line three large numbers: clients, download and upload rate;
// - one chip per mode, the active one filled with the accent colour;
// - the access point with its uptime;
// - the selected client: its position in a filled tag, name, address and
//   signal.
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

constexpr int32_t kContentWidth  = view::kScreenWidth;
constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;

static_assert(kChipStep * (model::kNetModeCount - 1) + kLeftX + kChipWidth == kRightEnd,
              "the four chips span x 10-310");
static_assert(kClientY + kClientHeight <= kContentHeight - 2,
              "rows 138-139, right above the soft-key bar, stay empty");

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

    // Clock and battery, one label in the strip, and the text it shows;
    // update() changes the text only.
    lv_obj_t* status_{nullptr};
    std::string status_shown_;

    // The selected client's line; select() changes their text only.
    lv_obj_t* tag_{nullptr};
    lv_obj_t* name_{nullptr};
    lv_obj_t* address_{nullptr};
    lv_obj_t* signal_{nullptr};
};

void InstrumentHomeLayout::build(const HomeContext& context) {
    build_status_strip(context);
    build_mode(context);
    build_numbers(context);
    build_chips(context);
    build_access_point(context);
    build_client_line(context);
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

    if (context.data.sample) {
        auto* mark = plain_label(strip, "");
        view::style::caption(mark, theme, "sample data");
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

    const lv_font_t* display = theme.font(view::FontRole::Display);
    const std::string word = home::upper_text(model::net_mode_name(context.data.mode));
    auto* mode = plain_label(context.content, word.c_str());
    view::style::text(mode, theme, view::FontRole::Display);
    fix_width(mode, kLeftWidth, display);
    lv_obj_set_pos(mode, kLeftX, kModeBaselineY - baseline_from_top(display));

    auto* detail = plain_label(context.content, context.data.mode_detail.c_str());
    view::style::text(detail, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(detail, kLeftWidth, theme.font(view::FontRole::Small));
    lv_obj_set_pos(detail, kLeftX, kModeDetailY);

    auto* divider = plain_object(context.content);
    lv_obj_set_pos(divider, kDividerX, kDividerTop);
    lv_obj_set_size(divider, 1, kDividerBottom - kDividerTop + 1);
    lv_obj_set_style_bg_color(divider, theme.colors.border, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
}

void InstrumentHomeLayout::build_numbers(const HomeContext& context) {
    const auto& theme = context.theme;
    const auto& data = context.data;

    const auto client_count = static_cast<int32_t>(std::min<std::size_t>(data.clients.size(), 99));
    const std::string clients = (client_count < 10 ? "0" : "") + std::to_string(client_count);
    const std::string down = home::rate_text(data.down_kbps);
    const std::string up = home::rate_text(data.up_kbps);

    struct NumberRow {
        const std::string& value;
        const char* caption;
    };
    const NumberRow rows[] = {
        {clients, "clients"},
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
        lv_obj_align_to(caption, number, LV_ALIGN_OUT_RIGHT_BOTTOM, kNumberGap, 0);
        lv_obj_set_style_translate_y(caption, caption_shift, 0);
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

void InstrumentHomeLayout::select(const HomeContext& context, int32_t index) {
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

// Only the strip's clock and battery follow the values for now. The label
// keeps its alignment, so LVGL places a text of another width again at the
// same right end.
void InstrumentHomeLayout::update(const HomeContext& context) {
    set_text_if_changed(status_, status_shown_, status_text(context.data));
}

} // namespace

std::unique_ptr<HomeLayout> make_instrument_home_layout() {
    return std::make_unique<InstrumentHomeLayout>();
}

} // namespace screen
