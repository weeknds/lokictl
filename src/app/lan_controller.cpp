/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "lan_controller.h"

#include "base_viewmodel.h"
#include "logger.h"
#include "mode_switch.h"

#include <string>
#include <utility>

namespace app {
namespace {

// The places are looked up again every this many ticks while the page is
// open (once a second).
constexpr int32_t kPlaceTicks = 4;

const char* way_word(const model::LanServed& test) {
    if (test.both) {
        return "both ways";
    }
    return test.to_peer ? "out" : "in";
}

} // namespace

LanController::LanController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::LanRunner> runner)
    : view_model_(view_model), runner_(std::move(runner)) {}

LanController::~LanController() {
    stop();
}

void LanController::start() {
    if (poll_timer_ || stopped_ || !runner_) {
        return;
    }
    requests_seen_ = lv_subject_get_int(view_model_.lan_request_subject());
    request_observer_ = lv_subject_add_observer(view_model_.lan_request_subject(), request_cb, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        LOG_WARN("lan: no timer to fetch the results; the LAN test stays off");
    }
    poll();
}

void LanController::stop() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    if (request_observer_) {
        lv_observer_remove(request_observer_);
        request_observer_ = nullptr;
    }
    if (runner_ && !stopped_) {
        stopped_ = true;
        const bool was_running = run_.state == model::ToolState::Running;
        runner_->shutdown();
        if (was_running) {
            LOG_INFO("lan: runner stopped");
        }
    }
}

void LanController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<LanController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void LanController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<LanController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->requests_seen_) {
        return;
    }
    controller->requests_seen_ = count;
    controller->handle_request();
}

std::vector<model::LanPlace> LanController::find_places() const {
    std::vector<model::LanPlace> places;
    const model::HomeData& home = view_model_.home();
    if (model::port_scan_address(home.uplink_address)) {
        places.push_back({home.uplink_address, "network"});
    }
    std::string hotspot;
    if (home.ap_active) {
        if (home.mode == model::NetMode::Router) {
            hotspot = platform::kHotspotInterface;
        } else if (home.mode == model::NetMode::Share) {
            hotspot = platform::kShareInterface;
        }
    }
    if (!hotspot.empty()) {
        const std::string address = runner_->interface_address(hotspot);
        if (model::port_scan_address(address) && address != home.uplink_address) {
            places.push_back({address, "hotspot"});
        }
    }
    return places;
}

const char* LanController::place_label(const std::string& address) const {
    for (const model::LanPlace& place : places_) {
        if (place.address == address) {
            return place.label == "hotspot" ? "hotspot" : "network";
        }
    }
    return nullptr;
}

void LanController::handle_request() {
    if (!poll_timer_) {
        return;  // nothing would show the run
    }
    using Request = viewmodel::BaseViewModel::LanRequest;
    const Request request = view_model_.requested_lan();
    const std::string& target = view_model_.requested_lan_target();
    if (request == Request::Stop) {
        if (run_.state == model::ToolState::Running) {
            off_why_ = "";
            runner_->stop();
            if (run_.mode == model::LanMode::Client) {
                LOG_INFO("lan: test stopped by the user");
            }
        }
    } else if (model::tool_running(view_model_.tools())) {
        return;  // one tool at a time
    } else if (request == Request::Test) {
        // The address is checked again here, whatever the page did.
        if (!model::valid_lan_target(target)) {
            LOG_WARN("lan: test refused: not a local address");
            return;
        }
        if (!runner_->start_client(target)) {
            return;  // one runs already
        }
        LOG_INFO("lan: test started");
    } else {
        places_ = find_places();
        const char* label = place_label(target);
        if (!label) {
            LOG_WARN("lan: server refused: not one of this device's addresses");
            return;
        }
        if (!runner_->start_server(target)) {
            return;
        }
        off_why_ = "";
        LOG_INFO("lan: server on ({})", label);
    }
    poll();
}

void LanController::poll() {
    if (stopped_ || !runner_) {
        return;
    }
    const bool page_open = view_model_.current_page() == model::AppPage::Lan;
    if (page_open && (ticks_++ % kPlaceTicks == 0)) {
        places_ = find_places();
    }
    // The server listens only while its page is open, on an address the
    // Cardputer still has.
    if (run_.state == model::ToolState::Running && run_.mode == model::LanMode::Server) {
        if (!page_open) {
            off_why_ = ", page left";
            runner_->stop();
        } else if (!place_label(run_.target)) {
            off_why_ = ", address gone";
            runner_->stop();
        }
    }
    if (!page_open) {
        ticks_ = 0;
    }
    model::LanRun next = run_;
    runner_->take(next);
    next.places = places_;
    if (next == run_) {
        return;
    }
    log_change(next);
    run_ = std::move(next);
    view_model_.set_lan(run_);
}

void LanController::log_change(const model::LanRun& next) const {
    if (next.mode == model::LanMode::Client) {
        if (next.state != run_.state || next.mode != run_.mode) {
            if (next.state == model::ToolState::Done) {
                LOG_INFO("lan: test done, down {} kbit/s ({} bytes), up {} kbit/s ({} bytes)", next.down_kbps,
                         next.down_bytes, next.up_kbps, next.up_bytes);
            } else if (next.state == model::ToolState::Failed) {
                LOG_INFO("lan: test failed: {}", next.reason);
            }
        }
        return;
    }
    if (next.tests > run_.tests && !next.served.empty()) {
        const model::LanServed& test = next.served.front();
        LOG_INFO("lan: server test done, {} kbit/s, {}", test.kbps, way_word(test));
    }
    if (next.state != run_.state || next.mode != run_.mode) {
        if (next.state == model::ToolState::Stopped) {
            LOG_INFO("lan: server off, tests: {}{}", next.tests, off_why_);
        } else if (next.state == model::ToolState::Failed) {
            LOG_INFO("lan: server failed: {}", next.reason);
        }
    }
}

} // namespace app
