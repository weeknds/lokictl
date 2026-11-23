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

// The Tools page, opened from the home page (key 7). It lists the tools in
// two columns: Ping, DNS and Trace on the left, Wi-Fi, Ports and Speed on
// the right. Next to Ping, DNS, Trace, Ports and Speed a short tag tells
// how the latest run stands ("running", the average time, how many
// addresses, hops or open ports, the download and upload speeds). Under the list, the status line says what the
// highlighted tool does, with the app-wide Sent count at its right.
//
// Up and down (F/X) move through Ping, DNS, Trace, Wi-Fi, Ports and Speed. Enter
// or key 6 (Open) opens the highlighted tool's page. Key 5 (Login) opens
// the Sign-in page (hotel Wi-Fi), key 7 (Check) the Health page and key 8
// (Watch) the Watch page. Key 4 or a short ESC
// goes back.
//
// The highlight lives in the view model (tools_selection_*), so it is kept
// across a theme switch.
class ToolsScreen : public BaseScreen {
public:
    // The tools that can be opened: Ping, DNS and Trace, in the order of
    // model::ToolKind, then Wi-Fi, Ports and Speed.
    static constexpr int32_t kOpenableRows = 6;
    static constexpr int32_t kWifiRow = 3;
    static constexpr int32_t kPortsRow = 4;
    static constexpr int32_t kSpeedRow = 5;

    ToolsScreen(viewmodel::BaseViewModel& view_model,
                app::AssetManager& assets,
                view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    static constexpr int32_t kCellCount = 6;

    struct Cell {
        lv_obj_t* cell{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* tag{nullptr};
        std::string tag_shown;
        int32_t tone_shown{-1};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    int32_t selected_row() const;
    void open_selected();
    void refresh();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::array<Cell, kCellCount> cells_{};
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    std::string status_shown_;
    std::string sent_shown_;
    int32_t shown_selected_{-1};
};

} // namespace screen
