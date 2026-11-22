/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme_controller.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "logger.h"
#include "screen_manager.h"
#include "theme_engine.h"

#include <chrono>
#include <utility>

namespace app {

ThemeController::ThemeController(viewmodel::BaseViewModel& view_model,
                                 AssetManager& assets,
                                 view::ThemeEngine& theme_engine,
                                 ScreenManager& screen_manager)
    : assets_(assets), theme_engine_(theme_engine), screen_manager_(screen_manager) {
    lv_subject_t* requests = view_model.next_theme_request_subject();
    // LVGL calls the observer once while it is added. Remembering the count
    // first makes that call a no-op, so the app does not switch at startup.
    seen_request_count_ = lv_subject_get_int(requests);
    request_observer_ = lv_subject_add_observer(requests, next_theme_requested_cb, this);
}

ThemeController::~ThemeController() {
    lv_async_call_cancel(apply_pending_async, this);
    if (request_observer_) {
        lv_observer_remove(request_observer_);
        request_observer_ = nullptr;
    }
}

void ThemeController::set_listener(Listener listener) {
    listener_ = std::move(listener);
}

void ThemeController::request(view::ThemeId id) {
    pending_ = id;
    schedule();
}

void ThemeController::request_next() {
    // Starts from a switch that is still waiting, so two quick presses move
    // two themes on.
    request(view::next_theme(pending_ ? *pending_ : theme_engine_.theme().id));
}

void ThemeController::schedule() {
    if (apply_scheduled_) {
        return;
    }
    apply_scheduled_ = lv_async_call(apply_pending_async, this) == LV_RESULT_OK;
    if (!apply_scheduled_) {
        LOG_ERROR("could not schedule the theme switch");
    }
}

void ThemeController::apply_pending_async(void* user_data) {
    auto* controller = static_cast<ThemeController*>(user_data);
    if (controller) {
        controller->apply_pending();
    }
}

void ThemeController::next_theme_requested_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<ThemeController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }

    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->seen_request_count_) {
        return;
    }
    controller->seen_request_count_ = count;
    controller->request_next();
}

void ThemeController::apply_pending() {
    apply_scheduled_ = false;

    const std::optional<view::ThemeId> target = pending_;
    pending_.reset();
    if (!target || *target == theme_engine_.theme().id) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    SwitchResult result{};
    result.previous = theme_engine_.theme().id;
    result.current = *target;

    // 1. New fonts and the new active theme; top-layer objects restyle now.
    result.fonts_complete = theme_engine_.set_theme(*target);
    if (!result.fonts_complete) {
        LOG_WARN("some fonts of the {} theme could not be loaded; built-in fonts are used instead",
                 theme_engine_.theme().name);
    }

    // 2. and 3. The page in the new theme, then the old fonts go.
    if (screen_manager_.rebuild_current_page()) {
        theme_engine_.release_retired_fonts();
    } else {
        LOG_ERROR("the page could not be built again in the {} theme; the old screen and its fonts "
                  "stay loaded",
                  theme_engine_.theme().name);
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    result.duration_ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    result.fonts_loaded = assets_.loaded_font_count();

    LOG_INFO("theme: {} (switched from {} in {} ms, {} fonts loaded)",
             view::theme_spec(result.current).name,
             view::theme_spec(result.previous).name,
             result.duration_ms,
             result.fonts_loaded);

    if (listener_) {
        listener_(result);
    }
}

} // namespace app
