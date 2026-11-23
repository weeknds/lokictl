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

// The Wi-Fi page, opened from the Tools page. It lists the networks the
// built-in Wi-Fi hears (model::WifiScan, in home().wifi_scan), strongest
// first. Under the title bar:
// - the summary row: the interface ("wlan0") and how many networks were
//   found;
// - one row per network, as many as fit: signal bars (drawn as shapes) and
//   the signal in per cent, the name ("hidden" for a hidden network), the
//   channel and the security; the joined network in the accent colour, with
//   a thin scroll mark at the right when not all fit. In place of the list,
//   a note says why there is none (the hotspot runs, the Wi-Fi is off, no
//   network service);
// - the status line: "Last scan 14:32", "Scanning…" or what went wrong,
//   with the app-wide Sent count at its right (a scan is sent by the
//   network service and is not counted).
//
// Keys: 4 (Back) or a short ESC goes back; 6 (Scan) or Enter asks for a
// fresh scan (switched off while one runs and while there is no list); up
// and down (F/X) scroll the list. Opening the page asks for nothing: it
// shows the list the network service already has.
//
// The first row shown lives in the view model (wifi_scroll_*), so it is
// kept across a theme switch.
class WifiScreen : public BaseScreen {
public:
    WifiScreen(viewmodel::BaseViewModel& view_model,
               app::AssetManager& assets,
               view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    // The page shows at most this many network rows (fewer when the
    // theme's lines are taller).
    static constexpr std::size_t kRows = 4;
    static constexpr std::size_t kBars = 4;

    struct Row {
        std::array<lv_obj_t*, kBars> bars{};
        lv_obj_t* name{nullptr};
        lv_obj_t* channel{nullptr};
        lv_obj_t* security{nullptr};
        lv_obj_t* signal{nullptr};
        std::string name_shown;
        std::string channel_shown;
        std::string security_shown;
        std::string signal_shown;
        int32_t name_tone{-1};
        int32_t bars_shown{-1};   // lit bars shown; -1 before the first
        int32_t bars_tone{-1};
        bool hidden{true};        // the row's objects are hidden
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // True when a scan may be asked for now: a list can be shown and no
    // scan runs.
    bool can_scan() const;
    void scan();
    void scroll(int32_t delta);
    // The first row shown, from the view model's scroll, clamped.
    int32_t first_row() const;

    void refresh();
    void refresh_rows();
    void refresh_status();
    void set_row_hidden(Row& row, bool hidden);

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* interface_caption_{nullptr};
    lv_obj_t* count_{nullptr};
    std::array<Row, kRows> rows_{};
    int32_t visible_rows_{0};
    int32_t list_top_{0};
    int32_t list_height_{0};
    int32_t row_height_{0};
    lv_obj_t* note_{nullptr};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};

    std::string count_shown_;
    std::string note_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    int32_t thumb_top_shown_{-1};
    int32_t thumb_height_shown_{-1};
    int32_t shown_can_scan_{-1};  // what the Scan key shows; -1 before the first
};

} // namespace screen
