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

// The page's title on the left; on the right the uplink's icon (Wi-Fi by
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

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    view::ThemeEngine& theme_engine_;
    lv_obj_t* time_label_{nullptr};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* ethernet_label_{nullptr};
    lv_obj_t* battery_icon_label_{nullptr};
    lv_obj_t* battery_percent_label_{nullptr};
    bool battery_low_shown_{false}; // the battery is drawn in the warning colour
};

} // namespace view::widgets
