/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "discover_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the summary row at the top, the device entries under it and the status
// line at the bottom, like the Wi-Fi page. The entries follow the theme's
// line heights.
constexpr int32_t kContentHeight  = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin     = 8;
constexpr int32_t kWidth          = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kSummaryTop     = 1;
constexpr int32_t kListGap        = 4;
constexpr int32_t kEntryGap       = 2;
constexpr int32_t kStatusHeight   = 18;
constexpr int32_t kStatusTop      = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap      = 8;
constexpr int32_t kCaptionGap     = 5;
constexpr int32_t kColumnGap      = 6;
constexpr int32_t kTrackX         = view::kScreenWidth - 6;
constexpr int32_t kThumbX         = kTrackX - 1;
constexpr int32_t kThumbWidth     = 3;
constexpr int32_t kThumbMinHeight = 8;
constexpr int32_t kEntryRight     = kThumbX - 4;

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

// "4:12": minutes and seconds.
std::string minutes_text(int32_t seconds) {
    seconds = std::max<int32_t>(seconds, 0);
    const int32_t rest = seconds % 60;
    return std::to_string(seconds / 60) + (rest < 10 ? ":0" : ":") + std::to_string(rest);
}

// How long ago a device was last heard: "now", "12 s ago", "3 min ago".
std::string age_text(int32_t seconds) {
    if (seconds < 2) {
        return "now";
    }
    if (seconds < 60) {
        return std::to_string(seconds) + " s ago";
    }
    return std::to_string(seconds / 60) + " min ago";
}

// A file's size: "512 B", "14 KB", "3.2 MB".
std::string size_text(uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        return std::to_string(bytes / 1024) + " KB";
    }
    const uint64_t tenths = bytes * 10 / (1024 * 1024);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

// The status line for the latest run (another tool running is handled by
// the caller). Saving the run as a file shows here too.
tools::ToneText status_text(const model::ListenRun& run, bool has_uplink) {
    const std::string left = minutes_text(model::kListenSeconds - run.elapsed_s) + " left";
    switch (run.save) {
        case model::CaptureSave::Saving:
            if (run.state == model::ToolState::Running) {
                return {"Saving, " + left + ", " + size_text(run.save_bytes), tools::kToneAccent};
            }
            return {"Saving…", tools::kToneAccent};
        case model::CaptureSave::Saved:
            return {"Saved " + run.save_name + (run.save_full ? ", full" : ""), tools::kToneSuccess};
        case model::CaptureSave::Failed:
            return {"Save failed: " + (run.save_reason.empty() ? std::string("error") : run.save_reason),
                    tools::kToneError};
        case model::CaptureSave::None:
            break;
    }
    switch (run.state) {
        case model::ToolState::Running:
            return {"Listening, " + left, tools::kToneAccent};
        case model::ToolState::Done:
            return {"Listened " + minutes_text(run.elapsed_s) + ", " + std::to_string(run.frames) + " frames",
                    tools::kToneMuted};
        case model::ToolState::Stopped:
            return {"Stopped after " + minutes_text(run.elapsed_s), tools::kToneMuted};
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                return {"Capture not allowed", tools::kToneError};
            }
            return {run.reason.empty() ? std::string("Listening failed") : capitalized(run.reason), tools::kToneError};
        case model::ToolState::Idle:
            break;
    }
    if (!has_uplink) {
        return {"Join a network first", tools::kToneWarning};
    }
    return {"Listening sends nothing", tools::kToneMuted};
}

// What stands in place of the list when it is empty.
std::string note_text(const model::ListenRun& run, const std::string& uplink) {
    switch (run.state) {
        case model::ToolState::Running:
            return "Listening for devices…";
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                return "The capture helper may not run. Use an account in the netdev group, or reinstall the "
                       "lokictl package.";
            }
            if (run.reason == "capture helper not installed") {
                return "The capture helper is missing. Reinstall the lokictl package.";
            }
            return "Nothing heard.";
        case model::ToolState::Done:
        case model::ToolState::Stopped:
            return "No devices heard.";
        case model::ToolState::Idle:
            break;
    }
    if (uplink.empty()) {
        return "Join a network first. Discover listens on the network the Cardputer is on.";
    }
    return "Press 6 to listen on " + uplink + ". Devices show as they are heard; nothing is sent.";
}

} // namespace

