/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"
#include "home_layout.h"

#include <cstdint>
#include <memory>

namespace screen {

// The home page, the first page of the app. It has no title bar: the whole
// 320x140 area above the soft-key bar belongs to the page, which shows the
// clock and the battery itself. What it draws depends on the theme; each
// theme has its own HomeLayout:
// - Instrument: a big-type gauge with the mode word, three large numbers,
//   the four mode chips and the selected client's line;
// - Constellation: a network map with the internet, this device and its
//   clients, and a panel with the mode, the traffic and the selected client;
// - Terminal: three boxed panes, net (traffic graphs and uplink), mode and a
//   table of clients with the selected row filled;
// - a theme without a layout of its own gets the Instrument one.
// Every layout shows the same values, the mode, the uplink, the access
// point, the traffic and the connected clients, and marks one client as
// selected.
//
// Keys are the same in every theme. Left and up (Z/F) select the previous
// client and right and down (C/X) the next one; the selection stops at the
// first and the last client shown. Key 6 (Setup) opens Settings. Keys 4, 5
// and 7 (Modes, Scan, Tools) are shown switched off until their pages exist,
// and key 8 is empty. Holding ESC quits, as on every first page.
//
// The values come from the view model (home()), which takes them from the
// model; for now they are sample values, and the page says so with a
// "sample data" mark. The selection lives in the view model as well
// (home_selection_subject()), so it is kept when Settings is opened and
// closed and across a theme switch, which builds the page again. Nothing on
// the page reads the clock or the device, so the same state always gives
// the same picture.
class HomeScreen : public BaseScreen {
public:
    HomeScreen(viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               view::ThemeEngine& theme_engine);
    ~HomeScreen() override;

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    // The page's help, also shown by the dev-help page.
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // What the layout draws with, made fresh for each call.
    HomeContext context();

    // Marks the selected client. The user data is the screen; the observer
    // is bound to the content area, so it goes when the screen goes.
    static void selection_observer(lv_observer_t* observer, lv_subject_t* subject);

    std::unique_ptr<HomeLayout> layout_;
    lv_obj_t* home_content_{nullptr};
};

} // namespace screen
