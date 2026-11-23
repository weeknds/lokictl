/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_screen.h"

#include "home_format.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the summary row at the top, the network rows under it and the status
// line at the bottom, like the Trace page. The rows follow the theme's line
// heights.
constexpr int32_t kContentHeight  = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin     = 8;
constexpr int32_t kWidth          = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kSummaryTop     = 1;
constexpr int32_t kListGap        = 4;
constexpr int32_t kStatusHeight   = 18;
constexpr int32_t kStatusTop      = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap      = 8;
constexpr int32_t kCaptionGap     = 5;
constexpr int32_t kColumnGap      = 6;
constexpr int32_t kTrackX         = view::kScreenWidth - 6;
constexpr int32_t kThumbX         = kTrackX - 1;
constexpr int32_t kThumbWidth     = 3;
constexpr int32_t kThumbMinHeight = 8;
constexpr int32_t kRowRight       = kThumbX - 4;
// The signal bars: four, each kBarWidth wide with kBarGap between them,
// the first kBarStep high and each next one kBarStep higher.
constexpr int32_t kBarWidth = 2;
constexpr int32_t kBarGap   = 1;
constexpr int32_t kBarStep  = 2;
constexpr int32_t kBarsWidth = 4 * kBarWidth + 3 * kBarGap;
// The interface the page lists (platform::kHotspotInterface).
constexpr const char* kInterfaceName = "wlan0";

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

lv_color_t tone_color(const view::Theme& theme, int32_t tone) {
    switch (tone) {
        case tools::kToneMuted:
            return theme.colors.muted;
        case tools::kToneSuccess:
            return theme.colors.success;
        case tools::kToneWarning:
            return theme.colors.warning;
        case tools::kToneError:
            return theme.colors.error;
        case tools::kToneAccent:
            return theme.colors.accent;
        default:
            return theme.colors.text;
    }
}

void set_tone_if_changed(lv_obj_t* label, int32_t& shown, int32_t tone, const view::Theme& theme) {
    if (label && tone != shown) {
        lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        shown = tone;
    }
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

int32_t text_width(const char* text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// True while the scan list cannot be shown at all.
bool no_list(model::WifiScanError error) {
    return error == model::WifiScanError::NoService || error == model::WifiScanError::NoDevice ||
           error == model::WifiScanError::WifiOff || error == model::WifiScanError::Hotspot;
}

// How many of the four bars are lit: nmcli's steps (above 80, 55, 30 and 5
// per cent).
int32_t lit_bars(int32_t strength) {
    if (strength > 80) {
        return 4;
    }
    if (strength > 55) {
        return 3;
    }
    if (strength > 30) {
        return 2;
    }
    return strength > 5 ? 1 : 0;
}

// What stands in place of the list when there is none.
std::string note_text(const model::WifiScan& scan) {
    switch (scan.error) {
        case model::WifiScanError::Hotspot:
            return "The Wi-Fi runs the hotspot. Go back to Client on the Modes page to scan.";
        case model::WifiScanError::WifiOff:
            return "The Wi-Fi radio is off.";
        case model::WifiScanError::NoService:
            return "No network service to ask.";
        case model::WifiScanError::NoDevice:
            return "No Wi-Fi device to scan with.";
        default:
            break;
    }
    return scan.scanning ? "Listening for networks…" : "No networks yet. Press 6 to scan.";
}

tools::ToneText status_text(const model::WifiScan& scan) {
    switch (scan.error) {
        case model::WifiScanError::Hotspot:
            return {"Not while the hotspot runs", tools::kToneWarning};
        case model::WifiScanError::WifiOff:
            return {"Wi-Fi is off", tools::kToneWarning};
        case model::WifiScanError::NoService:
            return {"No network service", tools::kToneWarning};
        case model::WifiScanError::NoDevice:
            return {"No Wi-Fi device", tools::kToneWarning};
        default:
            break;
    }
    if (scan.scanning) {
        return {"Scanning…", tools::kToneAccent};
    }
    switch (scan.error) {
        case model::WifiScanError::NotAllowed:
            return {"Scan not allowed: permission missing", tools::kToneError};
        case model::WifiScanError::TimedOut:
            return {"No scan result in 15 s", tools::kToneWarning};
        case model::WifiScanError::Refused:
            return {"Scan refused: " + scan.reason, tools::kToneWarning};
        default:
            break;
    }
    if (scan.last_scan_minutes >= 0) {
        return {"Last scan " + home::clock_text(scan.last_scan_minutes), tools::kToneMuted};
    }
    return {"Not scanned yet", tools::kToneMuted};
}

std::string count_text(const model::WifiScan& scan) {
    if (no_list(scan.error)) {
        return "";
    }
    return std::to_string(scan.found) + (scan.found == 1 ? " network" : " networks");
}

} // namespace

WifiScreen::WifiScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool WifiScreen::handles(model::AppPage page) {
    return page == model::AppPage::Wifi;
}

view::widgets::HelpContent WifiScreen::help_content() {
    return {"WI-FI",
            {
                {"ESC / 4", "Back"},
                {"F / X", "Scroll the networks"},
                {"ENTER / 6", "Scan for networks"},
                {"Accent", "The joined network"},
                {"Sent", "Scans are not counted"},
            }};
}

view::widgets::HelpContent WifiScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet WifiScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh() switches Scan on and off.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Scan", [this] { scan(); }, true},
        {},
        {},
    }};
}

