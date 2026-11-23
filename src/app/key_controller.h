/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "command_bar.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace viewmodel {
class BaseViewModel;
}

namespace view {
class ThemeEngine;
}

namespace view::widgets {
class CommandBar;
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
//  0. The wake handler (set_wake_handler()) sees every key first; when it
//     says the key only woke the screen, the key does nothing else.
//  1. fn+J takes a screenshot.
//  2. fn+H opens or closes the help panel, with the rows the page that is
//     shown gives (screen::BaseScreen::help()).
//  3. While the help panel is open, ESC closes it and every other key is
//     swallowed.
//  4. While the command bar is open, it takes every key: printable
//     characters type (keys 4-8 are digits, F/X/Z/C letters), Backspace
//     deletes, Enter runs the line (see command_bar.h), ESC closes the bar
//     at once, a TAB let go before 700 ms completes the first or second
//     word, and the real Up and Down step through the last lines that ran
//     (CommandHistory, memory only). A tool command opens the tool's page,
//     which then starts as its own key would (a first press: Ports and
//     Probe still ask first); `mode` opens the Modes page on that row and
//     asks for the switch as its key 6 does.
//  5. ESC: on the first page (nothing below it, and not typing) it shows
//     "Hold ESC to exit" while held and quits after 700 ms. Otherwise a
//     short ESC (let go before 700 ms) first goes to the page, which may use
//     it to end typing, and then goes back one page. A long ESC on a deeper
//     page does nothing: holding ESC belongs to the launcher.
//  6. TAB does nothing outside the command bar (a long TAB is reserved).
//  7. `:` opens the command bar (not while typing).
//  8. Keys 4-8 press the soft keys (not while typing).
//  9. F/X/Z/C become up/down/left/right (not while typing).
// 10. The page gets the key (screen::BaseScreen::on_key()).
//
// The platform also reports every key once more when it is still held after
// 700 ms. Only a held ESC that started step 5 acts on that call; a TAB in the
// command bar only notes it, so it completes nothing when let go. For every
// other key it does nothing, so a held soft key or arrow acts exactly once.
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

    // The message for a low battery at `percent`: "Battery low: 15%", or
    // with `very_low` "Battery very low: 5%, charge soon".
    void show_battery_warning(int32_t percent, bool very_low);

    // The message after the "Dim when idle" setting could not be saved.
    void show_dimming_not_saved();

    // The message after a device could not be blocked or unblocked, as the
    // data layer words it ("Block failed: helper not installed").
    void show_block_failed(const std::string& message);

    // The message after a USB Wi-Fi adapter was plugged in or removed: "USB
    // Wi-Fi adapter connected: wlan1" or "USB Wi-Fi adapter removed: wlan1".
    void show_usb_wifi(const std::string& interface_name, bool connected);

    // The message for a Watch alert: "Watch: Scan on you", or "Watch: 3 new
    // alerts" for several at once.
    void show_watch_alert(const std::string& text);

    // Called first for every key press (step 0). Returning true swallows
    // the key. Empty: no step 0.
    void set_wake_handler(std::function<bool()> handler);

private:
    void handle_press(uint32_t key);
    void handle_long_press(uint32_t key);
    void handle_release(uint32_t key);

    void take_screenshot();
    void toggle_help();
    // The command bar (step 4).
    void open_command_bar();
    void close_command_bar();
    void handle_command_key(uint32_t key);
    void complete_command_line();
    void run_command_line();
    void open_command_page(model::AppPage page);
    // A tool command. open_tool_page() opens its page unless it is shown
    // (false when the page path is full); start_command_tool() then sets
    // the typed target, if any, and asks the page to start
    // (BaseViewModel::request_tool_start()).
    bool open_tool_page(const CommandResult& result);
    void start_command_tool(const CommandResult& result);
    // `mode`: open_modes_page() opens the Modes page unless it is shown
    // (false when the page path is full); switch_command_mode() then puts
    // its highlight on the mode and asks for the switch.
    bool open_modes_page();
    void switch_command_mode(const CommandResult& result);
    // Up and Down in the command bar.
    void recall_command(bool older);
    // Follows ESC (or TAB in the command bar) from its press to its release.
    // `armed`: holding it quits the app.
    void track(uint32_t key, bool armed);
    // Forgets the followed key and hides the exit hint if it was showing.
    void stop_tracking();

    static void global_key_listener(uint32_t key,
                                    const char* key_name,
                                    bool long_pressed,
                                    void* user_data);
    static void key_release_listener(uint32_t key, const char* key_name, void* user_data);

    viewmodel::BaseViewModel& view_model_;
    view::ThemeEngine& theme_engine_;
    ScreenManager& screen_manager_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    std::unique_ptr<view::widgets::CommandBar> command_bar_;
    std::string command_line_; // what is typed in the command bar
    CommandHistory command_history_;
    std::function<bool()> wake_handler_;
    uint32_t tracked_key_{0};      // ESC, or TAB in the command bar, while held; 0 for none
    bool exit_armed_{false};       // holding the tracked key quits the app
    bool tracked_held_long_{false};
};

} // namespace app
