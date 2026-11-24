/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

// The place of `kind` in the tables kept for each tool.
std::size_t tool_slot(model::ToolKind kind) {
    const auto slot = static_cast<std::size_t>(kind);
    return slot < model::kToolKindCount ? slot : 0;
}

// Counts one more on a counter subject, starting again at 1 after the
// largest value.
void bump(reactive::IntSubject& counter) {
    const int32_t count = counter.value();
    counter.set(count == std::numeric_limits<int32_t>::max() ? 1 : count + 1);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      current_page_subject_(page_to_int(model_.current_page())),
      counter_subject_(0),
      quit_requested_subject_(false),
      theme_request_subject_(0),
      settings_theme_selection_subject_(kSettingsThemeFollowsActive),
      screen_dimming_subject_(true),
      keep_mode_subject_(false),
      status_scroll_subject_(0),
      devices_selection_subject_(0),
      block_request_subject_(0),
      modes_selection_subject_(kModesFollowsActive),
      mode_request_subject_(0),
      vpn_file_subject_(0),
      hotspot_subject_(0),
      hotspot_request_subject_(0),
      hotspot_typing_subject_(false),
      tools_selection_subject_(0),
      tool_target_subject_(0),
      tool_typing_subject_(false),
      tool_request_subject_(0),
      tool_start_subject_(0),
      tools_subject_(0),
      trace_scroll_subject_(kTraceFollow),
      ports_detail_subject_(kPortsDetailClosed),
      ports_finding_subject_(kPortsFindingClosed),
      wifi_scroll_subject_(0),
      wifi_scan_request_subject_(0),
      discover_scroll_subject_(0),
      watch_subject_(0),
      watch_request_subject_(0),
      watch_selection_subject_(0),
      watch_detail_subject_(false),
      talk_subject_(0),
      talk_device_subject_(0),
      talk_scroll_subject_(0),
      trap_subject_(0),
      trap_request_subject_(0),
      trap_selection_subject_(0),
      trap_detail_subject_(false),
      health_selection_subject_(0),
      health_detail_subject_(false),
      leak_selection_subject_(0),
      leak_detail_subject_(false),
      signin_detail_subject_(false),
      lan_subject_(0),
      lan_request_subject_(0),
      lan_choice_subject_(0),
      survey_subject_(0),
      survey_request_subject_(0),
      survey_view_subject_(0),
      report_request_subject_(0),
      map_selection_subject_(kMapRouter),
      home_selection_subject_(0),
      home_data_subject_(0),
      key_test_selection_subject_(0),
      key_test_picked_subject_(-1),
      key_test_last_key_subject_("-"),
      key_test_typing_subject_(false) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::counter_subject() {
    return counter_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

lv_subject_t* BaseViewModel::theme_request_subject() {
    return theme_request_subject_.native();
}

int32_t BaseViewModel::requested_theme() const {
    return requested_theme_;
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

std::size_t BaseViewModel::page_depth() const {
    return model_.page_depth();
}

void BaseViewModel::show_page(model::AppPage page) {
    tool_start_waiting_ = false;
    model_.reset_pages(page);
    publish_all();
}

bool BaseViewModel::open_page(model::AppPage page) {
    if (!model_.open_page(page)) {
        return false;
    }
    tool_start_waiting_ = false;
    publish_all();
    return true;
}

bool BaseViewModel::go_back() {
    if (!model_.go_back()) {
        return false;
    }
    tool_start_waiting_ = false;
    publish_all();
    return true;
}

void BaseViewModel::increment_counter() {
    counter_subject_.set(counter_subject_.value() + 1);
}

void BaseViewModel::decrement_counter() {
    const auto next = counter_subject_.value() - 1;
    counter_subject_.set(next < 0 ? 0 : next);
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::request_theme(int32_t row) {
    // Stored first: the observers run inside set() and read it from there.
    requested_theme_ = row;
    theme_request_subject_.set(theme_request_subject_.value() + 1);
}

lv_subject_t* BaseViewModel::settings_theme_selection_subject() {
    return settings_theme_selection_subject_.native();
}

int32_t BaseViewModel::settings_theme_selection() const {
    return settings_theme_selection_subject_.value();
}

void BaseViewModel::select_settings_theme(int32_t row) {
    settings_theme_selection_subject_.set(row);
}

bool BaseViewModel::open_settings() {
    settings_theme_selection_subject_.set(kSettingsThemeFollowsActive);
    return open_page(model::AppPage::Settings);
}

lv_subject_t* BaseViewModel::screen_dimming_subject() {
    return screen_dimming_subject_.native();
}

bool BaseViewModel::screen_dimming() const {
    return screen_dimming_subject_.value();
}

void BaseViewModel::set_screen_dimming(bool on) {
    if (on != screen_dimming_subject_.value()) {
        screen_dimming_subject_.set(on);
    }
}

lv_subject_t* BaseViewModel::keep_mode_subject() {
    return keep_mode_subject_.native();
}

bool BaseViewModel::keep_mode() const {
    return keep_mode_subject_.value();
}

void BaseViewModel::set_keep_mode(bool on) {
    if (on != keep_mode_subject_.value()) {
        keep_mode_subject_.set(on);
    }
}

lv_subject_t* BaseViewModel::status_scroll_subject() {
    return status_scroll_subject_.native();
}

int32_t BaseViewModel::status_scroll() const {
    return status_scroll_subject_.value();
}

void BaseViewModel::set_status_scroll(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != status_scroll_subject_.value()) {
        status_scroll_subject_.set(next);
    }
}

bool BaseViewModel::open_status() {
    set_status_scroll(0);
    return open_page(model::AppPage::Status);
}

lv_subject_t* BaseViewModel::devices_selection_subject() {
    return devices_selection_subject_.native();
}

int32_t BaseViewModel::devices_selection() const {
    return devices_selection_subject_.value();
}

void BaseViewModel::set_devices_selection(int32_t index) {
    const int32_t next = std::max<int32_t>(index, 0);
    if (next != devices_selection_subject_.value()) {
        devices_selection_subject_.set(next);
    }
}

bool BaseViewModel::open_devices() {
    set_devices_selection(home_selection());
    return open_page(model::AppPage::Devices);
}

lv_subject_t* BaseViewModel::modes_selection_subject() {
    return modes_selection_subject_.native();
}

int32_t BaseViewModel::modes_selection() const {
    return modes_selection_subject_.value();
}

void BaseViewModel::select_mode_row(int32_t row) {
    if (row != modes_selection_subject_.value()) {
        modes_selection_subject_.set(row);
    }
}

bool BaseViewModel::open_modes() {
    select_mode_row(kModesFollowsActive);
    return open_page(model::AppPage::Modes);
}

void BaseViewModel::request_block(const std::string& address, bool block) {
    // Stored first: the observers run inside set() and read it from there.
    requested_block_address_ = address;
    requested_block_ = block;
    bump(block_request_subject_);
}

lv_subject_t* BaseViewModel::block_request_subject() {
    return block_request_subject_.native();
}

const std::string& BaseViewModel::requested_block_address() const {
    return requested_block_address_;
}

bool BaseViewModel::requested_block() const {
    return requested_block_;
}

void BaseViewModel::request_mode(model::NetMode mode) {
    // Stored first: the observers run inside set() and read it from there.
    requested_mode_ = mode;
    requested_vpn_off_ = false;
    const int32_t count = mode_request_subject_.value();
    mode_request_subject_.set(count == std::numeric_limits<int32_t>::max() ? 1 : count + 1);
}

void BaseViewModel::request_vpn_off() {
    requested_mode_ = model::NetMode::Vpn;
    requested_vpn_off_ = true;
    const int32_t count = mode_request_subject_.value();
    mode_request_subject_.set(count == std::numeric_limits<int32_t>::max() ? 1 : count + 1);
}

bool BaseViewModel::requested_vpn_off() const {
    return requested_vpn_off_;
}

lv_subject_t* BaseViewModel::mode_request_subject() {
    return mode_request_subject_.native();
}

model::NetMode BaseViewModel::requested_mode() const {
    return requested_mode_;
}

std::string BaseViewModel::vpn_file() const {
    const std::vector<std::string>& files = home().vpn.files;
    if (std::find(files.begin(), files.end(), vpn_file_) != files.end()) {
        return vpn_file_;
    }
    return files.empty() ? std::string() : files.front();
}

void BaseViewModel::step_vpn_file(int32_t step) {
    const std::vector<std::string>& files = home().vpn.files;
    if (files.size() < 2 || step == 0) {
        return;
    }
    const auto current = std::find(files.begin(), files.end(), vpn_file());
    const auto count = static_cast<int32_t>(files.size());
    const auto index = static_cast<int32_t>(current - files.begin());
    const int32_t next = ((index + step) % count + count) % count;
    vpn_file_ = files[static_cast<std::size_t>(next)];
    bump(vpn_file_subject_);
}

bool BaseViewModel::select_vpn_file(const std::string& name) {
    const std::vector<std::string>& files = home().vpn.files;
    if (std::find(files.begin(), files.end(), name) == files.end()) {
        return false;
    }
    if (name != vpn_file()) {
        vpn_file_ = name;
        bump(vpn_file_subject_);
    }
    return true;
}

lv_subject_t* BaseViewModel::vpn_file_subject() {
    return vpn_file_subject_.native();
}

const std::string& BaseViewModel::hotspot_password() const {
    return hotspot_password_;
}

void BaseViewModel::set_hotspot_password(const std::string& password) {
    if (password == hotspot_password_) {
        return;
    }
    hotspot_password_ = password;
    bump(hotspot_subject_);
}

const std::string& BaseViewModel::hotspot_name() const {
    return hotspot_name_;
}

void BaseViewModel::set_hotspot_name(const std::string& name) {
    if (name == hotspot_name_) {
        return;
    }
    hotspot_name_ = name;
    bump(hotspot_subject_);
}

const std::string& BaseViewModel::default_hotspot_name() const {
    return default_hotspot_name_;
}

void BaseViewModel::set_default_hotspot_name(const std::string& name) {
    default_hotspot_name_ = name;
}

lv_subject_t* BaseViewModel::hotspot_subject() {
    return hotspot_subject_.native();
}

bool BaseViewModel::open_hotspot() {
    set_hotspot_typing(false);
    return open_page(model::AppPage::Hotspot);
}

void BaseViewModel::request_hotspot_rename(const std::string& name) {
    // Stored first: the observers run inside set() and read it from there.
    requested_hotspot_change_ = HotspotChange::Rename;
    requested_hotspot_name_ = name;
    bump(hotspot_request_subject_);
}

void BaseViewModel::request_new_hotspot_password() {
    requested_hotspot_change_ = HotspotChange::NewPassword;
    requested_hotspot_name_.clear();
    bump(hotspot_request_subject_);
}

lv_subject_t* BaseViewModel::hotspot_request_subject() {
    return hotspot_request_subject_.native();
}

HotspotChange BaseViewModel::requested_hotspot_change() const {
    return requested_hotspot_change_;
}

const std::string& BaseViewModel::requested_hotspot_name() const {
    return requested_hotspot_name_;
}

lv_subject_t* BaseViewModel::hotspot_typing_subject() {
    return hotspot_typing_subject_.native();
}

bool BaseViewModel::hotspot_typing() const {
    return hotspot_typing_subject_.value();
}

void BaseViewModel::set_hotspot_typing(bool typing) {
    if (typing != hotspot_typing_subject_.value()) {
        hotspot_typing_subject_.set(typing);
    }
}

const std::string& BaseViewModel::hotspot_draft() const {
    return hotspot_draft_;
}

void BaseViewModel::set_hotspot_draft(const std::string& draft) {
    hotspot_draft_ = draft;
}

lv_subject_t* BaseViewModel::tools_selection_subject() {
    return tools_selection_subject_.native();
}

int32_t BaseViewModel::tools_selection() const {
    return tools_selection_subject_.value();
}

void BaseViewModel::select_tools_row(int32_t row) {
    if (row != tools_selection_subject_.value()) {
        tools_selection_subject_.set(row);
    }
}

bool BaseViewModel::open_tools() {
    return open_page(model::AppPage::Tools);
}

bool BaseViewModel::open_tool(model::ToolKind kind) {
    set_tool_typing(false);
    set_ports_detail(kPortsDetailClosed);
    set_ports_finding(kPortsFindingClosed);
    switch (kind) {
        case model::ToolKind::Ping:
            break;
        case model::ToolKind::Dns:
            return open_page(model::AppPage::Dns);
        case model::ToolKind::Trace:
            return open_page(model::AppPage::Trace);
        case model::ToolKind::Ports:
            return open_page(model::AppPage::Ports);
        case model::ToolKind::Speed:
            return open_page(model::AppPage::Speed);
        case model::ToolKind::Listen:
        case model::ToolKind::Probe:
            return open_discover();
        case model::ToolKind::Health:
            return open_health();
        case model::ToolKind::Leak:
            return open_leak();
        case model::ToolKind::Signin:
            return open_signin();
    }
    return open_page(model::AppPage::Ping);
}

const std::string& BaseViewModel::speed_server() const {
    return speed_server_;
}

void BaseViewModel::set_speed_server(const std::string& server) {
    speed_server_ = server;
}

int32_t BaseViewModel::tool_target(model::ToolKind kind) const {
    return tool_targets_[tool_slot(kind)];
}

void BaseViewModel::set_tool_target(model::ToolKind kind, int32_t index) {
    int32_t& target = tool_targets_[tool_slot(kind)];
    if (index != target) {
        target = index;
        bump(tool_target_subject_);
    }
}

const std::string& BaseViewModel::tool_typed_target(model::ToolKind kind) const {
    return tool_typed_targets_[tool_slot(kind)];
}

void BaseViewModel::set_tool_typed_target(model::ToolKind kind, const std::string& target) {
    std::string& typed = tool_typed_targets_[tool_slot(kind)];
    if (target != typed) {
        typed = target;
        bump(tool_target_subject_);
    }
}

lv_subject_t* BaseViewModel::tool_target_subject() {
    return tool_target_subject_.native();
}

lv_subject_t* BaseViewModel::tool_typing_subject() {
    return tool_typing_subject_.native();
}

bool BaseViewModel::tool_typing() const {
    return tool_typing_subject_.value();
}

void BaseViewModel::set_tool_typing(bool typing) {
    if (typing != tool_typing_subject_.value()) {
        tool_typing_subject_.set(typing);
    }
}

const std::string& BaseViewModel::tool_draft() const {
    return tool_draft_;
}

void BaseViewModel::set_tool_draft(const std::string& draft) {
    tool_draft_ = draft;
}

void BaseViewModel::request_tool_run(model::ToolKind kind, const std::string& target) {
    // Stored first: the observers run inside set() and read it from there.
    requested_tool_stop_ = false;
    requested_capture_save_ = false;
    requested_tool_ = kind;
    requested_tool_target_ = target;
    bump(tool_request_subject_);
}

void BaseViewModel::request_tool_stop() {
    requested_tool_stop_ = true;
    requested_capture_save_ = false;
    requested_tool_target_.clear();
    bump(tool_request_subject_);
}

void BaseViewModel::request_capture_save() {
    requested_tool_stop_ = false;
    requested_capture_save_ = true;
    requested_tool_target_.clear();
    bump(tool_request_subject_);
}

bool BaseViewModel::requested_capture_save() const {
    return requested_capture_save_;
}

lv_subject_t* BaseViewModel::tool_request_subject() {
    return tool_request_subject_.native();
}

void BaseViewModel::request_tool_start(model::ToolKind kind) {
    // Stored first: the page that takes it may be listening already.
    tool_start_waiting_ = true;
    tool_start_kind_ = kind;
    bump(tool_start_subject_);
}

bool BaseViewModel::take_tool_start(model::ToolKind kind) {
    if (!tool_start_waiting_ || tool_start_kind_ != kind) {
        return false;
    }
    tool_start_waiting_ = false;
    return true;
}

lv_subject_t* BaseViewModel::tool_start_subject() {
    return tool_start_subject_.native();
}

bool BaseViewModel::requested_tool_stop() const {
    return requested_tool_stop_;
}

model::ToolKind BaseViewModel::requested_tool() const {
    return requested_tool_;
}

const std::string& BaseViewModel::requested_tool_target() const {
    return requested_tool_target_;
}

const model::ToolsData& BaseViewModel::tools() const {
    return tools_;
}

bool BaseViewModel::set_tools(const model::ToolsData& data) {
    if (data == tools_) {
        return false;
    }
    tools_ = data;
    bump(tools_subject_);
    return true;
}

lv_subject_t* BaseViewModel::tools_subject() {
    return tools_subject_.native();
}

lv_subject_t* BaseViewModel::trace_scroll_subject() {
    return trace_scroll_subject_.native();
}

int32_t BaseViewModel::trace_scroll() const {
    return trace_scroll_subject_.value();
}

void BaseViewModel::set_trace_scroll(int32_t row) {
    if (row != trace_scroll_subject_.value()) {
        trace_scroll_subject_.set(row);
    }
}

lv_subject_t* BaseViewModel::ports_detail_subject() {
    return ports_detail_subject_.native();
}

int32_t BaseViewModel::ports_detail() const {
    return ports_detail_subject_.value();
}

void BaseViewModel::set_ports_detail(int32_t row) {
    if (row != ports_detail_subject_.value()) {
        ports_detail_subject_.set(row);
    }
}

lv_subject_t* BaseViewModel::ports_finding_subject() {
    return ports_finding_subject_.native();
}

int32_t BaseViewModel::ports_finding() const {
    return ports_finding_subject_.value();
}

void BaseViewModel::set_ports_finding(int32_t index) {
    if (index != ports_finding_subject_.value()) {
        ports_finding_subject_.set(index);
    }
}

bool BaseViewModel::open_wifi() {
    set_wifi_scroll(0);
    return open_page(model::AppPage::Wifi);
}

lv_subject_t* BaseViewModel::wifi_scroll_subject() {
    return wifi_scroll_subject_.native();
}

int32_t BaseViewModel::wifi_scroll() const {
    return wifi_scroll_subject_.value();
}

void BaseViewModel::set_wifi_scroll(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != wifi_scroll_subject_.value()) {
        wifi_scroll_subject_.set(next);
    }
}

void BaseViewModel::request_wifi_scan() {
    bump(wifi_scan_request_subject_);
}

lv_subject_t* BaseViewModel::wifi_scan_request_subject() {
    return wifi_scan_request_subject_.native();
}

bool BaseViewModel::open_watch() {
    set_watch_selection(0);
    set_watch_detail(false);
    return open_page(model::AppPage::Watch);
}

const model::WatchData& BaseViewModel::watch() const {
    return watch_;
}

bool BaseViewModel::set_watch(const model::WatchData& data) {
    if (data == watch_) {
        return false;
    }
    watch_ = data;
    bump(watch_subject_);
    return true;
}

lv_subject_t* BaseViewModel::watch_subject() {
    return watch_subject_.native();
}

void BaseViewModel::request_watch(WatchRequest request, const std::string& hardware) {
    requested_watch_ = request;
    requested_watch_hardware_ = request == WatchRequest::MarkKnown ? hardware : std::string();
    bump(watch_request_subject_);
}

lv_subject_t* BaseViewModel::watch_request_subject() {
    return watch_request_subject_.native();
}

BaseViewModel::WatchRequest BaseViewModel::requested_watch() const {
    return requested_watch_;
}

const std::string& BaseViewModel::requested_watch_hardware() const {
    return requested_watch_hardware_;
}

lv_subject_t* BaseViewModel::watch_selection_subject() {
    return watch_selection_subject_.native();
}

int32_t BaseViewModel::watch_selection() const {
    return watch_selection_subject_.value();
}

void BaseViewModel::set_watch_selection(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != watch_selection_subject_.value()) {
        watch_selection_subject_.set(next);
    }
}

lv_subject_t* BaseViewModel::watch_detail_subject() {
    return watch_detail_subject_.native();
}

bool BaseViewModel::watch_detail() const {
    return watch_detail_subject_.value();
}

void BaseViewModel::set_watch_detail(bool open) {
    if (open != watch_detail_subject_.value()) {
        watch_detail_subject_.set(open);
    }
}

bool BaseViewModel::open_talk(const std::string& hardware) {
    std::string device = hardware;
    if (device.empty()) {
        const auto& clients = home().clients;
        if (!clients.empty()) {
            const auto index = static_cast<std::size_t>(
                std::clamp<int32_t>(devices_selection(), 0, static_cast<int32_t>(clients.size()) - 1));
            device = clients[index].hardware_address;
        }
    }
    set_talk_device(device);
    set_talk_scroll(0);
    return open_page(model::AppPage::Talk);
}

const model::TalkRun& BaseViewModel::talk() const {
    return talk_;
}

bool BaseViewModel::set_talk(const model::TalkRun& run) {
    if (run == talk_) {
        return false;
    }
    talk_ = run;
    bump(talk_subject_);
    return true;
}

lv_subject_t* BaseViewModel::talk_subject() {
    return talk_subject_.native();
}

const std::string& BaseViewModel::talk_device() const {
    return talk_device_;
}

void BaseViewModel::set_talk_device(const std::string& hardware) {
    if (hardware != talk_device_) {
        talk_device_ = hardware;
        bump(talk_device_subject_);
    }
}

lv_subject_t* BaseViewModel::talk_device_subject() {
    return talk_device_subject_.native();
}

lv_subject_t* BaseViewModel::talk_scroll_subject() {
    return talk_scroll_subject_.native();
}

int32_t BaseViewModel::talk_scroll() const {
    return talk_scroll_subject_.value();
}

void BaseViewModel::set_talk_scroll(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != talk_scroll_subject_.value()) {
        talk_scroll_subject_.set(next);
    }
}

