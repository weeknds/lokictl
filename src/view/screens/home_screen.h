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
// Every layout has two views. With an access point (HomeData::ap_active) it
// draws the router view: the mode, the uplink, the access point, the
// traffic and the connected clients, with one client marked as selected.
// Without one it draws the link view: no clients, and the uplink's own
// details (Wi-Fi, cable or no network) in their place.
//
// Keys are the same in every theme. Left and up (Z/F) select the previous
// client and right and down (C/X) the next one; the selection stops at the
// first and the last client shown, and without clients they do nothing. Key
// 6 (Setup) opens Settings. Keys 4, 5 and 7 (Modes, Scan, Tools) are shown
// switched off until their pages exist, and key 8 is empty. Holding ESC
// quits, as on every first page.
//
// The values come from the view model (home()), which the data layer fills
// (see app::DataController). The page keeps a copy of the block its layout
// shows. When home_data_subject() counts a change and the new block differs
// only in the values of home_values_only() (clock, battery, the uplink's
// values and the traffic), the layout's update() retexts the labels and
// resizes the bars that already exist.
// Any other change, a new situation, empties the content area and builds the
// layout again, once, with the new values and the current selection. The
// selection lives in the view model as well (home_selection_subject()), so
// it is kept when Settings is opened and closed and across a theme switch,
// which builds the page again with the current values. The page itself never
// reads the clock or the device, so the same values always give the same
// picture.
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

    // Makes the layout for the current theme, builds it into the content
    // area with the view model's values and remembers them in shown_.
    void build_layout();

    // Shows new values in place (HomeLayout::update()), or builds the
    // layout again for a new situation (see home_values_only()). The user
    // data is the screen; the observer is bound to the content area, so it
    // goes when the screen goes.
    static void data_observer(lv_observer_t* observer, lv_subject_t* subject);

    // Marks the selected client, unless the layout still shows another
    // situation than the view model holds (the data observer then follows
    // and builds it again). The user data is the screen; the observer is
    // bound to the content area, so it goes when the screen goes.
    static void selection_observer(lv_observer_t* observer, lv_subject_t* subject);

    std::unique_ptr<HomeLayout> layout_;
    lv_obj_t* home_content_{nullptr};
    model::HomeData shown_; // the values the layout shows now
};

} // namespace screen
