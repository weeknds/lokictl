/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the target row at the top, the results under it and the status line at
// the bottom, all 304 px wide. The rows follow the theme's line heights.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin   = 8;
constexpr int32_t kWidth        = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kRight        = view::kScreenWidth - kSideMargin;
constexpr int32_t kTargetTop    = 1;
constexpr int32_t kResultsGap   = 4;
constexpr int32_t kStatusHeight = 18;
constexpr int32_t kStatusTop    = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap    = 8;
constexpr int32_t kCaptionGap   = 5;
constexpr int32_t kColumnWidth  = kWidth / static_cast<int32_t>(model::kPingShownReplies);
// The typing panel.
constexpr int32_t kDraftHeight  = 30;
// The Trace page: the gaps between a hop row's columns, and the scroll mark
// (a thin track with a wider thumb) at the right edge, as on the Status
// page.
constexpr int32_t kHopColumnGap   = 6;
constexpr int32_t kTrackX         = view::kScreenWidth - 6;
constexpr int32_t kThumbX         = kTrackX - 1;
constexpr int32_t kThumbWidth     = 3;
constexpr int32_t kThumbMinHeight = 8;
constexpr int32_t kHopRight       = kThumbX - 4;
// The Ports page's host detail: how far in a note starts, and the space
// before a line's muted part.
constexpr int32_t kNoteIndent     = 12;
constexpr const char* kAsideGap   = "  ";
// A finding's view: the QR code at the top right (a version 4 code, 33
// modules, two pixels each, in a quiet zone of two modules), the lines
// beside and under it at most this far apart.
constexpr int32_t kFindingCodeSize = 74;
constexpr int32_t kFindingPitch    = 15;
constexpr int32_t kFindingGap      = 8;
// The lines beside the code; the others are under it, the whole width.
constexpr std::size_t kFindingBesideCode = 5;

// Three spaces between addresses on one line.
constexpr const char* kAddressGap = "   ";

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
    if (tone != shown) {
        lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        shown = tone;
    }
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (hidden == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

int32_t text_width(const std::string& text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// Puts the addresses on at most `max_lines` lines of `width`, as many to a
// line as fit; what does not fit is counted at the end ("+2").
std::vector<std::string> pack_lines(const std::vector<std::string>& items,
                                    const lv_font_t* font,
                                    int32_t width,
                                    std::size_t max_lines) {
    std::vector<std::string> lines;
    std::size_t next = 0;
    while (next < items.size() && lines.size() < max_lines) {
        std::string line = items[next++];
        while (next < items.size() && text_width(line + kAddressGap + items[next], font) <= width) {
            line += kAddressGap + items[next++];
        }
        lines.push_back(line);
    }
    if (next < items.size() && !lines.empty()) {
        lines.back() += "  +" + std::to_string(items.size() - next);
    }
    return lines;
}

// "#rrggbb " and "#" around `text`, for a label with recoloring on.
std::string colored(const std::string& text, lv_color_t color) {
    char head[10];
    std::snprintf(head, sizeof(head), "#%02x%02x%02x ", color.red, color.green, color.blue);
    return head + text + "#";
}

// Packs the open ports with their tags as pack_lines() packs addresses,
// measuring "22 ssh Old" and giving "22 ssh #rrggbb Old#" in the tag's
// colour. When they do not all fit, the tagged ports come first, so none
// of them hides behind "+2".
std::vector<std::string> pack_port_lines(const std::vector<tools::PortItem>& items,
                                         const view::Theme& theme,
                                         const lv_font_t* font,
                                         int32_t width,
                                         std::size_t max_lines);

// Lowercase letters, digits and the dot: the only characters a target has.
bool target_character(uint32_t key) {
    return (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9') || key == '.';
}

std::vector<std::string> pack_port_lines(const std::vector<tools::PortItem>& items,
                                         const view::Theme& theme,
                                         const lv_font_t* font,
                                         int32_t width,
                                         std::size_t max_lines) {
    const auto pack = [&](const std::vector<const tools::PortItem*>& order, bool& all) {
        std::vector<std::string> lines;
        std::size_t next = 0;
        const auto plain = [](const tools::PortItem& item) {
            return item.tag.text.empty() ? item.text : item.text + " " + item.tag.text;
        };
        const auto marked = [&theme](const tools::PortItem& item) {
            return item.tag.text.empty() ? item.text
                                         : item.text + " " + colored(item.tag.text, tone_color(theme, item.tag.tone));
        };
        while (next < order.size() && lines.size() < max_lines) {
            std::string measured = plain(*order[next]);
            std::string line = marked(*order[next]);
            ++next;
            while (next < order.size() && text_width(measured + kAddressGap + plain(*order[next]), font) <= width) {
                measured += kAddressGap + plain(*order[next]);
                line += kAddressGap + marked(*order[next]);
                ++next;
            }
            lines.push_back(line);
        }
        all = next >= order.size();
        if (!all && !lines.empty()) {
            lines.back() += "  +" + std::to_string(order.size() - next);
        }
        return lines;
    };
    std::vector<const tools::PortItem*> order;
    for (const tools::PortItem& item : items) {
        order.push_back(&item);
    }
    bool all = true;
    std::vector<std::string> lines = pack(order, all);
    if (!all) {
        std::stable_partition(order.begin(), order.end(),
                              [](const tools::PortItem* item) { return !item->tag.text.empty(); });
        lines = pack(order, all);
    }
    return lines;
}

} // namespace

ToolScreen::ToolScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine,
                       model::AppPage page)
    : BaseScreen(view_model, assets, theme_engine),
      kind_(page == model::AppPage::Dns     ? model::ToolKind::Dns
            : page == model::AppPage::Trace ? model::ToolKind::Trace
            : page == model::AppPage::Ports ? model::ToolKind::Ports
                                            : model::ToolKind::Ping) {
    init();
}

ToolScreen::~ToolScreen() {
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
        confirm_timer_ = nullptr;
    }
}

bool ToolScreen::handles(model::AppPage page) {
    return page == model::AppPage::Ping || page == model::AppPage::Dns || page == model::AppPage::Trace ||
           page == model::AppPage::Ports;
}

view::widgets::HelpContent ToolScreen::help_content(model::ToolKind kind) {
    if (kind == model::ToolKind::Ports) {
        return {"PORTS",
                {
                    {"6 twice", "Scan; 7 host details"},
                    {"Attacked", "Attackers use its hole"},
                    {"3 holes", "Known security holes"},
                    {"Old", "No more security fixes"},
                    {"Check", "May be fixed already"},
                }};
    }
    if (kind == model::ToolKind::Trace) {
        return {"TRACE",
                {
                    {"ESC / 4", "Back (a run goes on)"},
                    {"Z / C", "Previous / next target"},
                    {"F / X", "Scroll the hops"},
                    {"5", "Type a target (ENTER keeps)"},
                    {"ENTER / 6", "Run / stop"},
                }};
    }
    return {kind == model::ToolKind::Dns ? "DNS" : "PING",
            {
                {"ESC / 4", "Back (a run goes on)"},
                {"Z / C", "Previous / next target"},
                {"5", "Type a target (ENTER keeps)"},
                {"ENTER / 6", "Run / stop"},
                {"Sent", "Packets LokiCTL sent"},
            }};
}