bool BaseViewModel::open_trap() {
    set_trap_selection(0);
    set_trap_detail(false);
    return open_page(model::AppPage::Trap);
}

const model::TrapRun& BaseViewModel::trap() const {
    return trap_;
}

bool BaseViewModel::set_trap(const model::TrapRun& run) {
    if (run == trap_) {
        return false;
    }
    trap_ = run;
    bump(trap_subject_);
    return true;
}

lv_subject_t* BaseViewModel::trap_subject() {
    return trap_subject_.native();
}

void BaseViewModel::request_report() {
    bump(report_request_subject_);
}

lv_subject_t* BaseViewModel::report_request_subject() {
    return report_request_subject_.native();
}

void BaseViewModel::request_trap(TrapRequest request) {
    requested_trap_ = request;
    bump(trap_request_subject_);
}

lv_subject_t* BaseViewModel::trap_request_subject() {
    return trap_request_subject_.native();
}

BaseViewModel::TrapRequest BaseViewModel::requested_trap() const {
    return requested_trap_;
}

lv_subject_t* BaseViewModel::trap_selection_subject() {
    return trap_selection_subject_.native();
}

int32_t BaseViewModel::trap_selection() const {
    return trap_selection_subject_.value();
}

void BaseViewModel::set_trap_selection(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != trap_selection_subject_.value()) {
        trap_selection_subject_.set(next);
    }
}

