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
    void show_apple_screen();
    void show_butter_screen();
    void show_dev_fonts_screen(model::AppPage page);
    void show_dev_theme_screen();
    void show_dev_overlays_screen(model::AppPage page);
    void request_page(model::AppPage page);
    void flush_requested_page();
    lv_obj_t* current_screen() const;

private:
    void load_screen(std::unique_ptr<screen::BaseScreen> next_screen);

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
