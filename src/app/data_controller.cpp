/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_controller.h"

#include "application_config.h"
#include "base_viewmodel.h"
#include "firewall.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <random>
#include <string>
#include <utility>

namespace app {
namespace {

// How often the UI thread looks whether the reader has a new block: four
// times a second. A look that finds nothing new does nothing else.
constexpr uint32_t kPollPeriodMs = 250;

// A mode's name in lowercase, as the log writes it.
std::string lowercase_mode_name(model::NetMode mode) {
    std::string name = model::net_mode_name(mode);
    for (char& character : name) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return name;
}

// The charge as the log and the comparison use it: unknown for any negative
// number, at most 100 otherwise, as the page shows it.
int32_t logged_charge(int32_t percent) {
    return percent < 0 ? model::kBatteryPercentUnknown : std::min<int32_t>(percent, 100);
}

} // namespace

std::string random_hotspot_password() {
    constexpr char kCharacters[] = "abcdefghjkmnpqrstuvwxyz23456789";
    constexpr std::size_t kLength = 12;
    std::random_device random;
    std::uniform_int_distribution<std::size_t> pick(0, sizeof(kCharacters) - 2);
    std::string password;
    for (std::size_t index = 0; index < kLength; ++index) {
        password += kCharacters[pick(random)];
    }
    return password;
}

DataController::DataController(viewmodel::BaseViewModel& view_model,
                               std::unique_ptr<platform::DataSource> source)
    : view_model_(view_model),
      reader_(std::move(source)) {
    // LVGL calls the observer once right away, so the reader knows the
    // first page before the first reading.
    page_observer_ = lv_subject_add_observer(view_model_.current_page_subject(), page_observer_cb, this);
    // LVGL calls this observer once right away too; the count it holds then
    // is no request.
    mode_requests_seen_ = lv_subject_get_int(view_model_.mode_request_subject());
    mode_observer_ = lv_subject_add_observer(view_model_.mode_request_subject(), mode_request_cb, this);
    hotspot_requests_seen_ = lv_subject_get_int(view_model_.hotspot_request_subject());
    hotspot_observer_ = lv_subject_add_observer(view_model_.hotspot_request_subject(), hotspot_request_cb, this);
    block_requests_seen_ = lv_subject_get_int(view_model_.block_request_subject());
    block_observer_ = lv_subject_add_observer(view_model_.block_request_subject(), block_request_cb, this);
    wifi_scan_requests_seen_ = lv_subject_get_int(view_model_.wifi_scan_request_subject());
    wifi_scan_observer_ =
        lv_subject_add_observer(view_model_.wifi_scan_request_subject(), wifi_scan_request_cb, this);
    // Called once right away too: the reader gets the setting at start.
    keep_mode_observer_ = lv_subject_add_observer(view_model_.keep_mode_subject(), keep_mode_cb, this);
}

DataController::~DataController() {
    stop();
    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (mode_observer_) {
        lv_observer_remove(mode_observer_);
        mode_observer_ = nullptr;
    }
    if (keep_mode_observer_) {
        lv_observer_remove(keep_mode_observer_);
        keep_mode_observer_ = nullptr;
    }
    if (hotspot_observer_) {
        lv_observer_remove(hotspot_observer_);
        hotspot_observer_ = nullptr;
    }
    if (block_observer_) {
        lv_observer_remove(block_observer_);
        block_observer_ = nullptr;
    }
    if (wifi_scan_observer_) {
        lv_observer_remove(wifi_scan_observer_);
        wifi_scan_observer_ = nullptr;
    }
}

void DataController::read_first() {
    const auto started = std::chrono::steady_clock::now();
    // The live source's connection to the network service starts in the
    // background; a short, bounded wait lets the first page show its names.
    reader_.wait_until_ready(kFirstReadingWait);
    reader_.read_now();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_INFO("data: {} (first reading in {} ms)",
             platform::data_source_name(reader_.kind()),
             static_cast<long long>(elapsed_ms));

    if (!poll()) {
        // Only when the reading failed: the page keeps the values the view
        // model already holds, and the battery line says what they are.
        LOG_WARN("data: the first reading failed; the values stay as they were");
        const model::HomeData& shown = view_model_.home();
        log_battery(logged_charge(shown.battery_percent), shown.battery_state);
        log_link(shown.uplink_kind, shown.ap_active, shown.clients.size());
    }
}

void DataController::start() {
    if (reader_.running()) {
        return;
    }

    reader_.start();
    if (!reader_.running()) {
        LOG_WARN("data: the reader could not start; the values stay as they are");
        return;
    }

    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        // Nothing would fetch the readings, so the reader is not left running.
        reader_.stop();
        LOG_WARN("data: no timer to fetch new values; the values stay as they are");
    }
}