lv_subject_t* BaseViewModel::trap_detail_subject() {
    return trap_detail_subject_.native();
}

bool BaseViewModel::trap_detail() const {
    return trap_detail_subject_.value();
}

void BaseViewModel::set_trap_detail(bool open) {
    if (open != trap_detail_subject_.value()) {
        trap_detail_subject_.set(open);
    }
}

bool BaseViewModel::open_health() {
    set_health_selection(0);
    set_health_detail(false);
    return open_page(model::AppPage::Health);
}

lv_subject_t* BaseViewModel::health_selection_subject() {
    return health_selection_subject_.native();
}

int32_t BaseViewModel::health_selection() const {
    return health_selection_subject_.value();
}

void BaseViewModel::set_health_selection(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != health_selection_subject_.value()) {
        health_selection_subject_.set(next);
    }
}

lv_subject_t* BaseViewModel::health_detail_subject() {
    return health_detail_subject_.native();
}

bool BaseViewModel::health_detail() const {
    return health_detail_subject_.value();
}

void BaseViewModel::set_health_detail(bool open) {
    if (open != health_detail_subject_.value()) {
        health_detail_subject_.set(open);
    }
}

bool BaseViewModel::open_leak() {
    set_leak_selection(0);
    set_leak_detail(false);
    return open_page(model::AppPage::Leak);
}

