/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */


#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <cstdint>
#include <string>

namespace view {
class ThemeEngine;
}

namespace view::widgets {

class IconButton : public BaseWidgets {

public:
    // The icon is drawn in the theme's text colour, or in its muted colour
    // while the button is disabled.
    IconButton(lv_obj_t* parent,
               viewmodel::BaseViewModel& view_model,
               view::ThemeEngine& theme_engine,
               int32_t width,
               int32_t height,
               const char* text,
               const lv_font_t* font,
               lv_event_cb_t click_cb = nullptr,
               void* user_data = nullptr,
               bool show_indicator = false);

    void build() override;
    void set_text(const char* text);
    void set_font(const lv_font_t* font);
    void set_enabled(bool enabled);

private:
    void apply_colors();
    void apply_indicator_style();

    viewmodel::BaseViewModel& view_model_;
    view::ThemeEngine& theme_engine_;
    int32_t width_;
    int32_t height_;
    std::string text_;
    const lv_font_t* font_;
    lv_event_cb_t click_cb_;
    void* user_data_;
    bool show_indicator_{false};
    lv_obj_t* label_{nullptr};
    lv_obj_t* indicator_{nullptr};
    bool enabled_{true};

};

} // namespace view::widgets