void DataController::step() {
    if (reader_.running()) {
        return;
    }
    reader_.read_now();
    poll();
}

void DataController::stop() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }

    const bool was_running = reader_.running();
    const bool other_mode = block_.mode != model::NetMode::Client || block_.vpn.state != model::VpnState::Off ||
                            block_.vpn.kill_switch ||
                            block_.mode_switch.state == model::SwitchState::Switching;
    if (was_running && reader_.changes_network() && other_mode) {
        LOG_INFO("data: {}", view_model_.keep_mode() ? "mode kept after quitting" : "back to Client before quitting");
    }
    reader_.stop();
    if (was_running) {
        LOG_INFO("data: reader stopped");
    }
}

void DataController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<DataController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void DataController::page_observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* controller = static_cast<DataController*>(lv_observer_get_user_data(observer));
    if (controller) {
        controller->update_shown();
    }
}

void DataController::mode_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<DataController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->mode_requests_seen_) {
        return;
    }
    controller->mode_requests_seen_ = count;
    controller->request_mode(controller->view_model_.requested_mode());
}

void DataController::keep_mode_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* controller = static_cast<DataController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const bool keep = controller->view_model_.keep_mode();
    controller->reader_.set_keep_modes(keep);
    if (!controller->keep_mode_seen_) {
        controller->keep_mode_seen_ = true;
        return;
    }
    LOG_INFO("data: keep mode after quitting switched {}", keep ? "on" : "off");
    if (controller->keep_mode_saver_) {
        controller->keep_mode_saver_(keep);
    }
}

void DataController::set_keep_mode_saver(KeepModeSaver saver) {
    keep_mode_saver_ = std::move(saver);
}

void DataController::hotspot_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<DataController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->hotspot_requests_seen_) {
        return;
    }
    controller->hotspot_requests_seen_ = count;
    controller->change_hotspot();
}

void DataController::block_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<DataController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->block_requests_seen_) {
        return;
    }
    controller->block_requests_seen_ = count;
    controller->change_blocked();
}

void DataController::wifi_scan_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<DataController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->wifi_scan_requests_seen_) {
        return;
    }
    controller->wifi_scan_requests_seen_ = count;
    // A scan already under way takes no second one.
    if (controller->view_model_.home().wifi_scan.scanning) {
        return;
    }
    LOG_INFO("data: wi-fi scan asked");
    controller->scan_logged_ = true;
    controller->scan_seen_running_ = false;
    controller->reader_.request_wifi_scan();
    if (!controller->reader_.running()) {
        // Snapshots: the request is taken with a reading right away.
        controller->reader_.read_now();
        controller->poll();
    }
}

