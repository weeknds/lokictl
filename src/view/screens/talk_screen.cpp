/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "talk_screen.h"

#include "home_layout.h"
#include "style.h"
#include "talk_data.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the heading at the top, one line per name under it and the status line
// at the bottom, like the Watch page; the scroll mark at the right edge,
// as on the Devices page.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kHeadingTop    = 1;
constexpr int32_t kListGap       = 3;
constexpr int32_t kStatusHeight  = 18;
constexpr int32_t kStatusTop     = kContentHeight - kStatusHeight - 1;
constexpr int32_t kColumnGap     = 6;
constexpr int32_t kTrackX        = view::kScreenWidth - 6;
constexpr int32_t kThumbX        = kTrackX - 1;
constexpr int32_t kThumbWidth    = 3;
constexpr int32_t kThumbMinHeight = 6;
// Room left for the scroll mark at the right of the lines.
constexpr int32_t kTrackRoom     = 6;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, "");
    return label;
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

int32_t text_width(const std::string& text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// `name` cut from the left, with ".." in front, until it fits `width` in
// `font`: the end of a name says whose site it is.
std::string fit_tail(const std::string& name, const lv_font_t* font, int32_t width) {
    if (text_width(name, font) <= width) {
        return name;
    }
    for (std::size_t keep = name.size(); keep > 0; --keep) {
        const std::string cut = model::talk_tail(name, keep);
        if (text_width(cut, font) <= width) {
            return cut;
        }
    }
    return {};
}

lv_color_t tone_color(const view::Theme& theme, int32_t tone) {
    switch (tone) {
        case tools::kToneMuted:
            return theme.colors.muted;
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

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

} // namespace

TalkScreen::TalkScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool TalkScreen::handles(model::AppPage page) {
    return page == model::AppPage::Talk;
}

view::widgets::HelpContent TalkScreen::help_content() {
    return {"TALK",
            {
                {"ESC / 4", "Back"},
                {"5 6 / Z C", "Previous / next device"},
                {"8 / R", "Save a report for a PC"},
                {"Only", "Your own hotspot"},
                {"Memory", "Gone when hotspot stops"},
            }};
}

view::widgets::HelpContent TalkScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet TalkScreen::soft_keys() {
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Prev", [this] { step_device(-1); }, false, false},
        {"Next", [this] { step_device(1); }, false, false},
        {},
        {"Save", [&view_model] { view_model.request_report(); }},
    }};
}

std::vector<std::string> TalkScreen::device_order() const {
    std::vector<std::string> order;
    for (const model::HomeClient& client : view_model().home().clients) {
        if (!client.hardware_address.empty()) {
            order.push_back(client.hardware_address);
        }
    }
    for (const model::TalkDevice& device : view_model().talk().devices) {
        if (std::find(order.begin(), order.end(), device.hardware) == order.end()) {
            order.push_back(device.hardware);
        }
    }
    return order;
}

std::string TalkScreen::shown_device() const {
    const std::vector<std::string> order = device_order();
    if (order.empty()) {
        return {};
    }
    const std::string& wanted = view_model().talk_device();
    return std::find(order.begin(), order.end(), wanted) != order.end() ? wanted : order.front();
}

void TalkScreen::step_device(int32_t delta) {
    const std::vector<std::string> order = device_order();
    if (order.size() < 2) {
        return;
    }
    const std::string current = shown_device();
    const auto count = static_cast<int32_t>(order.size());
    const auto at = static_cast<int32_t>(std::find(order.begin(), order.end(), current) - order.begin());
    const int32_t next = ((at + delta) % count + count) % count;
    view_model().set_talk_scroll(0);
    view_model().set_talk_device(order[static_cast<std::size_t>(next)]);
}

void TalkScreen::scroll(int32_t delta) {
    const model::TalkDevice* device = model::talk_device(view_model().talk(), shown_device());
    const auto total = device ? static_cast<int32_t>(device->names.size()) : 0;
    const int32_t last = std::max<int32_t>(total - visible_rows_, 0);
    const int32_t first = std::clamp<int32_t>(view_model().talk_scroll(), 0, last);
    view_model().set_talk_scroll(std::clamp<int32_t>(first + delta, 0, last));
}

