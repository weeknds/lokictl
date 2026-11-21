/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "navbar.h"
#include "base_viewmodel.h"
#include "titlebar.h"
#include "lvgl.h"

#include <memory>

namespace app {
class AssetManager;
}

namespace view {
class ThemeEngine;
struct Theme;
} // namespace view

namespace screen {

class BaseScreen {
public:
    // Standard: title bar on top and navigation bar at the bottom.
    // FullScreen: no bars; the content area covers the whole 320x170 screen.
    enum class Layout {
        Standard,
        FullScreen,
    };

    // Screens are styled from theme_engine.theme() when they are built and
    // are built again when the theme changes.
    BaseScreen(viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               view::ThemeEngine& theme_engine);
    virtual ~BaseScreen();

    BaseScreen(const BaseScreen&) = delete;
    BaseScreen& operator=(const BaseScreen&) = delete;

    void init(Layout layout = Layout::Standard);
    lv_obj_t* root() const;

protected:
    virtual void build_content(lv_obj_t* content) = 0;

    viewmodel::BaseViewModel& view_model();
    app::AssetManager& assets();
    const view::Theme& theme() const;
    view::ThemeEngine& theme_engine();

private:
    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    view::ThemeEngine& theme_engine_;
    lv_obj_t* root_{nullptr};
    lv_obj_t* content_{nullptr};
    std::unique_ptr<view::widgets::TitleBar> title_bar_;
    std::unique_ptr<view::widgets::NavBar> nav_bar_;
};

} // namespace screen
