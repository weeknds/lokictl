/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <cstdint>
#include <string>

namespace screen {

// The Sign-in page (Tools key 5, or the command `signin`): helps with Wi-Fi
// that wants a sign-in first (a hotel, a cafe). Opening the page checks
// once (model::SigninRun, in tools().signin): one plain-HTTP request to a
// page that answers "no content" when the internet is open.
//
// Like the Hotspot page, without a title bar: a QR code at the left and a
// column at its right with the page's name, the verdict ("Sign-in
// needed", "No sign-in needed", "Checking…"), one short hint ("Scan with
// your phone"), the sign-in page's host while one is needed, whether the
// system's own check (NetworkManager's, which shows the message "Wi-Fi
// wants you to sign in") is on, and the Sent count. The code holds the
// sign-in address (model::signin_code_text()) only while a sign-in is
// needed; otherwise a note stands in its place.
//
// The detail view (Enter or key 5) shows the whole address (or the check
// page's), what it means and what to do, with the Share-mode tip: signing
// in from a phone on LokiCTL's hotspot gives every device on it internet.
//
// Keys: 4 (Back) or a short ESC closes the detail, else goes back (a check
// goes on); 5 (Info) or Enter opens or closes the detail; 6 (Check / Stop)
// checks again (off while another tool runs) and stops a check.
//
// The detail lives in the view model (signin_detail_*), so it is kept
// across a theme switch.
class SigninScreen : public BaseScreen {
public:
    SigninScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    bool on_back() override;
    view::widgets::HelpContent help() const override;

private:
    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    bool this_running() const;
    bool any_running() const;
    void check_or_stop();
    void toggle_detail();

    void refresh();
    void refresh_keys();
    void refresh_code();
    void refresh_column();
    void refresh_detail();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* main_panel_{nullptr};
    lv_obj_t* code_{nullptr};
    lv_obj_t* code_box_{nullptr};
    lv_obj_t* code_note_{nullptr};
    lv_obj_t* verdict_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_obj_t* page_caption_{nullptr};
    lv_obj_t* page_{nullptr};
    lv_obj_t* check_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    // The detail view.
    lv_obj_t* detail_{nullptr};
    lv_obj_t* detail_title_{nullptr};
    lv_obj_t* detail_verdict_{nullptr};
    lv_obj_t* detail_address_{nullptr};
    lv_obj_t* detail_advice_{nullptr};

    std::string code_text_shown_;
    bool code_ok_{false};
    std::string note_shown_;
    std::string verdict_shown_;
    int32_t verdict_tone_{-1};
    std::string hint_shown_;
    std::string page_shown_;
    int32_t page_tone_{-1};
    std::string check_shown_;
    int32_t check_tone_{-1};
    std::string sent_shown_;
    std::string detail_title_shown_;
    std::string detail_verdict_shown_;
    int32_t detail_tone_{-1};
    std::string detail_address_shown_;
    std::string detail_advice_shown_;
    int32_t detail_shown_{-1};   // 0 main, 1 detail; -1 before the first refresh
    int32_t shown_check_key_{-1}; // 0 off, 1 Check, 2 Stop; -1 before the first
};

} // namespace screen