bool WifiScreen::can_scan() const {
    const model::WifiScan& wifi = view_model().home().wifi_scan;
    return !wifi.scanning && !no_list(wifi.error);
}

void WifiScreen::scan() {
    if (can_scan()) {
        view_model().request_wifi_scan();
    }
}

int32_t WifiScreen::first_row() const {
    const auto count = static_cast<int32_t>(view_model().home().wifi_scan.networks.size());
    const int32_t last_top = std::max<int32_t>(count - visible_rows_, 0);
    return std::clamp<int32_t>(view_model().wifi_scroll(), 0, last_top);
}

void WifiScreen::scroll(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().home().wifi_scan.networks.size());
    const int32_t last_top = std::max<int32_t>(count - visible_rows_, 0);
    view_model().set_wifi_scroll(std::clamp<int32_t>(first_row() + delta, 0, last_top));
}

bool WifiScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            scroll(-1);
            return true;
        case LV_KEY_DOWN:
            scroll(1);
            return true;
        case LV_KEY_ENTER:
            scan();
            return true;
        default:
            return false;
    }
}

void WifiScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    // The summary row: the caption stands on the count's baseline.
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    interface_caption_ = plain_label(content, "");
    view::style::caption(interface_caption_, theme, kInterfaceName);
    lv_obj_set_pos(interface_caption_, kSideMargin, kSummaryTop + small_drop);
    lv_obj_update_layout(interface_caption_);
    count_ = plain_label(content, "");
    view::style::text(count_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_obj_set_pos(count_, kSideMargin + lv_obj_get_width(interface_caption_) + kCaptionGap, kSummaryTop);

    // The rows end above the status line, whatever the theme's line height.
    row_height_ = lv_font_get_line_height(small);
    list_top_ = kSummaryTop + lv_font_get_line_height(mono) + kListGap;
    visible_rows_ = std::clamp<int32_t>((kStatusTop - 1 - list_top_) / row_height_, 1, static_cast<int32_t>(kRows));
    list_height_ = visible_rows_ * row_height_;

    // Columns: the bars, the name (ends in dots when longer), the channel,
    // the security and the signal (right-aligned).
    const int32_t signal_width = text_width("100%", small);
    const int32_t signal_x = kRowRight - signal_width;
    const int32_t security_width = text_width("WPA2-EAP", small);
    const int32_t security_x = signal_x - kColumnGap - security_width;
    const int32_t channel_width = text_width("ch 13", small);
    const int32_t channel_x = security_x - kColumnGap - channel_width;
    const int32_t name_x = kSideMargin + kBarsWidth + kColumnGap;
    const int32_t name_width = std::max<int32_t>(channel_x - kColumnGap - name_x, 0);
    const int32_t bars_bottom = baseline_from_top(small);

    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        const int32_t top = list_top_ + index * row_height_;
        for (std::size_t bar = 0; bar < kBars; ++bar) {
            const auto step = static_cast<int32_t>(bar) + 1;
            const int32_t height = step * kBarStep;
            row.bars[bar] = plain_object(content);
            lv_obj_set_size(row.bars[bar], kBarWidth, height);
            lv_obj_set_pos(row.bars[bar], kSideMargin + static_cast<int32_t>(bar) * (kBarWidth + kBarGap),
                           top + bars_bottom - height);
            lv_obj_set_style_bg_opa(row.bars[bar], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(row.bars[bar], theme.colors.border_subtle, 0);
        }
        row.name = plain_label(content, "");
        view::style::text(row.name, theme, view::FontRole::Small);
        lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.name, name_width, row_height_);
        lv_obj_set_pos(row.name, name_x, top);
        row.channel = plain_label(content, "");
        view::style::text(row.channel, theme, view::FontRole::Small, theme.colors.muted);
        lv_obj_set_size(row.channel, channel_width, row_height_);
        lv_obj_set_pos(row.channel, channel_x, top);
        row.security = plain_label(content, "");
        view::style::text(row.security, theme, view::FontRole::Small, theme.colors.muted);
        lv_label_set_long_mode(row.security, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(row.security, security_width, row_height_);
        lv_obj_set_pos(row.security, security_x, top);
        row.signal = plain_label(content, "");
        view::style::text(row.signal, theme, view::FontRole::Small);
        lv_obj_set_size(row.signal, signal_width, row_height_);
        lv_obj_set_style_text_align(row.signal, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.signal, signal_x, top);
        row.hidden = false;
        set_row_hidden(row, true);
    }

    note_ = plain_label(content, "");
    view::style::text(note_, theme, view::FontRole::Mono, theme.colors.muted);
    lv_obj_set_width(note_, kWidth);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(note_, kSideMargin, list_top_);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

    track_ = plain_object(content);
    lv_obj_set_pos(track_, kTrackX, list_top_);
    lv_obj_set_size(track_, 1, list_height_);
    lv_obj_set_style_bg_color(track_, theme.colors.border_subtle, 0);
    lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
    lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);
    thumb_ = plain_object(content);
    lv_obj_set_style_bg_color(thumb_, theme.colors.muted, 0);
    lv_obj_set_style_bg_opa(thumb_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb_, std::min<int32_t>(theme.metrics.radius, 1), 0);
    lv_obj_add_flag(thumb_, LV_OBJ_FLAG_HIDDEN);

    // The status line, with the Sent count and its caption at the right.
    status_ = plain_label(content, "");
    view::style::text(status_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_, kSideMargin, kStatusTop);
    sent_ = plain_label(content, "");
    view::style::text(sent_, theme, view::FontRole::Mono, theme.colors.text);
    lv_obj_align(sent_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kStatusTop);
    sent_caption_ = plain_label(content, "");
    view::style::caption(sent_caption_, theme, "sent");

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().wifi_scroll_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
}

