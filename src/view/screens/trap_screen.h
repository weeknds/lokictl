/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>
#include <cstdint>
#include <string>

namespace screen {

// The Trap page (Watch key 8, or the command `trap`): fake services that
// catch whoever connects (a honeypot; model::TrapRun, in trap()). Under the
// title bar:
// - the summary row: whether the trap is on, and how many connections it
//   caught;
// - one line per connection, the newest first, as many as fit: the time,
//   who connected and what it tried ("tried web"); the selected one
//   highlighted. In place of the list, what the trap is and its ports;
// - the status line: "On 4:12, 6 fake ports", how the run ended or what
//   went wrong.
// The detail view (Enter or key 5) shows the selected connection in full:
// what it tried and when, who (address and port), the fake service and
// what it sent first.
//
// Keys: 4 (Back) or a short ESC closes the detail, else goes back (the trap
// stays); 5 (Info) or Enter opens or closes the detail; 6 (Start / Stop)
// sets the trap or takes it away; up and down (F/X) pick a connection.
//
// The selection and the detail live in the view model (trap_selection_*,
// trap_detail_*), so they are kept across a theme switch. While a
// connection other than the newest is picked, new ones do not move the
// selection off it.
class TrapScreen : public BaseScreen {
public:
    TrapScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    bool on_back() override;
    view::widgets::HelpContent help() const override;

private:
    static constexpr std::size_t kRows = 5;

    struct Row {
        lv_obj_t* box{nullptr};
        lv_obj_t* time{nullptr};
        lv_obj_t* who{nullptr};
        lv_obj_t* what{nullptr};
        std::string time_shown;
        std::string who_shown;
        std::string what_shown;
        int32_t style_shown{-1};  // 0 plain, 1 selected
        bool hidden{true};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    bool running() const;
    void start_or_stop();
    void toggle_detail();
    void select(int32_t delta);
    int32_t selected() const;

    void refresh();
    void refresh_list();
    void refresh_detail();
    void refresh_status();
    void refresh_keys();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* state_caption_{nullptr};
    lv_obj_t* state_{nullptr};
    lv_obj_t* caught_caption_{nullptr};
    std::array<Row, kRows> rows_{};
    int32_t visible_rows_{0};
    int32_t first_row_{0};
    lv_obj_t* note_{nullptr};
    lv_obj_t* ports_note_{nullptr};
    lv_obj_t* status_{nullptr};
    // The detail view.
    lv_obj_t* detail_{nullptr};
    lv_obj_t* detail_name_{nullptr};
    lv_obj_t* detail_time_{nullptr};
    lv_obj_t* detail_who_{nullptr};
    lv_obj_t* detail_fact_{nullptr};
    lv_obj_t* detail_about_{nullptr};

    std::string state_shown_;
    int32_t state_tone_{-1};
    std::string caught_shown_;
    std::string note_shown_;
    std::string ports_note_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string detail_name_shown_;
    std::string detail_time_shown_;
    std::string detail_who_shown_;
    std::string detail_fact_shown_;
    std::string detail_about_shown_;
    int32_t detail_shown_{-1};  // 0 list, 1 detail; -1 before the first refresh
    int32_t raised_seen_{-1};   // run.raised at the last refresh; -1 before the first
    int32_t shown_run_key_{-1};
    int32_t shown_info_key_{-1};
};

} // namespace screen
