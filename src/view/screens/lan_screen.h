/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "lan_speed.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The LAN page (Speed key 7, or `lan`): the speed between the Cardputer and
// another device on the same network, measured with iperf3. Two sides,
// switched with key 7 while nothing runs:
//
// - Test (the start): the target row (what it is, its address and its
//   place in the list: the router, the devices Discover and the hotspot
//   found, then a typed address), then DOWN and UP as on the Speed page
//   (down is from that device to the Cardputer), then the status line with
//   the Sent count. Z/C pick the device, 5 types an address (digits and
//   dots), 6 or Enter runs a test (10 s each way) or stops it. A test goes
//   on when the page is left. The other device needs an iperf3 server.
// - Server: "type this in the app" with the Cardputer's address on this
//   network (Z/C switch to the hotspot's while it runs), the port, the
//   latest tests ("From 192.168.1.20: 94 Mbit/s", "To ..." when the
//   Cardputer sent), then the status line. 6 or Enter starts and stops the
//   server; leaving the page stops it (app::LanController).
//
// Nothing starts while another tool runs. The run lives in the view model
// (lan()), and what the page shows and picked too, so a theme switch keeps
// them.
class LanScreen : public BaseScreen {
public:
    LanScreen(viewmodel::BaseViewModel& view_model,
              app::AssetManager& assets,
              view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content(bool server);

    bool on_key(uint32_t key) override;
    bool on_back() override;
    bool text_entry() const override;
    view::widgets::HelpContent help() const override;

private:
    struct Column {
        lv_obj_t* caption{nullptr};
        lv_obj_t* speed{nullptr};
        lv_obj_t* unit{nullptr};
        lv_obj_t* bytes{nullptr};
        lv_obj_t* graph{nullptr};
        std::string speed_shown;
        std::string bytes_shown;
        std::array<uint8_t, model::kLanSamples> bars_shown{};
    };
    struct Target {
        std::string address;
        std::string caption;
    };
    static constexpr std::size_t kRowMax = 3;

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;
    void build_column(lv_obj_t* parent, Column& column, const char* caption, int32_t left, int32_t top,
                      lv_color_t bar_color);
    void build_server(lv_obj_t* parent);

    std::vector<Target> targets() const;
    int32_t target_index(const std::vector<Target>& list) const;
    int32_t place_index() const;
    bool running() const;
    bool other_running() const;
    bool server_side() const;

    void step(int32_t delta);
    void main_key();
    void switch_side();
    void start_edit();
    bool type_key(uint32_t key);

    void refresh();
    void refresh_keys();
    void refresh_target();
    void refresh_column(Column& column, int32_t kbps, uint64_t bytes, const std::vector<int32_t>& samples,
                        int32_t scale);
    void refresh_server();
    void refresh_status();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* test_panel_{nullptr};
    lv_obj_t* server_panel_{nullptr};
    // Test side.
    lv_obj_t* target_caption_{nullptr};
    lv_obj_t* target_value_{nullptr};
    lv_obj_t* target_place_{nullptr};
    Column down_;
    Column up_;
    std::string caption_shown_;
    std::string target_shown_;
    std::string place_shown_;
    // Server side.
    lv_obj_t* app_caption_{nullptr};
    lv_obj_t* where_{nullptr};
    lv_obj_t* address_{nullptr};
    lv_obj_t* port_{nullptr};
    std::array<lv_obj_t*, kRowMax> rows_{};
    std::array<std::string, kRowMax> rows_shown_{};
    std::array<int32_t, kRowMax> row_tones_{-1, -1, -1};
    std::size_t row_count_{0};  // the rows that fit
    std::string where_shown_;
    std::string address_shown_;
    // Both sides.
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    int32_t side_shown_{-1};
    int32_t keys_shown_{-1};
    // Typing an address.
    bool typing_{false};
    bool draft_invalid_{false};
    std::string draft_;
};

} // namespace screen