void WifiScreen::set_row_hidden(Row& row, bool hidden) {
    if (row.hidden == hidden) {
        return;
    }
    for (lv_obj_t* bar : row.bars) {
        set_hidden(bar, hidden);
    }
    set_hidden(row.name, hidden);
    set_hidden(row.channel, hidden);
    set_hidden(row.security, hidden);
    set_hidden(row.signal, hidden);
    row.hidden = hidden;
}

void WifiScreen::refresh_rows() {
    const auto& theme = this->theme();
    const model::WifiScan& wifi = view_model().home().wifi_scan;
    const auto count = static_cast<int32_t>(wifi.networks.size());
    const int32_t first = first_row();
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        if (first + index >= count) {
            set_row_hidden(row, true);
            continue;
        }
        const model::WifiNetwork& network = wifi.networks[static_cast<std::size_t>(first + index)];
        set_row_hidden(row, false);
        const bool hidden_name = network.name.empty();
        set_text_if_changed(row.name, row.name_shown, hidden_name ? std::string("hidden") : network.name);
        const int32_t tone = network.joined ? tools::kToneAccent : hidden_name ? tools::kToneMuted : tools::kToneText;
        set_tone_if_changed(row.name, row.name_tone, tone, theme);
        set_text_if_changed(row.channel, row.channel_shown,
                            network.channel > 0 ? "ch " + std::to_string(network.channel) : std::string("ch --"));
        set_text_if_changed(row.security, row.security_shown, home::text_or_unknown(network.security));
        set_text_if_changed(row.signal, row.signal_shown,
                            std::to_string(std::clamp<int32_t>(network.strength, 0, 100)) + "%");
        const int32_t lit = lit_bars(network.strength);
        const int32_t bars_tone = network.joined ? tools::kToneAccent : tools::kToneText;
        if (lit != row.bars_shown || bars_tone != row.bars_tone) {
            for (std::size_t bar = 0; bar < kBars; ++bar) {
                const bool on = static_cast<int32_t>(bar) < lit;
                lv_obj_set_style_bg_color(row.bars[bar],
                                          on ? tone_color(theme, bars_tone) : theme.colors.border_subtle, 0);
            }
            row.bars_shown = lit;
            row.bars_tone = bars_tone;
        }
    }

    const bool empty = count == 0;
    set_hidden(note_, !empty);
    if (empty) {
        set_text_if_changed(note_, note_shown_, note_text(wifi));
    }

    // The scroll mark, only when the networks do not all fit.
    const bool scrolls = count > visible_rows_;
    set_hidden(track_, !scrolls);
    set_hidden(thumb_, !scrolls);
    if (scrolls) {
        const int32_t thumb_height = std::max<int32_t>(list_height_ * visible_rows_ / count, kThumbMinHeight);
        const int32_t travel = list_height_ - thumb_height;
        const int32_t last_top = count - visible_rows_;
        const int32_t offset = last_top > 0 ? travel * first / last_top : 0;
        if (thumb_height != thumb_height_shown_ || offset != thumb_top_shown_) {
            lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
            lv_obj_set_pos(thumb_, kThumbX, list_top_ + offset);
            thumb_height_shown_ = thumb_height;
            thumb_top_shown_ = offset;
        }
    }
}

