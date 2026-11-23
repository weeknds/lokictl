/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "tool_data.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The Ping, DNS, Trace and Ports pages, opened from the Tools page. One
// class draws all four; the page decides which tool it shows. Under the
// title bar:
// - the target row: a caption ("target", "router" for the router, "name";
//   on the Ports page also "this device" and a found device's name), the
//   target, and its place among the targets ("‹ 2/3 ›");
// - the results. Ping: a table of the last five echoes (sequence number
//   and time, "lost", "…" while it waits) and two summary lines (sent,
//   received and lost; min, avg and max). DNS: the addresses found (IPv4
//   first, up to six, several to a line when they fit) and the server
//   asked with the time taken. Trace: one row per hop (number, address,
//   reverse name, time in ms; "*" and "no answer" for a silent hop), as
//   many as fit, with a thin scroll mark at the right when there are more.
//   Ports: the open ports with their services ("22 ssh   80 http"),
//   several to a line, and how many were open, closed and silent;
// - the status line: how the run stands, or why it failed ("Ping not
//   allowed for this user", "No such name", "No answer"), with the app-wide
//   Sent count at its right.
//
// Keys: 4 (Back) or a short ESC goes back, and a run keeps going; 5 (Edit)
// types a target; 6 (Run) or Enter starts a run on the target, and stops
// it while it runs (Stop). Left and right (Z/C) step through the targets:
// for Ping and Trace the router (when there is one), 9.9.9.9 and 1.1.1.1;
// for DNS example.com and wikipedia.org; then the typed target, if any.
// While this tool runs, the row shows the run's target and Edit and the
// arrows do nothing; while another tool runs, Run is switched off. On the
// Trace page up and down (F/X) scroll the hops; while it runs and was not
// scrolled up, the list follows the newest hop.
//
// The Ports page asks first, like the Discover page's Probe: its targets
// are the router, this device (the uplink's address), the devices of the
// Discover list and the hotspot's connected devices with a private IPv4
// address, then the typed one (digits and dots only). The first 6 (Scan)
// or Enter shows what will be asked and "Only devices you own or may test.
// Press 6 again to scan." in place of the results; nothing is sent. A
// second 6 or Enter scans that target. Any other key, a short ESC, leaving
// the page, the target changing, another tool starting or kConfirmMs
// cancels; nothing remembers it. A finished scan shows only while its
// target is the selected one, so one device's ports never stand under
// another's address.
//
// Typing: a panel takes the page's place. Letters, digits and '.' are
// added (at most model::kToolTargetMax), Backspace takes one away, Enter
// keeps a valid target (model::valid_tool_target()) and selects it, Enter
// on an empty text drops the typed target, and a short ESC cancels.
//
// The target choice, the typed target, typing, the runs and the Trace
// page's scroll live in the view model, so they are kept when the page is
// left and across a theme switch.
class ToolScreen : public BaseScreen {
public:
    ToolScreen(viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               view::ThemeEngine& theme_engine,
               model::AppPage page);
    // Deletes the confirmation's timer.
    ~ToolScreen() override;

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content(model::ToolKind kind);

    bool on_key(uint32_t key) override;
    bool on_back() override;
    bool text_entry() const override;
    view::widgets::HelpContent help() const override;

private:
    static constexpr std::size_t kTableColumns = model::kPingShownReplies;
    static constexpr std::size_t kAddressLines = 3;
    // The Trace page shows at most this many hop rows (fewer when the
    // theme's lines are taller).
    static constexpr std::size_t kTraceRows = 4;
    // A Ports confirmation not given in this long is cancelled.
    static constexpr uint32_t kConfirmMs = 10000;

    struct HopRow {
        lv_obj_t* number{nullptr};
        lv_obj_t* address{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* time{nullptr};
        std::string number_shown;
        std::string address_shown;
        std::string name_shown;
        std::string time_shown;
        int32_t address_tone{-1};
    };

