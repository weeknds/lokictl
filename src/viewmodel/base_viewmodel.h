/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"

#include "lvgl.h"

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* greeting_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* counter_subject();
    lv_subject_t* bold_text_subject();
    lv_subject_t* info_visible_subject();
    lv_subject_t* quit_requested_subject();
    // Counts how often the user asked for the next theme (key 6). The view
    // model only passes the request on: the app applies it
    // (app::ThemeController), and view::ThemeEngine::subject() publishes the
    // theme that is active.
    lv_subject_t* next_theme_request_subject();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void show_apple_page();
    void show_butter_page();
    void toggle_page();
    void increment_counter();
    void decrement_counter();
    void toggle_bold_text();
    void toggle_info();
    void request_quit();
    void request_next_theme();

private:
    void publish_all();

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::StringSubject<32> greeting_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::IntSubject counter_subject_;
    reactive::BoolSubject bold_text_subject_;
    reactive::BoolSubject info_visible_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject next_theme_request_subject_;
};

} // namespace viewmodel
