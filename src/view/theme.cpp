/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

#include "bundled_fonts.h"

#include <cstddef>

namespace view {
namespace {

// The three themes. Colours come from the design mockups, except for our
// own additions in the same spirit: success, warning and error in every
// theme, the surface of Instrument and Constellation, and Instrument's
// subtle border (docs/themes.md marks them).
// Positional order follows the structs in theme.h:
//   colours: background, surface, text, muted, accent, accent2, border,
//            success, warning, error, border_strong, border_subtle,
//            on_accent, selection, on_selection
//   fonts:   Display, Title, Body, Strong, Mono, Small
//   metrics: space_xs, space_sm, space_md, space_lg, radius, border_width,
//            caption_letter_space, dark, pane_title_in_border,
//            uppercase_captions, selection_filled
constexpr ThemeSpec kThemes[] = {
    // Instrument: warm paper ground, ink rules, square corners, big Space
    // Grotesk numbers, orange filled selection, spaced capital captions.
    {
        ThemeId::Instrument,
        "instrument",
        "Instrument",
        {
            0xECEAE4, 0xF6F4EE, 0x141414, 0x57544E, 0xFF4F1A,
            0xC2380C, 0x141414, 0x1F7A3D, 0xA35F00, 0xC1121F,
            0x141414, 0xCFCBC2, 0x141414, 0xFF4F1A, 0x141414,
        },
        {
            {fonts::kSpaceGroteskBold, 36},
            {fonts::kSpaceGroteskBold, 18},
            {fonts::kInterRegular, 14},
            {fonts::kInterSemiBold, 14},
            {fonts::kIbmPlexMonoRegular, 13},
            {fonts::kIbmPlexMonoRegular, 12},
        },
        {2, 4, 8, 12, 0, 1, 1, false, false, true, true},
    },
    // Constellation: near-black blue ground, thin dark-blue lines, slightly
    // rounded corners, lime accent with an outlined selection, violet links.
    {
        ThemeId::Constellation,
        "constellation",
        "Constellation",
        {
            0x06080F, 0x0C1120, 0xE8ECF5, 0x7D89A6, 0xC8FF4D,
            0x8C7CFF, 0x1A2238, 0x5BE49B, 0xFFC857, 0xFF6B7A,
            0x3A4766, 0x151D33, 0x06080F, 0xC8FF4D, 0x06080F,
        },
        {
            {fonts::kGeistMonoBold, 30},
            {fonts::kGeistMonoBold, 16},
            {fonts::kGeistMonoRegular, 13},
            {fonts::kGeistMonoBold, 13},
            {fonts::kGeistMonoRegular, 13},
            {fonts::kGeistMonoRegular, 12},
        },
        {2, 4, 8, 12, 3, 1, 0, true, false, true, false},
    },
    // Terminal: black ground, grey boxed panes with the title in the top
    // border, amber accent, cyan addresses, magenta filled selection.
    {
        ThemeId::Terminal,
        "terminal",
        "Terminal",
        {
            0x000000, 0x0A0A0A, 0xD0D0D0, 0x8A8A8A, 0xFFB000,
            0x5FD7FF, 0x3A3A3A, 0x5FD75F, 0xFFD75F, 0xFF5F5F,
            0xFFB000, 0x1C1C1C, 0x000000, 0xFF5FAF, 0x000000,
        },
        {
            {fonts::kJetBrainsMonoBold, 28},
            {fonts::kJetBrainsMonoBold, 14},
            {fonts::kJetBrainsMonoRegular, 13},
            {fonts::kJetBrainsMonoBold, 13},
            {fonts::kJetBrainsMonoRegular, 13},
            {fonts::kJetBrainsMonoRegular, 12},
        },
        {2, 4, 6, 10, 0, 1, 0, true, true, false, true},
    },
};

static_assert(sizeof(kThemes) / sizeof(kThemes[0]) == kThemeCount,
              "kThemes needs exactly one entry per ThemeId");

constexpr bool table_order_matches_ids() {
    for (int index = 0; index < kThemeCount; ++index) {
        if (static_cast<int>(kThemes[index].id) != index) {
            return false;
        }
    }
    return true;
}
static_assert(table_order_matches_ids(), "kThemes must be in ThemeId order");

constexpr bool all_text_sizes_readable() {
    for (const auto& theme : kThemes) {
        for (const auto& font : theme.fonts) {
            if (font.size_px < kMinTextSizePx) {
                return false;
            }
        }
    }
    return true;
}
static_assert(all_text_sizes_readable(), "every theme font must be at least kMinTextSizePx");

} // namespace

const ThemeSpec& theme_spec(ThemeId id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kThemeCount) {
        return kThemes[static_cast<int>(kDefaultThemeId)];
    }
    return kThemes[index];
}

const ThemeSpec* find_theme(std::string_view name) {
    for (const auto& theme : kThemes) {
        if (name == theme.name) {
            return &theme;
        }
    }
    return nullptr;
}

std::string theme_names() {
    std::string names;
    for (const auto& theme : kThemes) {
        if (!names.empty()) {
            names += ", ";
        }
        names += theme.name;
    }
    return names;
}

ThemeId next_theme(ThemeId id) {
    int index = static_cast<int>(id);
    if (index < 0 || index >= kThemeCount) {
        index = static_cast<int>(kDefaultThemeId);
    }
    return kThemes[(index + 1) % kThemeCount].id;
}

ThemeColors to_colors(const ThemeColorSpec& spec) {
    return {
        lv_color_hex(spec.background),
        lv_color_hex(spec.surface),
        lv_color_hex(spec.text),
        lv_color_hex(spec.muted),
        lv_color_hex(spec.accent),
        lv_color_hex(spec.accent2),
        lv_color_hex(spec.border),
        lv_color_hex(spec.success),
        lv_color_hex(spec.warning),
        lv_color_hex(spec.error),
        lv_color_hex(spec.border_strong),
        lv_color_hex(spec.border_subtle),
        lv_color_hex(spec.on_accent),
        lv_color_hex(spec.selection),
        lv_color_hex(spec.on_selection),
    };
}

const lv_font_t* Theme::font(FontRole role) const {
    const int index = static_cast<int>(role);
    if (index < 0 || index >= kFontRoleCount) {
        return fonts[static_cast<int>(FontRole::Body)];
    }
    return fonts[index];
}

} // namespace view
