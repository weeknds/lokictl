/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <memory>

namespace viewmodel {
class BaseViewModel;
}

namespace view {
class ThemeEngine;
}

namespace view::widgets {
class HelpPopup;
class Toast;
} // namespace view::widgets

namespace app {

class ScreenManager;

// Decides what every key does, the same way in the simulator, on the device
// and in snapshot runs with --keys. platform/linux_input only reads the keys
// and times long presses; this class gets each key from there.
//
// A key press goes through these steps, in this order, and the first one
// that takes the key ends it:
//  1. fn+J takes a screenshot.
//  2. fn+H opens or closes the help panel, with the rows the page that is
//     shown gives (screen::BaseScreen::help()).
//  3. While the help panel is open, ESC closes it and every other key is
//     swallowed.
//  4. ESC: on the first page (nothing below it, and not typing) it shows
//     "Hold ESC to exit" while held and quits after 700 ms. Otherwise a
//     short ESC (let go before 700 ms) first goes to the page, which may use
//     it to end typing, and then goes back one page. A long ESC on a deeper
//     page does nothing: holding ESC belongs to the launcher.
//  5. Key 4 on the demo home page is that page's hold-to-exit key.
//  6. Keys 4-8 press the soft keys (not while typing).
//  7. F/X/Z/C become up/down/left/right (not while typing).
//  8. The page gets the key (screen::BaseScreen::on_key()).
//
// The platform also reports every key once more when it is still held after
// 700 ms. Only a held ESC or home key 4 that started step 4 or 5 acts on that
// call; for every other key it does nothing, so a held soft key or arrow
// acts exactly once.
//
// Everything here runs inside LVGL input callbacks (or, in snapshot runs,
// between LVGL steps): it only asks the view model or the page for
// something. Page changes and theme switches are carried out later.
//
// Create it after ScreenManager::start() and destroy it before the
// ScreenManager.
class KeyController {
public:
    KeyController(viewmodel::BaseViewModel& view_model,
                  view::ThemeEngine& theme_engine,
                  ScreenManager& screen_manager);
    ~KeyController();

    KeyController(const KeyController&) = delete;
    KeyController& operator=(const KeyController&) = delete;

    // The message after the user picked a theme. The toast has already
    // taken the new theme's look by then. `saved` is false when the choice
    // could not be written to the settings file.
    void show_theme_switched(const char* display_name, bool saved);

private:
    void handle_press(uint32_t key);
    void handle_long_press(uint32_t key);
    void handle_release(uint32_t key);

    void take_screenshot();
    void toggle_help();
    // Follows ESC or home key 4 from its press to its release. `armed`:
    // holding it quits the app.
    void track(uint32_t key, bool armed);
    // Forgets the followed key and hides the exit hint if it was showing.
    void stop_tracking();

    static void global_key_listener(uint32_t key,
                                    const char* key_name,
                                    bool long_pressed,
                                    void* user_data);
    static void key_release_listener(uint32_t key, const char* key_name, void* user_data);

    viewmodel::BaseViewModel& view_model_;
    ScreenManager& screen_manager_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    uint32_t tracked_key_{0};      // ESC or home key 4 while held; 0 for none
    bool exit_armed_{false};       // holding the tracked key quits the app
    bool tracked_held_long_{false};
};

} // namespace app
