/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

namespace app {
class AssetManager;
}

namespace view {
class ThemeEngine;
}

namespace view::widgets {

// The page's title on the left; next to it a small WATCH tag while Watch
// runs (BaseViewModel::watch()), or a TRAP tag while the trap is set
// (BaseViewModel::trap(); it comes first); on the right the uplink's icon (Wi-Fi by
// signal, or the cable), the battery and the clock. Those come from the
// data layer (BaseViewModel::home()), so they read the same as on the home
// page, and the bar changes them in place when home_data_subject() counts
// a change, only where a text really changed. It reads no file and has no
// timer of its own.
class TitleBar : public BaseWidgets {
public:
    // Styled from theme_engine.theme() when built; see view::ThemeEngine.
    TitleBar(lv_obj_t* parent,
             viewmodel::BaseViewModel& view_model,
             app::AssetManager& assets,
             view::ThemeEngine& theme_engine);
    ~TitleBar() override;

    void build() override;

private:
    // Shows the values of view_model_.home().
    void refresh();
    static void data_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void watch_observer(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    view::ThemeEngine& theme_engine_;
    lv_obj_t* time_label_{nullptr};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* ethernet_label_{nullptr};
    lv_obj_t* battery_icon_label_{nullptr};
    lv_obj_t* battery_percent_label_{nullptr};
    lv_obj_t* watch_tag_{nullptr};
    int32_t tag_shown_{-1};         // 0 none, 1 Watch, 2 Trap
    bool battery_low_shown_{false}; // the battery is drawn in the warning colour
};

} // namespace view::widgets
