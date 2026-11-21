/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme_engine.h"

#include "asset_manager.h"
#include "bundled_fonts.h"

#include <cstring>
#include <utility>

namespace view {
namespace {

bool is_jetbrains_mono(const char* file) {
    return std::strcmp(file, fonts::kJetBrainsMonoRegular) == 0 ||
           std::strcmp(file, fonts::kJetBrainsMonoBold) == 0;
}

// Last resort when a font file cannot be loaded at all: LVGL's built-in
// Montserrat at the largest compiled-in size that is not bigger than the
// requested one, and never smaller than 12 px.
const lv_font_t* built_in_font(uint16_t size_px) {
    if (size_px >= 28) {
        return &lv_font_montserrat_28;
    }
    if (size_px >= 24) {
        return &lv_font_montserrat_24;
    }
    if (size_px >= 20) {
        return &lv_font_montserrat_20;
    }
    if (size_px >= 18) {
        return &lv_font_montserrat_18;
    }
    if (size_px >= 16) {
        return &lv_font_montserrat_16;
    }
    if (size_px >= 14) {
        return &lv_font_montserrat_14;
    }
    return &lv_font_montserrat_12;
}

// Loads the font for one role into `set`. JetBrains Mono is the only bundled
// family with every symbol the themes use (see docs/fonts.md), so every other
// font gets JetBrains Mono Regular at the same size as its fallback. The
// fallback is loaded first, so the set gives it back last.
const lv_font_t* load_role_font(app::FontSet& set, const FontSpec& spec, bool& complete) {
    const lv_font_t* fallback = nullptr;
    if (!is_jetbrains_mono(spec.file)) {
        fallback = set.load(fonts::kJetBrainsMonoRegular, spec.size_px);
        if (!fallback) {
            complete = false;
        }
    }

    const lv_font_t* font = set.load(spec.file, spec.size_px, fallback);
    if (font) {
        return font;
    }

    complete = false;
    return fallback ? fallback : built_in_font(spec.size_px);
}

} // namespace

ThemeEngine::ThemeEngine(app::AssetManager& assets, ThemeId initial)
    : assets_(assets), subject_(static_cast<int32_t>(initial)) {
    set_theme(initial);
}

ThemeEngine::~ThemeEngine() {
    // The display is left alone: by now it is gone or no longer drawn.
    fonts_.reset();
    retired_fonts_.clear();
}

const Theme& ThemeEngine::theme() const {
    return theme_;
}

bool ThemeEngine::fonts_complete() const {
    return fonts_complete_;
}

lv_subject_t* ThemeEngine::subject() {
    return subject_.native();
}

void ThemeEngine::attach_display(lv_display_t* display) {
    display_ = display;
    apply_display_theme();
}

bool ThemeEngine::set_theme(ThemeId id) {
    const ThemeSpec& spec = theme_spec(id);

    auto fonts = std::make_unique<app::FontSet>(assets_);
    bool complete = true;

    Theme next{};
    next.id = spec.id;
    next.name = spec.name;
    next.display_name = spec.display_name;
    next.colors = to_colors(spec.colors);
    next.metrics = spec.metrics;
    for (int role = 0; role < kFontRoleCount; ++role) {
        next.fonts[role] = load_role_font(*fonts, spec.fonts[role], complete);
    }

    // The old fonts stay loaded: screens built with them still exist until
    // the caller has rebuilt them and calls release_retired_fonts().
    if (fonts_) {
        retired_fonts_.push_back(std::move(fonts_));
    }
    fonts_ = std::move(fonts);
    theme_ = next;
    fonts_complete_ = complete;

    apply_display_theme();
    subject_.set(static_cast<int32_t>(spec.id));
    return fonts_complete_;
}

void ThemeEngine::release_retired_fonts() {
    retired_fonts_.clear();
}

void ThemeEngine::apply_display_theme() {
    if (!display_) {
        return;
    }

    lv_theme_t* lvgl_theme = lv_theme_default_init(display_,
                                                   theme_.colors.accent,
                                                   theme_.colors.accent2,
                                                   theme_.metrics.dark,
                                                   theme_.font(FontRole::Body));
    lv_display_set_theme(display_, lvgl_theme);
}

} // namespace view