void DataController::log_wifi_scan(const model::WifiScan& scan) {
    if (!scan_logged_ || view_model_.current_page() != model::AppPage::Wifi) {
        return;
    }
    if (scan.scanning) {
        scan_seen_running_ = true;
        return;
    }
    const bool no_wifi = scan.error == model::WifiScanError::NoService ||
                         scan.error == model::WifiScanError::NoDevice ||
                         scan.error == model::WifiScanError::WifiOff || scan.error == model::WifiScanError::Hotspot;
    if (!scan_seen_running_ && !no_wifi) {
        return;  // a block from before the request was taken says nothing yet
    }
    switch (scan.error) {
        case model::WifiScanError::None:
            LOG_INFO("data: wi-fi scan done ({} networks)", scan.found);
            break;
        case model::WifiScanError::NotAllowed:
            LOG_INFO("data: wi-fi scan: not allowed");
            break;
        case model::WifiScanError::TimedOut:
            LOG_INFO("data: wi-fi scan: no result in time");
            break;
        case model::WifiScanError::Refused:
            LOG_INFO("data: wi-fi scan: refused ({})", scan.reason);
            break;
        default:
            LOG_INFO("data: wi-fi scan: no Wi-Fi to scan");
            break;
    }
    scan_logged_ = false;
}

void DataController::set_blocked_devices(const std::vector<std::string>& addresses) {
    blocked_ = addresses;
    reader_.set_blocked(blocked_);
}

void DataController::set_blocked_saver(BlockedSaver saver) {
    blocked_saver_ = std::move(saver);
}

void DataController::set_block_failure_listener(BlockFailureListener listener) {
    block_failure_listener_ = std::move(listener);
}

void DataController::set_usb_wifi_listener(UsbWifiListener listener) {
    usb_wifi_listener_ = std::move(listener);
}

void DataController::change_blocked() {
    const std::string& address = view_model_.requested_block_address();
    if (!platform::valid_device_address(address)) {
        return;
    }
    // A list that already says so (a change the firewall refused before) is
    // handed over again without saving, so the data layer tries once more.
    const auto found = std::find(blocked_.begin(), blocked_.end(), address);
    bool changed = false;
    if (view_model_.requested_block() && found == blocked_.end()) {
        blocked_.push_back(address);
        if (blocked_.size() > platform::kMaxBlockedDevices) {
            blocked_.erase(blocked_.begin());
        }
        changed = true;
        LOG_INFO("data: block a device ({} blocked)", blocked_.size());
    } else if (!view_model_.requested_block() && found != blocked_.end()) {
        blocked_.erase(found);
        changed = true;
        LOG_INFO("data: unblock a device ({} blocked)", blocked_.size());
    }
    if (changed && blocked_saver_ && reader_.changes_network()) {
        blocked_saver_(blocked_);
    }
    reader_.set_blocked(blocked_);
    if (!reader_.running()) {
        // Snapshots: the list is taken with a reading right away.
        reader_.read_now();
        poll();
    }
}

void DataController::set_password_generator(std::function<std::string()> generator) {
    password_generator_ = std::move(generator);
}

void DataController::set_hotspot_saver(HotspotSaver saver) {
    hotspot_saver_ = std::move(saver);
}

std::string DataController::new_password() const {
    return password_generator_ ? password_generator_() : random_hotspot_password();
}

void DataController::save_hotspot(const std::optional<std::string>& name, const std::optional<std::string>& password) {
    if (hotspot_saver_ && reader_.changes_network()) {
        hotspot_saver_(name, password);
    }
}

void DataController::request_mode(model::NetMode mode) {
    platform::ModeRequest request;
    request.mode = mode;
    if (model::is_hotspot_mode(mode)) {
        request.name = view_model_.hotspot_name();
        request.password = view_model_.hotspot_password();
        if (request.password.empty() && reader_.changes_network()) {
            request.password = new_password();
            view_model_.set_hotspot_password(request.password);
            save_hotspot(std::nullopt, request.password);
        }
    }
    if (mode == model::NetMode::Vpn) {
        request.vpn_off = view_model_.requested_vpn_off();
        if (!request.vpn_off) {
            request.vpn_file = view_model_.vpn_file();
        }
    }
    send(request);
}

