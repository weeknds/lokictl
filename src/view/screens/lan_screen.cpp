/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "lan_screen.h"

#include "bar_graph.h"
#include "home_format.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar,
// 304 px wide. Test side: the target row, the two columns, the status
// line. Server side: the caption row, the address, the result rows, the
// status line.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin   = 8;
constexpr int32_t kWidth        = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kRight        = view::kScreenWidth - kSideMargin;
constexpr int32_t kTopRow       = 1;
constexpr int32_t kColumnGap    = 16;
constexpr int32_t kColumnWidth  = (kWidth - kColumnGap) / 2;
constexpr int32_t kRowGap       = 2;
constexpr int32_t kUnitGap      = 4;
constexpr int32_t kStatusHeight = 18;
constexpr int32_t kStatusTop    = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap    = 8;
constexpr int32_t kCaptionGap   = 5;
constexpr int32_t kBarGap       = 1;
constexpr auto kBars            = static_cast<int32_t>(model::kLanSamples);
constexpr int32_t kBarWidth     = (kColumnWidth + kBarGap) / kBars - kBarGap;
// A caption longer than this would leave no room for the address.
constexpr std::size_t kCaptionMax = 12;

static_assert(kBarWidth >= 2, "the bars must stay visible");

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

// What the status line says while another tool runs.
const char* other_tool_text(const model::ToolsData& data) {
    if (data.ping.state == model::ToolState::Running) {
        return "Ping is running";
    }
    if (data.dns.state == model::ToolState::Running) {
        return "DNS lookup is running";
    }
    if (data.trace.state == model::ToolState::Running) {
        return "Trace is running";
    }
    if (data.listen.state == model::ToolState::Running) {
        return "Discover is listening";
    }
    if (data.probe.state == model::ToolState::Running) {
        return "Discover is probing";
    }
    if (data.speed.state == model::ToolState::Running) {
        return "Speed test is running";
    }
    if (data.health.state == model::ToolState::Running) {
        return "Health check is running";
    }
    if (data.leak.state == model::ToolState::Running) {
        return "Leak test is running";
    }
    if (data.signin.state == model::ToolState::Running) {
        return "Sign-in check is running";
    }
    return "Port scan is running";
}

std::string mbits_text(int32_t kbps) {
    return home::rate_text(kbps) + " Mbit/s";
}

// One line of the server's list: "From 192.168.1.20: 94.1 Mbit/s".
std::string served_line(const std::string& peer, bool to_peer, int32_t kbps) {
    const std::string who = peer.empty() ? std::string("a device") : peer;
    return (to_peer ? "To " : "From ") + who + ": " + mbits_text(kbps);
}

bool is_digit_or_dot(uint32_t key) {
    return (key >= '0' && key <= '9') || key == '.';
}

} // namespace

LanScreen::LanScreen(viewmodel::BaseViewModel& view_model,
                     app::AssetManager& assets,
                     view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool LanScreen::handles(model::AppPage page) {
    return page == model::AppPage::Lan;
}

view::widgets::HelpContent LanScreen::help_content(bool server) {
    if (server) {
        return {"LAN",
                {
                    {"ESC / 4", "Back (turns it off)"},
                    {"Z / C", "Network or hotspot"},
                    {"ENTER / 6", "Listen / stop"},
                    {"7", "Test another device"},
                    {"App", "Any iperf3 app works"},
                }};
    }
    return {"LAN",
            {
                {"ESC / 4", "Back (a test goes on)"},
                {"Z / C", "Pick a device; 5 types"},
                {"ENTER / 6", "Test 10 s each way"},
                {"7", "Be the server instead"},
                {"Other end", "Needs iperf3 -s"},
            }};
}

view::widgets::HelpContent LanScreen::help() const {
    return help_content(server_side());
}

view::widgets::SoftKeySet LanScreen::soft_keys() {
    // refresh_keys() sets them for the side and the run.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Run", [this] { main_key(); }, true},
        {},
        {},
    }};
}

bool LanScreen::server_side() const {
    return view_model().lan_server_side();
}

bool LanScreen::running() const {
    return view_model().lan().state == model::ToolState::Running;
}

