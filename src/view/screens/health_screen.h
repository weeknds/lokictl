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

// The Health page (Tools key 7, or the command `health`): one health check
// of the uplink's own network (model::HealthRun, in tools().health), with
// what it found in plain words. Under the title bar:
// - the summary row: the interface and the counts ("2 risks, 1 warning");
// - one line per finding, the worst first, as many as fit: the level's tag
//   (Risk, Warn, Info), the short name ("Telnet open") and who it is about
//   ("router", "this device", an address, "Wi-Fi"); the selected one
//   highlighted. In place of the list, a note when there is none, or the
//   confirmation;
// - the status line: what the check is doing or how it ended, and the
//   Sent count.
// The detail view (Enter or key 5) shows the selected finding in full: its
// name and level, who it is about, one line of facts, what it means and
// what to do.
//
// Keys: 4 (Back) or a short ESC closes the detail, else goes back (a check
// goes on); 5 (Info) or Enter opens or closes the detail; 6 (Check / Stop):
// the first press shows what will be asked and sends nothing, a second
// press within kConfirmMs starts the check on the uplink (only a private
// home network; off without a network and while another tool runs), and
// while it runs 6 stops it; up and down (F/X) pick a finding. Any other key
// while the confirmation shows only cancels it.
//
// The selection and the detail live in the view model (health_selection_*,
// health_detail_*), so they are kept across a theme switch.
class HealthScreen : public BaseScreen {
public:
    HealthScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);
    ~HealthScreen() override;

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    bool on_back() override;
    view::widgets::HelpContent help() const override;

    // A second 6 within this long starts the check.
    static constexpr uint32_t kConfirmMs = 10000;

private:
    // The page shows at most this many findings (fewer when the theme's
    // lines are taller).
    static constexpr std::size_t kRows = 5;

    struct Row {
        lv_obj_t* box{nullptr};
        lv_obj_t* tag{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* who{nullptr};
        std::string tag_shown;
        std::string name_shown;
        std::string who_shown;
        int32_t style_shown{-1};  // 0 plain, 1 selected
        int32_t tone_shown{-1};
        int32_t name_width{-1};
        bool hidden{true};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    std::string uplink() const;
    // The uplink's address when it is on a private home network; else empty.
    std::string home_address() const;
    bool checking() const;
    bool can_check() const;
    void check_or_stop();
    // Key 8 and R: asks for a report (a waiting confirmation only goes).
    void save_report();
    void cancel_confirm();
    static void confirm_timer_cb(lv_timer_t* timer);
    void toggle_detail();
    void select(int32_t delta);
    int32_t selected() const;

    void refresh();
    void refresh_list();
    void refresh_detail();
    void refresh_status();
    void refresh_keys();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* interface_caption_{nullptr};
    lv_obj_t* summary_{nullptr};
    std::array<Row, kRows> rows_{};
    int32_t visible_rows_{0};
    int32_t first_row_{0};
    int32_t row_inner_{0};  // a row's width inside its frame
    int32_t name_x_{0};     // where the name starts in a row
    lv_obj_t* note_{nullptr};
    lv_obj_t* confirm_{nullptr};
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    // The detail view.
    lv_obj_t* detail_{nullptr};
    lv_obj_t* detail_name_{nullptr};
    lv_obj_t* detail_level_{nullptr};
    lv_obj_t* detail_where_{nullptr};
    lv_obj_t* detail_fact_{nullptr};
    lv_obj_t* detail_about_{nullptr};
    lv_obj_t* detail_fix_{nullptr};

    std::string interface_shown_;
    std::string summary_shown_;
    int32_t summary_tone_{-1};
    std::string note_shown_;
    int32_t note_tone_{-1};
    std::string confirm_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    std::string detail_name_shown_;
    std::string detail_level_shown_;
    std::string detail_where_shown_;
    std::string detail_fact_shown_;
    std::string detail_about_shown_;
    std::string detail_fix_shown_;
    int32_t detail_tone_{-1};
    int32_t detail_shown_{-1};  // 0 list, 1 detail; -1 before the first refresh
    int32_t shown_check_key_{-1};  // 0 off, 1 Check, 2 Stop; -1 before the first
    int32_t shown_info_key_{-1};
    // The check waits for its confirmation (the second 6); the timer
    // cancels it.
    bool confirming_{false};
    lv_timer_t* confirm_timer_{nullptr};
};

} // namespace screen
