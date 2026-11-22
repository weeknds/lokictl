/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace view {
class ThemeEngine;
}

namespace view::widgets {

// One line of the help panel: the keys on the left, what they do on the
// right.
struct HelpRow {
    std::string keys;
    std::string action;
};

// What the help panel shows for a page (see screen::BaseScreen::help()).
struct HelpContent {
    std::string badge;         // short page name in the header; empty: no badge
    std::vector<HelpRow> rows; // at most kHelpRowMax are shown
};

// Five rows is what fits the 170 px screen in every theme.
constexpr std::size_t kHelpRowMax = 5;

// The keyboard help panel for the current page, over a dimmed screen. It
// usually lives on the top layer, outside any screen, so it follows theme
// changes itself.
class HelpPopup : public BaseWidgets {
public:
    HelpPopup(lv_obj_t* parent, view::ThemeEngine& theme_engine);

    void build() override;
    // Shows `content`. Rows after the first kHelpRowMax are left out, with a
    // warning in the log. The panel keeps its own copy of the content.
    void show(const HelpContent& content);
    void hide();
    bool visible() const;

private:
    struct RowWidgets {
        lv_obj_t* row{nullptr};
        lv_obj_t* key{nullptr};
        lv_obj_t* action{nullptr};
    };

    void update_rows();
    // Sets the key column to the widest key text that is shown, so the
    // action column lines up in every theme.
    void update_key_column();
    void apply_theme();
    static void backdrop_clicked_cb(lv_event_t* event);
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    view::ThemeEngine& theme_engine_;
    HelpContent content_;
    lv_obj_t* panel_{nullptr};
    lv_obj_t* header_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* page_badge_{nullptr};
    lv_obj_t* rows_box_{nullptr};
    lv_obj_t* footer_{nullptr};
    std::array<RowWidgets, kHelpRowMax> rows_{};
};

} // namespace view::widgets
