/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dev_fonts_screen.h"

#include "bundled_fonts.h"
#include "style.h"
#include "theme.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

namespace screen {
namespace {

constexpr uint32_t kSmallSize = 12;
constexpr uint32_t kLargeSize = 28;

constexpr int32_t kSidePadding   = 5;
constexpr int32_t kOverviewTop   = 5;
constexpr int32_t kOverviewPitch = 16;
constexpr int32_t kFamilyTop     = 4;

struct Family {
    model::AppPage page;
    const char* name;
    const char* regular_file;
    const char* heavy_file;
    const char* heavy_weight;
};

constexpr Family kFamilies[] = {
    {model::AppPage::DevFontsSpaceGrotesk,
     "Space Grotesk",
     view::fonts::kSpaceGroteskRegular,
     view::fonts::kSpaceGroteskBold,
     "Bold"},
    {model::AppPage::DevFontsIbmPlexMono,
     "IBM Plex Mono",
     view::fonts::kIbmPlexMonoRegular,
     view::fonts::kIbmPlexMonoSemiBold,
     "SemiBold"},
    {model::AppPage::DevFontsGeistMono,
     "Geist Mono",
     view::fonts::kGeistMonoRegular,
     view::fonts::kGeistMonoBold,
     "Bold"},
    {model::AppPage::DevFontsJetBrainsMono,
     "JetBrains Mono",
     view::fonts::kJetBrainsMonoRegular,
     view::fonts::kJetBrainsMonoBold,
     "Bold"},
    {model::AppPage::DevFontsInter,
     "Inter",
     view::fonts::kInterRegular,
     view::fonts::kInterSemiBold,
     "SemiBold"},
};

// The symbols the themes plan to use, one character per entry, in the same
// order as the table in docs/fonts.md. The page draws these exact strings and
// counts them, so the picture and the count line always agree. Written as
// escapes so look-alike characters (minus and hyphen, the two kinds of X)
// cannot be mixed up.
constexpr const char* kArrows[] = {
    "\u2191", // up arrow
    "\u2193", // down arrow
    "\u2190", // left arrow
    "\u2192", // right arrow
};
constexpr const char* kMarks[] = {
    "\u25CF", // black circle
    "\u203A", // single right-pointing angle quotation mark
    "\u00B7", // middle dot
    "\u2212", // minus sign
    "\u2713", // check mark
    "\u2715", // multiplication X
};
constexpr const char* kBoxDrawing[] = {
    "\u2500", // light horizontal
    "\u2502", // light vertical
    "\u250C", // light down and right (top-left corner)
    "\u2510", // light down and left (top-right corner)
    "\u2514", // light up and right (bottom-left corner)
    "\u2518", // light up and left (bottom-right corner)
    "\u251C", // light vertical and right
    "\u2524", // light vertical and left
};
constexpr const char* kBrailleSamples[] = {
    "\u2801", // braille dots 1
    "\u283F", // braille dots 1-2-3-4-5-6
    "\u28C0", // braille dots 7-8
    "\u28E4", // braille dots 3-6-7-8
    "\u28F6", // braille dots 2-3-5-6-7-8
    "\u28FF", // braille dots 1-2-3-4-5-6-7-8
};

// The whole braille block is counted, not only the samples above.
constexpr uint32_t kBrailleFirst = 0x2800;
constexpr uint32_t kBrailleLast  = 0x28FF;

constexpr const char* kSampleAddress = "10.0.0.12";
constexpr const char* kSampleDigits  = "0123456789 Ag";

// Code point of a string that holds exactly one UTF-8 encoded character.
uint32_t code_point(const char* utf8) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(utf8);
    const uint32_t first = bytes[0];
    if (first < 0x80) {
        return first;
    }
    if ((first & 0xE0) == 0xC0) {
        return ((first & 0x1F) << 6) | (uint32_t{bytes[1]} & 0x3F);
    }
    if ((first & 0xF0) == 0xE0) {
        return ((first & 0x0F) << 12) | ((uint32_t{bytes[1]} & 0x3F) << 6) | (uint32_t{bytes[2]} & 0x3F);
    }
    return ((first & 0x07) << 18) | ((uint32_t{bytes[1]} & 0x3F) << 12) |
           ((uint32_t{bytes[2]} & 0x3F) << 6) | (uint32_t{bytes[3]} & 0x3F);
}

// True if the font has its own drawing for the character. For FreeType fonts
// LVGL also reports success for a missing character (it then draws the
// font's empty box), so is_placeholder is what tells the two apart.
bool has_glyph(const lv_font_t* font, uint32_t letter) {
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(font, &dsc, letter, 0) && !dsc.is_placeholder;
}

template <size_t N>
std::string join(const char* const (&symbols)[N]) {
    std::string text;
    for (size_t i = 0; i < N; ++i) {
        if (i > 0) {
            text += ' ';
        }
        text += symbols[i];
    }
    return text;
}

template <size_t N>
std::string count_of(const lv_font_t* font, const char* const (&symbols)[N]) {
    size_t found = 0;
    for (const char* symbol : symbols) {
        if (has_glyph(font, code_point(symbol))) {
            ++found;
        }
    }
    return std::to_string(found) + "/" + std::to_string(N);
}

std::string braille_count(const lv_font_t* font) {
    uint32_t found = 0;
    for (uint32_t letter = kBrailleFirst; letter <= kBrailleLast; ++letter) {
        if (has_glyph(font, letter)) {
            ++found;
        }
    }
    return std::to_string(found) + "/" + std::to_string(kBrailleLast - kBrailleFirst + 1);
}

// For example "arrows 4/4 marks 6/6 box 8/8 braille 0/256".
std::string count_line(const lv_font_t* font) {
    if (!font) {
        return {};
    }

    return "arrows " + count_of(font, kArrows) + " marks " + count_of(font, kMarks) + " box " +
           count_of(font, kBoxDrawing) + " braille " + braille_count(font);
}

// Adds one line of text drawn in `font`, in the theme's text colour. If the
// font did not load, the line says so in the theme's Small font and error
// colour; the sample itself is never drawn in another font.
lv_obj_t* add_text(lv_obj_t* parent,
                   const view::Theme& theme,
                   const lv_font_t* font,
                   const char* file_name,
                   const std::string& text) {
    auto* label = lv_label_create(parent);
    if (!font) {
        const std::string missing = std::string("MISSING ") + file_name;
        lv_label_set_text(label, missing.c_str());
        view::style::text(label, theme, view::FontRole::Small, theme.colors.error);
        return label;
    }

    lv_label_set_text(label, text.c_str());
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, theme.colors.text, 0);
    return label;
}

} // namespace