void DataController::send(const platform::ModeRequest& request) {
    reader_.request_mode(request);
    if (!reader_.running()) {
        // Snapshots: the request is taken with a reading right away.
        reader_.read_now();
        poll();
    }
}

void DataController::change_hotspot() {
    // While a switch runs a restart would be ignored, so nothing changes:
    // the pages never show a name or a password the hotspot does not use.
    if (view_model_.home().mode_switch.state == model::SwitchState::Switching) {
        return;
    }
    std::string name = view_model_.hotspot_name();
    std::string password = view_model_.hotspot_password();
    std::optional<std::string> saved_name;
    std::optional<std::string> saved_password;
    if (view_model_.requested_hotspot_change() == viewmodel::HotspotChange::Rename) {
        // An empty name puts the default name back; the saved line is then
        // left empty, so the default counts again at the next start.
        const std::string& requested = view_model_.requested_hotspot_name();
        const std::string& target = requested.empty() ? view_model_.default_hotspot_name() : requested;
        if (!valid_hotspot_name(target) || target == name) {
            return;
        }
        name = target;
        saved_name = requested;
        LOG_INFO("data: hotspot renamed");
    } else {
        password = new_password();
        saved_password = password;
        LOG_INFO("data: new hotspot password");
    }
    if (password.empty() && reader_.changes_network()) {
        // A hotspot left running without a saved password: the restart
        // needs one.
        password = new_password();
        saved_password = password;
    }
    view_model_.set_hotspot_name(name);
    view_model_.set_hotspot_password(password);
    save_hotspot(saved_name, saved_password);

    // While Router or Share runs, the hotspot starts again with the new
    // values.
    const model::HomeData& data = view_model_.home();
    if (model::is_hotspot_mode(data.mode) && data.ap_active) {
        platform::ModeRequest request;
        request.mode = data.mode;
        request.name = name;
        request.password = password;
        request.restart = true;
        send(request);
    }
}

void DataController::set_screen_dark(bool dark) {
    if (dark == screen_dark_) {
        return;
    }
    screen_dark_ = dark;
    update_shown();
}

void DataController::update_shown() {
    const model::AppPage page = view_model_.current_page();
    reader_.set_traffic_shown(!screen_dark_ && page == model::AppPage::Home);
    reader_.set_details_shown(!screen_dark_ && page == model::AppPage::Status);
    // The health check takes NetworkManager's last scan list as it starts.
    reader_.set_wifi_shown(!screen_dark_ && (page == model::AppPage::Wifi || page == model::AppPage::Health));
}

bool DataController::poll() {
    if (!reader_.take(block_)) {
        return false;
    }
    deliver(block_);
    return true;
}

void DataController::deliver(const model::HomeData& data) {
    view_model_.set_home_data(data);
    log_wifi_scan(data.wifi_scan);

    if (data.block_failures != block_failures_seen_) {
        block_failures_seen_ = data.block_failures;
        LOG_WARN("data: {}", data.block_message);
        if (block_failure_listener_) {
            block_failure_listener_(data.block_message);
        }
    }

    const int32_t charge = logged_charge(data.battery_percent);
    if (!battery_logged_ || charge != logged_percent_ || data.battery_state != logged_state_) {
        log_battery(charge, data.battery_state);
    }

    log_mode(data);

    const std::size_t clients = data.clients.size();
    if (!link_logged_ || data.uplink_kind != logged_kind_ || data.ap_active != logged_ap_active_ ||
        clients != logged_clients_) {
        log_link(data.uplink_kind, data.ap_active, clients);
    }

    check_usb_wifi(data.usb_wifi);
}

void DataController::log_battery(int32_t percent, model::BatteryState state) {
    battery_logged_ = true;
    logged_percent_ = percent;
    logged_state_ = state;

    if (percent < 0 && state == model::BatteryState::Unknown) {
        LOG_INFO("data: battery unknown");
        return;
    }
    const std::string charge = percent < 0 ? std::string("--%") : std::to_string(percent) + "%";
    LOG_INFO("data: battery {} {}", charge, model::battery_state_name(state));
}

