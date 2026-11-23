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
#include <vector>

namespace screen {

// The Talk page, "What is it talking to?" (Devices key 5, or the command
// `talk`): the names one device on LokiCTL's hotspot looked up
// (model::TalkRun, in talk()), newest first. Under the title bar:
// - the heading "Looked up by phone" (the device's name, else its address)
//   and, with more than one device, where it stands ("2 of 3");
// - one line per name, as many as fit: the name, cut from the left when
//   it is too long (the end says whose site it is), and how often it was
//   looked up ("x3") when more than once; a thin mark at the right edge
//   shows where the lines lie when there are more. In place of the list, a
//   note: "Hotspot is off", "No devices on the hotspot", "No names looked
//   up yet";
// - the status line: "Hotspot only, sends nothing", "Turn on Router or
//   Share first", or what went wrong ("Capture not allowed").
//
// Keys: 4 (Back) or a short ESC goes back; 5 (Prev) and 6 (Next), or left
// and right (Z/C), show the previous or next device on the hotspot (both
// ends wrap round); up and down (F/X) scroll the names; 8 (Save) or R saves
// a report, which then holds the names too.
//
// The device (by hardware address) and the first name shown live in the
// view model (talk_device(), talk_scroll_*), so they are kept across a
// theme switch.
class TalkScreen : public BaseScreen {
public:
    TalkScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    // The page shows at most this many names (fewer when the theme's lines
    // are taller).
    static constexpr std::size_t kRows = 6;

    struct Row {
        lv_obj_t* name{nullptr};
        lv_obj_t* count{nullptr};
        std::string name_shown;
        std::string count_shown;
        bool hidden{true};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The devices Z/C step through: the hotspot's devices in the Devices
    // page's order, then any other device heard; by hardware address.
    std::vector<std::string> device_order() const;
    // The device shown: talk_device() when it is in device_order(), else
    // the first one; empty without devices.
    std::string shown_device() const;
    void step_device(int32_t delta);
    void scroll(int32_t delta);

    void refresh();
    void refresh_keys(std::size_t devices);

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* heading_{nullptr};
    lv_obj_t* place_{nullptr};
    std::array<Row, kRows> rows_{};
    int32_t visible_rows_{0};
    int32_t name_width_{0};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
    lv_obj_t* note_{nullptr};
    lv_obj_t* status_{nullptr};
    int32_t list_top_{0};
    int32_t row_height_{0};

    std::string heading_shown_;
    std::string place_shown_;
    std::string note_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    int32_t step_keys_shown_{-1};
};

} // namespace screen
