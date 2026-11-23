/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "screen_manager.h"

#include "butter_screen.h"
#include "dev_fonts_screen.h"
#include "dev_overlays_screen.h"
#include "dev_softkeys_screen.h"
#include "dev_theme_screen.h"
#include "home_screen.h"
#include "key_test_screen.h"
#include "logger.h"
#include "devices_screen.h"
#include "hotspot_screen.h"
#include "modes_screen.h"
#include "page_registry.h"
#include "settings_screen.h"
#include "status_screen.h"
#include "tool_screen.h"
#include "tools_screen.h"

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

bool ScreenManager::show_home_screen() {
    if (!load_screen(std::make_unique<screen::HomeScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Home);
    return true;
}

bool ScreenManager::show_butter_screen() {
    if (!load_screen(std::make_unique<screen::ButterScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Butter);
    return true;
}

bool ScreenManager::show_key_test_screen() {
    if (!load_screen(std::make_unique<screen::KeyTestScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::KeyTest);
    return true;
}

bool ScreenManager::show_settings_screen() {
    if (!load_screen(std::make_unique<screen::SettingsScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Settings);
    return true;
}

bool ScreenManager::show_status_screen() {
    if (!load_screen(std::make_unique<screen::StatusScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Status);
    return true;
}

bool ScreenManager::show_modes_screen() {
    if (!load_screen(std::make_unique<screen::ModesScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Modes);
    return true;
}

bool ScreenManager::show_hotspot_screen() {
    if (!load_screen(std::make_unique<screen::HotspotScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Hotspot);
    return true;
}

bool ScreenManager::show_devices_screen() {
    if (!load_screen(std::make_unique<screen::DevicesScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Devices);
    return true;
}

bool ScreenManager::show_tools_screen() {
    if (!load_screen(std::make_unique<screen::ToolsScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::Tools);
    return true;
}

bool ScreenManager::show_tool_screen(model::AppPage page) {
    if (!load_screen(std::make_unique<screen::ToolScreen>(view_model_, assets_, theme_engine_, page))) {
        return false;
    }
    page_loaded(page);
    return true;
}

bool ScreenManager::show_dev_fonts_screen(model::AppPage page) {
    if (!load_screen(
            std::make_unique<screen::DevFontsScreen>(view_model_, assets_, theme_engine_, page))) {
        return false;
    }
    page_loaded(page);
    return true;
}

bool ScreenManager::show_dev_theme_screen() {
    if (!load_screen(std::make_unique<screen::DevThemeScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::DevTheme);
    return true;
}

bool ScreenManager::show_dev_overlays_screen(model::AppPage page) {
    if (!load_screen(
            std::make_unique<screen::DevOverlaysScreen>(view_model_, assets_, theme_engine_, page))) {
        return false;
    }
    page_loaded(page);
    return true;
}

bool ScreenManager::show_dev_softkeys_screen() {
    if (!load_screen(std::make_unique<screen::DevSoftKeysScreen>(view_model_, assets_, theme_engine_))) {
        return false;
    }
    page_loaded(model::AppPage::DevSoftKeys);
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

    if (screen::DevSoftKeysScreen::handles(page)) {
        return show_dev_softkeys_screen();
    }

    if (page == model::AppPage::Butter) {
        return show_butter_screen();
    }

    if (screen::KeyTestScreen::handles(page)) {
        return show_key_test_screen();
    }

    if (screen::SettingsScreen::handles(page)) {
        return show_settings_screen();
    }

    if (screen::StatusScreen::handles(page)) {
        return show_status_screen();
    }

    if (screen::ModesScreen::handles(page)) {
        return show_modes_screen();
    }

    if (screen::HotspotScreen::handles(page)) {
        return show_hotspot_screen();
    }

    if (screen::DevicesScreen::handles(page)) {
        return show_devices_screen();
    }

    if (screen::ToolsScreen::handles(page)) {
        return show_tools_screen();
    }

    if (screen::ToolScreen::handles(page)) {
        return show_tool_screen(page);
    }

    return show_home_screen();
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

bool ScreenManager::dispatch_key(uint32_t key) {
    return current_screen_ && current_screen_->on_key(key);
}

bool ScreenManager::dispatch_back() {
    return current_screen_ && current_screen_->on_back();
}

bool ScreenManager::text_entry_active() const {
    return current_screen_ && current_screen_->text_entry();
}

view::widgets::HelpContent ScreenManager::help() const {
    return current_screen_ ? current_screen_->help() : view::widgets::HelpContent{};
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

void ScreenManager::page_loaded(model::AppPage page) {
    // The depth is read now, when the deferred load happens, so it always
    // belongs to the page that is shown. The same page built again (after a
    // theme switch) is not a page change and logs nothing.
    const std::size_t depth = view_model_.page_depth();
    if (!has_loaded_page_) {
        LOG_INFO("page: {} (start, depth {})", page_name(page), depth);
    } else if (page != loaded_page_) {
        const char* how = depth > loaded_depth_   ? "opened"
                          : depth < loaded_depth_ ? "back"
                                                  : "shown";
        LOG_INFO("page: {} ({}, depth {})", page_name(page), how, depth);
    }

    loaded_page_ = page;
    loaded_depth_ = depth;
    has_loaded_page_ = true;
}

} // namespace app
