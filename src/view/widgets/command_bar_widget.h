/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"

#include <string>

namespace view {
class ThemeEngine;
}

namespace view::widgets {

enum class CommandBarTone {
    Choices, // muted: the commands TAB found
    Error,   // the error colour: what went wrong
};

// The command bar: one line at the bottom of the screen, as tall as the
// soft-key bar and drawn over it, with a `:` prompt, the typed line and a
// cursor. A message line above it shows choices or an error when there is
// one. It only shows what it is given: app::KeyController reads the keys.
// It usually lives on the top layer, outside any screen, so it follows theme
// changes itself.
class CommandBar : public BaseWidgets {
public:
    CommandBar(lv_obj_t* parent, view::ThemeEngine& theme_engine);

    void build() override;
    // Shows the bar with an empty line and no message.
    void show();
    void hide();
    bool visible() const;
    void set_line(const std::string& line);
    // An empty message hides the message line.
    void set_message(const std::string& message, CommandBarTone tone);

private:
    void apply_theme();
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    view::ThemeEngine& theme_engine_;
    CommandBarTone tone_{CommandBarTone::Choices};
    lv_obj_t* message_box_{nullptr};
    lv_obj_t* message_{nullptr};
    lv_obj_t* input_row_{nullptr};
    lv_obj_t* prompt_{nullptr};
    lv_obj_t* line_{nullptr};
    lv_obj_t* cursor_{nullptr};
    lv_obj_t* hint_{nullptr};
};

} // namespace view::widgets
