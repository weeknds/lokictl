/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "key_controller.h"

#include "base_viewmodel.h"
#include "command_bar_widget.h"
#include "help_popup.h"
#include "linux_input.h"
#include "logger.h"
#include "page_registry.h"
#include "screen_manager.h"
#include "screenshot_service.h"
#include "theme.h"
#include "theme_engine.h"
#include "toast.h"

#include "lvgl.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

namespace app {
namespace {

constexpr uint32_t kThemeToastDurationMs = 2500;
constexpr uint32_t kBatteryToastDurationMs = 5000;
constexpr uint32_t kBlockToastDurationMs = 4000;
constexpr const char* kPagePathFull = "Too many pages open: go back first";

bool is_soft_key(uint32_t key) {
    return key >= '4' && key <= '8';
}

} // namespace

KeyController::KeyController(viewmodel::BaseViewModel& view_model,
                             view::ThemeEngine& theme_engine,
                             ScreenManager& screen_manager)
    : view_model_(view_model),
      theme_engine_(theme_engine),
      screen_manager_(screen_manager),
      toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), theme_engine)),
      help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), theme_engine)),
      command_bar_(std::make_unique<view::widgets::CommandBar>(lv_layer_top(), theme_engine)) {
    toast_->build();
    command_bar_->build();
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

void KeyController::show_block_failed(const std::string& message) {
    toast_->show(message, view::widgets::ToastTone::Error, kBlockToastDurationMs);
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

    // 4. The command bar takes every key while it is open.
    if (command_bar_->visible()) {
        handle_command_key(key);
        return;
    }

    // Asked at every key rather than remembered, because a theme switch
    // builds the page again and the page knows best whether it is typing.
    const bool typing = screen_manager_.text_entry_active();

    // 5. ESC acts when it is let go (or held long enough to quit), so a long
    // press can never also count as a short one. Whether it may quit is
    // decided now: the first page is the one with nothing below it, whatever
    // page that is.
    if (key == LV_KEY_ESC) {
        track(key, view_model_.page_depth() == 1 && !typing);
        return;
    }

    // 6. TAB only means something in the command bar.
    if (key == LV_KEY_NEXT) {
        return;
    }

    // 7. The command bar opens on any page that isn't typing.
    if (!typing && key == ':') {
        open_command_bar();
        return;
    }

    // 8. Keys 4-8 belong to the soft keys; an unused or switched-off key
    // does nothing, and the page never sees the digit.
    if (!typing && is_soft_key(key)) {
        platform::press_soft_key(static_cast<std::size_t>(key - '4'));
        return;
    }

    // 9. and 10. F/X/Z/C as arrows, then the page.
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

    // TAB in the command bar completes only when it was let go in time.
    if (key == LV_KEY_NEXT) {
        const bool complete = !tracked_held_long_ && command_bar_->visible();
        stop_tracking();
        if (complete) {
            complete_command_line();
        }
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

void KeyController::open_command_bar() {
    toast_->hide();
    command_line_.clear();
    command_history_.reset();
    command_bar_->show();
}

void KeyController::close_command_bar() {
    command_line_.clear();
    command_bar_->hide();
}

void KeyController::handle_command_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ESC:
            // Not followed, so holding it neither quits nor shows the hint.
            close_command_bar();
            return;
        case LV_KEY_NEXT:
            track(key, false);
            return;
        case LV_KEY_ENTER:
            run_command_line();
            return;
        case LV_KEY_UP:
        case LV_KEY_DOWN:
            recall_command(key == LV_KEY_UP);
            return;
        case LV_KEY_BACKSPACE:
            if (!command_line_.empty()) {
                command_line_.pop_back();
            }
            command_history_.reset();
            break;
        default:
            if (!is_command_character(key) || command_line_.size() >= kCommandMaxLength) {
                return;
            }
            command_line_ += static_cast<char>(key);
            command_history_.reset();
            break;
    }
    command_bar_->set_line(command_line_);
    command_bar_->set_message("", view::widgets::CommandBarTone::Choices);
}

void KeyController::complete_command_line() {
    const CommandCompletion completion = complete_command(command_line_, view_model_.tools(), view_model_.home());
    command_line_ = completion.line.substr(0, kCommandMaxLength);
    command_history_.reset();
    command_bar_->set_line(command_line_);
    command_bar_->set_message(command_choices_text(completion.choices), view::widgets::CommandBarTone::Choices);
}

void KeyController::recall_command(bool older) {
    std::string line;
    if (!(older ? command_history_.older(command_line_, line) : command_history_.newer(line))) {
        return;
    }
    command_line_ = line.substr(0, kCommandMaxLength);
    command_bar_->set_line(command_line_);
    command_bar_->set_message("", view::widgets::CommandBarTone::Choices);
}

void KeyController::run_command_line() {
    const CommandResult result = parse_command(command_line_, view_model_.home());
    if (result.action == CommandAction::Error) {
        command_bar_->set_message(result.message, view::widgets::CommandBarTone::Error);
        return;
    }
    // Refused as the pages refuse it; the bar stays open and says why.
    std::string refusal;
    if (result.action == CommandAction::Tool) {
        refusal = tool_command_refusal(result.tool, view_model_.tools(), view_model_.home());
        if (refusal.empty() && !open_tool_page(result)) {
            refusal = kPagePathFull;
        }
    } else if (result.action == CommandAction::Mode) {
        refusal = mode_command_refusal(result.mode, view_model_.home());
        if (refusal.empty() && !open_modes_page()) {
            refusal = kPagePathFull;
        }
    }
    if (!refusal.empty()) {
        command_bar_->set_message(refusal, view::widgets::CommandBarTone::Error);
        return;
    }

    // Only lines that ran are kept for Up.
    command_history_.add(command_line_);
    close_command_bar();
    switch (result.action) {
        case CommandAction::Help:
            LOG_INFO("command: help");
            help_popup_->show(command_help());
            return;
        case CommandAction::Theme: {
            const auto& spec = view::theme_spec(static_cast<view::ThemeId>(result.theme_row));
            LOG_INFO("command: theme {}", spec.name);
            if (spec.id == theme_engine_.theme().id) {
                // Nothing to switch; the usual message says which one it is.
                toast_->show_highlighted("Theme: ",
                                         spec.display_name,
                                         "",
                                         view::widgets::ToastTone::Default,
                                         kThemeToastDurationMs);
                return;
            }
            view_model_.request_theme(result.theme_row);
            return;
        }
        case CommandAction::OpenPage:
            open_command_page(result.page);
            return;
        case CommandAction::Mode:
            switch_command_mode(result);
            return;
        case CommandAction::Tool:
            start_command_tool(result);
            return;
        case CommandAction::None:
        case CommandAction::Error:
            return;
    }
}

void KeyController::open_command_page(model::AppPage page) {
    LOG_INFO("command: {}", page_name(page));
    // The same ways in as the pages' own keys, so each page starts the way
    // it does from there.
    switch (page) {
        case model::AppPage::Map:
            view_model_.open_map();
            return;
        case model::AppPage::Tools:
            view_model_.open_tools();
            return;
        case model::AppPage::Discover:
            view_model_.open_discover();
            return;
        case model::AppPage::Wifi:
            view_model_.open_wifi();
            return;
        case model::AppPage::Status:
            view_model_.open_status();
            return;
        case model::AppPage::Settings:
            view_model_.open_settings();
            return;
        default:
            view_model_.open_page(page);
            return;
    }
}

bool KeyController::open_tool_page(const CommandResult& result) {
    // Nothing to open when the page is shown already.
    return view_model_.current_page() == result.page || view_model_.open_tool(result.tool);
}

void KeyController::start_command_tool(const CommandResult& result) {
    // The command only: targets, like everything else from the user's
    // network, stay out of the log.
    LOG_INFO("command: {}", result.tool == model::ToolKind::Listen ? "scan" : model::tool_name(result.tool));
    if (!result.target.empty()) {
        view_model_.set_tool_typed_target(result.tool, result.target);
        view_model_.set_tool_target(result.tool, viewmodel::BaseViewModel::kToolTargetTyped);
    }
    // The page (shown now, or built later from the request above) takes it
    // and does its own key's first press.
    view_model_.request_tool_start(result.tool);
}

bool KeyController::open_modes_page() {
    // Nothing to open when the page is shown already.
    return view_model_.current_page() == model::AppPage::Modes || view_model_.open_modes();
}

void KeyController::switch_command_mode(const CommandResult& result) {
    std::string name = model::net_mode_name(result.mode);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    LOG_INFO("command: mode {}", name);
    // As the Modes page's key 6 on that row: the highlight first (the
    // page's "Switching…" line leans on it), then the request.
    view_model_.select_mode_row(static_cast<int32_t>(result.mode));
    view_model_.request_mode(result.mode);
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
