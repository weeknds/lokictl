/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dev_theme_screen.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

namespace screen {
namespace {

// Layout of the 320x170 page: a strip of colour chips on top, the font
// scale on the left and the sample widgets on the right.
constexpr int32_t kChipWidth    = 38;
constexpr int32_t kChipHeight   = 16;
constexpr int32_t kChipGap      = 1;
constexpr int32_t kChipsPerRow  = 8;
constexpr int32_t kChipsTop     = 3;
constexpr int32_t kColumnsTop   = 39;
constexpr int32_t kSideMargin   = 4;
constexpr int32_t kLeftWidth    = 158;
constexpr int32_t kColumnGap    = 6;
constexpr int32_t kRightLeft    = kSideMargin + kLeftWidth + kColumnGap;
constexpr int32_t kRightWidth   = view::kScreenWidth - kSideMargin - kRightLeft;

struct Swatch {
    const char* name;
    lv_color_t view::ThemeColors::*color;
};

// Every colour token, in the order of ThemeColorSpec.
constexpr Swatch kSwatches[] = {
    {"bg", &view::ThemeColors::background},
    {"surf", &view::ThemeColors::surface},
    {"text", &view::ThemeColors::text},
    {"muted", &view::ThemeColors::muted},
    {"acc", &view::ThemeColors::accent},
    {"acc2", &view::ThemeColors::accent2},
    {"line", &view::ThemeColors::border},
    {"ok", &view::ThemeColors::success},
    {"warn", &view::ThemeColors::warning},
    {"err", &view::ThemeColors::error},
    {"line+", &view::ThemeColors::border_strong},
    {"line-", &view::ThemeColors::border_subtle},
    {"onacc", &view::ThemeColors::on_accent},
    {"sel", &view::ThemeColors::selection},
    {"onsel", &view::ThemeColors::on_selection},
};

struct Symbol {
    const char* utf8;
    uint32_t code_point;
};

// The symbols the themes rely on. Several bundled fonts lack some of them
// and get them from their fallback font (see docs/fonts.md). Written as
// escapes so look-alike characters cannot be mixed up.
constexpr Symbol kSymbols[] = {
    {"\u2191", 0x2191}, // up arrow
    {"\u2193", 0x2193}, // down arrow
    {"\u25CF", 0x25CF}, // black circle
    {"\u2713", 0x2713}, // check mark
    {"\u2715", 0x2715}, // multiplication X
    {"\u203A", 0x203A}, // single right-pointing angle quotation mark
};

constexpr const char* kSelectedPrefix   = "\u203A ";
constexpr const char* kUnselectedPrefix = "  ";

struct Host {
    const char* address;
    const char* name;
};

// Short names on purpose: with the two-character row prefix of some themes,
// the widest row must still fit the narrow right-hand column.
constexpr Host kHosts[] = {
    {"10.0.0.1", "modem"},
    {"10.0.0.7", "phone"},
    {"10.0.0.9", "nas"},
};
constexpr std::size_t kSelectedHost = 0;

std::string symbol_text() {
    std::string text;
    for (const auto& symbol : kSymbols) {
        text += symbol.utf8;
    }
    return text;
}

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent, const std::string& text) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text.c_str());
    return label;
}

// Relative luminance and contrast ratio as defined by WCAG 2.
double linear_channel(uint8_t value) {
    const double channel = value / 255.0;
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double luminance(lv_color_t color) {
    return 0.2126 * linear_channel(color.red) + 0.7152 * linear_channel(color.green) +
           0.0722 * linear_channel(color.blue);
}

double contrast(lv_color_t first, lv_color_t second) {
    double lighter = luminance(first);
    double darker = luminance(second);
    if (lighter < darker) {
        std::swap(lighter, darker);
    }
    return (lighter + 0.05) / (darker + 0.05);
}

// The theme's text or ground colour, whichever reads better on `fill`.
lv_color_t readable_on(const view::Theme& theme, lv_color_t fill) {
    return contrast(fill, theme.colors.text) >= contrast(fill, theme.colors.background)
               ? theme.colors.text
               : theme.colors.background;
}

// True if the font, or one of its fallbacks, has its own drawing for the
// character (not the "missing character" box).
bool has_glyph(const lv_font_t* font, uint32_t code_point) {
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(font, &dsc, code_point, 0) && !dsc.is_placeholder;
}

} // namespace

DevThemeScreen::DevThemeScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init(Layout::FullScreen);
}

bool DevThemeScreen::handles(model::AppPage page) {
    return page == model::AppPage::DevTheme;
}

void DevThemeScreen::build_content(lv_obj_t* content) {
    build_swatches(content);
    build_font_scale(content);
    build_samples(content);
    build_symbol_check(content);
}

