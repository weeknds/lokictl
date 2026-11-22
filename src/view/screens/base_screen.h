/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_viewmodel.h"
#include "help_popup.h"
#include "soft_key_bar.h"
#include "titlebar.h"
#include "lvgl.h"

#include <cstdint>
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
    // Standard: title bar on top and soft-key bar at the bottom.
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

    // Keys for this page, handed on by app::KeyController after the keys
    // every page shares (help, ESC, soft keys 4-8, F/X/Z/C as arrows). They
    // run inside an LVGL input callback, so they may only ask the view model
    // for something (a page change, for example, is carried out later); they
    // must never delete or build a screen.
    //
    // A key press for this page, already translated: LV_KEY_UP, LV_KEY_DOWN,
    // LV_KEY_LEFT, LV_KEY_RIGHT, LV_KEY_ENTER, LV_KEY_BACKSPACE or a
    // printable character. Returns true when the page used the key. The
    // default uses none.
    virtual bool on_key(uint32_t key);
    // A short ESC. Returns true when the page used it itself, for example to
    // end typing; otherwise the app goes back one page. The default returns
    // false.
    virtual bool on_back();
    // True while the user types on this page: letters stay letters and keys
    // 4-8 arrive as digits instead of pressing the soft keys. The default
    // returns false.
    virtual bool text_entry() const;
    // What fn+H shows on this page: a short name for the badge and at most
    // view::widgets::kHelpRowMax rows of keys and what they do. Like the
    // functions above it runs inside an LVGL input callback, so it may only
    // ask the view model for something. The default has no badge and no
    // rows.
    virtual view::widgets::HelpContent help() const;

protected:
    virtual void build_content(lv_obj_t* content) = 0;

    // What keys 4-8 do on this page (index 0-4 = keys 4-8), shown in the
    // soft-key bar of Standard pages. Called once from init(), before
    // build_content(); the default leaves all five keys unused. Actions run
    // inside an LVGL click event and may only ask the view model for
    // something. A screen that needs its own members here sets them before
    // it calls init().
    virtual view::widgets::SoftKeySet soft_keys();
    // The page's soft-key bar, for changing a key later with set_key();
    // nullptr on FullScreen pages.
    view::widgets::SoftKeyBar* soft_key_bar();

    viewmodel::BaseViewModel& view_model();
    const viewmodel::BaseViewModel& view_model() const;
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
    std::unique_ptr<view::widgets::SoftKeyBar> soft_key_bar_;
};

} // namespace screen
