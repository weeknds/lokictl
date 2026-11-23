/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "modes_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the four rows at the top and the status line under them, all 304 px wide.
constexpr int32_t kSideMargin  = 8;
constexpr int32_t kWidth       = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kListTop     = 2;
constexpr int32_t kRowHeight   = 19;
constexpr int32_t kRowGap      = 1;
constexpr int32_t kRowCount    = model::kNetModeCount;
constexpr int32_t kListHeight  = kRowCount * kRowHeight + (kRowCount - 1) * kRowGap;
constexpr int32_t kHintLeft    = 78;
constexpr int32_t kStatusTop   = kListTop + kListHeight + 5;
constexpr int32_t kStatusHeight = 20;
constexpr int32_t kStatusGap   = 8;
constexpr int32_t kCaptionGap  = 5;
// The hint ends this far from the row's right end, clear of the tick.
constexpr int32_t kHintRightGap = 22;

static_assert(kStatusTop + kStatusHeight <=
                  view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight,
              "the status line must end above the soft-key bar");

// Drawn at the right end of the row of the mode that is on.
constexpr const char* kActiveMark = "✓"; // check mark

// The short hint after each mode's name, in model::NetMode order. VPN's
// is made from its files (vpn_hint()).
constexpr const char* kHints[kRowCount] = {
    "join a Wi-Fi network",
    "share the cable over Wi-Fi",
    "share Wi-Fi as a hotspot",
    "",
};

constexpr int32_t kVpnRow = static_cast<int32_t>(model::NetMode::Vpn);

// Where the user puts WireGuard files, as the pages name it.
constexpr const char* kVpnFolderText = "~/.config/lokictl/wireguard";

// Index 2 of the soft-key set is key 6, index 4 key 8.
constexpr std::size_t kUseKeyIndex = 2;
constexpr std::size_t kOffKeyIndex = 4;

// The colours of the status line's left part.
enum StatusTone : int32_t {
    kToneText = 0,
    kToneMuted,
    kToneWarning,
    kToneError,
};

// True while LokiCTL's tunnel is being set up or is up.
bool vpn_active(const model::HomeData& data) {
    return data.vpn.state != model::VpnState::Off || data.mode == model::NetMode::Vpn;
}

// True while the VPN gateway's kill switch holds the hotspot devices back
// with no tunnel at all (not even one being set up).
bool gateway_down(const model::HomeData& data) {
    return data.vpn.kill_switch && data.vpn.state == model::VpnState::Off && data.ap_active;
}

// True when the row can ever be used now: Client, Router and Share always;
// VPN with a file to use, or to turn the tunnel off.
bool switchable(int32_t row, const model::HomeData& data) {
    const auto mode = static_cast<model::NetMode>(row);
    if (mode == model::NetMode::Vpn) {
        return !data.vpn.files.empty() || vpn_active(data);
    }
    return mode == model::NetMode::Client || model::is_hotspot_mode(mode);
}

// "4 s ago", "2 min ago"; "none" before any reply.
std::string reply_text(int32_t seconds) {
    if (seconds < 0) {
        return "none";
    }
    if (seconds < 120) {
        return std::to_string(seconds) + " s ago";
    }
    return std::to_string(seconds / 60) + " min ago";
}

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    return label;
}

} // namespace

ModesScreen::ModesScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool ModesScreen::handles(model::AppPage page) {
    return page == model::AppPage::Modes;
}

view::widgets::HelpContent ModesScreen::help_content() {
    return {"MODES",
            {
                {"ESC / 4", "Back"},
                {"ENTER / 6", "Switch to the mode"},
                {"Z / C", "VPN: pick a file"},
                {"5", "VPN leak test"},
                {"7", "Hotspot: join code, name"},
            }};
}

view::widgets::HelpContent ModesScreen::help() const {
    return help_content();
}

int32_t ModesScreen::active_row() const {
    return std::clamp<int32_t>(static_cast<int32_t>(view_model().home().mode), 0, kRowCount - 1);
}

int32_t ModesScreen::selected_row() const {
    const int32_t selection = view_model().modes_selection();
    if (selection >= 0 && selection < kRowCount) {
        return selection;
    }
    return active_row();
}

