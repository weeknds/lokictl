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
    void refresh_time();
    void refresh_device_status();
    static void refresh_timer_cb(lv_timer_t* timer);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    view::ThemeEngine& theme_engine_;
    lv_obj_t* time_label_{nullptr};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* ethernet_label_{nullptr};
    lv_obj_t* battery_icon_label_{nullptr};
    lv_obj_t* battery_percent_label_{nullptr};
    lv_timer_t* refresh_timer_{nullptr};
    uint32_t refresh_tick_{0};
};

} // namespace view::widgets
