/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cstddef>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      current_page_subject_(page_to_int(model_.current_page())),
      counter_subject_(0),
      quit_requested_subject_(false),
      theme_request_subject_(0),
      settings_theme_selection_subject_(kSettingsThemeFollowsActive),
      home_selection_subject_(0),
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

const model::HomeData& BaseViewModel::home() const {
    return model_.home();
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