DevFontsScreen::DevFontsScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               view::ThemeEngine& theme_engine,
                               model::AppPage page)
    : BaseScreen(view_model, assets, theme_engine), page_(page), fonts_(assets) {
    // build_content() runs inside init() and needs fonts_, so init() is
    // called here rather than in the base class.
    init(Layout::FullScreen);
}

DevFontsScreen::~DevFontsScreen() {
    // The labels point at the fonts in fonts_, and the base class deletes the
    // screen only after this destructor has finished. Remove the labels
    // first, then give the fonts back.
    if (body_ && lv_obj_is_valid(body_)) {
        lv_obj_clean(body_);
    }
    fonts_.clear();
}

bool DevFontsScreen::handles(model::AppPage page) {
    if (page == model::AppPage::DevFonts) {
        return true;
    }
    for (const auto& family : kFamilies) {
        if (family.page == page) {
            return true;
        }
    }
    return false;
}

void DevFontsScreen::build_content(lv_obj_t* content) {
    body_ = content;

    for (size_t i = 0; i < std::size(kFamilies); ++i) {
        if (kFamilies[i].page == page_) {
            build_family(content, i);
            return;
        }
    }
    build_overview(content);
}

// One line per font file, each drawn in its own font.
void DevFontsScreen::build_overview(lv_obj_t* content) {
    int32_t y = kOverviewTop;
    for (const auto& family : kFamilies) {
        const char* const files[] = {family.regular_file, family.heavy_file};
        const char* const weights[] = {"Regular", family.heavy_weight};
        for (size_t i = 0; i < std::size(files); ++i) {
            auto* font = fonts_.load(files[i], kSmallSize);
            const std::string text = std::string(family.name) + " " + weights[i] + "  " + kSampleAddress +
                                     " " + kArrows[0] + kArrows[1];
            auto* label = add_text(content, theme(), font, files[i], text);
            lv_obj_set_pos(label, kSidePadding, y);
            y += kOverviewPitch;
        }
    }
}

// Both weights at a small and a large size, the planned symbols, and how
// many of them the font can draw.
void DevFontsScreen::build_family(lv_obj_t* content, size_t family_index) {
    const auto& family = kFamilies[family_index];
    auto* regular       = fonts_.load(family.regular_file, kSmallSize);
    auto* heavy         = fonts_.load(family.heavy_file, kSmallSize);
    auto* regular_large = fonts_.load(family.regular_file, kLargeSize);
    auto* heavy_large   = fonts_.load(family.heavy_file, kLargeSize);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, kSidePadding, 0);
    lv_obj_set_style_pad_top(column, kFamilyTop, 0);
    lv_obj_set_style_pad_row(column, 0, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    const std::string name(family.name);
    const auto& theme = this->theme();
    add_text(column, theme, regular, family.regular_file, name + " Regular  " + kSampleDigits);
    add_text(column, theme, heavy, family.heavy_file, name + " " + family.heavy_weight + "  " + kSampleDigits);
    add_text(column, theme, regular, family.regular_file, join(kArrows) + "   " + join(kMarks));
    add_text(column, theme, regular, family.regular_file, join(kBoxDrawing) + "   " + join(kBrailleSamples));
    add_text(column, theme, regular, family.regular_file, count_line(regular));
    add_text(column, theme, regular_large, family.regular_file, kSampleAddress);
    add_text(column,
             theme,
             heavy_large,
             family.heavy_file,
             std::string(kArrows[0]) + "12.4 " + kArrows[1] + "3.1 " + kMarks[0] + kMarks[4] + kMarks[5]);
}

} // namespace screen