view::widgets::HelpContent ToolScreen::help() const {
    if (finding_open()) {
        return {"HOLE",
                {
                    {"ESC / 4", "Back to the host"},
                    {"Z / C", "Previous / next"},
                    {"QR code", "Read more on a phone"},
                    {"Score", "0 to 10, 10 is worst"},
                    {"Version", "Checked by number only"},
                }};
    }
    if (detail_open()) {
        return {"HOST",
                {
                    {"ESC / 4", "Back to the scan"},
                    {"F / X", "Scroll"},
                    {"6 / ENTER", "One finding at a time"},
                    {"CVE", "A hole's official number"},
                    {"Version", "Checked by number only"},
                }};
    }
    return help_content(kind_);
}

view::widgets::SoftKeySet ToolScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh_keys() sets them for the run and typing.
    view::widgets::SoftKey host;
    if (kind_ == model::ToolKind::Ports) {
        host = {"Host", [this] { open_detail(); }, false, false};
    }
    return {{
        {"Back", [this] { back_key(); }},
        {"Edit", [this] { start_edit(); }},
        {kind_ == model::ToolKind::Ports ? "Scan" : "Run", [this] { run_or_stop(); }, true},
        host,
        {},
    }};
}

std::vector<ToolScreen::Target> ToolScreen::targets() const {
    std::vector<Target> list;
    if (kind_ == model::ToolKind::Ports) {
        list = ports_targets();
    } else if (kind_ != model::ToolKind::Dns) {
        const std::string& router = view_model().home().gateway_address;
        if (model::is_ipv4_text(router)) {
            list.push_back({router, "router"});
        }
        for (const char* preset : model::kPingPresets) {
            list.push_back({preset, ""});
        }
    } else {
        for (const char* preset : model::kDnsPresets) {
            list.push_back({preset, ""});
        }
    }
    const std::string& typed = view_model().tool_typed_target(kind_);
    if (!typed.empty()) {
        list.push_back({typed, ""});
    }
    return list;
}

std::vector<ToolScreen::Target> ToolScreen::ports_targets() const {
    const model::HomeData& home = view_model().home();
    std::vector<Target> list;
    const auto add = [&list](const std::string& address, std::string caption) {
        if (!model::port_scan_address(address)) {
            return;
        }
        for (const Target& known : list) {
            if (known.text == address) {
                return;
            }
        }
        // A long name would leave no room for the address.
        constexpr std::size_t kCaptionMax = 12;
        if (caption.size() > kCaptionMax) {
            caption = caption.substr(0, kCaptionMax - 1) + "…";
        }
        list.push_back({address, caption});
    };
    add(home.gateway_address, "router");
    add(home.uplink_address, "this device");
    const model::ToolsData& data = view_model().tools();
    for (const model::DiscoverDevice& entry : model::discover_devices(data.listen, data.probe)) {
        const model::HeardDevice& device = entry.device;
        if (!device.own_port.empty()) {
            continue;  // one of the Cardputer's own ports
        }
        add(device.address, !device.name.empty() ? device.name : !device.kind.empty() ? device.kind : "device");
    }
    for (const model::HomeClient& client : home.clients) {
        add(client.address, client.name.empty() ? std::string("device") : client.name);
    }
    return list;
}

int32_t ToolScreen::target_index(const std::vector<Target>& list) const {
    return std::clamp<int32_t>(view_model().tool_target(kind_), 0, std::max<int32_t>(
                                                                        static_cast<int32_t>(list.size()) - 1, 0));
}

std::string ToolScreen::selected_target() const {
    const std::vector<Target> list = targets();
    if (list.empty()) {
        return {};
    }
    return list[static_cast<std::size_t>(target_index(list))].text;
}

bool ToolScreen::this_running() const {
    return model::tool_state(view_model().tools(), kind_) == model::ToolState::Running;
}

bool ToolScreen::any_running() const {
    return model::tool_running(view_model().tools());
}

void ToolScreen::step_target(int32_t delta) {
    if (this_running()) {
        return;
    }
    const std::vector<Target> list = targets();
    const auto count = static_cast<int32_t>(list.size());
    if (count == 0) {
        return;
    }
    const int32_t next = (target_index(list) + delta + count) % count;
    view_model().set_tool_target(kind_, next);
}

void ToolScreen::start_edit() {
    if (confirming_) {
        cancel_confirm();  // like any other key: only cancels
        return;
    }
    if (this_running() || view_model().tool_typing()) {
        return;
    }
    draft_invalid_ = false;
    view_model().set_tool_draft(view_model().tool_typed_target(kind_));
    view_model().set_tool_typing(true);
}

void ToolScreen::run_or_stop() {
    if (view_model().tool_typing()) {
        return;
    }
    if (this_running()) {
        view_model().request_tool_stop();
        return;
    }
    if (any_running()) {
        return;  // the other tool runs
    }
    const std::vector<Target> list = targets();
    if (list.empty()) {
        return;
    }
    const std::string& target = list[static_cast<std::size_t>(target_index(list))].text;
    if (kind_ == model::ToolKind::Ports) {
        if (!confirming_ || target != confirm_target_) {
            // The first 6: what would be asked; nothing is sent.
            if (!model::valid_tool_target(kind_, target)) {
                return;
            }
            confirming_ = true;
            confirm_target_ = target;
            if (!confirm_timer_) {
                confirm_timer_ = lv_timer_create(confirm_timer_cb, kConfirmMs, this);
            }
            refresh();
            return;
        }
        // The second 6: the confirmation, given for this scan only.
        const std::string confirmed = confirm_target_;
        cancel_confirm();
        view_model().request_tool_run(kind_, confirmed);
        return;
    }
    if (kind_ == model::ToolKind::Trace) {
        view_model().set_trace_scroll(viewmodel::BaseViewModel::kTraceFollow);
    }
    view_model().request_tool_run(kind_, target);
}

void ToolScreen::cancel_confirm() {
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
        confirm_timer_ = nullptr;
    }
    if (confirming_) {
        confirming_ = false;
        confirm_target_.clear();
        refresh();
    }
}

void ToolScreen::confirm_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<ToolScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->cancel_confirm();
    }
}

bool ToolScreen::detail_open() const {
    return kind_ == model::ToolKind::Ports &&
           view_model().ports_detail() != viewmodel::BaseViewModel::kPortsDetailClosed;
}

bool ToolScreen::detail_allowed() const {
    if (kind_ != model::ToolKind::Ports || confirming_ || view_model().tool_typing() || !ports_result_shown()) {
        return false;
    }
    const model::ToolState state = view_model().tools().ports.state;
    return state == model::ToolState::Done || state == model::ToolState::Stopped;
}

void ToolScreen::open_detail() {
    if (!detail_open() && detail_allowed()) {
        view_model().set_ports_detail(0);
    }
}

void ToolScreen::close_detail() {
    view_model().set_ports_finding(viewmodel::BaseViewModel::kPortsFindingClosed);
    view_model().set_ports_detail(viewmodel::BaseViewModel::kPortsDetailClosed);
}

bool ToolScreen::finding_open() const {
    return detail_open() && view_model().ports_finding() != viewmodel::BaseViewModel::kPortsFindingClosed &&
           !view_model().tools().ports.findings.empty();
}

void ToolScreen::open_finding() {
    if (!detail_open() || view_model().tools().ports.findings.empty()) {
        return;
    }
    // The first finding at or below the top line shown, else the first.
    int32_t index = 0;
    for (std::size_t line = static_cast<std::size_t>(std::max<int32_t>(detail_first_, 0));
         line < detail_findings_.size(); ++line) {
        if (detail_findings_[line] >= 0) {
            index = detail_findings_[line];
            break;
        }
    }
    view_model().set_ports_finding(index);
}

