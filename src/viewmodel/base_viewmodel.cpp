/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

// Counts one more on a counter subject, starting again at 1 after the
// largest value.
void bump(reactive::IntSubject& counter) {
    const int32_t count = counter.value();
    counter.set(count == std::numeric_limits<int32_t>::max() ? 1 : count + 1);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      current_page_subject_(page_to_int(model_.current_page())),
      counter_subject_(0),
      quit_requested_subject_(false),
      theme_request_subject_(0),
      settings_theme_selection_subject_(kSettingsThemeFollowsActive),
      screen_dimming_subject_(true),
      status_scroll_subject_(0),
      devices_selection_subject_(0),
      modes_selection_subject_(kModesFollowsActive),
      mode_request_subject_(0),
      hotspot_subject_(0),
      hotspot_request_subject_(0),
      hotspot_typing_subject_(false),
      home_selection_subject_(0),
      home_data_subject_(0),
      key_test_selection_subject_(0),
      key_test_picked_subject_(-1),
      key_test_last_key_subject_("-"),
      key_test_typing_subject_(false) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::counter_subject() {
    return counter_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::theme_request_subject() {
    return theme_request_subject_.native();
}

int32_t BaseViewModel::requested_theme() const {
    return requested_theme_;
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

std::size_t BaseViewModel::page_depth() const {
    return model_.page_depth();
}

void BaseViewModel::show_page(model::AppPage page) {
    model_.reset_pages(page);
    publish_all();
}

bool BaseViewModel::open_page(model::AppPage page) {
    if (!model_.open_page(page)) {
        return false;
    }
    publish_all();
    return true;
}

bool BaseViewModel::go_back() {
    if (!model_.go_back()) {
        return false;
    }
    publish_all();
    return true;
}

void BaseViewModel::increment_counter() {
    counter_subject_.set(counter_subject_.value() + 1);
}

void BaseViewModel::decrement_counter() {
    const auto next = counter_subject_.value() - 1;
    counter_subject_.set(next < 0 ? 0 : next);
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::request_theme(int32_t row) {
    // Stored first: the observers run inside set() and read it from there.
    requested_theme_ = row;
    theme_request_subject_.set(theme_request_subject_.value() + 1);
}

lv_subject_t* BaseViewModel::settings_theme_selection_subject() {
    return settings_theme_selection_subject_.native();
}

int32_t BaseViewModel::settings_theme_selection() const {
    return settings_theme_selection_subject_.value();
}

void BaseViewModel::select_settings_theme(int32_t row) {
    settings_theme_selection_subject_.set(row);
}

bool BaseViewModel::open_settings() {
    settings_theme_selection_subject_.set(kSettingsThemeFollowsActive);
    return open_page(model::AppPage::Settings);
}

lv_subject_t* BaseViewModel::screen_dimming_subject() {
    return screen_dimming_subject_.native();
}

bool BaseViewModel::screen_dimming() const {
    return screen_dimming_subject_.value();
}

void BaseViewModel::set_screen_dimming(bool on) {
    if (on != screen_dimming_subject_.value()) {
        screen_dimming_subject_.set(on);
    }
}

lv_subject_t* BaseViewModel::status_scroll_subject() {
    return status_scroll_subject_.native();
}

int32_t BaseViewModel::status_scroll() const {
    return status_scroll_subject_.value();
}

void BaseViewModel::set_status_scroll(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != status_scroll_subject_.value()) {
        status_scroll_subject_.set(next);
    }
}

bool BaseViewModel::open_status() {
    set_status_scroll(0);
    return open_page(model::AppPage::Status);
}

lv_subject_t* BaseViewModel::devices_selection_subject() {
    return devices_selection_subject_.native();
}

int32_t BaseViewModel::devices_selection() const {
    return devices_selection_subject_.value();
}

void BaseViewModel::set_devices_selection(int32_t index) {
    const int32_t next = std::max<int32_t>(index, 0);
    if (next != devices_selection_subject_.value()) {
        devices_selection_subject_.set(next);
    }
}

bool BaseViewModel::open_devices() {
    set_devices_selection(home_selection());
    return open_page(model::AppPage::Devices);
}

lv_subject_t* BaseViewModel::modes_selection_subject() {
    return modes_selection_subject_.native();
}

int32_t BaseViewModel::modes_selection() const {
    return modes_selection_subject_.value();
}

void BaseViewModel::select_mode_row(int32_t row) {
    if (row != modes_selection_subject_.value()) {
        modes_selection_subject_.set(row);
    }
}

bool BaseViewModel::open_modes() {
    select_mode_row(kModesFollowsActive);
    return open_page(model::AppPage::Modes);
}

void BaseViewModel::request_mode(model::NetMode mode) {
    // Stored first: the observers run inside set() and read it from there.
    requested_mode_ = mode;
    const int32_t count = mode_request_subject_.value();
    mode_request_subject_.set(count == std::numeric_limits<int32_t>::max() ? 1 : count + 1);
}

lv_subject_t* BaseViewModel::mode_request_subject() {
    return mode_request_subject_.native();
}

model::NetMode BaseViewModel::requested_mode() const {
    return requested_mode_;
}

const std::string& BaseViewModel::hotspot_password() const {
    return hotspot_password_;
}

void BaseViewModel::set_hotspot_password(const std::string& password) {
    if (password == hotspot_password_) {
        return;
    }
    hotspot_password_ = password;
    bump(hotspot_subject_);
}

const std::string& BaseViewModel::hotspot_name() const {
    return hotspot_name_;
}

void BaseViewModel::set_hotspot_name(const std::string& name) {
    if (name == hotspot_name_) {
        return;
    }
    hotspot_name_ = name;
    bump(hotspot_subject_);
}

const std::string& BaseViewModel::default_hotspot_name() const {
    return default_hotspot_name_;
}

void BaseViewModel::set_default_hotspot_name(const std::string& name) {
    default_hotspot_name_ = name;
}

lv_subject_t* BaseViewModel::hotspot_subject() {
    return hotspot_subject_.native();
}

bool BaseViewModel::open_hotspot() {
    set_hotspot_typing(false);
    return open_page(model::AppPage::Hotspot);
}

void BaseViewModel::request_hotspot_rename(const std::string& name) {
    // Stored first: the observers run inside set() and read it from there.
    requested_hotspot_change_ = HotspotChange::Rename;
    requested_hotspot_name_ = name;
    bump(hotspot_request_subject_);
}

void BaseViewModel::request_new_hotspot_password() {
    requested_hotspot_change_ = HotspotChange::NewPassword;
    requested_hotspot_name_.clear();
    bump(hotspot_request_subject_);
}

lv_subject_t* BaseViewModel::hotspot_request_subject() {
    return hotspot_request_subject_.native();
}

HotspotChange BaseViewModel::requested_hotspot_change() const {
    return requested_hotspot_change_;
}

const std::string& BaseViewModel::requested_hotspot_name() const {
    return requested_hotspot_name_;
}

lv_subject_t* BaseViewModel::hotspot_typing_subject() {
    return hotspot_typing_subject_.native();
}

bool BaseViewModel::hotspot_typing() const {
    return hotspot_typing_subject_.value();
}

void BaseViewModel::set_hotspot_typing(bool typing) {
    if (typing != hotspot_typing_subject_.value()) {
        hotspot_typing_subject_.set(typing);
    }
}

const std::string& BaseViewModel::hotspot_draft() const {
    return hotspot_draft_;
}

void BaseViewModel::set_hotspot_draft(const std::string& draft) {
    hotspot_draft_ = draft;
}

const model::HomeData& BaseViewModel::home() const {
    return model_.home();
}

bool BaseViewModel::set_home_data(const model::HomeData& data) {
    if (data == model_.home()) {
        return false;
    }
    model_.set_home(data);

    // The selection stays on a client that is shown. Set before the count
    // below, so the page marks a client that exists when it updates.
    const auto shown = static_cast<int32_t>(
        std::min<std::size_t>(data.clients.size(), static_cast<std::size_t>(kHomeClientRows)));
    const int32_t last = std::max<int32_t>(shown - 1, 0);
    if (home_selection_subject_.value() > last) {
        home_selection_subject_.set(last);
    }

    // Starts again from 0 instead of overflowing; it still is a change.
    const int32_t count = home_data_subject_.value();
    home_data_subject_.set(count == std::numeric_limits<int32_t>::max() ? 0 : count + 1);
    return true;
}

lv_subject_t* BaseViewModel::home_data_subject() {
    return home_data_subject_.native();
}

lv_subject_t* BaseViewModel::home_selection_subject() {
    return home_selection_subject_.native();
}

int32_t BaseViewModel::home_selection() const {
    return home_selection_subject_.value();
}

void BaseViewModel::home_select(int32_t delta) {
    const auto shown = static_cast<int32_t>(
        std::min<std::size_t>(model_.home().clients.size(), static_cast<std::size_t>(kHomeClientRows)));
    if (shown == 0) {
        return;
    }

    const int32_t current = home_selection_subject_.value();
    const int32_t next = std::clamp<int32_t>(current + delta, 0, shown - 1);
    if (next != current) {
        home_selection_subject_.set(next);
    }
}

lv_subject_t* BaseViewModel::key_test_selection_subject() {
    return key_test_selection_subject_.native();
}

lv_subject_t* BaseViewModel::key_test_picked_subject() {
    return key_test_picked_subject_.native();
}

lv_subject_t* BaseViewModel::key_test_last_key_subject() {
    return key_test_last_key_subject_.native();
}

lv_subject_t* BaseViewModel::key_test_typing_subject() {
    return key_test_typing_subject_.native();
}

bool BaseViewModel::key_test_typing() const {
    return key_test_typing_subject_.value();
}

void BaseViewModel::key_test_move(int32_t delta) {
    const int32_t next = std::clamp<int32_t>(key_test_selection_subject_.value() + delta,
                                             0,
                                             kKeyTestRowCount - 1);
    key_test_selection_subject_.set(next);
}

void BaseViewModel::key_test_pick() {
    key_test_picked_subject_.set(key_test_selection_subject_.value());
}

void BaseViewModel::key_test_note_key(const char* name) {
    key_test_last_key_subject_.set(name);
}

void BaseViewModel::key_test_set_typing(bool typing) {
    key_test_typing_subject_.set(typing);
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

} // namespace viewmodel