    struct Target {
        std::string text;
        std::string caption;  // empty: the page's usual caption ("target", "name")
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;
    void build_ping_results(lv_obj_t* panel);
    void build_dns_results(lv_obj_t* panel);
    void build_trace_results(lv_obj_t* panel);
    void build_ports_results(lv_obj_t* panel);
    void build_typing_panel(lv_obj_t* content);

    // The targets to step through: the presets, then the typed one.
    std::vector<Target> targets() const;
    // The Ports page's targets before the typed one.
    std::vector<Target> ports_targets() const;
    // The selected target, clamped to targets().
    int32_t target_index(const std::vector<Target>& list) const;
    // The selected target's text; empty without one.
    std::string selected_target() const;
    // Ports: whether the latest scan is shown (it runs, or its target is
    // the selected one); otherwise the page looks as before any scan.
    bool ports_result_shown() const;
    // True while this page's tool runs, and while either runs.
    bool this_running() const;
    bool any_running() const;

    void step_target(int32_t delta);
    // Trace: scrolls the hops by one row.
    void scroll_hops(int32_t delta);
    // Trace: the first hop row shown, from the view model's scroll.
    int32_t first_hop_row() const;
    void start_edit();
    void run_or_stop();
    // Ports: cancels the confirmation, if one waits, and draws the page.
    void cancel_confirm();
    static void confirm_timer_cb(lv_timer_t* timer);
    bool type_key(uint32_t key);

    void refresh();
    void refresh_keys();
    void refresh_target();
    void refresh_ping();
    void refresh_dns();
    void refresh_trace();
    void refresh_ports();
    void refresh_status();
    void refresh_typing();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    model::ToolKind kind_;

    lv_obj_t* main_panel_{nullptr};
    lv_obj_t* target_caption_{nullptr};
    lv_obj_t* target_value_{nullptr};
    lv_obj_t* target_place_{nullptr};
    // Ping
    std::array<lv_obj_t*, kTableColumns> sequences_{};
    std::array<lv_obj_t*, kTableColumns> times_{};
    lv_obj_t* placeholder_{nullptr};  // in place of the table before the first run
    lv_obj_t* counts_{nullptr};
    lv_obj_t* stats_{nullptr};
    // DNS
    std::array<lv_obj_t*, kAddressLines> lines_{};
    lv_obj_t* server_{nullptr};
    // Trace
    std::array<HopRow, kTraceRows> hop_rows_{};
    int32_t visible_hop_rows_{0};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
    int32_t hop_list_top_{0};
    int32_t hop_list_height_{0};
    int32_t thumb_top_shown_{-1};
    int32_t thumb_height_shown_{-1};
    // Ports (with lines_ and server_ for the open ports and the counts)
    lv_obj_t* confirm_{nullptr};
    // All
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    // Typing
    lv_obj_t* typing_panel_{nullptr};
    lv_obj_t* draft_{nullptr};
    lv_obj_t* count_{nullptr};
    lv_obj_t* typing_note_{nullptr};

    std::string caption_shown_;
    std::string target_shown_;
    std::string place_shown_;
    std::array<std::string, kTableColumns> sequences_shown_{};
    std::array<std::string, kTableColumns> times_shown_{};
    std::array<int32_t, kTableColumns> time_tones_{-1, -1, -1, -1, -1};
    std::string counts_shown_;
    std::string stats_shown_;
    std::array<std::string, kAddressLines> lines_shown_{};
    int32_t lines_tone_{-1};
    std::string server_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    std::string draft_shown_;
    std::string count_shown_;
    std::string typing_note_shown_;
    int32_t typing_note_tone_{-1};
    int32_t shown_typing_{-1};  // 1 typing, 0 not, -1 before the first
    int32_t shown_keys_{-1};    // what refresh_keys() set last
    bool draft_invalid_{false}; // Enter was pressed on a target that is not taken
    std::string confirm_shown_;
    // Ports: a scan waits for its confirmation (the second 6) of
    // confirm_target_; the timer cancels it.
    bool confirming_{false};
    std::string confirm_target_;
    lv_timer_t* confirm_timer_{nullptr};
};

} // namespace screen