lv_subject_t* BaseViewModel::leak_selection_subject() {
    return leak_selection_subject_.native();
}

int32_t BaseViewModel::leak_selection() const {
    return leak_selection_subject_.value();
}

void BaseViewModel::set_leak_selection(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != leak_selection_subject_.value()) {
        leak_selection_subject_.set(next);
    }
}

lv_subject_t* BaseViewModel::leak_detail_subject() {
    return leak_detail_subject_.native();
}

bool BaseViewModel::leak_detail() const {
    return leak_detail_subject_.value();
}

void BaseViewModel::set_leak_detail(bool open) {
    if (open != leak_detail_subject_.value()) {
        leak_detail_subject_.set(open);
    }
}

bool BaseViewModel::open_signin() {
    set_signin_detail(false);
    if (!open_page(model::AppPage::Signin)) {
        return false;  // nothing is sent for a page that did not open
    }
    // One check as the page opens, unless a tool runs (key 6 checks again).
    if (!model::tool_running(tools())) {
        request_tool_run(model::ToolKind::Signin, model::kSigninCheckHost);
    }
    return true;
}

bool BaseViewModel::open_lan() {
    return open_page(model::AppPage::Lan);
}

bool BaseViewModel::open_survey() {
    return open_page(model::AppPage::Survey);
}

