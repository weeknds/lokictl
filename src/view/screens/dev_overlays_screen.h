/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"
#include "command_bar_widget.h"
#include "help_popup.h"
#include "toast.h"

#include <array>
#include <cstddef>
#include <memory>

namespace screen {

// Developer pages that show the widgets which normally float above every
// screen, so they can be checked in a snapshot:
// - dev-help: the keyboard help panel for the home page;
// - dev-toast: three toasts, one per highlight tone;
// - dev-command: two command bars, an empty one with an error and one with
//   the choices TAB found after `ping ` (more than fit, ending in a count).
// The widgets are placed on the page itself instead of the top layer, which
// snapshots do not capture. Opened by name only (snapshot --page); not
// reachable from the normal UI.
class DevOverlaysScreen : public BaseScreen {
public:
    static constexpr std::size_t kToastCount = 3;
    static constexpr std::size_t kCommandBarCount = 2;

    DevOverlaysScreen(viewmodel::BaseViewModel& view_model,
                      app::AssetManager& assets,
                      view::ThemeEngine& theme_engine,
                      model::AppPage page);
    ~DevOverlaysScreen() override;

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;
    void build_help(lv_obj_t* content);
    void build_toasts(lv_obj_t* content);
    void build_command_bars(lv_obj_t* content);

    model::AppPage page_;
    // Destroyed in ~DevOverlaysScreen(), before the base class deletes the
    // screen and with it the widgets' LVGL objects.
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    std::array<std::unique_ptr<view::widgets::Toast>, kToastCount> toasts_;
    std::array<std::unique_ptr<view::widgets::CommandBar>, kCommandBarCount> command_bars_;
};

} // namespace screen