bool LanScreen::other_running() const {
    return model::tool_running(view_model().tools());
}

std::vector<LanScreen::Target> LanScreen::targets() const {
    const model::HomeData& home = view_model().home();
    const model::LanRun& run = view_model().lan();
    std::vector<Target> list;
    const auto add = [&](const std::string& address, std::string caption) {
        if (!model::valid_lan_target(address) || address == home.uplink_address) {
            return;
        }
        for (const model::LanPlace& place : run.places) {
            if (place.address == address) {
                return;  // the Cardputer itself
            }
        }
        for (const Target& known : list) {
            if (known.address == address) {
                return;
            }
        }
        if (caption.size() > kCaptionMax) {
            caption = caption.substr(0, kCaptionMax - 1) + "…";
        }
        list.push_back({address, caption});
    };
    add(home.gateway_address, "router");
    const model::ToolsData& data = view_model().tools();
    for (const model::DiscoverDevice& entry : model::discover_devices(data.listen, data.probe)) {
        const model::HeardDevice& device = entry.device;
        if (!device.own_port.empty()) {
            continue;
        }
        add(device.address, !device.name.empty() ? device.name : !device.kind.empty() ? device.kind : "device");
    }
    for (const model::HomeClient& client : home.clients) {
        add(client.address, client.name.empty() ? std::string("device") : client.name);
    }
    const std::string& typed = view_model().lan_typed_target();
    if (!typed.empty()) {
        // Always last, even when the list has it already.
        list.erase(std::remove_if(list.begin(), list.end(), [&typed](const Target& t) { return t.address == typed; }),
                   list.end());
        list.push_back({typed, "typed"});
    }
    return list;
}

int32_t LanScreen::target_index(const std::vector<Target>& list) const {
    return std::clamp<int32_t>(view_model().lan_target(), 0, std::max<int32_t>(static_cast<int32_t>(list.size()) - 1, 0));
}

int32_t LanScreen::place_index() const {
    const auto count = static_cast<int32_t>(view_model().lan().places.size());
    return std::clamp<int32_t>(view_model().lan_place(), 0, std::max<int32_t>(count - 1, 0));
}

void LanScreen::step(int32_t delta) {
    if (running()) {
        return;
    }
    if (server_side()) {
        const auto count = static_cast<int32_t>(view_model().lan().places.size());
        if (count > 1) {
            view_model().set_lan_place((place_index() + delta + count) % count);
        }
        return;
    }
    const std::vector<Target> list = targets();
    const auto count = static_cast<int32_t>(list.size());
    if (count > 1) {
        view_model().set_lan_target((target_index(list) + delta + count) % count);
    }
}

void LanScreen::main_key() {
    if (typing_) {
        return;
    }
    const model::LanRun& run = view_model().lan();
    const bool mine = run.mode == (server_side() ? model::LanMode::Server : model::LanMode::Client);
    if (running()) {
        if (mine) {
            view_model().request_lan(viewmodel::BaseViewModel::LanRequest::Stop);
        }
        return;
    }
    if (other_running()) {
        return;
    }
    if (server_side()) {
        if (run.places.empty()) {
            return;
        }
        view_model().request_lan(viewmodel::BaseViewModel::LanRequest::Serve,
                                 run.places[static_cast<std::size_t>(place_index())].address);
        return;
    }
    const std::vector<Target> list = targets();
    if (list.empty()) {
        return;
    }
    view_model().request_lan(viewmodel::BaseViewModel::LanRequest::Test,
                             list[static_cast<std::size_t>(target_index(list))].address);
}

void LanScreen::switch_side() {
    if (typing_ || running()) {
        return;
    }
    view_model().set_lan_server_side(!server_side());
}

void LanScreen::start_edit() {
    if (typing_ || running() || server_side()) {
        return;
    }
    typing_ = true;
    draft_invalid_ = false;
    draft_ = view_model().lan_typed_target();
    refresh();
}