void ToolScreen::close_finding() {
    view_model().set_ports_finding(viewmodel::BaseViewModel::kPortsFindingClosed);
}

void ToolScreen::step_finding(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().tools().ports.findings.size());
    if (!finding_open() || count == 0) {
        return;
    }
    const int32_t index = std::clamp<int32_t>(view_model().ports_finding(), 0, count - 1);
    view_model().set_ports_finding((index + delta + count) % count);
}

void ToolScreen::scroll_detail(int32_t delta) {
    const int32_t first = view_model().ports_detail();
    if (first == viewmodel::BaseViewModel::kPortsDetailClosed) {
        return;
    }
    const int32_t shown = std::clamp<int32_t>(first, 0, detail_last_top_);
    view_model().set_ports_detail(std::clamp<int32_t>(shown + delta, 0, detail_last_top_));
}

void ToolScreen::back_key() {
    if (finding_open()) {
        close_finding();
        return;
    }
    if (detail_open()) {
        close_detail();
        return;
    }
    view_model().go_back();
}

int32_t ToolScreen::first_hop_row() const {
    const auto count = static_cast<int32_t>(view_model().tools().trace.hops.size());
    const int32_t last_top = std::max<int32_t>(count - visible_hop_rows_, 0);
    const int32_t scroll = view_model().trace_scroll();
    return scroll == viewmodel::BaseViewModel::kTraceFollow ? last_top : std::clamp<int32_t>(scroll, 0, last_top);
}

void ToolScreen::scroll_hops(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().tools().trace.hops.size());
    const int32_t last_top = std::max<int32_t>(count - visible_hop_rows_, 0);
    const int32_t next = std::clamp<int32_t>(first_hop_row() + delta, 0, last_top);
    // At the end the list follows the newest hops again.
    view_model().set_trace_scroll(next >= last_top ? viewmodel::BaseViewModel::kTraceFollow : next);
}

bool ToolScreen::text_entry() const {
    return view_model().tool_typing();
}

bool ToolScreen::on_key(uint32_t key) {
    if (view_model().tool_typing()) {
        return type_key(key);
    }
    if (finding_open()) {
        if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
            step_finding(-1);
        } else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
            step_finding(1);
        } else if (key == LV_KEY_ENTER) {
            close_finding();
        }
        return true;  // the pages under it take no key
    }
    if (detail_open()) {
        if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
            scroll_detail(key == LV_KEY_UP ? -1 : 1);
        } else if (key == LV_KEY_ENTER) {
            if (view_model().tools().ports.findings.empty()) {
                close_detail();
            } else {
                open_finding();
            }
        }
        return true;  // the page under it takes no key
    }
    if (confirming_) {
        if (key == LV_KEY_ENTER) {
            run_or_stop();  // the second press
        } else {
            cancel_confirm();  // any other key only cancels
        }
        return true;
    }
    switch (key) {
        case LV_KEY_LEFT:
            step_target(-1);
            return true;
        case LV_KEY_RIGHT:
            step_target(1);
            return true;
        case LV_KEY_UP:
        case LV_KEY_DOWN:
            if (kind_ != model::ToolKind::Trace) {
                return false;
            }
            scroll_hops(key == LV_KEY_UP ? -1 : 1);
            return true;
        case LV_KEY_ENTER:
            // On a finished scan of the selected target, Enter shows the
            // host; otherwise it does what 6 does.
            if (detail_allowed()) {
                open_detail();
            } else {
                run_or_stop();
            }
            return true;
        default:
            return false;
    }
}

// While typing every key is taken.
bool ToolScreen::type_key(uint32_t key) {
    std::string draft = view_model().tool_draft();
    if (key == LV_KEY_ENTER) {
        if (draft.empty()) {
            // Drops the typed target; the choice goes back to the first one.
            draft_invalid_ = false;
            view_model().set_tool_typing(false);
            if (!view_model().tool_typed_target(kind_).empty()) {
                view_model().set_tool_typed_target(kind_, "");
                view_model().set_tool_target(kind_, 0);
            }
            return true;
        }
        if (!model::valid_tool_target(kind_, draft)) {
            draft_invalid_ = true;
            refresh();
            return true;
        }
        draft_invalid_ = false;
        view_model().set_tool_typed_target(kind_, draft);
        // The typed target is the last one.
        view_model().set_tool_target(kind_, static_cast<int32_t>(targets().size()) - 1);
        view_model().set_tool_typing(false);
        return true;
    }
    if (key == LV_KEY_BACKSPACE) {
        if (!draft.empty()) {
            draft.pop_back();
        }
    } else {
        if (key >= 'A' && key <= 'Z') {
            key = key - 'A' + 'a';
        }
        if (!target_character(key) || (kind_ == model::ToolKind::Ports && key >= 'a' && key <= 'z')) {
            return true;  // the Ports page takes an address only
        }
        if (draft.size() < model::kToolTargetMax) {
            draft += static_cast<char>(key);
        }
    }
    draft_invalid_ = false;
    view_model().set_tool_draft(draft);
    refresh();
    return true;
}

// A short ESC cancels typing; otherwise the app goes back, and a run goes on.
bool ToolScreen::on_back() {
    if (finding_open()) {
        close_finding();
        return true;
    }
    if (detail_open()) {
        close_detail();
        return true;
    }
    if (confirming_) {
        cancel_confirm();
        return true;
    }
    if (view_model().tool_typing()) {
        draft_invalid_ = false;
        view_model().set_tool_typing(false);
        return true;
    }
    return false;
}

void ToolScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    main_panel_ = plain_object(content);
    lv_obj_set_size(main_panel_, view::kScreenWidth, kContentHeight);

    // The target row: the caption and the place stand on the target's
    // baseline.
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    target_caption_ = plain_label(main_panel_, "");
    view::style::caption(target_caption_, theme, "");
    lv_obj_set_pos(target_caption_, kSideMargin, kTargetTop + small_drop);
    target_value_ = plain_label(main_panel_, "");
    view::style::text(target_value_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_label_set_long_mode(target_value_, LV_LABEL_LONG_DOT);
    target_place_ = plain_label(main_panel_, "");
    view::style::text(target_place_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(target_place_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kTargetTop + small_drop);

    switch (kind_) {
        case model::ToolKind::Ping:
            build_ping_results(main_panel_);
            break;
        case model::ToolKind::Dns:
            build_dns_results(main_panel_);
            break;
        case model::ToolKind::Trace:
            build_trace_results(main_panel_);
            break;
        case model::ToolKind::Ports:
            build_ports_results(main_panel_);
            break;
        case model::ToolKind::Listen:
        case model::ToolKind::Probe:
        case model::ToolKind::Speed:
            break;  // the Discover and Speed pages', never a tool page's
    }

    // The status line, with the Sent count and its caption at the right.
    status_ = plain_label(main_panel_, "");
    view::style::text(status_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_, kSideMargin, kStatusTop);
    sent_ = plain_label(main_panel_, "");
    view::style::text(sent_, theme, view::FontRole::Mono, theme.colors.text);
    lv_obj_align(sent_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kStatusTop);
    sent_caption_ = plain_label(main_panel_, "");
    view::style::caption(sent_caption_, theme, "sent");

    build_typing_panel(content);
    if (kind_ == model::ToolKind::Ports) {
        build_detail_panel(content);
        build_finding_panel(content);
        // The open ports' tags are coloured inline.
        for (lv_obj_t* line : lines_) {
            lv_label_set_recolor(line, true);
        }
    }

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().tool_target_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().tool_typing_subject(), observer_cb, content, this);
    if (kind_ != model::ToolKind::Dns) {
        // The router comes and goes with the network.
        lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
    }
    if (kind_ == model::ToolKind::Trace) {
        lv_subject_add_observer_obj(view_model().trace_scroll_subject(), observer_cb, content, this);
    }
    if (kind_ == model::ToolKind::Ports) {
        lv_subject_add_observer_obj(view_model().ports_detail_subject(), observer_cb, content, this);
        lv_subject_add_observer_obj(view_model().ports_finding_subject(), observer_cb, content, this);
    }
    // Last, so a start the command bar asked for finds the page built.
    lv_subject_add_observer_obj(view_model().tool_start_subject(), start_cb, content, this);
}