DiscoverScreen::DiscoverScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool DiscoverScreen::handles(model::AppPage page) {
    return page == model::AppPage::Discover;
}

view::widgets::HelpContent DiscoverScreen::help_content() {
    return {"DISCOVER",
            {
                {"ESC / 4", "Back (listening goes on)"},
                {"F / X", "Scroll the devices"},
                {"6", "Listen 5 min, sends none"},
                {"7", "Save as PCAP, 4 MB at most"},
                {"Accent", "This Cardputer's own ports"},
            }};
}

view::widgets::HelpContent DiscoverScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet DiscoverScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh() switches Start on and off and turns it into Stop.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Start", [this] { listen_or_stop(); }, true},
        {"Save", [this] { save(); }, false, false},
        {},
    }};
}

std::string DiscoverScreen::uplink() const {
    const std::string& name = view_model().home().uplink_name;
    return model::valid_interface_name(name) ? name : std::string();
}

bool DiscoverScreen::listening() const {
    return view_model().tools().listen.state == model::ToolState::Running;
}

bool DiscoverScreen::can_listen() const {
    if (listening()) {
        return true;
    }
    return !model::tool_running(view_model().tools()) && !uplink().empty();
}

void DiscoverScreen::listen_or_stop() {
    if (listening()) {
        view_model().request_tool_stop();
    } else if (can_listen()) {
        view_model().set_discover_scroll(0);
        view_model().request_tool_run(model::ToolKind::Listen, uplink());
    }
}

bool DiscoverScreen::can_save() const {
    const model::ToolsData& data = view_model().tools();
    const model::ListenRun& run = data.listen;
    if (run.save != model::CaptureSave::None) {
        return false;
    }
    if (run.state == model::ToolState::Running) {
        return true;
    }
    return run.state != model::ToolState::Idle && !model::tool_running(data) && run.frames > 0;
}

void DiscoverScreen::save() {
    if (can_save()) {
        view_model().request_capture_save();
    }
}

int32_t DiscoverScreen::first_entry() const {
    const auto count = static_cast<int32_t>(view_model().tools().listen.devices.size());
    const int32_t last_top = std::max<int32_t>(count - visible_entries_, 0);
    return std::clamp<int32_t>(view_model().discover_scroll(), 0, last_top);
}

void DiscoverScreen::scroll(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().tools().listen.devices.size());
    const int32_t last_top = std::max<int32_t>(count - visible_entries_, 0);
    view_model().set_discover_scroll(std::clamp<int32_t>(first_entry() + delta, 0, last_top));
}

bool DiscoverScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            scroll(-1);
            return true;
        case LV_KEY_DOWN:
            scroll(1);
            return true;
        default:
            return false;
    }
}

void DiscoverScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    // The summary row: the captions stand on the count's baseline.
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    interface_caption_ = plain_label(content, "");
    view::style::caption(interface_caption_, theme, "");
    lv_obj_set_pos(interface_caption_, kSideMargin, kSummaryTop + small_drop);
    count_ = plain_label(content, "");
    view::style::text(count_, theme, view::FontRole::Mono, theme.colors.accent2);
    step_caption_ = plain_label(content, "");
    view::style::caption(step_caption_, theme, "step 1 of 3: listen");
    lv_obj_align(step_caption_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kSummaryTop + small_drop);

    // Each entry is two lines in the small font; the entries end above the
    // status line, whatever the theme's line heights.
    const int32_t first_line = lv_font_get_line_height(small);
    const int32_t second_line = first_line;
    entry_height_ = first_line + second_line + kEntryGap;
    list_top_ = kSummaryTop + lv_font_get_line_height(mono) + kListGap;
    visible_entries_ = std::clamp<int32_t>((kStatusTop - 1 - list_top_ + kEntryGap) / entry_height_, 1,
                                           static_cast<int32_t>(kEntries));
    list_height_ = visible_entries_ * entry_height_ - kEntryGap;

    // Columns: the name ends in dots when longer; the address and the time
    // are right-aligned.
    const int32_t address_width = text_width("255.255.255.255", small);
    const int32_t address_x = kEntryRight - address_width;
    const int32_t name_width = std::max<int32_t>(address_x - kColumnGap - kSideMargin, 0);
    const int32_t age_width = text_width("59 min ago", small);
    const int32_t age_x = kEntryRight - age_width;
    // The kind takes the room between the hardware address and the time.
    const int32_t hardware_room = std::max<int32_t>(age_x - kColumnGap - kSideMargin, 0);
    const int32_t hardware_width = std::min<int32_t>(text_width("00:00:00:00:00:00", small), hardware_room);
    const int32_t kind_x = kSideMargin + hardware_width + kColumnGap;
    const int32_t kind_width = std::max<int32_t>(age_x - kColumnGap - kind_x, 0);

    for (int32_t index = 0; index < visible_entries_; ++index) {
        Entry& entry = entries_[static_cast<std::size_t>(index)];
        const int32_t top = list_top_ + index * entry_height_;
        entry.name = plain_label(content, "");
        view::style::text(entry.name, theme, view::FontRole::Small);
        lv_label_set_long_mode(entry.name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(entry.name, name_width, first_line);
        lv_obj_set_pos(entry.name, kSideMargin, top);
        entry.address = plain_label(content, "");
        view::style::text(entry.address, theme, view::FontRole::Small);
        lv_obj_set_size(entry.address, address_width, first_line);
        lv_obj_set_style_text_align(entry.address, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(entry.address, address_x, top);
        entry.hardware = plain_label(content, "");
        view::style::text(entry.hardware, theme, view::FontRole::Small, theme.colors.muted);
        lv_label_set_long_mode(entry.hardware, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(entry.hardware, hardware_width, second_line);
        lv_obj_set_pos(entry.hardware, kSideMargin, top + first_line);
        entry.kind = plain_label(content, "");
        view::style::text(entry.kind, theme, view::FontRole::Small);
        lv_label_set_long_mode(entry.kind, LV_LABEL_LONG_DOT);
        lv_obj_set_size(entry.kind, kind_width, second_line);
        lv_obj_set_pos(entry.kind, kind_x, top + first_line);
        entry.age = plain_label(content, "");
        view::style::text(entry.age, theme, view::FontRole::Small, theme.colors.muted);
        lv_obj_set_size(entry.age, age_width, second_line);
        lv_obj_set_style_text_align(entry.age, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(entry.age, age_x, top + first_line);
        entry.hidden = false;
        set_entry_hidden(entry, true);
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
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().discover_scroll_subject(), observer_cb, content, this);
    // The uplink comes and goes with the network.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
}

void DiscoverScreen::set_entry_hidden(Entry& entry, bool hidden) {
    if (entry.hidden == hidden) {
        return;
    }
    set_hidden(entry.name, hidden);
    set_hidden(entry.address, hidden);
    set_hidden(entry.hardware, hidden);
    set_hidden(entry.kind, hidden);
    set_hidden(entry.age, hidden);
    entry.hidden = hidden;
}

void DiscoverScreen::refresh_entries() {
    const auto& theme = this->theme();
    const model::ListenRun& run = view_model().tools().listen;
    const auto count = static_cast<int32_t>(run.devices.size());
    const int32_t first = first_entry();
    for (int32_t index = 0; index < visible_entries_; ++index) {
        Entry& entry = entries_[static_cast<std::size_t>(index)];
        if (first + index >= count) {
            set_entry_hidden(entry, true);
            continue;
        }
        const model::HeardDevice& device = run.devices[static_cast<std::size_t>(first + index)];
        set_entry_hidden(entry, false);
        std::string name = device.name;
        int32_t tone = tools::kToneText;
        if (!device.own_port.empty()) {
            name = "this device, " + device.own_port;
            tone = tools::kToneAccent;
        } else if (name.empty() && !device.address.empty() && device.address == view_model().home().gateway_address) {
            name = "router";
        } else if (name.empty()) {
            name = "unnamed";
            tone = tools::kToneMuted;
        }
        set_text_if_changed(entry.name, entry.name_shown, name);
        set_tone_if_changed(entry.name, entry.name_tone, tone, theme);
        const bool has_address = !device.address.empty();
        set_text_if_changed(entry.address, entry.address_shown,
                            has_address ? device.address : std::string("no address"));
        set_tone_if_changed(entry.address, entry.address_tone, has_address ? tools::kToneText : tools::kToneMuted,
                            theme);
        set_text_if_changed(entry.hardware, entry.hardware_shown, device.hardware_address);
        set_text_if_changed(entry.kind, entry.kind_shown, device.kind);
        set_text_if_changed(entry.age, entry.age_shown, age_text(run.elapsed_s - device.heard_s));
    }

    const bool empty = count == 0;
    set_hidden(note_, !empty);
    if (empty) {
        set_text_if_changed(note_, note_shown_, note_text(run, uplink()));
    }

    // The scroll mark, only when the devices do not all fit.
    const bool scrolls = count > visible_entries_;
    set_hidden(track_, !scrolls);
    set_hidden(thumb_, !scrolls);
    if (scrolls) {
        const int32_t thumb_height = std::max<int32_t>(list_height_ * visible_entries_ / count, kThumbMinHeight);
        const int32_t travel = list_height_ - thumb_height;
        const int32_t last_top = count - visible_entries_;
        const int32_t offset = last_top > 0 ? travel * first / last_top : 0;
        if (thumb_height != thumb_height_shown_ || offset != thumb_top_shown_) {
            lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
            lv_obj_set_pos(thumb_, kThumbX, list_top_ + offset);
            thumb_height_shown_ = thumb_height;
            thumb_top_shown_ = offset;
        }
    }
}

void DiscoverScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    tools::ToneText status = status_text(data.listen, !uplink().empty());
    if (!listening() && model::tool_running(data)) {
        if (data.ping.state == model::ToolState::Running) {
            status = {"Ping is running", tools::kToneMuted};
        } else if (data.dns.state == model::ToolState::Running) {
            status = {"DNS lookup is running", tools::kToneMuted};
        } else {
            status = {"Trace is running", tools::kToneMuted};
        }
    }
    const bool sent_changed = set_text_if_changed(sent_, sent_shown_, std::to_string(data.sent));
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

void DiscoverScreen::refresh() {
    const model::ListenRun& run = view_model().tools().listen;
    // The run's own interface once there was one, else the one a run
    // would use.
    std::string interface_name = run.state != model::ToolState::Idle ? run.interface_name : uplink();
    if (interface_name.empty()) {
        interface_name = "no network";
    }
    if (interface_name != interface_shown_) {
        interface_shown_ = interface_name;
        view::style::caption(interface_caption_, theme(), interface_name.c_str());
        lv_obj_update_layout(interface_caption_);
        lv_obj_set_pos(count_, kSideMargin + lv_obj_get_width(interface_caption_) + kCaptionGap, kSummaryTop);
    }
    const std::size_t heard = run.devices.size();
    set_text_if_changed(count_, count_shown_,
                        run.state == model::ToolState::Idle
                            ? std::string()
                            : std::to_string(heard) + (heard == 1 ? " device" : " devices"));
    refresh_entries();
    refresh_status();

    const int32_t key = listening() ? 2 : can_listen() ? 1 : 0;
    if (key != shown_key_) {
        shown_key_ = key;
        if (auto* bar = soft_key_bar()) {
            bar->set_key(2, {key == 2 ? "Stop" : "Start", [this] { listen_or_stop(); }, true, key != 0});
        }
    }
    const int32_t save_key = can_save() ? 1 : 0;
    if (save_key != shown_save_key_) {
        shown_save_key_ = save_key;
        if (auto* bar = soft_key_bar()) {
            bar->set_key(3, {"Save", [this] { save(); }, false, save_key != 0});
        }
    }
}

void DiscoverScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<DiscoverScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