const model::SurveyRun& BaseViewModel::survey() const {
    return survey_;
}

bool BaseViewModel::set_survey(const model::SurveyRun& run) {
    if (run == survey_) {
        return false;
    }
    survey_ = run;
    bump(survey_subject_);
    return true;
}

lv_subject_t* BaseViewModel::survey_subject() {
    return survey_subject_.native();
}

void BaseViewModel::request_survey(SurveyRequest request) {
    requested_survey_ = request;
    bump(survey_request_subject_);
}

lv_subject_t* BaseViewModel::survey_request_subject() {
    return survey_request_subject_.native();
}

BaseViewModel::SurveyRequest BaseViewModel::requested_survey() const {
    return requested_survey_;
}

lv_subject_t* BaseViewModel::survey_view_subject() {
    return survey_view_subject_.native();
}

bool BaseViewModel::survey_chart() const {
    return survey_chart_;
}

void BaseViewModel::set_survey_chart(bool chart) {
    if (chart != survey_chart_) {
        survey_chart_ = chart;
        bump(survey_view_subject_);
    }
}

int32_t BaseViewModel::survey_scroll() const {
    return survey_scroll_;
}

void BaseViewModel::set_survey_scroll(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != survey_scroll_) {
        survey_scroll_ = next;
        bump(survey_view_subject_);
    }
}

