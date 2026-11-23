/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "speed_screen.h"

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

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the server row at the top, the two columns under it and the status line
// at the bottom, all 304 px wide. Each column has a caption line (DOWN or
// UP and the bytes), the speed and the bar graph.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin   = 8;
constexpr int32_t kWidth        = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kRight        = view::kScreenWidth - kSideMargin;
constexpr int32_t kServerTop    = 1;
constexpr int32_t kColumnGap    = 16;
constexpr int32_t kColumnWidth  = (kWidth - kColumnGap) / 2;
constexpr int32_t kRowGap       = 2;
constexpr int32_t kUnitGap      = 4;
constexpr int32_t kStatusHeight = 18;
constexpr int32_t kStatusTop    = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap    = 8;
constexpr int32_t kCaptionGap   = 5;
// One bar per sample; the graph is as wide as a column allows.
constexpr int32_t kBarGap       = 1;
constexpr auto kBars            = static_cast<int32_t>(model::kSpeedSamples);
constexpr int32_t kBarWidth     = (kColumnWidth + kBarGap) / kBars - kBarGap;

static_assert(kBarWidth >= 2, "the bars must stay visible");

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
    if (data.health.state == model::ToolState::Running) {
        return "Health check is running";
    }
    if (data.leak.state == model::ToolState::Running) {
        return "Leak test is running";
    }
    return "Port scan is running";
}

} // namespace

SpeedScreen::SpeedScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool SpeedScreen::handles(model::AppPage page) {
    return page == model::AppPage::Speed;
}

view::widgets::HelpContent SpeedScreen::help_content() {
    return {"SPEED",
            {
                {"ESC / 4", "Back (a run goes on)"},
                {"ENTER / 6", "Run / stop"},
                {"Data", "Up to " + std::to_string(model::kSpeedPhaseMs / 1000) + " s, " +
                             std::to_string(model::kSpeedPhaseBytesMax / 1000000) + " MB each way"},
                {"Sent", "One per request here"},
            }};
}

view::widgets::HelpContent SpeedScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet SpeedScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh_keys() sets them for the run.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Run", [this] { run_or_stop(); }, true},
        {},
        {},
    }};
}

bool SpeedScreen::this_running() const {
    return view_model().tools().speed.state == model::ToolState::Running;
}

bool SpeedScreen::any_running() const {
    return model::tool_running(view_model().tools());
}

void SpeedScreen::run_or_stop() {
    if (this_running()) {
        view_model().request_tool_stop();
        return;
    }
    if (any_running()) {
        return;  // another tool runs
    }
    view_model().request_tool_run(model::ToolKind::Speed, view_model().speed_server());
}

bool SpeedScreen::on_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        run_or_stop();
        return true;
    }
    return false;
}

void SpeedScreen::build_column(lv_obj_t* parent,
                               Column& column,
                               const char* caption,
                               int32_t left,
                               int32_t top,
                               lv_color_t bar_color) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* title = theme.font(view::FontRole::Title);

    // The caption line: DOWN or UP at the left, the bytes at the right.
    column.caption = plain_label(parent, "");
    view::style::caption(column.caption, theme, caption);
    lv_obj_set_pos(column.caption, left, top);
    column.bytes = plain_label(parent, "");
    view::style::text(column.bytes, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(column.bytes, LV_ALIGN_TOP_RIGHT, -(view::kScreenWidth - left - kColumnWidth), top);

    // The speed, with its unit on the same baseline.
    const int32_t speed_top = top + lv_font_get_line_height(small);
    column.speed = plain_label(parent, "");
    view::style::text(column.speed, theme, view::FontRole::Title, theme.colors.text);
    lv_obj_set_pos(column.speed, left, speed_top);
    column.unit = plain_label(parent, "Mbit/s");
    view::style::text(column.unit, theme, view::FontRole::Small, theme.colors.muted);

    // The graph fills the rest of the column above the status line.
    const int32_t graph_top = speed_top + lv_font_get_line_height(title) + kRowGap;
    const int32_t graph_height = std::max<int32_t>(kStatusTop - kRowGap - 2 - graph_top, 8);
    column.graph = view::widgets::build_bar_graph(parent, column.bars_shown.data(), column.bars_shown.size(),
                                                  kBarWidth, kBarGap, graph_height, bar_color);
    lv_obj_set_pos(column.graph, left, graph_top);
}

void SpeedScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    // The server row: the caption stands on the host's baseline.
    server_caption_ = plain_label(content, "");
    view::style::caption(server_caption_, theme, "server");
    lv_obj_set_pos(server_caption_, kSideMargin, kServerTop + baseline_from_top(mono) - baseline_from_top(small));
    lv_obj_update_layout(server_caption_);
    const int32_t server_left = kSideMargin + lv_obj_get_width(server_caption_) + kCaptionGap;
    server_ = plain_label(content, "");
    view::style::text(server_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_obj_set_pos(server_, server_left, kServerTop);
    fix_width(server_, kRight - server_left, mono);

    const int32_t columns_top = kServerTop + lv_font_get_line_height(mono) + kRowGap;
    build_column(content, down_, "down", kSideMargin, columns_top, theme.colors.accent);
    build_column(content, up_, "up", kSideMargin + kColumnWidth + kColumnGap, columns_top, theme.colors.accent2);

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

    // LVGL calls the observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    // Last, so a start the command bar asked for finds the page built.
    lv_subject_add_observer_obj(view_model().tool_start_subject(), start_cb, content, this);
}

void SpeedScreen::refresh_keys() {
    // 0: idle; 1: the test runs (Stop); 2: another tool runs (Run off).
    int32_t keys = 0;
    if (this_running()) {
        keys = 1;
    } else if (any_running()) {
        keys = 2;
    }
    if (keys == shown_keys_) {
        return;
    }
    shown_keys_ = keys;
    if (auto* bar = soft_key_bar()) {
        bar->set_key(2, {keys == 1 ? "Stop" : "Run", [this] { run_or_stop(); }, true, keys != 2});
    }
}

void SpeedScreen::refresh_column(Column& column,
                                 int32_t kbps,
                                 uint64_t bytes,
                                 const std::vector<int32_t>& samples,
                                 int32_t scale) {
    const auto& theme = this->theme();
    if (set_text_if_changed(column.speed, column.speed_shown, home::rate_text(kbps))) {
        // The unit follows the number, on its baseline.
        lv_obj_update_layout(column.speed);
        lv_obj_align_to(column.unit, column.speed, LV_ALIGN_OUT_RIGHT_BOTTOM, kUnitGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Title)->base_line);
    }
    // Before its phase has data, the most it will move.
    set_text_if_changed(column.bytes, column.bytes_shown,
                        kbps < 0 ? tools::speed_cap_text() : tools::bytes_text(bytes));

    std::array<uint8_t, model::kSpeedSamples> bars{};
    for (std::size_t index = 0; index < bars.size() && index < samples.size(); ++index) {
        bars[index] = static_cast<uint8_t>(
            scale > 0 ? std::clamp<int64_t>(static_cast<int64_t>(samples[index]) * 100 / scale, 0, 100) : 0);
    }
    if (bars != column.bars_shown) {
        column.bars_shown = bars;
        view::widgets::update_bar_graph(column.graph, bars.data(), bars.size());
    }
}

void SpeedScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    tools::ToneText status = tools::speed_status(data.speed);
    if (!this_running() && any_running()) {
        status = {other_tool_text(data), tools::kToneMuted};
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
    if (status.tone != status_tone_) {
        lv_obj_set_style_text_color(status_, tone_color(theme, status.tone), 0);
        status_tone_ = status.tone;
    }
}

void SpeedScreen::refresh() {
    const model::SpeedRun& run = view_model().tools().speed;
    refresh_keys();
    // A run shows its own server; before one, the one a run would use.
    const std::string& server = run.state == model::ToolState::Idle ? view_model().speed_server() : run.server;
    set_text_if_changed(server_, server_shown_, model::speed_server_host(server));

    // One scale for both graphs: the fastest half second of the run.
    int32_t scale = 0;
    for (const int32_t sample : run.down_samples) {
        scale = std::max(scale, sample);
    }
    for (const int32_t sample : run.up_samples) {
        scale = std::max(scale, sample);
    }
    refresh_column(down_, run.down_kbps, run.down_bytes, run.down_samples, scale);
    refresh_column(up_, run.up_kbps, run.up_bytes, run.up_samples, scale);
    refresh_status();
}

void SpeedScreen::start_from_command() {
    // Always a first press: a running test (or another tool) is never
    // stopped.
    if (any_running()) {
        return;
    }
    run_or_stop();
}

void SpeedScreen::start_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<SpeedScreen*>(lv_observer_get_user_data(observer));
    if (screen && screen->view_model().take_tool_start(model::ToolKind::Speed)) {
        screen->start_from_command();
    }
}

void SpeedScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<SpeedScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
