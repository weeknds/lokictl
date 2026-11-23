/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "leak_check.h"

#include <array>
#include <cstdint>
#include <string>

namespace screen {

// The Leak page (Modes key 5, or the command `leak`): the VPN leak test
// (model::LeakRun, in tools().leak). Under the title bar:
// - one line per check, in model::LeakCheck order, in plain words and the
//   verdict's colour: "Name lookups: through VPN", "IPv6: no leak",
//   "Public address: 198.51.100.7", "Kill switch: on"; a failing line says
//   what leaks ("Name lookups: NOT through VPN"). The selected one is
//   highlighted;
// - the status line: whether the VPN is off, how the test stands or how it
//   ended ("No leaks found", "1 leak found"), and the Sent count.
// A finished run made with the VPN in another state (on or off, another
// file, or the gateway on or off) is not shown: the page then reads as
// before a run.
// The detail view (Enter or key 5) shows the selected check in full: its
// name and verdict, one line of facts and what it means and what to do.
//
// Keys: 4 (Back) or a short ESC closes the detail, else goes back (a test
// goes on); 5 (Info) or Enter opens or closes the detail; 6 (Run / Stop)
// runs the test (off while the VPN is starting or another tool runs) and
// stops it while it runs; up and down (F/X) pick a check, in the detail
// too. With the VPN off a run only notes the public address, which the
// next run with the VPN on compares with. Opening the page sends nothing.
//
// The selection and the detail live in the view model (leak_selection_*,
// leak_detail_*), so they are kept across a theme switch.
class LeakScreen : public BaseScreen {
public:
    LeakScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    bool on_back() override;
    view::widgets::HelpContent help() const override;

private:
    struct Row {
        lv_obj_t* box{nullptr};
        lv_obj_t* text{nullptr};
        std::string text_shown;
        int32_t style_shown{-1};  // 0 plain, 1 selected
        int32_t tone_shown{-1};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The run the page shows: the latest one, unless the VPN now differs
    // from that run's (model::leak_run_current()); its verdicts then no
    // longer hold, and the page reads as before a run.
    const model::LeakRun& shown_run() const;
    bool this_running() const;
    bool any_running() const;
    bool can_run() const;
    void run_or_stop();
    void toggle_detail();
    void select(int32_t delta);
    int32_t selected() const;

    void refresh();
    void refresh_list();
    void refresh_detail();
    void refresh_status();
    void refresh_keys();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::array<Row, model::kLeakCheckCount> rows_{};
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    // The detail view.
    lv_obj_t* detail_{nullptr};
    lv_obj_t* detail_title_{nullptr};
    lv_obj_t* detail_verdict_{nullptr};
    lv_obj_t* detail_fact_{nullptr};
    lv_obj_t* detail_advice_{nullptr};

    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    std::string detail_title_shown_;
    std::string detail_verdict_shown_;
    std::string detail_fact_shown_;
    std::string detail_advice_shown_;
    int32_t detail_tone_{-1};
    int32_t detail_shown_{-1};  // 0 list, 1 detail; -1 before the first refresh
    int32_t shown_run_key_{-1};  // 0 off, 1 Run, 2 Stop; -1 before the first
};

} // namespace screen