void ToolScreen::build_ping_results(lv_obj_t* panel) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const int32_t sequence_top = kTargetTop + lv_font_get_line_height(mono) + kResultsGap;
    const int32_t time_top = sequence_top + lv_font_get_line_height(small) - 1;
    const int32_t counts_top = time_top + lv_font_get_line_height(mono) + 3;
    const int32_t stats_top = counts_top + lv_font_get_line_height(small);

    for (std::size_t column = 0; column < kTableColumns; ++column) {
        const int32_t x = kSideMargin + static_cast<int32_t>(column) * kColumnWidth;
        sequences_[column] = plain_label(panel, "");
        view::style::text(sequences_[column], theme, view::FontRole::Small, theme.colors.muted);
        fix_width(sequences_[column], kColumnWidth, small);
        lv_obj_set_style_text_align(sequences_[column], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(sequences_[column], x, sequence_top);
        times_[column] = plain_label(panel, "");
        view::style::text(times_[column], theme, view::FontRole::Mono);
        fix_width(times_[column], kColumnWidth, mono);
        lv_obj_set_style_text_align(times_[column], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(times_[column], x, time_top);
    }
    placeholder_ = plain_label(panel, "Results show here");
    view::style::text(placeholder_, theme, view::FontRole::Mono, theme.colors.muted);
    fix_width(placeholder_, kWidth, mono);
    lv_obj_set_pos(placeholder_, kSideMargin, sequence_top);
    lv_obj_add_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);
    counts_ = plain_label(panel, "");
    view::style::text(counts_, theme, view::FontRole::Small);
    fix_width(counts_, kWidth, small);
    lv_obj_set_pos(counts_, kSideMargin, counts_top);
    stats_ = plain_label(panel, "");
    view::style::text(stats_, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(stats_, kWidth, small);
    lv_obj_set_pos(stats_, kSideMargin, stats_top);
}

void ToolScreen::build_dns_results(lv_obj_t* panel) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    int32_t top = kTargetTop + lv_font_get_line_height(mono) + kResultsGap;
    for (std::size_t line = 0; line < kAddressLines; ++line) {
        lines_[line] = plain_label(panel, "");
        view::style::text(lines_[line], theme, view::FontRole::Mono);
        fix_width(lines_[line], kWidth, mono);
        lv_obj_set_pos(lines_[line], kSideMargin, top);
        top += lv_font_get_line_height(mono);
    }
    server_ = plain_label(panel, "");
    view::style::text(server_, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(server_, kWidth, small);
    lv_obj_set_pos(server_, kSideMargin, top + 1);
}

// The open ports use the DNS page's lines and summary; the confirmation
// stands under the first line.
void ToolScreen::build_ports_results(lv_obj_t* panel) {
    build_dns_results(panel);
    const auto& theme = this->theme();
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    confirm_ = plain_label(panel, "");
    view::style::text(confirm_, theme, view::FontRole::Mono, theme.colors.warning);
    lv_obj_set_width(confirm_, kWidth);
    lv_label_set_long_mode(confirm_, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(confirm_, kSideMargin, kTargetTop + 2 * lv_font_get_line_height(mono) + kResultsGap);
    lv_obj_add_flag(confirm_, LV_OBJ_FLAG_HIDDEN);
}

void ToolScreen::build_trace_results(lv_obj_t* panel) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const int32_t row_height = lv_font_get_line_height(small);
    hop_list_top_ = kTargetTop + lv_font_get_line_height(mono) + kResultsGap;
    // The rows end above the status line, whatever the theme's line height.
    visible_hop_rows_ = std::clamp<int32_t>((kStatusTop - 1 - hop_list_top_) / row_height, 1,
                                            static_cast<int32_t>(kTraceRows));
    hop_list_height_ = visible_hop_rows_ * row_height;

    // Columns: the hop number (right-aligned), the address, the name (ends
    // in dots when longer) and the time (right-aligned).
    const int32_t number_width = text_width("20", small);
    const int32_t address_x = kSideMargin + number_width + kHopColumnGap;
    const int32_t address_width = text_width("255.255.255.255", small);
    const int32_t time_width = text_width("99.9ms", small);
    const int32_t time_x = kHopRight - time_width;
    const int32_t name_x = address_x + address_width + kHopColumnGap;
    const int32_t name_width = std::max<int32_t>(time_x - kHopColumnGap - name_x, 0);

    for (int32_t index = 0; index < visible_hop_rows_; ++index) {
        HopRow& row = hop_rows_[static_cast<std::size_t>(index)];
        const int32_t top = hop_list_top_ + index * row_height;
        row.number = plain_label(panel, "");
        view::style::text(row.number, theme, view::FontRole::Small, theme.colors.muted);
        lv_obj_set_size(row.number, number_width, row_height);
        lv_obj_set_style_text_align(row.number, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.number, kSideMargin, top);
        row.address = plain_label(panel, "");
        view::style::text(row.address, theme, view::FontRole::Small);
        lv_label_set_long_mode(row.address, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(row.address, address_width, row_height);
        lv_obj_set_pos(row.address, address_x, top);
        row.name = plain_label(panel, "");
        view::style::text(row.name, theme, view::FontRole::Small, theme.colors.muted);
        lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.name, name_width, row_height);
        lv_obj_set_pos(row.name, name_x, top);
        row.time = plain_label(panel, "");
        view::style::text(row.time, theme, view::FontRole::Small);
        lv_obj_set_size(row.time, time_width, row_height);
        lv_obj_set_style_text_align(row.time, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.time, time_x, top);
    }

    placeholder_ = plain_label(panel, "Results show here");
    view::style::text(placeholder_, theme, view::FontRole::Mono, theme.colors.muted);
    fix_width(placeholder_, kWidth, mono);
    lv_obj_set_pos(placeholder_, kSideMargin, hop_list_top_);
    lv_obj_add_flag(placeholder_, LV_OBJ_FLAG_HIDDEN);

    track_ = plain_object(panel);
    lv_obj_set_pos(track_, kTrackX, hop_list_top_);
    lv_obj_set_size(track_, 1, hop_list_height_);
    lv_obj_set_style_bg_color(track_, theme.colors.border_subtle, 0);
    lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
    lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);
    thumb_ = plain_object(panel);
    lv_obj_set_style_bg_color(thumb_, theme.colors.muted, 0);
    lv_obj_set_style_bg_opa(thumb_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb_, std::min<int32_t>(theme.metrics.radius, 1), 0);
    lv_obj_add_flag(thumb_, LV_OBJ_FLAG_HIDDEN);
}