bool ModesScreen::can_use(int32_t row) const {
    const model::HomeData& data = view_model().home();
    if (data.mode_switch.state == model::SwitchState::Switching || !switchable(row, data)) {
        return false;
    }
    // On the VPN row while the tunnel runs, Use turns it off.
    return row != active_row() || (row == kVpnRow && vpn_active(data));
}

bool ModesScreen::turns_vpn_off(int32_t row) const {
    return row == kVpnRow && vpn_active(view_model().home());
}

void ModesScreen::use_selected() {
    const int32_t row = selected_row();
    if (!can_use(row)) {
        return;
    }
    if (turns_vpn_off(row)) {
        view_model().request_vpn_off();
    } else {
        view_model().request_mode(static_cast<model::NetMode>(row));
    }
}

bool ModesScreen::offers_vpn_off() const {
    const model::HomeData& data = view_model().home();
    return selected_row() == kVpnRow && gateway_down(data) && data.mode_switch.state != model::SwitchState::Switching;
}

std::string ModesScreen::vpn_hint() const {
    const model::HomeData& data = view_model().home();
    if (vpn_active(data)) {
        return data.vpn.file.empty() ? std::string("tunnel on") : data.vpn.file;
    }
    const std::vector<std::string>& files = data.vpn.files;
    if (files.empty()) {
        return "no WireGuard files";
    }
    const std::string file = view_model().vpn_file();
    if (files.size() == 1) {
        return file;
    }
    const auto index = std::find(files.begin(), files.end(), file) - files.begin();
    return file + "  ‹ " + std::to_string(index + 1) + "/" + std::to_string(files.size()) + " ›";
}

view::widgets::SoftKeySet ModesScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it. Key 6
    // starts switched off; refresh() switches it on where Use works.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Leak", [&view_model] { view_model.open_leak(); }},
        {"Use", [this] { use_selected(); }, true, false},
        {"QR", [&view_model] { view_model.open_hotspot(); }},
        {},  // key 8: Off while the gateway is down (see refresh())
    }};
}

// Up and down move the highlight and stop at the first and the last row;
// Enter is Use.
bool ModesScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_DOWN: {
            const int32_t current = selected_row();
            const int32_t next = std::clamp<int32_t>(current + (key == LV_KEY_UP ? -1 : 1), 0, kRowCount - 1);
            if (next != current) {
                view_model().select_mode_row(next);
            }
            return true;
        }
        case LV_KEY_ENTER:
            use_selected();
            return true;
        case LV_KEY_LEFT:
        case LV_KEY_RIGHT:
            // Picks another WireGuard file while the VPN row is highlighted
            // and no tunnel runs.
            if (selected_row() != kVpnRow || vpn_active(view_model().home())) {
                return false;
            }
            view_model().step_vpn_file(key == LV_KEY_LEFT ? -1 : 1);
            return true;
        default:
            return false;
    }
}

void ModesScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    auto* list = plain_object(content);
    lv_obj_set_pos(list, kSideMargin, kListTop);
    lv_obj_set_size(list, kWidth, kListHeight);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, kRowGap, 0);

    // Fixed row heights and places, so nothing moves when the highlight
    // switches a row to the Strong font. The name and the tick inherit the
    // row's font and colour (view::style::list_row()); the hint keeps the
    // Small font of its own.
    for (int32_t index = 0; index < kRowCount; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        row.row = plain_object(list);
        lv_obj_set_size(row.row, LV_PCT(100), kRowHeight);

        row.name = plain_label(row.row, model::net_mode_name(static_cast<model::NetMode>(index)));
        lv_obj_align(row.name, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);

        row.hint = plain_label(row.row, kHints[index]);
        lv_obj_set_style_text_font(row.hint, theme.font(view::FontRole::Small), 0);
        lv_label_set_long_mode(row.hint, LV_LABEL_LONG_DOT);
        lv_obj_set_width(row.hint, kWidth - kHintLeft - kHintRightGap);
        lv_obj_align(row.hint, LV_ALIGN_LEFT_MID, kHintLeft, 0);

        row.mark = plain_label(row.row, kActiveMark);
        lv_obj_align(row.mark, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
        lv_obj_add_flag(row.mark, LV_OBJ_FLAG_HIDDEN);
    }

    // The status line: one text across the line, or the hotspot's name on
    // the left and its password, with a caption, on the right.
    const int32_t inset = theme.metrics.border_width + theme.metrics.space_xs + theme.metrics.space_sm;
    status_left_ = plain_label(content, "");
    view::style::text(status_left_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_left_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_left_, kSideMargin + inset, kStatusTop);
    status_right_ = plain_label(content, "");
    view::style::text(status_right_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_obj_align(status_right_, LV_ALIGN_TOP_RIGHT, -(kSideMargin + inset), kStatusTop);
    status_caption_ = plain_label(content, "");
    view::style::caption(status_caption_, theme, "password");
    lv_obj_add_flag(status_caption_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away; the first call draws the
    // rows, the tick, the status line and key 6.
    lv_subject_add_observer_obj(view_model().home_data_subject(), data_observer, content, this);
    lv_subject_add_observer_obj(view_model().modes_selection_subject(), selection_observer, content, this);
    lv_subject_add_observer_obj(view_model().vpn_file_subject(), selection_observer, content, this);
}

void ModesScreen::refresh() {
    const auto& theme = this->theme();
    const model::HomeData& data = view_model().home();
    const int32_t selected = selected_row();
    const int32_t active = active_row();

    set_text_if_changed(rows_[static_cast<std::size_t>(kVpnRow)].hint, vpn_hint_shown_, vpn_hint());

    const int32_t vpn_switchable = switchable(kVpnRow, data) ? 1 : 0;
    if (selected != shown_selected_ || vpn_switchable != shown_vpn_switchable_) {
        for (int32_t index = 0; index < kRowCount; ++index) {
            const Row& row = rows_[static_cast<std::size_t>(index)];
            const bool is_selected = index == selected;
            view::style::list_row(row.row, theme, is_selected);
            // Off rows and hints are muted, except on the highlight, whose
            // colours come from the row.
            if (is_selected) {
                lv_obj_remove_local_style_prop(row.hint, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.name, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(row.hint, theme.colors.muted, 0);
                if (switchable(index, data)) {
                    lv_obj_remove_local_style_prop(row.name, LV_STYLE_TEXT_COLOR, 0);
                } else {
                    lv_obj_set_style_text_color(row.name, theme.colors.muted, 0);
                }
            }
        }
        shown_selected_ = selected;
        shown_vpn_switchable_ = vpn_switchable;
    }

    // The tick on the mode that is on, and in the VPN gateway on the VPN
    // row as well.
    const int32_t vpn_mark = active != kVpnRow && vpn_active(data) ? 1 : 0;
    if (active != shown_active_ || vpn_mark != shown_vpn_mark_) {
        for (int32_t index = 0; index < kRowCount; ++index) {
            lv_obj_t* mark = rows_[static_cast<std::size_t>(index)].mark;
            if (index == active || (index == kVpnRow && vpn_mark == 1)) {
                lv_obj_clear_flag(mark, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
            }
        }
        shown_active_ = active;
        shown_vpn_mark_ = vpn_mark;
    }

    // Key 6: 0 off, 1 Use, 2 Off (the VPN row while the tunnel runs).
    const int32_t use = can_use(selected) ? (turns_vpn_off(selected) ? 2 : 1) : 0;
    if (use != shown_use_) {
        if (auto* bar = soft_key_bar()) {
            bar->set_key(kUseKeyIndex, {use == 2 ? "Off" : "Use", [this] { use_selected(); }, true, use != 0});
        }
        shown_use_ = use;
    }

    // Key 8: Off while the gateway is down and the VPN row is highlighted.
    const int32_t off = offers_vpn_off() ? 1 : 0;
    if (off != shown_off_) {
        if (auto* bar = soft_key_bar()) {
            if (off == 1) {
                bar->set_key(kOffKeyIndex, {"Off", [this] {
                                                if (offers_vpn_off()) {
                                                    view_model().request_vpn_off();
                                                }
                                            }});
            } else {
                bar->set_key(kOffKeyIndex, {});
            }
        }
        shown_off_ = off;
    }

    refresh_status();
}

void ModesScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::HomeData& data = view_model().home();
    const model::ModeSwitch& change = data.mode_switch;

    std::string left;
    std::string right;
    std::string caption = "password";
    int32_t tone = kToneText;
    if (change.state == model::SwitchState::Switching) {
        // The highlight stays on the mode asked for, so the line need not
        // name it.
        left = "Switching\u2026";
    } else if (change.state == model::SwitchState::Failed) {
        switch (change.error) {
            case model::SwitchError::NoCable:
                left = "Plug in a cable first";
                tone = kToneWarning;
                break;
            case model::SwitchError::NoWifi:
                left = "Join a Wi-Fi network first";
                tone = kToneWarning;
                break;
            case model::SwitchError::ClientFirst:
                left = "Back to Client first";
                tone = kToneWarning;
                break;
            case model::SwitchError::NotAllowed:
                left = "Not allowed: permission missing";
                tone = kToneError;
                break;
            case model::SwitchError::Failed:
            case model::SwitchError::None:
                left = "Failed: " + (change.reason.empty() ? std::string("unknown error") : change.reason);
                tone = kToneError;
                break;
        }
    } else if (gateway_down(data)) {
        // The kill switch holds the hotspot devices back until the VPN is
        // on again (Use) or off (key 8).
        left = "VPN down: hotspot devices blocked";
        tone = kToneWarning;
    } else if (model::is_hotspot_mode(data.mode) && data.ap_active &&
               !(selected_row() == kVpnRow && vpn_active(data))) {
        left = data.ap_name.empty() ? std::string("Hotspot on") : data.ap_name;
        right = view_model().hotspot_password();
    } else if (vpn_active(data)) {
        // The server, and how long ago data last came back through the
        // tunnel (WireGuard is "on" without any answer).
        if (data.vpn.state == model::VpnState::Connecting) {
            left = "VPN connecting…";
        } else {
            left = data.vpn.server.empty() ? std::string("VPN on") : "VPN " + data.vpn.server;
            right = reply_text(data.vpn.reply_seconds);
            caption = "reply";
        }
    } else if (selected_row() == kVpnRow && data.vpn.files.empty()) {
        // Where to put them; the row's hint says there are none.
        tone = kToneMuted;
        left = kVpnFolderText;
    } else {
        tone = kToneMuted;
        switch (data.uplink_kind) {
            case model::LinkKind::Wifi:
                left = data.wifi_name.empty() ? std::string("On Wi-Fi") : "On Wi-Fi " + data.wifi_name;
                break;
            case model::LinkKind::Cable:
                left = "On the cable " + data.uplink_name;
                break;
            case model::LinkKind::None:
                left = "No network";
                break;
        }
    }

    const bool caption_changed = caption != status_caption_shown_;
    if (caption_changed) {
        status_caption_shown_ = caption;
        view::style::caption(status_caption_, theme, caption.c_str());
    }
    const bool right_changed = set_text_if_changed(status_right_, status_right_shown_, right);
    if (set_text_if_changed(status_left_, status_left_shown_, left) || right_changed || caption_changed ||
        status_tone_ < 0) {
        // The left part gets the room the right part leaves; longer text
        // ends in dots.
        const int32_t inset = theme.metrics.border_width + theme.metrics.space_xs + theme.metrics.space_sm;
        int32_t width = kWidth - 2 * inset;
        if (!right.empty()) {
            // The caption stands left of the password, on its baseline.
            lv_obj_clear_flag(status_caption_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(status_right_);
            lv_obj_align_to(status_caption_, status_right_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                            theme.font(view::FontRole::Small)->base_line -
                                theme.font(view::FontRole::Mono)->base_line);
            lv_obj_update_layout(status_caption_);
            width -= lv_obj_get_width(status_right_) + kCaptionGap + lv_obj_get_width(status_caption_) + kStatusGap;
        } else {
            lv_obj_add_flag(status_caption_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_size(status_left_, std::max<int32_t>(width, 0), kStatusHeight);
    }
    if (tone != status_tone_) {
        const lv_color_t colors[] = {theme.colors.text, theme.colors.muted, theme.colors.warning, theme.colors.error};
        lv_obj_set_style_text_color(status_left_, colors[tone], 0);
        status_tone_ = tone;
    }
}

void ModesScreen::selection_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ModesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void ModesScreen::data_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ModesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
