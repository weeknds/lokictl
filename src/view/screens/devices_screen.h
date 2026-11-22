/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"

#include <array>
#include <cstdint>
#include <string>

namespace screen {

// The Devices page, opened from the home page (Enter): every device on the
// hotspot (HomeData::clients), as a list of cards under the title bar. Each
// card has three lines: the name (or "unnamed") with "new" and the signal
// at the right; the address with the traffic; the hardware address with
// how long ago it joined. A value that is not known reads "--", except the
// signal, which is left out (some Wi-Fi chips never tell it in access
// point mode).
//
// kVisibleCards cards are on screen at once. The highlighted card is the
// top one, unless the list ends; a thin mark at the right edge shows where
// the cards on screen lie when there are more. Without the hotspot a box
// says that devices show while Router runs; with the hotspot and nobody on
// it, the box says so and names the hotspot.
//
// Up and down (F/X), left and right (Z/C) and keys 5 and 6 (Up, Down) move
// the highlight and stop at the first and the last device. Key 7 (QR)
// opens the Hotspot page with the join code. Key 4 or a short ESC goes
// back. The highlight lives in the view model (devices_selection_*), so it
// is kept across a theme switch; the page shows it clamped to the devices
// there are. Only the card slots exist; when the values or the highlight
// change, they are given the devices from the top card on, and only texts
// and styles that changed are touched; nothing is built again.
class DevicesScreen : public BaseScreen {
public:
    DevicesScreen(viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets,
                  view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    // The page's help.
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

    // How many cards the page shows at once.
    static constexpr int32_t kVisibleCards = 2;

private:
    // One card and the texts it shows.
    struct Card {
        lv_obj_t* card{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* signal{nullptr};
        lv_obj_t* address{nullptr};
        lv_obj_t* rate{nullptr};
        lv_obj_t* hardware{nullptr};
        lv_obj_t* joined{nullptr};
        std::string name_shown;
        std::string signal_shown;
        std::string address_shown;
        std::string rate_shown;
        std::string hardware_shown;
        std::string joined_shown;
        int32_t style_shown{-1}; // -1 hidden, 0 plain, 1 highlighted
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The highlighted device, clamped to the devices there are; 0 without
    // devices.
    int32_t selected() const;
    // Moves the highlight by `delta` devices; it stops at both ends.
    void move(int32_t delta);

    // Shows the values and the highlight.
    void refresh();
    void style_card(Card& card, int32_t style);

    // The user data is the screen; both observers are bound to the content
    // area, so they go when the screen goes.
    static void data_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void selection_observer(lv_observer_t* observer, lv_subject_t* subject);

    std::array<Card, kVisibleCards> cards_{};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
    // The box shown without devices, its two lines and their texts.
    lv_obj_t* empty_box_{nullptr};
    lv_obj_t* empty_title_{nullptr};
    lv_obj_t* empty_note_{nullptr};
    std::string empty_title_shown_;
    std::string empty_note_shown_;
};

} // namespace screen