void WifiScreen::refresh_status() {
    const auto& theme = this->theme();
    const tools::ToneText status = status_text(view_model().home().wifi_scan);
    const bool sent_changed = set_text_if_changed(sent_, sent_shown_, std::to_string(view_model().tools().sent));
    if (set_text_if_changed(status_, status_shown_, status.text) || sent_changed) {
        // The caption stands left of the count, on its baseline; the status
        // gets the rest of the line and ends in dots when it is longer.
        lv_obj_update_layout(sent_);
        lv_obj_align_to(sent_caption_, sent_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Mono)->base_line);
        lv_obj_update_layout(sent_caption_);
        const int32_t width =
            kWidth - lv_obj_get_width(sent_) - kCaptionGap - lv_obj_get_width(sent_caption_) - kStatusGap;
        lv_obj_set_size(status_, std::max<int32_t>(width, 0), kStatusHeight);
    }
    set_tone_if_changed(status_, status_tone_, status.tone, theme);
}

void WifiScreen::refresh() {
    set_text_if_changed(count_, count_shown_, count_text(view_model().home().wifi_scan));
    refresh_rows();
    refresh_status();

    const int32_t allowed = can_scan() ? 1 : 0;
    if (allowed != shown_can_scan_) {
        shown_can_scan_ = allowed;
        if (auto* bar = soft_key_bar()) {
            bar->set_key(2, {"Scan", [this] { scan(); }, true, allowed == 1});
        }
    }
}

void WifiScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<WifiScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
