/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace viewmodel {

// The number of rows in the list of the key test page (demo-keys).
constexpr int32_t kKeyTestRowCount = 4;

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

    // The path of open pages (see model::BaseModel). Only the top page is
    // shown; current_page_subject() publishes it, and the app builds that
    // page later, never inside the caller's callback.
    model::AppPage current_page() const;
    std::size_t page_depth() const;
    // Makes `page` the only page in the path; used at startup and by
    // snapshots.
    void show_page(model::AppPage page);
    // Opens `page` on top, or goes back to it when it is already further
    // down the path. Returns false when nothing changed (already on top, or
    // the path is full).
    bool open_page(model::AppPage page);
    // Goes back one page. Returns false on the first page.
    bool go_back();
    void increment_counter();
    void decrement_counter();
    void toggle_bold_text();
    void toggle_info();
    void request_quit();
    void request_next_theme();

    // What the key test page (demo-keys) shows. It lives here rather than in
    // the screen, so it is kept while the app runs: after going back,
    // opening the page again and a theme switch.
    // The selected row, 0 to kKeyTestRowCount - 1; starts at 0.
    lv_subject_t* key_test_selection_subject();
    // The picked row, or -1 while none has been picked.
    lv_subject_t* key_test_picked_subject();
    // The name of the last key the page got; "-" before the first.
    lv_subject_t* key_test_last_key_subject();
    // True while the page is in typing mode.
    lv_subject_t* key_test_typing_subject();
    bool key_test_typing() const;
    // Moves the selection by `delta` rows. It stops at the first and the
    // last row; it does not wrap around.
    void key_test_move(int32_t delta);
    // Picks the selected row.
    void key_test_pick();
    // Records `name` as the last key (at most 15 characters are kept).
    void key_test_note_key(const char* name);
    void key_test_set_typing(bool typing);

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
    reactive::IntSubject key_test_selection_subject_;
    reactive::IntSubject key_test_picked_subject_;
    reactive::StringSubject<16> key_test_last_key_subject_;
    reactive::BoolSubject key_test_typing_subject_;
};

} // namespace viewmodel
