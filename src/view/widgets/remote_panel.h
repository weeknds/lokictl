/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"

#include <cstdint>
#include <string>

namespace view {
class ThemeEngine;
}

namespace view::widgets {

// The Remote view panel: a box over the page, between the title bar and the
// soft-key bar, that tells the user what to open in a browser and which
// code to type there:
//
//   REMOTE VIEW                    1:52 left
//        Open http://10.0.0.12:8780
//               482 913
//
// It can also show a short message instead (why Remote view could not
// start). It lives on the top layer, outside any screen, so it follows
// theme changes itself (like HelpPopup), and the keys still reach the page
// under it.
class RemotePanel : public BaseWidgets {
public:
    RemotePanel(lv_obj_t* parent, view::ThemeEngine& theme_engine);

    void build() override;
    // Shows `url` and the six-digit `code` (drawn as "482 913") with the
    // code's time left. Calling it again with the same values redraws
    // nothing.
    void show_code(const std::string& url, const std::string& code, int32_t seconds_left);
    // Shows `message` in place of the address and the code.
    void show_message(const std::string& message);
    void hide();
    bool visible() const;

private:
    void apply_theme();
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    view::ThemeEngine& theme_engine_;
    lv_obj_t* header_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* time_left_{nullptr};
    lv_obj_t* address_row_{nullptr};
    lv_obj_t* open_{nullptr};
    lv_obj_t* url_{nullptr};
    lv_obj_t* code_{nullptr};
    lv_obj_t* message_{nullptr};
};

} // namespace view::widgets