void ToolScreen::build_typing_panel(lv_obj_t* content) {
    const auto& theme = this->theme();
    typing_panel_ = plain_object(content);
    lv_obj_set_size(typing_panel_, view::kScreenWidth, kContentHeight);
    lv_obj_add_flag(typing_panel_, LV_OBJ_FLAG_HIDDEN);
    auto* column = plain_object(typing_panel_);
    lv_obj_set_pos(column, kSideMargin, theme.metrics.space_sm);
    lv_obj_set_size(column, kWidth, kContentHeight - theme.metrics.space_sm);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, theme.metrics.space_sm, 0);

    auto* title = plain_label(column, kind_ == model::ToolKind::Dns     ? "Name to look up"
                                      : kind_ == model::ToolKind::Trace ? "Host to trace"
                                      : kind_ == model::ToolKind::Ports ? "Address to scan"
                                                                        : "Host to ping");
    view::style::text(title, theme, view::FontRole::Strong);
    auto* box = plain_object(column);
    view::style::card(box, theme);
    lv_obj_set_style_border_color(box, theme.colors.accent, 0);
    lv_obj_set_size(box, LV_PCT(100), kDraftHeight);
    draft_ = plain_label(box, "");
    view::style::text(draft_, theme, view::FontRole::Mono);
    lv_obj_set_width(draft_, LV_PCT(100));
    lv_label_set_long_mode(draft_, LV_LABEL_LONG_CLIP);
    lv_obj_align(draft_, LV_ALIGN_LEFT_MID, 0, 0);
    count_ = plain_label(column, "");
    view::style::text(count_, theme, view::FontRole::Small, theme.colors.muted);
    typing_note_ = plain_label(column, "");
    view::style::text(typing_note_, theme, view::FontRole::Small);
    lv_obj_set_width(typing_note_, LV_PCT(100));
    lv_label_set_long_mode(typing_note_, LV_LABEL_LONG_WRAP);
}

