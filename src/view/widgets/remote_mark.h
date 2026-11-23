/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"

namespace view {
class ThemeEngine;
}

namespace view::widgets {

// The "being viewed" mark: a small REMOTE tag in the top-left corner that
// shows on every page while a browser is paired with Remote view.
//
// It sits on the top layer, outside any screen, at the same place on every
// page: over the app name in the title bar, and over the app name, the
// sample mark or the first pane's title on the Home pages. The middle and
// the right of the top edge stay free (Home's notes, the clock and the
// battery). It never changes by itself, so a still screen stays still, and
// it follows theme changes itself (like RemotePanel). It takes no clicks.
class RemoteMark : public BaseWidgets {
public:
    RemoteMark(lv_obj_t* parent, view::ThemeEngine& theme_engine);

    void build() override;
    // Both do nothing when the mark already shows, or is already hidden.
    void show();
    void hide();
    bool visible() const;

private:
    void apply_theme();
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    view::ThemeEngine& theme_engine_;
    lv_obj_t* label_{nullptr};
};

} // namespace view::widgets
