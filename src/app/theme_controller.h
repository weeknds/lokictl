/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "theme.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace viewmodel {
class BaseViewModel;
}

namespace view {
class ThemeEngine;
}

namespace app {

class AssetManager;
class ScreenManager;

// Switches the theme while the app runs. It listens for the view model's
// next-theme requests (key 6) and can also be asked directly (snapshot
// --switch-to uses that, so both take the same path).
//
// A switch has three steps, in this order:
// 1. ThemeEngine::set_theme() loads the new theme's fonts, makes it the
//    active theme and notifies ThemeEngine::subject(). Objects on the top
//    layer, such as the toast and the help panel, restyle themselves then.
// 2. ScreenManager::rebuild_current_page() builds the page again from
//    scratch in the new theme and deletes the old screen.
// 3. ThemeEngine::release_retired_fonts() frees the old theme's fonts. This
//    only happens when step 2 worked: otherwise the old screen still uses
//    them, and they stay loaded.
//
// Requests never switch at once. Key 6 arrives as a click on a soft key
// that the rebuild deletes, and the request also passes through an
// observer callback, which must not delete the screen it runs under. So a
// request only records the target and schedules one lv_async_call(); the
// switch runs from lv_timer_handler() afterwards, like a page change.
// Several requests before that fold into one switch.
//
// Create it after the ScreenManager and destroy it before; everything runs
// on the UI thread.
class ThemeController {
public:
    struct SwitchResult {
        view::ThemeId previous;
        view::ThemeId current;
        bool fonts_complete;       // ThemeEngine::fonts_complete() after the switch
        uint32_t duration_ms;      // time the three steps took
        std::size_t fonts_loaded;  // AssetManager::loaded_font_count() afterwards
    };
    // Called on the UI thread after every completed switch.
    using Listener = std::function<void(const SwitchResult&)>;

    ThemeController(viewmodel::BaseViewModel& view_model,
                    AssetManager& assets,
                    view::ThemeEngine& theme_engine,
                    ScreenManager& screen_manager);
    ~ThemeController();

    ThemeController(const ThemeController&) = delete;
    ThemeController& operator=(const ThemeController&) = delete;

    void set_listener(Listener listener);

    // Asks for a switch to `id`, or to the theme after the one that is active
    // or already asked for. Safe to call from LVGL event and observer
    // callbacks. A request for the theme that is already active does nothing.
    void request(view::ThemeId id);
    void request_next();

private:
    static void apply_pending_async(void* user_data);
    static void next_theme_requested_cb(lv_observer_t* observer, lv_subject_t* subject);

    void schedule();
    void apply_pending();

    AssetManager& assets_;
    view::ThemeEngine& theme_engine_;
    ScreenManager& screen_manager_;
    Listener listener_;
    std::optional<view::ThemeId> pending_;
    bool apply_scheduled_{false};
    int32_t seen_request_count_{0};
    lv_observer_t* request_observer_{nullptr};
};

} // namespace app