void DataController::log_mode(const model::HomeData& data) {
    const char* target = model::net_mode_name(data.mode_switch.target);
    if (data.mode_switch.state != logged_switch_ || !mode_logged_) {
        switch (data.mode_switch.state) {
            case model::SwitchState::Switching:
                LOG_INFO("data: switching to {}", target);
                break;
            case model::SwitchState::Failed:
                LOG_INFO("data: switch to {} failed: {}",
                         target,
                         data.mode_switch.error == model::SwitchError::NoCable       ? "no cable"
                         : data.mode_switch.error == model::SwitchError::NoWifi      ? "no Wi-Fi network"
                         : data.mode_switch.error == model::SwitchError::ClientFirst ? "another mode runs"
                         : data.mode_switch.error == model::SwitchError::NotAllowed  ? "not allowed"
                                                                                     : data.mode_switch.reason);
                break;
            case model::SwitchState::Idle:
                if (mode_logged_ && logged_switch_ == model::SwitchState::Switching) {
                    LOG_INFO("data: switch to {} done", target);
                }
                break;
        }
        logged_switch_ = data.mode_switch.state;
    }
    if (!mode_logged_ || data.mode != logged_mode_) {
        LOG_INFO("data: mode {}", lowercase_mode_name(data.mode));
        logged_mode_ = data.mode;
    }
    mode_logged_ = true;
}

void DataController::log_link(model::LinkKind kind, bool ap_active, std::size_t clients) {
    link_logged_ = true;
    logged_kind_ = kind;
    logged_ap_active_ = ap_active;
    logged_clients_ = clients;

    LOG_INFO("data: link {}, access point {}, {} clients",
             model::link_kind_name(kind),
             ap_active ? "on" : "off",
             clients);
}

void DataController::check_usb_wifi(const std::vector<model::UsbWifiAdapter>& adapters) {
    const auto contains = [](const std::vector<model::UsbWifiAdapter>& list, const model::UsbWifiAdapter& adapter) {
        return std::any_of(list.begin(), list.end(), [&adapter](const model::UsbWifiAdapter& other) {
            return other.device == adapter.device;
        });
    };
    if (!usb_wifi_logged_) {
        // The adapters there at the start are only logged: none of them was
        // just plugged in.
        usb_wifi_logged_ = true;
        if (adapters.empty()) {
            LOG_INFO("data: usb wi-fi: none");
        }
        for (const model::UsbWifiAdapter& adapter : adapters) {
            LOG_INFO("data: usb wi-fi adapter {} ({}, {})",
                     adapter.interface_name,
                     adapter.driver.empty() ? "unknown" : adapter.driver,
                     adapter.usb_id.empty() ? "unknown" : adapter.usb_id);
        }
        usb_wifi_ = adapters;
        return;
    }
    // Only the adapters themselves count: a new managed state or a renamed
    // interface on the same stick says nothing.
    for (const model::UsbWifiAdapter& adapter : usb_wifi_) {
        if (!contains(adapters, adapter)) {
            LOG_INFO("data: usb wi-fi adapter removed: {}", adapter.interface_name);
            if (usb_wifi_listener_) {
                usb_wifi_listener_(adapter.interface_name, false);
            }
        }
    }
    for (const model::UsbWifiAdapter& adapter : adapters) {
        if (!contains(usb_wifi_, adapter)) {
            LOG_INFO("data: usb wi-fi adapter connected: {} ({}, {})",
                     adapter.interface_name,
                     adapter.driver.empty() ? "unknown" : adapter.driver,
                     adapter.usb_id.empty() ? "unknown" : adapter.usb_id);
            if (usb_wifi_listener_) {
                usb_wifi_listener_(adapter.interface_name, true);
            }
        }
    }
    usb_wifi_ = adapters;
}

} // namespace app
