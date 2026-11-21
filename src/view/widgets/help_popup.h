/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_widget.h"

#include <array>

namespace view {
class ThemeEngine;
}

namespace view::widgets {

// The keyboard help panel for the current page, over a dimmed screen. It
// usually lives on the top layer, outside any screen, so it follows theme
// changes itself.
class HelpPopup : public BaseWidgets {
public:
    HelpPopup(lv_obj_t* parent, view::ThemeEngine& theme_engine);

    void build() override;
    void show(model::AppPage page);
    void hide();
    bool visible() const;

private:
    struct RowWidgets {
        lv_obj_t* row{nullptr};
        lv_obj_t* key{nullptr};
        lv_obj_t* action{nullptr};
    };

    void update_rows(model::AppPage page);
    void apply_theme();
    static void backdrop_clicked_cb(lv_event_t* event);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    view::ThemeEngine& theme_engine_;
    lv_obj_t* panel_{nullptr};
    lv_obj_t* header_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* page_badge_{nullptr};
    lv_obj_t* rows_box_{nullptr};
    lv_obj_t* footer_{nullptr};
    std::array<RowWidgets, 5> rows_{};
};

} // namespace view::widgets