const model::LanRun& BaseViewModel::lan() const {
    return lan_;
}

bool BaseViewModel::set_lan(const model::LanRun& run) {
    if (run == lan_) {
        return false;
    }
    lan_ = run;
    bump(lan_subject_);
    return true;
}

lv_subject_t* BaseViewModel::lan_subject() {
    return lan_subject_.native();
}

void BaseViewModel::request_lan(LanRequest request, const std::string& target) {
    requested_lan_ = request;
    requested_lan_target_ = target;
    bump(lan_request_subject_);
}

lv_subject_t* BaseViewModel::lan_request_subject() {
    return lan_request_subject_.native();
}

BaseViewModel::LanRequest BaseViewModel::requested_lan() const {
    return requested_lan_;
}

const std::string& BaseViewModel::requested_lan_target() const {
    return requested_lan_target_;
}

lv_subject_t* BaseViewModel::lan_choice_subject() {
    return lan_choice_subject_.native();
}

bool BaseViewModel::lan_server_side() const {
    return lan_server_side_;
}

void BaseViewModel::set_lan_server_side(bool server) {
    if (server != lan_server_side_) {
        lan_server_side_ = server;
        bump(lan_choice_subject_);
    }
}

int32_t BaseViewModel::lan_target() const {
    return lan_target_;
}