bool TalkScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            scroll(-1);
            return true;
        case LV_KEY_DOWN:
            scroll(1);
            return true;
        case LV_KEY_LEFT:
            step_device(-1);
            return true;
        case LV_KEY_RIGHT:
            step_device(1);
            return true;
        case 'r':
        case 'R':
            view_model().request_report();
            return true;
        default:
            return false;
    }
}

void TalkScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    // The heading, and where the device stands at the right, on the
    // heading's baseline.
    const int32_t place_width = text_width("00 of 00", small) + 4;
    heading_ = plain_label(content);
    view::style::text(heading_, theme, view::FontRole::Strong);
    fix_width(heading_, kWidth - place_width - kColumnGap, strong);
    lv_obj_set_pos(heading_, kSideMargin, kHeadingTop);
    place_ = plain_label(content);
    view::style::text(place_, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(place_, place_width, small);
    lv_obj_set_style_text_align(place_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(place_, kSideMargin + kWidth - place_width,
                   kHeadingTop + std::max<int32_t>(baseline_from_top(strong) - baseline_from_top(small), 0));

    // One line per name; the lines end above the status line, whatever the
    // theme's line heights.
    row_height_ = lv_font_get_line_height(body) + 1;
    list_top_ = kHeadingTop + lv_font_get_line_height(strong) + kListGap;
    visible_rows_ =
        std::clamp<int32_t>((kStatusTop - 1 - list_top_) / row_height_, 1, static_cast<int32_t>(kRows));
    const int32_t count_width = text_width("x999", small);
    name_width_ = std::max<int32_t>(kWidth - kTrackRoom - count_width - kColumnGap, 0);
    const int32_t small_down = std::max<int32_t>(baseline_from_top(body) - baseline_from_top(small), 0);
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        const int32_t y = list_top_ + index * row_height_;
        row.name = plain_label(content);
        view::style::text(row.name, theme, view::FontRole::Body);
        fix_width(row.name, name_width_, body);
        lv_obj_set_pos(row.name, kSideMargin, y);
        row.count = plain_label(content);
        view::style::text(row.count, theme, view::FontRole::Small, theme.colors.accent2);
        fix_width(row.count, count_width, small);
        lv_obj_set_style_text_align(row.count, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.count, kSideMargin + kWidth - kTrackRoom - count_width, y + small_down);
        set_hidden(row.name, true);
        set_hidden(row.count, true);
        row.hidden = true;
    }

    track_ = plain_object(content);
    lv_obj_set_pos(track_, kTrackX, list_top_);
    lv_obj_set_size(track_, 1, visible_rows_ * row_height_);
    lv_obj_set_style_bg_color(track_, theme.colors.border_subtle, 0);
    lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
    thumb_ = plain_object(content);
    lv_obj_set_style_bg_color(thumb_, theme.colors.muted, 0);
    lv_obj_set_style_bg_opa(thumb_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb_, std::min<int32_t>(theme.metrics.radius, 1), 0);
    set_hidden(track_, true);
    set_hidden(thumb_, true);

    note_ = plain_label(content);
    view::style::text(note_, theme, view::FontRole::Mono, theme.colors.muted);
    fix_width(note_, kWidth, mono);
    lv_obj_set_pos(note_, kSideMargin, list_top_);
    set_hidden(note_, true);

    status_ = plain_label(content);
    view::style::text(status_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(status_, kWidth, kStatusHeight);
    lv_obj_set_pos(status_, kSideMargin, kStatusTop);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().talk_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().talk_device_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().talk_scroll_subject(), observer_cb, content, this);
    // The hotspot and its devices come and go with the network.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
}

