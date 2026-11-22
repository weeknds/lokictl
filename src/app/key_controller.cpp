/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "key_controller.h"

#include "base_viewmodel.h"
#include "help_popup.h"
#include "linux_input.h"
#include "logger.h"
#include "screen_manager.h"
#include "screenshot_service.h"
#include "toast.h"

#include "lvgl.h"

#include <cstddef>
#include <string>
#include <utility>

namespace app {
namespace {

constexpr uint32_t kThemeToastDurationMs = 2500;
constexpr uint32_t kBatteryToastDurationMs = 5000;

bool is_soft_key(uint32_t key) {
    return key >= '4' && key <= '8';
}

} // namespace

KeyController::KeyController(viewmodel::BaseViewModel& view_model,
                             view::ThemeEngine& theme_engine,
                             ScreenManager& screen_manager)
    : view_model_(view_model),
      screen_manager_(screen_manager),
      toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), theme_engine)),
      help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), theme_engine)) {
    toast_->build();
    help_popup_->build();
    platform::set_global_key_listener(global_key_listener, this);
    platform::set_key_release_listener(key_release_listener, this);
}

KeyController::~KeyController() {
    platform::clear_key_release_listener(key_release_listener, this);
    platform::clear_global_key_listener(global_key_listener, this);
}

void KeyController::show_theme_switched(const char* display_name, bool saved) {
    toast_->show_highlighted("Theme: ",
                             display_name,
                             saved ? "" : " (not saved)",
                             saved ? view::widgets::ToastTone::Default
                                   : view::widgets::ToastTone::Warning,
                             kThemeToastDurationMs);
}

void KeyController::show_battery_warning(int32_t percent, bool very_low) {
    toast_->show_highlighted(very_low ? "Battery very low: " : "Battery low: ",
                             std::to_string(percent) + "%",
                             very_low ? ", charge soon" : "",
                             very_low ? view::widgets::ToastTone::Error
                                      : view::widgets::ToastTone::Warning,
                             kBatteryToastDurationMs);
}

void KeyController::show_dimming_not_saved() {
    toast_->show("Dim when idle: not saved", view::widgets::ToastTone::Warning, kThemeToastDurationMs);
}

void KeyController::set_wake_handler(std::function<bool()> handler) {
    wake_handler_ = std::move(handler);
}

void KeyController::handle_press(uint32_t key) {
    // The platform follows one held key at a time: once another key goes
    // down, the release of the earlier one is never reported. A key that is
    // still followed here is therefore given up, and its exit hint with it.
    stop_tracking();

    // 0. A key that only woke the screen does nothing else. It is not
    // followed, so letting it go (or holding it) does nothing either.
    if (wake_handler_ && wake_handler_()) {
        return;
    }

    // 1. and 2. Screenshot and help work on every page, also while typing.
    if (key == platform::kKeyPrintScreen) {
        take_screenshot();
        return;
    }
    if (key == platform::kKeyHelp) {
        toggle_help();
        return;
    }

    // 3. The help panel swallows every key. The ESC that closes it is not
    // followed, so letting it go does nothing more.
    if (help_popup_->visible()) {
        if (key == LV_KEY_ESC) {
            help_popup_->hide();
        }
        return;
    }

    // Asked at every key rather than remembered, because a theme switch
    // builds the page again and the page knows best whether it is typing.
    const bool typing = screen_manager_.text_entry_active();

    // 4. ESC acts when it is let go (or held long enough to quit), so a long
    // press can never also count as a short one. Whether it may quit is
    // decided now: the first page is the one with nothing below it, whatever
    // page that is.
    if (key == LV_KEY_ESC) {
        track(key, view_model_.page_depth() == 1 && !typing);
        return;
    }

    // 5. Keys 4-8 belong to the soft keys; an unused or switched-off key
    // does nothing, and the page never sees the digit.
    if (!typing && is_soft_key(key)) {
        platform::press_soft_key(static_cast<std::size_t>(key - '4'));
        return;
    }

    // 6. and 7. F/X/Z/C as arrows, then the page.
    if (!typing) {
        key = platform::arrow_for_letter(key);
    }
    screen_manager_.dispatch_key(key);
}

void KeyController::handle_long_press(uint32_t key) {
    if (tracked_key_ == 0 || key != tracked_key_) {
        return;
    }

    tracked_held_long_ = true;
    if (exit_armed_) {
        toast_->hide();
        view_model_.request_quit();
    }
}

void KeyController::handle_release(uint32_t key) {
    if (tracked_key_ == 0 || key != tracked_key_) {
        return;
    }

    // Back only for a short ESC that could not quit: an ESC held 700 ms or
    // longer on a deeper page does nothing, also when it is let go.
    const bool go_back = key == LV_KEY_ESC && !exit_armed_ && !tracked_held_long_;
    stop_tracking();
    if (go_back && !screen_manager_.dispatch_back()) {
        view_model_.go_back();
    }
}

void KeyController::take_screenshot() {
    const auto result = platform::screenshot::capture_active_screen();
    if (result.success) {
        LOG_INFO("screenshot saved: {}", result.path);
        toast_->show_highlighted("Saved to ",
                                 result.path,
                                 "",
                                 view::widgets::ToastTone::Success,
                                 5200,
                                 true);
    }
    else {
        LOG_WARN("screenshot failed: {}", result.error);
        toast_->show("Screenshot failed: " + result.error, view::widgets::ToastTone::Error, 3200);
    }
}

void KeyController::toggle_help() {
    toast_->hide();
    if (help_popup_->visible()) {
        help_popup_->hide();
    }
    else {
        help_popup_->show(screen_manager_.help());
    }
}

void KeyController::track(uint32_t key, bool armed) {
    tracked_key_ = key;
    exit_armed_ = armed;
    tracked_held_long_ = false;
    if (armed) {
        toast_->show_persistent_highlighted("Hold ",
                                            "ESC",
                                            " to exit",
                                            view::widgets::ToastTone::Warning);
    }
}

void KeyController::stop_tracking() {
    if (tracked_key_ != 0 && exit_armed_) {
        toast_->hide();
    }
    tracked_key_ = 0;
    exit_armed_ = false;
    tracked_held_long_ = false;
}

void KeyController::global_key_listener(uint32_t key,
                                        const char*,
                                        bool long_pressed,
                                        void* user_data) {
    auto* controller = static_cast<KeyController*>(user_data);
    if (!controller) {
        return;
    }

    if (long_pressed) {
        controller->handle_long_press(key);
    }
    else {
        controller->handle_press(key);
    }
}

void KeyController::key_release_listener(uint32_t key, const char*, void* user_data) {
    auto* controller = static_cast<KeyController*>(user_data);
    if (controller) {
        controller->handle_release(key);
    }
}

} // namespace app