// The host detail: a header row like the target row (caption, address,
// how many are open), then rows of two columns, with a scroll mark at the
// right edge when they do not all fit.
void ToolScreen::build_detail_panel(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    detail_panel_ = plain_object(content);
    lv_obj_set_size(detail_panel_, view::kScreenWidth, kContentHeight);
    lv_obj_add_flag(detail_panel_, LV_OBJ_FLAG_HIDDEN);

    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    detail_caption_ = plain_label(detail_panel_, "");
    view::style::caption(detail_caption_, theme, "");
    lv_obj_set_pos(detail_caption_, kSideMargin, kTargetTop + small_drop);
    detail_address_ = plain_label(detail_panel_, "");
    view::style::text(detail_address_, theme, view::FontRole::Mono, theme.colors.accent2);
    detail_count_ = plain_label(detail_panel_, "");
    view::style::text(detail_count_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(detail_count_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kTargetTop + small_drop);

    const int32_t row_height = lv_font_get_line_height(small);
    detail_list_top_ = kTargetTop + lv_font_get_line_height(mono) + kResultsGap;
    visible_detail_rows_ = std::clamp<int32_t>((kContentHeight - 1 - detail_list_top_) / row_height, 1,
                                               static_cast<int32_t>(kDetailRows));
    for (int32_t index = 0; index < visible_detail_rows_; ++index) {
        DetailRow& row = detail_rows_[static_cast<std::size_t>(index)];
        const int32_t top = detail_list_top_ + index * row_height;
        row.left = plain_label(detail_panel_, "");
        view::style::text(row.left, theme, view::FontRole::Small, theme.colors.muted);
        lv_label_set_long_mode(row.left, LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(row.left, kSideMargin, top);
        row.right = plain_label(detail_panel_, "");
        view::style::text(row.right, theme, view::FontRole::Small);
        lv_label_set_long_mode(row.right, LV_LABEL_LONG_DOT);
        lv_obj_set_y(row.right, top);
    }

    detail_track_ = plain_object(detail_panel_);
    lv_obj_set_pos(detail_track_, kTrackX, detail_list_top_);
    lv_obj_set_size(detail_track_, 1, visible_detail_rows_ * row_height);
    lv_obj_set_style_bg_color(detail_track_, theme.colors.border_subtle, 0);
    lv_obj_set_style_bg_opa(detail_track_, LV_OPA_COVER, 0);
    lv_obj_add_flag(detail_track_, LV_OBJ_FLAG_HIDDEN);
    detail_thumb_ = plain_object(detail_panel_);
    lv_obj_set_style_bg_color(detail_thumb_, theme.colors.muted, 0);
    lv_obj_set_style_bg_opa(detail_thumb_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(detail_thumb_, std::min<int32_t>(theme.metrics.radius, 1), 0);
    lv_obj_add_flag(detail_thumb_, LV_OBJ_FLAG_HIDDEN);
}

// One finding: its lines at the left and the QR code of its link at the
// top right; the last lines run under the code, the whole width.
void ToolScreen::build_finding_panel(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    finding_panel_ = plain_object(content);
    lv_obj_set_size(finding_panel_, view::kScreenWidth, kContentHeight);
    lv_obj_add_flag(finding_panel_, LV_OBJ_FLAG_HIDDEN);

    finding_code_ = lv_qrcode_create(finding_panel_);
    lv_qrcode_set_size(finding_code_, kFindingCodeSize);
    lv_qrcode_set_dark_color(finding_code_, lv_color_black());
    lv_qrcode_set_light_color(finding_code_, lv_color_white());
    lv_qrcode_set_quiet_zone(finding_code_, true);
    lv_obj_set_pos(finding_code_, kRight - kFindingCodeSize, kTargetTop);

    const int32_t pitch = std::min<int32_t>(lv_font_get_line_height(small), kFindingPitch);
    const int32_t beside = kRight - kFindingCodeSize - kFindingGap - kSideMargin;
    for (std::size_t line = 0; line < kFindingLines; ++line) {
        lv_obj_t* label = plain_label(finding_panel_, "");
        view::style::text(label, theme, view::FontRole::Small);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(label, line < kFindingBesideCode ? beside : kWidth, lv_font_get_line_height(small));
        lv_obj_set_pos(label, kSideMargin, kTargetTop + static_cast<int32_t>(line) * pitch);
        finding_lines_[line] = label;
    }
}

void ToolScreen::refresh_keys() {
    // 0: idle; 1: this tool runs (Edit off, Stop); 2: the other tool runs
    // (Run off); 3: typing (all off, keys 4-8 type digits).
    // 4: the host detail is open (Back, and More with findings); 5: a
    // finding is shown (Back, Prev, Next). Ports: + 8 when Host may open,
    // + 16 with findings (More) or more than one (Prev, Next).
    int32_t keys = 0;
    if (view_model().tool_typing()) {
        keys = 3;
    } else if (finding_open()) {
        keys = 5;
    } else if (detail_open()) {
        keys = 4;
    } else if (this_running()) {
        keys = 1;
    } else if (any_running()) {
        keys = 2;
    }
    const bool host = kind_ == model::ToolKind::Ports && keys == 0 && detail_allowed();
    const std::size_t findings = view_model().tools().ports.findings.size();
    const bool more = (keys == 4 && findings > 0) || (keys == 5 && findings > 1);
    const int32_t shown = keys + (host ? 8 : 0) + (more ? 16 : 0);
    if (shown == shown_keys_) {
        return;
    }
    shown_keys_ = shown;
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    bar->set_key(0, {"Back", [this] { back_key(); }, false, keys != 3});
    if (keys == 5) {
        bar->set_key(1, {"Prev", [this] { step_finding(-1); }, false, more});
        bar->set_key(2, {"Next", [this] { step_finding(1); }, true, more});
    } else if (keys == 4) {
        bar->set_key(1, {"Edit", [this] { start_edit(); }, false, false});
        bar->set_key(2, {"More", [this] { open_finding(); }, true, more});
    } else {
        bar->set_key(1, {"Edit", [this] { start_edit(); }, false, keys == 0 || keys == 2});
        const char* run = kind_ == model::ToolKind::Ports ? "Scan" : "Run";
        bar->set_key(2, {keys == 1 ? "Stop" : run, [this] { run_or_stop(); }, true, keys == 0 || keys == 1});
    }
    if (kind_ == model::ToolKind::Ports) {
        bar->set_key(3, {"Host", [this] { open_detail(); }, false, host});
    }
}

void ToolScreen::refresh_target() {
    const model::ToolsData& data = view_model().tools();
    std::string caption = kind_ == model::ToolKind::Dns ? "name" : "target";
    std::string value;
    std::string place;
    if (this_running()) {
        // The run's own target, whatever is selected.
        switch (kind_) {
            case model::ToolKind::Ping:
                value = data.ping.target;
                break;
            case model::ToolKind::Dns:
                value = data.dns.name;
                break;
            case model::ToolKind::Trace:
                value = data.trace.target;
                break;
            case model::ToolKind::Ports:
                value = data.ports.target;
                break;
            case model::ToolKind::Listen:
            case model::ToolKind::Probe:
            case model::ToolKind::Speed:
                break;  // the Discover and Speed pages', never a tool page's
        }
        if (kind_ == model::ToolKind::Ports) {
            for (const Target& target : ports_targets()) {
                if (target.text == value) {
                    caption = target.caption;
                    break;
                }
            }
        } else if (kind_ != model::ToolKind::Dns && value == view_model().home().gateway_address) {
            caption = "router";
        }
    } else {
        const std::vector<Target> list = targets();
        if (!list.empty()) {
            const int32_t index = target_index(list);
            const Target& target = list[static_cast<std::size_t>(index)];
            value = target.text;
            if (!target.caption.empty()) {
                caption = target.caption;
            }
            place = "‹ " + std::to_string(index + 1) + "/" + std::to_string(list.size()) + " ›";
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
        // The target gets the room between the caption and the place, and
        // ends in dots when it is longer.
        const auto& theme = this->theme();
        lv_obj_update_layout(target_caption_);
        lv_obj_update_layout(target_place_);
        const int32_t left = kSideMargin + lv_obj_get_width(target_caption_) + kCaptionGap;
        const int32_t right = place.empty() ? kRight : kRight - lv_obj_get_width(target_place_) - kStatusGap;
        lv_obj_set_pos(target_value_, left, kTargetTop);
        lv_obj_set_size(target_value_, std::max<int32_t>(right - left, 0),
                        lv_font_get_line_height(theme.font(view::FontRole::Mono)));
    }
}

void ToolScreen::refresh_ping() {
    const auto& theme = this->theme();
    const model::PingRun& run = view_model().tools().ping;
    const bool running = run.state == model::ToolState::Running;
    for (std::size_t column = 0; column < kTableColumns; ++column) {
        std::string sequence;
        tools::ToneText time{"", tools::kToneText};
        if (column < run.replies.size()) {
            const model::PingReply& reply = run.replies[column];
            sequence = "#" + std::to_string(reply.sequence);
            time = tools::reply_text(reply, running);
        }
        set_text_if_changed(sequences_[column], sequences_shown_[column], sequence);
        set_text_if_changed(times_[column], times_shown_[column], time.text);
        set_tone_if_changed(times_[column], time_tones_[column], time.tone, theme);
    }

    set_hidden(placeholder_, run.state != model::ToolState::Idle);
    set_text_if_changed(counts_, counts_shown_, tools::ping_counts_text(run));
    set_text_if_changed(stats_, stats_shown_, tools::ping_times_text(run));
}

void ToolScreen::refresh_dns() {
    const auto& theme = this->theme();
    const model::DnsRun& run = view_model().tools().dns;
    std::vector<std::string> lines;
    int32_t tone = tools::kToneText;
    if (run.state == model::ToolState::Idle) {
        lines.push_back("Results show here");
        tone = tools::kToneMuted;
    } else {
        lines = pack_lines(run.addresses, theme.font(view::FontRole::Mono), kWidth, kAddressLines);
    }
    for (std::size_t line = 0; line < kAddressLines; ++line) {
        set_text_if_changed(lines_[line], lines_shown_[line], line < lines.size() ? lines[line] : std::string());
    }
    if (tone != lines_tone_) {
        for (lv_obj_t* label : lines_) {
            lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        }
        lines_tone_ = tone;
    }

    std::string server;
    if (run.state != model::ToolState::Idle && !run.server.empty()) {
        server = "server " + run.server;
        if (run.time_ms >= 0) {
            server += " · " + std::to_string(run.time_ms) + " ms";
        }
    }
    set_text_if_changed(server_, server_shown_, server);
}

void ToolScreen::refresh_trace() {
    const auto& theme = this->theme();
    const model::TraceRun& run = view_model().tools().trace;
    const bool running = run.state == model::ToolState::Running;
    const auto count = static_cast<int32_t>(run.hops.size());
    const int32_t first = first_hop_row();
    for (int32_t index = 0; index < visible_hop_rows_; ++index) {
        HopRow& row = hop_rows_[static_cast<std::size_t>(index)];
        tools::TraceRow texts;
        texts.address.tone = tools::kToneText;
        if (first + index < count) {
            texts = tools::trace_row(run.hops[static_cast<std::size_t>(first + index)], running);
        }
        set_text_if_changed(row.number, row.number_shown, texts.number);
        set_text_if_changed(row.address, row.address_shown, texts.address.text);
        set_tone_if_changed(row.address, row.address_tone, texts.address.tone, theme);
        set_text_if_changed(row.name, row.name_shown, texts.name);
        set_text_if_changed(row.time, row.time_shown, texts.time.text);
    }
    set_hidden(placeholder_, run.state != model::ToolState::Idle);

    // The scroll mark, only when the hops do not all fit.
    const bool scrolls = count > visible_hop_rows_;
    set_hidden(track_, !scrolls);
    set_hidden(thumb_, !scrolls);
    if (scrolls) {
        const int32_t thumb_height = std::max<int32_t>(hop_list_height_ * visible_hop_rows_ / count, kThumbMinHeight);
        const int32_t travel = hop_list_height_ - thumb_height;
        const int32_t last_top = count - visible_hop_rows_;
        const int32_t offset = last_top > 0 ? travel * first / last_top : 0;
        if (thumb_height != thumb_height_shown_ || offset != thumb_top_shown_) {
            lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
            lv_obj_set_pos(thumb_, kThumbX, hop_list_top_ + offset);
            thumb_height_shown_ = thumb_height;
            thumb_top_shown_ = offset;
        }
    }
}

bool ToolScreen::ports_result_shown() const {
    const model::PortRun& run = view_model().tools().ports;
    return run.state == model::ToolState::Running ||
           (run.state != model::ToolState::Idle && run.target == selected_target());
}

void ToolScreen::refresh_ports() {
    const auto& theme = this->theme();
    const model::PortRun& run = view_model().tools().ports;
    std::vector<std::string> lines;
    std::string confirm;
    std::string counts;
    int32_t tone = tools::kToneText;
    if (confirming_) {
        lines.push_back("Asks " + std::to_string(model::common_ports().size()) + " common ports, " +
                        std::to_string(1000 / model::kPortIntervalMs) + " a second");
        confirm = "Only devices you own or may test.\nPress 6 again to scan.";
    } else if (!ports_result_shown()) {
        lines.push_back("Results show here");
        tone = tools::kToneMuted;
    } else {
        lines = pack_port_lines(tools::port_items(run), theme, theme.font(view::FontRole::Mono), kWidth, kAddressLines);
        if (lines.empty() && run.state != model::ToolState::Failed) {
            lines.push_back(run.state == model::ToolState::Running ? "None open yet" : "No open ports");
            tone = tools::kToneMuted;
        }
        counts = tools::ports_counts_text(run);
    }
    for (std::size_t line = 0; line < kAddressLines; ++line) {
        set_text_if_changed(lines_[line], lines_shown_[line], line < lines.size() ? lines[line] : std::string());
    }
    if (tone != lines_tone_) {
        for (lv_obj_t* label : lines_) {
            lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        }
        lines_tone_ = tone;
    }
    set_hidden(confirm_, confirm.empty());
    set_text_if_changed(confirm_, confirm_shown_, confirm);
    set_text_if_changed(server_, server_shown_, counts);
}

void ToolScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    tools::ToneText status;
    switch (kind_) {
        case model::ToolKind::Ping:
            status = tools::ping_status(data.ping);
            break;
        case model::ToolKind::Dns:
            status = tools::dns_status(data.dns);
            break;
        case model::ToolKind::Trace:
            status = tools::trace_status(data.trace);
            break;
        case model::ToolKind::Ports:
            status = tools::ports_status(ports_result_shown() ? data.ports : model::PortRun{});
            break;
        case model::ToolKind::Listen:
        case model::ToolKind::Probe:
        case model::ToolKind::Speed:
            break;  // the Discover and Speed pages', never a tool page's
    }
    if (confirming_) {
        status = {"Nothing sent yet", tools::kToneMuted};
    } else if (!this_running() && any_running()) {
        if (data.ping.state == model::ToolState::Running) {
            status = {"Ping is running", tools::kToneMuted};
        } else if (data.dns.state == model::ToolState::Running) {
            status = {"DNS lookup is running", tools::kToneMuted};
        } else if (data.listen.state == model::ToolState::Running) {
            status = {"Discover is listening", tools::kToneMuted};
        } else if (data.probe.state == model::ToolState::Running) {
            status = {"Discover is probing", tools::kToneMuted};
        } else if (data.ports.state == model::ToolState::Running) {
            status = {"Port scan is running", tools::kToneMuted};
        } else if (data.speed.state == model::ToolState::Running) {
            status = {"Speed test is running", tools::kToneMuted};
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

void ToolScreen::refresh_typing() {
    const auto& theme = this->theme();
    const std::string& draft = view_model().tool_draft();
    // The end of a long target stays in view.
    std::string shown = draft + "_";
    const int32_t room = kWidth - 2 * (theme.metrics.space_sm + theme.metrics.border_width);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    std::size_t cut = 0;
    while (cut < draft.size() && text_width(shown.substr(cut), mono) > room) {
        ++cut;
    }
    if (cut > 0) {
        shown = "…" + shown.substr(cut + 1);
    }
    set_text_if_changed(draft_, draft_shown_, shown);
    set_text_if_changed(count_, count_shown_,
                        std::to_string(draft.size()) + "/" + std::to_string(model::kToolTargetMax) +
                            " · ENTER keeps · ESC cancels");
    std::string note;
    int32_t tone = tools::kToneMuted;
    if (kind_ == model::ToolKind::Ports) {
        if (draft_invalid_) {
            tone = tools::kToneError;
            note = "Only private addresses: 10.x, 172.16-31.x, 192.168.x or 169.254.x";
        } else if (draft.empty() && !view_model().tool_typed_target(kind_).empty()) {
            note = "Empty: ENTER drops the typed target";
        } else {
            note = "A private IPv4 address on your own network";
        }
    } else if (draft_invalid_) {
        tone = tools::kToneError;
        if (kind_ == model::ToolKind::Dns && model::valid_tool_target(model::ToolKind::Ping, draft)) {
            note = "Type a name, not an address";
        } else {
            note = "Letters, digits and dots, with no dot at either end or two in a row";
        }
    } else if (draft.empty()) {
        note = view_model().tool_typed_target(kind_).empty()
                   ? (kind_ == model::ToolKind::Dns ? "A name, such as example.com" : "An IPv4 address or a name")
                   : "Empty: ENTER drops the typed target";
    } else {
        note = "Letters, digits and dots";
    }
    set_text_if_changed(typing_note_, typing_note_shown_, note);
    set_tone_if_changed(typing_note_, typing_note_tone_, tone, theme);
}

void ToolScreen::refresh_detail() {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const model::PortRun& run = view_model().tools().ports;

    // Who it is: the caption of its target, and what Discover or the
    // hotspot knows about the address.
    std::string caption = "host";
    for (const Target& target : ports_targets()) {
        if (target.text == run.target) {
            caption = target.caption;
            break;
        }
    }
    std::string name;
    std::string hardware_address;
    const model::ToolsData& data = view_model().tools();
    for (const model::DiscoverDevice& entry : model::discover_devices(data.listen, data.probe)) {
        if (entry.device.address == run.target) {
            name = entry.device.name;
            hardware_address = entry.device.hardware_address;
            break;
        }
    }
    for (const model::HomeClient& client : view_model().home().clients) {
        if (client.address == run.target) {
            if (name.empty()) {
                name = client.name;
            }
            if (hardware_address.empty()) {
                hardware_address = client.hardware_address;
            }
            break;
        }
    }
    const std::vector<tools::DetailLine> lines = tools::host_detail_lines(run, name, hardware_address);

    const bool caption_changed = caption != detail_caption_shown_;
    if (caption_changed) {
        detail_caption_shown_ = caption;
        view::style::caption(detail_caption_, theme, caption.c_str());
    }
    const bool count_changed =
        set_text_if_changed(detail_count_, detail_count_shown_, std::to_string(run.open.size()) + " open");
    set_text_if_changed(detail_address_, detail_address_shown_, run.target);
    if (caption_changed || count_changed) {
        lv_obj_update_layout(detail_caption_);
        lv_obj_update_layout(detail_count_);
        const int32_t left = kSideMargin + lv_obj_get_width(detail_caption_) + kCaptionGap;
        const int32_t right = kRight - lv_obj_get_width(detail_count_) - kStatusGap;
        lv_obj_set_pos(detail_address_, left, kTargetTop);
        lv_obj_set_size(detail_address_, std::max<int32_t>(right - left, 0),
                        lv_font_get_line_height(theme.font(view::FontRole::Mono)));
    }

    // The right column starts after the widest left text ("8080
    // http-proxy"), at most half the width in.
    int32_t column = 0;
    for (const tools::DetailLine& line : lines) {
        column = std::max(column, text_width(line.left, small));
    }
    column = kSideMargin + std::min(column, kWidth / 2) + kStatusGap;

    // Each line as it fits, measured against the narrower width a scroll
    // mark leaves: its text, else a shorter one; the muted part only when
    // it fits too; an optional line that does not fit is left out.
    struct Drawn {
        std::string left;
        std::string text;
        std::string aside;
        int32_t tone;
        int32_t finding;
    };
    std::vector<Drawn> drawn;
    drawn.reserve(lines.size());
    for (const tools::DetailLine& line : lines) {
        const int32_t room = kHopRight - (line.left.empty() ? kSideMargin + kNoteIndent : column);
        std::string text = line.right.text;
        std::string aside;
        if (text_width(text, small) <= room) {
            if (!line.aside.empty() && text_width(text + kAsideGap + line.aside, small) <= room) {
                aside = line.aside;
            }
        } else {
            bool fits = false;
            for (const std::string& shorter : line.shorter) {
                text = shorter;
                if (text_width(text, small) <= room) {
                    fits = true;
                    break;
                }
            }
            if (!fits && line.optional) {
                continue;
            }
        }
        drawn.push_back({line.left, text, aside, line.right.tone, line.finding});
    }
    detail_findings_.clear();
    for (const Drawn& line : drawn) {
        detail_findings_.push_back(line.finding);
    }

    const auto count = static_cast<int32_t>(drawn.size());
    const int32_t last_top = std::max<int32_t>(count - visible_detail_rows_, 0);
    detail_last_top_ = last_top;
    const int32_t first = std::clamp<int32_t>(view_model().ports_detail(), 0, last_top);
    detail_first_ = first;
    const bool scrolls = count > visible_detail_rows_;
    const int32_t right_end = scrolls ? kHopRight : kRight;
    const int32_t row_height = lv_font_get_line_height(small);
    for (int32_t index = 0; index < visible_detail_rows_; ++index) {
        DetailRow& row = detail_rows_[static_cast<std::size_t>(index)];
        Drawn line{"", "", "", tools::kToneText, -1};
        if (first + index < count) {
            line = drawn[static_cast<std::size_t>(first + index)];
        }
        set_text_if_changed(row.left, row.left_shown, line.left);
        // A muted part after the text needs the label's recolouring; any
        // other text is shown as it is ("#" and all).
        const bool recolor = !line.aside.empty();
        if (recolor != row.recolor) {
            lv_label_set_recolor(row.right, recolor);
            row.recolor = recolor;
            row.right_shown.clear();
        }
        set_text_if_changed(row.right, row.right_shown,
                            recolor ? line.text + kAsideGap + colored(line.aside, theme.colors.muted) : line.text);
        set_tone_if_changed(row.right, row.right_tone, line.tone, theme);
        if (column != detail_column_) {
            lv_obj_set_size(row.left, column - kStatusGap - kSideMargin, row_height);
        }
        // A note (no left text) starts a little in from the left, so it
        // has nearly the whole line.
        const int32_t x = line.left.empty() ? kSideMargin + kNoteIndent : column;
        if (x != row.right_x) {
            lv_obj_set_x(row.right, x);
            row.right_x = x;
        }
        lv_obj_set_size(row.right, std::max<int32_t>(right_end - x, 0), row_height);
    }
    detail_column_ = column;

    // The scroll mark, only when the rows do not all fit.
    set_hidden(detail_track_, !scrolls);
    set_hidden(detail_thumb_, !scrolls);
    if (scrolls) {
        const int32_t list_height = visible_detail_rows_ * row_height;
        const int32_t thumb_height = std::max<int32_t>(list_height * visible_detail_rows_ / count, kThumbMinHeight);
        const int32_t offset = (list_height - thumb_height) * first / last_top;
        if (thumb_height != detail_thumb_height_ || offset != detail_thumb_top_) {
            lv_obj_set_size(detail_thumb_, kThumbWidth, thumb_height);
            lv_obj_set_pos(detail_thumb_, kThumbX, detail_list_top_ + offset);
            detail_thumb_height_ = thumb_height;
            detail_thumb_top_ = offset;
        }
    }
}

void ToolScreen::refresh_finding() {
    const auto& theme = this->theme();
    const model::PortRun& run = view_model().tools().ports;
    const auto index = static_cast<std::size_t>(std::max<int32_t>(view_model().ports_finding(), 0));
    const tools::FindingView view = tools::finding_view(run, index);
    std::array<tools::ToneText, kFindingLines> texts{};
    texts[0] = {view.place, tools::kToneMuted};
    texts[1] = {view.product, tools::kToneText};
    texts[2] = view.severity;
    for (std::size_t sentence = 0; sentence < view.sentences.size() && sentence < 2; ++sentence) {
        texts[3 + sentence] = view.sentences[sentence];
    }
    texts[5] = {view.official, tools::kToneMuted};
    texts[6] = {view.link_text, tools::kToneAccent};
    for (std::size_t line = 0; line < kFindingLines; ++line) {
        set_text_if_changed(finding_lines_[line], finding_shown_[line], texts[line].text);
        if (texts[line].tone != finding_tones_[line]) {
            // The link in the theme's link colour, the rest by tone.
            lv_obj_set_style_text_color(finding_lines_[line],
                                        line == 6 ? theme.colors.accent2 : tone_color(theme, texts[line].tone), 0);
            finding_tones_[line] = texts[line].tone;
        }
    }
    if (view.link != finding_code_shown_) {
        finding_code_shown_ = view.link;
        const bool shown = !view.link.empty() &&
                           lv_qrcode_update(finding_code_, view.link.data(), static_cast<uint32_t>(view.link.size())) ==
                               LV_RESULT_OK;
        set_hidden(finding_code_, !shown);
    }
}

void ToolScreen::refresh() {
    const bool typing = view_model().tool_typing();
    // The host detail goes when its scan is no longer the one shown (the
    // target changed, a scan started) or the page types. The nested
    // refresh this causes draws the page; this one then changes nothing.
    if (detail_open() && (typing || !detail_allowed())) {
        close_detail();
    }
    if (!finding_open() && view_model().ports_finding() != viewmodel::BaseViewModel::kPortsFindingClosed) {
        close_finding();
    }
    const int32_t panel = typing ? 1 : finding_open() ? 3 : detail_open() ? 2 : 0;
    if (panel != shown_panel_) {
        set_hidden(main_panel_, panel != 0);
        set_hidden(typing_panel_, panel != 1);
        if (detail_panel_) {
            set_hidden(detail_panel_, panel != 2);
        }
        if (finding_panel_) {
            set_hidden(finding_panel_, panel != 3);
        }
        shown_panel_ = panel;
    }
    // A confirmation goes when its target is no longer the selected one, or
    // a tool runs.
    if (confirming_ && (typing || any_running() || selected_target() != confirm_target_)) {
        if (confirm_timer_) {
            lv_timer_delete(confirm_timer_);
            confirm_timer_ = nullptr;
        }
        confirming_ = false;
        confirm_target_.clear();
    }
    refresh_keys();
    if (typing) {
        refresh_typing();
        return;
    }
    if (panel == 2) {
        refresh_detail();
        return;
    }
    if (panel == 3) {
        refresh_finding();
        return;
    }
    refresh_target();
    switch (kind_) {
        case model::ToolKind::Ping:
            refresh_ping();
            break;
        case model::ToolKind::Dns:
            refresh_dns();
            break;
        case model::ToolKind::Trace:
            refresh_trace();
            break;
        case model::ToolKind::Ports:
            refresh_ports();
            break;
        case model::ToolKind::Listen:
        case model::ToolKind::Probe:
        case model::ToolKind::Speed:
            break;  // the Discover and Speed pages', never a tool page's
    }
    refresh_status();
}

void ToolScreen::start_from_command() {
    // Always a first press: a confirmation already waiting is dropped, so a
    // command never gives the second 6, and a running tool is never stopped.
    if (detail_open()) {
        close_detail();
    }
    cancel_confirm();
    if (view_model().tool_typing() || any_running()) {
        return;
    }
    run_or_stop();
}

void ToolScreen::start_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ToolScreen*>(lv_observer_get_user_data(observer));
    if (screen && screen->view_model().take_tool_start(screen->kind_)) {
        screen->start_from_command();
    }
}

void ToolScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ToolScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