void BaseViewModel::set_lan_target(int32_t index) {
    const int32_t next = std::max<int32_t>(index, 0);
    if (next != lan_target_) {
        lan_target_ = next;
        bump(lan_choice_subject_);
    }
}

const std::string& BaseViewModel::lan_typed_target() const {
    return lan_typed_target_;
}

void BaseViewModel::set_lan_typed_target(const std::string& target) {
    if (target != lan_typed_target_) {
        lan_typed_target_ = target;
        bump(lan_choice_subject_);
    }
}

int32_t BaseViewModel::lan_place() const {
    return lan_place_;
}

void BaseViewModel::set_lan_place(int32_t index) {
    const int32_t next = std::max<int32_t>(index, 0);
    if (next != lan_place_) {
        lan_place_ = next;
        bump(lan_choice_subject_);
    }
}

lv_subject_t* BaseViewModel::signin_detail_subject() {
    return signin_detail_subject_.native();
}

bool BaseViewModel::signin_detail() const {
    return signin_detail_subject_.value();
}

void BaseViewModel::set_signin_detail(bool open) {
    if (open != signin_detail_subject_.value()) {
        signin_detail_subject_.set(open);
    }
}

bool BaseViewModel::open_discover() {
    set_discover_scroll(0);
    return open_page(model::AppPage::Discover);
}