void TalkScreen::refresh() {
    const auto& theme = this->theme();
    const model::HomeData& home = view_model().home();
    const model::TalkRun& run = view_model().talk();
    const std::vector<std::string> order = device_order();
    const std::string hardware = shown_device();
    const model::TalkDevice* device = model::talk_device(run, hardware);

    // The heading: the device's name, else its address, else its hardware
    // address.
    std::string who;
    for (const model::HomeClient& client : home.clients) {
        if (client.hardware_address == hardware) {
            who = !client.name.empty() ? client.name : client.address;
            break;
        }
    }
    if (who.empty() && device) {
        who = device->address;
    }
    if (who.empty()) {
        who = hardware;
    }
    set_text_if_changed(heading_, heading_shown_, who.empty() ? std::string("Names looked up") : "Looked up by " + who);
    std::string place;
    if (order.size() > 1) {
        const auto at = static_cast<std::size_t>(std::find(order.begin(), order.end(), hardware) - order.begin());
        place = std::to_string(at + 1) + " of " + std::to_string(order.size());
    }
    set_text_if_changed(place_, place_shown_, place);

    // The names, from the first one scrolled to.
    const auto total = device ? static_cast<int32_t>(device->names.size()) : 0;
    const int32_t first = std::clamp<int32_t>(view_model().talk_scroll(), 0, std::max<int32_t>(total - visible_rows_, 0));
    const lv_font_t* body = theme.font(view::FontRole::Body);
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        const int32_t place_in_list = first + index;
        const bool hidden = place_in_list >= total;
        if (hidden != row.hidden) {
            set_hidden(row.name, hidden);
            set_hidden(row.count, hidden);
            row.hidden = hidden;
        }
        if (hidden) {
            continue;
        }
        const model::TalkName& name = device->names[static_cast<std::size_t>(place_in_list)];
        if (name.name != row.name_shown) {
            row.name_shown = name.name;
            lv_label_set_text(row.name, fit_tail(name.name, body, name_width_).c_str());
        }
        set_text_if_changed(row.count, row.count_shown,
                            name.count > 1 ? "x" + std::to_string(std::min<int32_t>(name.count, 999)) : std::string());
    }

    // The scroll mark, only when there are more names than lines.
    if (total > visible_rows_) {
        const int32_t list_height = visible_rows_ * row_height_;
        const int32_t thumb_height = std::max<int32_t>(kThumbMinHeight, list_height * visible_rows_ / total);
        const int32_t thumb_y = list_top_ + (list_height - thumb_height) * first / (total - visible_rows_);
        lv_obj_set_pos(thumb_, kThumbX, thumb_y);
        lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
        set_hidden(track_, false);
        set_hidden(thumb_, false);
    } else {
        set_hidden(track_, true);
        set_hidden(thumb_, true);
    }

    std::string note;
    if (total == 0) {
        if (!home.ap_active) {
            note = "Hotspot is off";
        } else if (hardware.empty()) {
            note = "No devices on the hotspot";
        } else {
            note = "No names looked up yet";
        }
    }
    if (set_text_if_changed(note_, note_shown_, note)) {
        set_hidden(note_, note.empty());
    }

    tools::ToneText status;
    if (!home.ap_active) {
        status = {"Turn on Router or Share first", tools::kToneWarning};
    } else {
        switch (run.state) {
            case model::ToolState::Failed:
                if (run.error == model::ToolError::NotAllowed) {
                    status = {"Capture not allowed", tools::kToneError};
                } else {
                    status = {capitalized(run.reason.empty() ? std::string("capture failed") : run.reason),
                              tools::kToneError};
                }
                break;
            case model::ToolState::Running:
            case model::ToolState::Idle:
            case model::ToolState::Done:
            case model::ToolState::Stopped:
                status = {"Hotspot only, sends nothing", tools::kToneMuted};
                break;
        }
    }
    set_text_if_changed(status_, status_shown_, status.text);
    if (status.tone != status_tone_) {
        lv_obj_set_style_text_color(status_, tone_color(theme, status.tone), 0);
        status_tone_ = status.tone;
    }

    refresh_keys(order.size());
}

void TalkScreen::refresh_keys(std::size_t devices) {
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    const int32_t step = devices > 1 ? 1 : 0;
    if (step != step_keys_shown_) {
        step_keys_shown_ = step;
        bar->set_key(1, {"Prev", [this] { step_device(-1); }, false, step != 0});
        bar->set_key(2, {"Next", [this] { step_device(1); }, false, step != 0});
    }
}

void TalkScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<TalkScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
