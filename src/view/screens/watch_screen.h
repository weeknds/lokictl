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

// The Watch page (Tools key 8, or the command `watch`): passive alerts
// about the network the uplink is on (model::WatchData, in watch()). Watch
// only listens; it never sends and never blocks anything.
// Under the title bar:
// - the summary row: the interface, how many alerts, how many devices are
//   known;
// - one line per alert, the newest first, as many as fit: the time, the
//   short name ("Scan on you") and who it is about (the IPv4 address, else
//   the hardware address); the selected one highlighted. In place of the
//   list, a note when there is none ("Not watching", "No alerts yet");
// - the status line: "Watching 12:34, sends none", "Learning devices, 0:42
//   left", how the run ended or what went wrong.
// The detail view (Enter or key 5) shows the selected alert in full: its
// name and time, the address and hardware address, what else is known
// ("Tried 20 ports", "Router was ..."), and the plain explanation.
//
// Keys: 4 (Back) or a short ESC closes the detail, else goes back (Watch
// goes on); 5 (Info) or Enter opens or closes the detail; 6 (Start / Stop)
// starts Watch on the uplink or stops it (off without a network); 7
// (Known) marks the selected new device as known, so it raises no alert
// again; up and down (F/X) pick an alert.
//
// The selection and the detail live in the view model (watch_selection_*,
// watch_detail_*), so they are kept across a theme switch. While an alert
// other than the newest is picked, new alerts do not move the selection
// off it.
class WatchScreen : public BaseScreen {
public:
    WatchScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    bool on_back() override;
    view::widgets::HelpContent help() const override;

private:
    // The page shows at most this many alerts (fewer when the theme's
    // lines are taller).
    static constexpr std::size_t kRows = 5;

    struct Row {
        lv_obj_t* box{nullptr};
        lv_obj_t* time{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* who{nullptr};
        std::string time_shown;
        std::string name_shown;
        std::string who_shown;
        int32_t style_shown{-1};  // 0 plain, 1 selected
        int32_t tone_shown{-1};
        bool hidden{true};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    std::string uplink() const;
    bool watching() const;
    bool can_watch() const;
    void watch_or_stop();
    void toggle_detail();
    bool can_mark_known() const;
    void mark_known();
    void select(int32_t delta);
    int32_t selected() const;

    void refresh();
    void refresh_list();
    void refresh_detail();
    void refresh_status();
    void refresh_keys();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* interface_caption_{nullptr};
    lv_obj_t* count_{nullptr};
    lv_obj_t* known_caption_{nullptr};
    std::array<Row, kRows> rows_{};
    int32_t visible_rows_{0};
    int32_t first_row_{0};
    lv_obj_t* note_{nullptr};
    lv_obj_t* status_{nullptr};
    // The detail view.
    lv_obj_t* detail_{nullptr};
    lv_obj_t* detail_name_{nullptr};
    lv_obj_t* detail_time_{nullptr};
    lv_obj_t* detail_who_{nullptr};
    lv_obj_t* detail_fact_{nullptr};
    lv_obj_t* detail_about_{nullptr};

    std::string interface_shown_;
    std::string count_shown_;
    std::string known_shown_;
    std::string note_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string detail_name_shown_;
    std::string detail_time_shown_;
    std::string detail_who_shown_;
    std::string detail_fact_shown_;
    std::string detail_about_shown_;
    int32_t detail_shown_{-1};  // 0 list, 1 detail; -1 before the first refresh
    int32_t raised_seen_{-1};   // run.raised at the last refresh; -1 before the first
    int32_t shown_watch_key_{-1};
    int32_t shown_info_key_{-1};
    int32_t shown_known_key_{-1};
};

} // namespace screen