lv_subject_t* BaseViewModel::discover_scroll_subject() {
    return discover_scroll_subject_.native();
}

int32_t BaseViewModel::discover_scroll() const {
    return discover_scroll_subject_.value();
}

void BaseViewModel::set_discover_scroll(int32_t row) {
    const int32_t next = std::max<int32_t>(row, 0);
    if (next != discover_scroll_subject_.value()) {
        discover_scroll_subject_.set(next);
    }
}

bool BaseViewModel::open_map() {
    set_map_selection(kMapRouter);
    map_slots_ = {};
    return open_page(model::AppPage::Map);
}

lv_subject_t* BaseViewModel::map_selection_subject() {
    return map_selection_subject_.native();
}

int32_t BaseViewModel::map_selection() const {
    return map_selection_subject_.value();
}

void BaseViewModel::set_map_selection(int32_t node) {
    if (node != map_selection_subject_.value()) {
        map_selection_subject_.set(node);
    }
}

const std::array<std::string, kMapSlots>& BaseViewModel::map_slots() const {
    return map_slots_;
}

void BaseViewModel::set_map_slots(const std::array<std::string, kMapSlots>& slots) {
    map_slots_ = slots;
}

const model::HomeData& BaseViewModel::home() const {
    return model_.home();
}

bool BaseViewModel::set_home_data(const model::HomeData& data) {
    if (data == model_.home()) {
        return false;
    }
    model_.set_home(data);

    // The selection stays on a client that is shown. Set before the count
    // below, so the page marks a client that exists when it updates.
    const auto shown = static_cast<int32_t>(
        std::min<std::size_t>(data.clients.size(), static_cast<std::size_t>(kHomeClientRows)));
    const int32_t last = std::max<int32_t>(shown - 1, 0);
    if (home_selection_subject_.value() > last) {
        home_selection_subject_.set(last);
    }

    // Starts again from 0 instead of overflowing; it still is a change.
    const int32_t count = home_data_subject_.value();
    home_data_subject_.set(count == std::numeric_limits<int32_t>::max() ? 0 : count + 1);
    return true;
}

lv_subject_t* BaseViewModel::home_data_subject() {
    return home_data_subject_.native();
}

lv_subject_t* BaseViewModel::home_selection_subject() {
    return home_selection_subject_.native();
}

int32_t BaseViewModel::home_selection() const {
    return home_selection_subject_.value();
}

void BaseViewModel::home_select(int32_t delta) {
    const auto shown = static_cast<int32_t>(
        std::min<std::size_t>(model_.home().clients.size(), static_cast<std::size_t>(kHomeClientRows)));
    if (shown == 0) {
        return;
    }

    const int32_t current = home_selection_subject_.value();
    const int32_t next = std::clamp<int32_t>(current + delta, 0, shown - 1);
    if (next != current) {
        home_selection_subject_.set(next);
    }
}

lv_subject_t* BaseViewModel::key_test_selection_subject() {
    return key_test_selection_subject_.native();
}

lv_subject_t* BaseViewModel::key_test_picked_subject() {
    return key_test_picked_subject_.native();
}

lv_subject_t* BaseViewModel::key_test_last_key_subject() {
    return key_test_last_key_subject_.native();
}

lv_subject_t* BaseViewModel::key_test_typing_subject() {
    return key_test_typing_subject_.native();
}

bool BaseViewModel::key_test_typing() const {
    return key_test_typing_subject_.value();
}

void BaseViewModel::key_test_move(int32_t delta) {
    const int32_t next = std::clamp<int32_t>(key_test_selection_subject_.value() + delta,
                                             0,
                                             kKeyTestRowCount - 1);
    key_test_selection_subject_.set(next);
}

void BaseViewModel::key_test_pick() {
    key_test_picked_subject_.set(key_test_selection_subject_.value());
}

void BaseViewModel::key_test_note_key(const char* name) {
    key_test_last_key_subject_.set(name);
}

void BaseViewModel::key_test_set_typing(bool typing) {
    key_test_typing_subject_.set(typing);
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

} // namespace viewmodel