// While typing every key is taken: digits and dots, Backspace, Enter.
bool LanScreen::type_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        if (draft_.empty()) {
            // Drops the typed address.
            typing_ = false;
            if (!view_model().lan_typed_target().empty()) {
                view_model().set_lan_typed_target("");
                view_model().set_lan_target(0);
            }
            refresh();
            return true;
        }
        if (!model::valid_lan_target(draft_)) {
            draft_invalid_ = true;
            refresh();
            return true;
        }
        typing_ = false;
        view_model().set_lan_typed_target(draft_);
        // The typed address is the last one.
        view_model().set_lan_target(viewmodel::BaseViewModel::kToolTargetTyped);
        refresh();
        return true;
    }
    if (key == LV_KEY_BACKSPACE) {
        if (!draft_.empty()) {
            draft_.pop_back();
        }
    } else if (is_digit_or_dot(key) && draft_.size() < 15) {
        draft_ += static_cast<char>(key);
    }
    draft_invalid_ = false;
    refresh();
    return true;
}

bool LanScreen::on_key(uint32_t key) {
    if (typing_) {
        return type_key(key);
    }
    switch (key) {
        case LV_KEY_LEFT:
            step(-1);
            return true;
        case LV_KEY_RIGHT:
            step(1);
            return true;
        case LV_KEY_ENTER:
            main_key();
            return true;
        default:
            return false;
    }
}

bool LanScreen::on_back() {
    if (typing_) {
        typing_ = false;
        draft_invalid_ = false;
        refresh();
        return true;
    }
    return false;
}

bool LanScreen::text_entry() const {
    return typing_;
}

void LanScreen::build_column(lv_obj_t* parent,
                             Column& column,
                             const char* caption,
                             int32_t left,
                             int32_t top,
                             lv_color_t bar_color) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* title = theme.font(view::FontRole::Title);

    column.caption = plain_label(parent, "");
    view::style::caption(column.caption, theme, caption);
    lv_obj_set_pos(column.caption, left, top);
    column.bytes = plain_label(parent, "");
    view::style::text(column.bytes, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(column.bytes, LV_ALIGN_TOP_RIGHT, -(view::kScreenWidth - left - kColumnWidth), top);

    const int32_t speed_top = top + lv_font_get_line_height(small);
    column.speed = plain_label(parent, "");
    view::style::text(column.speed, theme, view::FontRole::Title, theme.colors.text);
    lv_obj_set_pos(column.speed, left, speed_top);
    column.unit = plain_label(parent, "Mbit/s");
    view::style::text(column.unit, theme, view::FontRole::Small, theme.colors.muted);

    const int32_t graph_top = speed_top + lv_font_get_line_height(title) + kRowGap;
    const int32_t graph_height = std::max<int32_t>(kStatusTop - kRowGap - 2 - graph_top, 8);
    column.graph = view::widgets::build_bar_graph(parent, column.bars_shown.data(), column.bars_shown.size(),
                                                  kBarWidth, kBarGap, graph_height, bar_color);
    lv_obj_set_pos(column.graph, left, graph_top);
}

void LanScreen::build_server(lv_obj_t* parent) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* title = theme.font(view::FontRole::Title);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    app_caption_ = plain_label(parent, "");
    view::style::caption(app_caption_, theme, "type this in the app");
    lv_obj_set_pos(app_caption_, kSideMargin, kTopRow);
    where_ = plain_label(parent, "");
    view::style::text(where_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(where_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kTopRow);

    const int32_t address_top = kTopRow + lv_font_get_line_height(small);
    address_ = plain_label(parent, "");
    view::style::text(address_, theme, view::FontRole::Title, theme.colors.accent2);
    lv_obj_set_pos(address_, kSideMargin, address_top);
    port_ = plain_label(parent, ("port " + std::to_string(model::kLanPort)).c_str());
    view::style::text(port_, theme, view::FontRole::Small, theme.colors.muted);

    // As many result rows as fit above the status line.
    const int32_t rows_top = address_top + lv_font_get_line_height(title) + kRowGap;
    const int32_t pitch = lv_font_get_line_height(mono);
    row_count_ = std::min<std::size_t>(kRowMax, static_cast<std::size_t>(std::max<int32_t>(
                                                    (kStatusTop - rows_top) / std::max<int32_t>(pitch, 1), 0)));
    for (std::size_t index = 0; index < row_count_; ++index) {
        rows_[index] = plain_label(parent, "");
        view::style::text(rows_[index], theme, view::FontRole::Mono, theme.colors.text);
        lv_label_set_long_mode(rows_[index], LV_LABEL_LONG_DOT);
        lv_obj_set_width(rows_[index], kWidth);
        lv_obj_set_pos(rows_[index], kSideMargin, rows_top + static_cast<int32_t>(index) * pitch);
    }
}

void LanScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    // The test side.
    test_panel_ = plain_object(content);
    lv_obj_set_size(test_panel_, view::kScreenWidth, kContentHeight);
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    target_caption_ = plain_label(test_panel_, "");
    view::style::caption(target_caption_, theme, "");
    lv_obj_set_pos(target_caption_, kSideMargin, kTopRow + small_drop);
    target_value_ = plain_label(test_panel_, "");
    view::style::text(target_value_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_label_set_long_mode(target_value_, LV_LABEL_LONG_DOT);
    target_place_ = plain_label(test_panel_, "");
    view::style::text(target_place_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(target_place_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kTopRow + small_drop);
    const int32_t columns_top = kTopRow + lv_font_get_line_height(mono) + kRowGap;
    build_column(test_panel_, down_, "down", kSideMargin, columns_top, theme.colors.accent);
    build_column(test_panel_, up_, "up", kSideMargin + kColumnWidth + kColumnGap, columns_top, theme.colors.accent2);

    // The server side.
    server_panel_ = plain_object(content);
    lv_obj_set_size(server_panel_, view::kScreenWidth, kContentHeight);
    build_server(server_panel_);

    // The status line of both, with the Sent count at its right.
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
    lv_subject_add_observer_obj(view_model().lan_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().lan_choice_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
}

void LanScreen::refresh_keys() {
    const bool server = server_side();
    const model::LanRun& run = view_model().lan();
    const bool mine = run.mode == (server ? model::LanMode::Server : model::LanMode::Client);
    // Bits: the side, this side runs, the other side runs, another tool
    // runs, typing.
    const int32_t keys = (server ? 1 : 0) | (running() && mine ? 2 : 0) | (running() && !mine ? 4 : 0) |
                         (other_running() ? 8 : 0) | (typing_ ? 16 : 0);
    if (keys == keys_shown_) {
        return;
    }
    keys_shown_ = keys;
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    const bool this_runs = (keys & 2) != 0;
    const bool can_start = !running() && !other_running() && !typing_;
    if (server) {
        bar->set_key(1, {});
        bar->set_key(2, {this_runs ? "Stop" : "Start", [this] { main_key(); }, true, this_runs || can_start});
        bar->set_key(3, {"Test", [this] { switch_side(); }, false, !running() && !typing_});
    } else {
        bar->set_key(1, {"Edit", [this] { start_edit(); }, false, !running() && !typing_});
        bar->set_key(2, {this_runs ? "Stop" : "Run", [this] { main_key(); }, true, this_runs || can_start});
        bar->set_key(3, {"Serve", [this] { switch_side(); }, false, !running() && !typing_});
    }
}

void LanScreen::refresh_target() {
    std::string caption = "to";
    std::string value;
    std::string place;
    if (typing_) {
        caption = "type";
        value = draft_ + "_";
    } else {
        const model::LanRun& run = view_model().lan();
        if (run.mode == model::LanMode::Client && run.state != model::ToolState::Idle) {
            value = run.target;  // the run's own target, while it runs and after
        }
        const std::vector<Target> list = targets();
        if (!list.empty()) {
            const int32_t index = target_index(list);
            const Target& target = list[static_cast<std::size_t>(index)];
            if (value.empty() || (!running() && value != target.address)) {
                value = target.address;
            }
            for (const Target& known : list) {
                if (known.address == value) {
                    caption = known.caption;
                }
            }
            place = "‹ " + std::to_string(index + 1) + "/" + std::to_string(list.size()) + " ›";
        } else if (value.empty()) {
            value = "--";
        }
    }
    const bool caption_changed = caption != caption_shown_;
    if (caption_changed) {
        caption_shown_ = caption;
        view::style::caption(target_caption_, theme(), caption.c_str());
    }
    const bool place_changed = set_text_if_changed(target_place_, place_shown_, place);
    set_text_if_changed(target_value_, target_shown_, value);
    if (caption_changed || place_changed) {
        // The address gets the room between the caption and the place.
        lv_obj_update_layout(target_caption_);
        lv_obj_update_layout(target_place_);
        const int32_t left = kSideMargin + lv_obj_get_width(target_caption_) + kCaptionGap;
        lv_obj_set_pos(target_value_, left, kTopRow);
        const int32_t room = kRight - left - lv_obj_get_width(target_place_) - kCaptionGap;
        fix_width(target_value_, std::max<int32_t>(room, 0), theme().font(view::FontRole::Mono));
    }
}

void LanScreen::refresh_column(Column& column,
                               int32_t kbps,
                               uint64_t bytes,
                               const std::vector<int32_t>& samples,
                               int32_t scale) {
    const auto& theme = this->theme();
    if (set_text_if_changed(column.speed, column.speed_shown, home::rate_text(kbps))) {
        lv_obj_update_layout(column.speed);
        lv_obj_align_to(column.unit, column.speed, LV_ALIGN_OUT_RIGHT_BOTTOM, kUnitGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Title)->base_line);
    }
    set_text_if_changed(column.bytes, column.bytes_shown,
                        kbps < 0 ? std::to_string(model::kLanPhaseSeconds) + " s" : tools::bytes_text(bytes));
    std::array<uint8_t, model::kLanSamples> bars{};
    for (std::size_t index = 0; index < bars.size() && index < samples.size(); ++index) {
        bars[index] = static_cast<uint8_t>(
            scale > 0 ? std::clamp<int64_t>(static_cast<int64_t>(samples[index]) * 100 / scale, 0, 100) : 0);
    }
    if (bars != column.bars_shown) {
        column.bars_shown = bars;
        view::widgets::update_bar_graph(column.graph, bars.data(), bars.size());
    }
}

void LanScreen::refresh_server() {
    const auto& theme = this->theme();
    const model::LanRun& run = view_model().lan();
    const std::size_t count = run.places.size();
    std::string where;
    std::string address = "--";
    if (count > 0) {
        const auto index = static_cast<std::size_t>(place_index());
        const model::LanPlace& place = run.places[index];
        address = place.address;
        where = place.label;
        if (count > 1) {
            where = "‹ " + where + " " + std::to_string(index + 1) + "/" + std::to_string(count) + " ›";
        }
    }
    // A running server shows where it listens.
    if (run.mode == model::LanMode::Server && run.state == model::ToolState::Running) {
        address = run.target;
    }
    set_text_if_changed(where_, where_shown_, where);
    if (set_text_if_changed(address_, address_shown_, address)) {
        lv_obj_update_layout(address_);
        lv_obj_align_to(port_, address_, LV_ALIGN_OUT_RIGHT_BOTTOM, kUnitGap * 2,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Title)->base_line);
    }

    // The latest tests, newest first; a two-way test takes two rows.
    std::vector<tools::ToneText> lines;
    if (run.mode == model::LanMode::Server) {
        for (const model::LanServed& test : run.served) {
            const int32_t tone = test.done ? tools::kToneText : tools::kToneAccent;
            if (test.both) {
                lines.push_back({served_line(test.peer, false, test.kbps), tone});
                lines.push_back({served_line(test.peer, true, test.back_kbps), tone});
            } else {
                lines.push_back({served_line(test.peer, test.to_peer, test.kbps), tone});
            }
        }
    }
    if (lines.empty()) {
        lines.push_back({"Works with any iperf3 app", tools::kToneMuted});
    }
    for (std::size_t index = 0; index < row_count_; ++index) {
        const tools::ToneText line = index < lines.size() ? lines[index] : tools::ToneText{"", tools::kToneText};
        set_text_if_changed(rows_[index], rows_shown_[index], line.text);
        if (line.tone != row_tones_[index]) {
            row_tones_[index] = line.tone;
            lv_obj_set_style_text_color(rows_[index], tone_color(theme, line.tone), 0);
        }
    }
}

void LanScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::LanRun& run = view_model().lan();
    const bool server = server_side();
    const bool mine = run.mode == (server ? model::LanMode::Server : model::LanMode::Client);
    const model::ToolState state = mine ? run.state : model::ToolState::Idle;
    tools::ToneText status{"", tools::kToneMuted};
    if (typing_) {
        status = draft_invalid_ ? tools::ToneText{"Use a local address", tools::kToneError}
                                : tools::ToneText{"Type an address, ENTER keeps", tools::kToneAccent};
    } else if (state != model::ToolState::Running && other_running()) {
        status = {other_tool_text(view_model().tools()), tools::kToneMuted};
    } else if (state != model::ToolState::Running && running()) {
        status = {server ? "A test is running" : "The server is on", tools::kToneMuted};
    } else if (state == model::ToolState::Failed) {
        status = {run.reason.empty() ? std::string("The test failed") : run.reason, tools::kToneError};
    } else if (server) {
        if (state == model::ToolState::Running) {
            if (run.phase == model::LanPhase::Testing) {
                const std::string peer = run.served.empty() ? std::string() : run.served.front().peer;
                status = {peer.empty() ? std::string("Testing now") : "Testing with " + peer, tools::kToneAccent};
            } else {
                status = {"Waiting for a phone or PC", tools::kToneAccent};
            }
        } else if (run.places.empty()) {
            status = {"Join a network first", tools::kToneWarning};
        } else {
            status = {"Off, press 6 to listen", tools::kToneMuted};
        }
    } else {
        const std::string seconds =
            std::to_string(run.phase_ms / 1000) + " of " + std::to_string(model::kLanPhaseSeconds) + " s";
        switch (state) {
            case model::ToolState::Idle:
                status = targets().empty() ? tools::ToneText{"Type an address with 5", tools::kToneMuted}
                                           : tools::ToneText{"Tests 10 s each way", tools::kToneMuted};
                break;
            case model::ToolState::Running:
                status = run.phase == model::LanPhase::Download ? tools::ToneText{"Download " + seconds, tools::kToneAccent}
                         : run.phase == model::LanPhase::Upload ? tools::ToneText{"Upload " + seconds, tools::kToneAccent}
                                                                : tools::ToneText{"Connecting", tools::kToneAccent};
                break;
            case model::ToolState::Done:
                status = {"Done", tools::kToneSuccess};
                break;
            case model::ToolState::Stopped:
                status = {"Stopped", tools::kToneMuted};
                break;
            case model::ToolState::Failed:
                break;
        }
    }
    const bool sent_changed = set_text_if_changed(sent_, sent_shown_, std::to_string(view_model().tools().sent));
    if (set_text_if_changed(status_, status_shown_, status.text) || sent_changed) {
        lv_obj_update_layout(sent_);
        lv_obj_align_to(sent_caption_, sent_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Mono)->base_line);
        lv_obj_update_layout(sent_caption_);
        const int32_t width =
            kWidth - lv_obj_get_width(sent_) - kCaptionGap - lv_obj_get_width(sent_caption_) - kStatusGap;
        lv_obj_set_size(status_, std::max<int32_t>(width, 0), kStatusHeight);
    }
    if (status.tone != status_tone_) {
        lv_obj_set_style_text_color(status_, tone_color(theme, status.tone), 0);
        status_tone_ = status.tone;
    }
}

void LanScreen::refresh() {
    const bool server = server_side();
    if (static_cast<int32_t>(server) != side_shown_) {
        side_shown_ = static_cast<int32_t>(server);
        if (server) {
            lv_obj_add_flag(test_panel_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(server_panel_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(server_panel_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(test_panel_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    refresh_keys();
    if (server) {
        refresh_server();
    } else {
        refresh_target();
        const model::LanRun& run = view_model().lan();
        static const model::LanRun kNone;
        const model::LanRun& shown = run.mode == model::LanMode::Client ? run : kNone;
        int32_t scale = 0;
        for (const int32_t sample : shown.down_samples) {
            scale = std::max(scale, sample);
        }
        for (const int32_t sample : shown.up_samples) {
            scale = std::max(scale, sample);
        }
        refresh_column(down_, shown.down_kbps, shown.down_bytes, shown.down_samples, scale);
        refresh_column(up_, shown.up_kbps, shown.up_bytes, shown.up_samples, scale);
    }
    refresh_status();
}

void LanScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<LanScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