// Two rows of chips, one per colour token, each named in a readable colour.
void DevThemeScreen::build_swatches(lv_obj_t* content) {
    const auto& theme = this->theme();
    constexpr int32_t row_width = kChipsPerRow * kChipWidth + (kChipsPerRow - 1) * kChipGap;
    const int32_t left = (view::kScreenWidth - row_width) / 2;

    for (std::size_t index = 0; index < std::size(kSwatches); ++index) {
        const auto& swatch = kSwatches[index];
        const lv_color_t fill = theme.colors.*swatch.color;
        const auto column = static_cast<int32_t>(index % kChipsPerRow);
        const auto row = static_cast<int32_t>(index / kChipsPerRow);

        auto* chip = plain_object(content);
        lv_obj_set_size(chip, kChipWidth, kChipHeight);
        lv_obj_set_pos(chip,
                       left + column * (kChipWidth + kChipGap),
                       kChipsTop + row * (kChipHeight + kChipGap));
        lv_obj_set_style_bg_color(chip, fill, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(chip, theme.colors.border, 0);
        lv_obj_set_style_border_width(chip, theme.metrics.border_width, 0);
        lv_obj_set_style_border_opa(chip, LV_OPA_COVER, 0);

        auto* name = plain_label(chip, swatch.name);
        view::style::text(name, theme, view::FontRole::Small, readable_on(theme, fill));
        lv_obj_center(name);
    }
}

// One line per font role, each drawn in that role's font.
void DevThemeScreen::build_font_scale(lv_obj_t* content) {
    const auto& theme = this->theme();
    const std::string symbols = symbol_text();

    auto* column = plain_object(content);
    lv_obj_set_pos(column, kSideMargin, kColumnsTop);
    lv_obj_set_size(column, kLeftWidth, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 0, 0);

    auto* display = plain_label(column, symbols);
    view::style::text(display, theme, view::FontRole::Display);

    auto* title = plain_label(column, "Title " + symbols);
    view::style::text(title, theme, view::FontRole::Title);

    // Body and Strong share one line, as they share one size.
    auto* body_line = plain_object(column);
    lv_obj_set_size(body_line, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(body_line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body_line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    auto* body = plain_label(body_line, "Body ");
    view::style::text(body, theme, view::FontRole::Body);
    auto* strong = plain_label(body_line, "Strong");
    view::style::text(strong, theme, view::FontRole::Strong);
    auto* body_symbols = plain_label(body_line, " " + symbols);
    view::style::text(body_symbols, theme, view::FontRole::Body);

    auto* mono = plain_label(column, "mono 0.9M " + symbols);
    view::style::text(mono, theme, view::FontRole::Mono);

    auto* small = plain_label(column, "small " + symbols);
    view::style::text(small, theme, view::FontRole::Small);
}

// A card and a pane holding a short list with one selected row.
void DevThemeScreen::build_samples(lv_obj_t* content) {
    const auto& theme = this->theme();
    const auto& metrics = theme.metrics;

    auto* column = plain_object(content);
    lv_obj_set_pos(column, kRightLeft, kColumnsTop);
    lv_obj_set_size(column, kRightWidth, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, metrics.space_sm, 0);

    auto* card = plain_object(column);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    view::style::card(card, theme);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(card, metrics.space_md, 0);
    auto* card_caption = plain_label(card, "");
    view::style::caption(card_caption, theme, "mode");
    auto* card_value = plain_label(card, "router");
    view::style::text(card_value, theme, view::FontRole::Title, theme.colors.accent);
    // Both labels sit on the card's bottom edge; move the caption so the two
    // share one baseline even though the fonts have different descents.
    lv_obj_set_style_translate_y(card_caption,
                                 theme.font(view::FontRole::Small)->base_line -
                                     theme.font(view::FontRole::Title)->base_line,
                                 0);

    const auto pane =
        view::style::pane(column, theme, "hosts 14", kRightWidth, LV_SIZE_CONTENT, theme.colors.selection);
    lv_obj_set_flex_flow(pane.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pane.content, 0, 0);

    for (std::size_t index = 0; index < std::size(kHosts); ++index) {
        const bool selected = index == kSelectedHost;
        const auto& host = kHosts[index];

        auto* row = plain_object(pane.content);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        view::style::list_row(row, theme, selected);

        std::string address = host.address;
        if (metrics.pane_title_in_border) {
            address = (selected ? kSelectedPrefix : kUnselectedPrefix) + address;
        }
        auto* address_label = plain_label(row, address);
        if (!selected) {
            lv_obj_set_style_text_color(address_label, theme.colors.accent2, 0);
        }
        plain_label(row, std::string(" ") + host.name);
    }
}

// Normally adds nothing. If any role font cannot draw one of the symbols,
// even with its fallback, a line in the error colour says how many are
// missing.
void DevThemeScreen::build_symbol_check(lv_obj_t* content) {
    const auto& theme = this->theme();

    int missing = 0;
    for (int role = 0; role < view::kFontRoleCount; ++role) {
        const lv_font_t* font = theme.font(static_cast<view::FontRole>(role));
        for (const auto& symbol : kSymbols) {
            if (!has_glyph(font, symbol.code_point)) {
                ++missing;
            }
        }
    }
    if (missing == 0) {
        return;
    }

    auto* warning = plain_label(content, "missing symbols: " + std::to_string(missing));
    view::style::text(warning, theme, view::FontRole::Small, theme.colors.error);
    lv_obj_set_style_bg_color(warning, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(warning, LV_OPA_COVER, 0);
    lv_obj_align(warning, LV_ALIGN_BOTTOM_LEFT, kSideMargin, 0);
}

} // namespace screen
