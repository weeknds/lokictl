/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asset_manager.h"
#include "base_screen.h"
#include "base_viewmodel.h"
#include "lvgl.h"

#include <memory>

namespace view {
class ThemeEngine;
}

namespace app {

class ScreenManager {
public:
    ScreenManager(viewmodel::BaseViewModel& view_model,
                  AssetManager& assets,
                  view::ThemeEngine& theme_engine);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    void start();
    // Each show_* function builds its page, loads it and deletes the screen
    // shown before. They return false when the page could not be built; the
    // old screen then stays.
    bool show_apple_screen();
    bool show_butter_screen();
    bool show_dev_fonts_screen(model::AppPage page);
    bool show_dev_theme_screen();
    bool show_dev_overlays_screen(model::AppPage page);
    bool show_dev_softkeys_screen();
    void request_page(model::AppPage page);
    void flush_requested_page();
    // Builds the current page again from scratch, after a theme change, and
    // then deletes the old screen. A page change requested in the same tick
    // is honoured. Before the first page has loaded it does nothing and
    // returns true, because that pending load already uses the active theme.
    // Returns false when the page could not be built; the old screen then
    // stays. It deletes the active screen, so it must not be called from an
    // LVGL event or observer callback.
    bool rebuild_current_page();
    lv_obj_t* current_screen() const;

private:
    bool show_page(model::AppPage page);
    bool load_screen(std::unique_ptr<screen::BaseScreen> next_screen);
    // Records `page` as shown, after its screen has loaded.
    void page_loaded(model::AppPage page);

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    view::ThemeEngine& theme_engine_;
    std::unique_ptr<screen::BaseScreen> current_screen_;
    lv_observer_t* page_observer_ = nullptr;
    model::AppPage requested_page_ = model::AppPage::Apple;
    model::AppPage loaded_page_ = model::AppPage::Apple;
    bool has_loaded_page_ = false;
    bool page_switch_scheduled_ = false;
};

} // namespace app
