/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "theme.h"

#include "lvgl.h"

#include <memory>
#include <vector>

namespace app {
class AssetManager;
class FontSet;
} // namespace app

namespace view {

// Owns the active theme: its colours, metrics and loaded fonts.
//
// How screens use it:
// - Screens and widgets read theme() while they are built and set their
//   styles directly. They do not watch for theme changes; when the theme
//   changes, the current screen is built again from scratch.
// - Objects that outlive screens (toasts, popups on the top layer) observe
//   subject() and restyle themselves, fonts included, when it fires.
//
// Lifetimes:
// - The reference returned by theme() stays valid for the engine's whole
//   life; its contents change on set_theme().
// - The font pointers in theme() stay valid until release_retired_fonts()
//   is called after the next set_theme(). Rebuild or restyle everything that
//   uses them before that call.
// - Create the engine after lv_init() and the AssetManager, and destroy it
//   after every LVGL object that uses its fonts.
class ThemeEngine {
public:
    ThemeEngine(app::AssetManager& assets, ThemeId initial);
    ~ThemeEngine();

    ThemeEngine(const ThemeEngine&) = delete;
    ThemeEngine& operator=(const ThemeEngine&) = delete;

    const Theme& theme() const;
    // False if any font file of the active theme could not be loaded. The
    // role then uses its fallback font or a built-in font, so some text or
    // symbols look wrong.
    bool fonts_complete() const;
    // Integer subject holding the active ThemeId. It is notified after a
    // set_theme() that changes the theme.
    lv_subject_t* subject();

    // Applies LVGL's default theme for this theme to `display` now and again
    // after every set_theme(). nullptr detaches.
    void attach_display(lv_display_t* display);

    // Loads the fonts of `id`, makes it the active theme and notifies
    // subject(). The previous theme's fonts stay loaded until
    // release_retired_fonts(). Returns fonts_complete().
    bool set_theme(ThemeId id);
    // Gives back the fonts of earlier themes. Call it once nothing uses them
    // any more (after the screen has been rebuilt).
    void release_retired_fonts();

private:
    void apply_display_theme();

    app::AssetManager& assets_;
    std::unique_ptr<app::FontSet> fonts_;
    std::vector<std::unique_ptr<app::FontSet>> retired_fonts_;
    Theme theme_{};
    reactive::IntSubject subject_;
    lv_display_t* display_{nullptr};
    bool fonts_complete_{true};
};

} // namespace view
