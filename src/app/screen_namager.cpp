/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "screen_manager.h"

#include "apple_screen.h"
#include "butter_screen.h"
#include "dev_fonts_screen.h"
#include "dev_overlays_screen.h"
#include "dev_theme_screen.h"

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) {
        manager->flush_requested_page();
    }
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (!manager) {
        return;
    }

    manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
}

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model,
                             AssetManager& assets,
                             view::ThemeEngine& theme_engine)
    : view_model_(view_model), assets_(assets), theme_engine_(theme_engine) {}

ScreenManager::~ScreenManager() {
    lv_async_call_cancel(flush_requested_page_async, this);

    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) {
        return;
    }

    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer,
                                                         this,
                                                         nullptr);
}

bool ScreenManager::show_apple_screen() {
    if (!load_screen(std::make_unique<screen::AppleScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    loaded_page_ = model::AppPage::Apple;
    has_loaded_page_ = true;
    return true;
}

bool ScreenManager::show_butter_screen() {
    if (!load_screen(std::make_unique<screen::ButterScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    loaded_page_ = model::AppPage::Butter;
    has_loaded_page_ = true;
    return true;
}

bool ScreenManager::show_dev_fonts_screen(model::AppPage page) {
    if (!load_screen(
            std::make_unique<screen::DevFontsScreen>(view_model_, assets_, theme_engine_, page))) {
        return false;
    }
    loaded_page_ = page;
    has_loaded_page_ = true;
    return true;
}

bool ScreenManager::show_dev_theme_screen() {
    if (!load_screen(std::make_unique<screen::DevThemeScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    loaded_page_ = model::AppPage::DevTheme;
    has_loaded_page_ = true;
    return true;
}

bool ScreenManager::show_dev_overlays_screen(model::AppPage page) {
    if (!load_screen(
            std::make_unique<screen::DevOverlaysScreen>(view_model_, assets_, theme_engine_, page))) {
        return false;
    }
    loaded_page_ = page;
    has_loaded_page_ = true;
    return true;
}

void ScreenManager::request_page(model::AppPage page) {
    requested_page_ = page;

    if (page_switch_scheduled_) {
        return;
    }

    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;

    if (has_loaded_page_ && requested_page_ == loaded_page_) {
        return;
    }

    show_page(requested_page_);
}

bool ScreenManager::rebuild_current_page() {
    if (!has_loaded_page_) {
        return true;
    }

    // requested_page_, not loaded_page_: if a page change is still waiting
    // for its deferred flush, that page is built now and the flush then has
    // nothing left to do.
    return show_page(requested_page_);
}

bool ScreenManager::show_page(model::AppPage page) {
    if (screen::DevFontsScreen::handles(page)) {
        return show_dev_fonts_screen(page);
    }

    if (screen::DevThemeScreen::handles(page)) {
        return show_dev_theme_screen();
    }

    if (screen::DevOverlaysScreen::handles(page)) {
        return show_dev_overlays_screen(page);
    }

    if (page == model::AppPage::Butter) {
        return show_butter_screen();
    }

    return show_apple_screen();
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

bool ScreenManager::load_screen(std::unique_ptr<screen::BaseScreen> next_screen) {
    if (!next_screen || !next_screen->root()) {
        return false;
    }

    // The new screen is loaded first; replacing current_screen_ then deletes
    // the old one, together with its LVGL objects.
    lv_screen_load(next_screen->root());
    current_screen_ = std::move(next_screen);
    return true;
}

} // namespace app
