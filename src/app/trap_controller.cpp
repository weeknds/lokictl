/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "trap_controller.h"

#include "base_viewmodel.h"
#include "logger.h"
#include "watch_controller.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace app {

void add_trap_alerts(WatchController& watch, const std::vector<model::TrapHit>& fresh) {
    for (auto hit = fresh.rbegin(); hit != fresh.rend(); ++hit) {
        model::WatchAlert alert;
        alert.kind = model::WatchAlertKind::Trap;
        alert.time = hit->time;
        alert.address = hit->address;
        alert.count = hit->port;
        watch.add_alert(alert);
    }
}

TrapController::TrapController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::TrapRunner> runner)
    : view_model_(view_model), runner_(std::move(runner)) {}

TrapController::~TrapController() {
    stop();
}

void TrapController::set_hit_listener(HitListener listener) {
    hit_listener_ = std::move(listener);
}

void TrapController::start() {
    if (poll_timer_ || stopped_ || !runner_) {
        return;
    }
    requests_seen_ = lv_subject_get_int(view_model_.trap_request_subject());
    request_observer_ = lv_subject_add_observer(view_model_.trap_request_subject(), request_cb, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        LOG_WARN("trap: no timer to fetch the connections; the trap stays off");
    }
    view_model_.set_trap(run_);
}

void TrapController::stop() {
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
        runner_->shutdown();
        if (run_.state == model::ToolState::Running) {
            LOG_INFO("trap: runner stopped");
        }
    }
}

void TrapController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<TrapController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void TrapController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<TrapController*>(lv_observer_get_user_data(observer));
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

void TrapController::handle_request() {
    if (!poll_timer_) {
        return;  // nothing would show the run
    }
    switch (view_model_.requested_trap()) {
        case viewmodel::BaseViewModel::TrapRequest::Start:
            if (!runner_->start()) {
                return;  // one runs already
            }
            run_ = model::TrapRun{};
            LOG_INFO("trap: set by the user");
            break;
        case viewmodel::BaseViewModel::TrapRequest::Stop:
            if (run_.state == model::ToolState::Running) {
                runner_->stop();
                LOG_INFO("trap: taken away by the user");
            }
            break;
    }
    // The page shows the change at once, not at the next tick.
    poll();
}

// The log gets what changed: never an address or what was sent.
void TrapController::log_changes(const model::TrapRun& next) const {
    const bool opened = run_.state != model::ToolState::Running || model::trap_open_ports(run_) == 0;
    if (next.state == model::ToolState::Running && opened && model::trap_open_ports(next) > 0) {
        LOG_INFO("trap: on, ports open: {}", model::trap_open_ports(next));
    }
    if (!next.helper_problem.empty() && next.helper_problem != run_.helper_problem) {
        LOG_WARN("trap: firewall helper: {}", next.helper_problem);
    }
    const int32_t fresh = next.raised - run_.raised;
    for (int32_t index = 0; index < fresh && index < static_cast<int32_t>(next.hits.size()); ++index) {
        LOG_INFO("trap: connection to port {}", next.hits[static_cast<std::size_t>(index)].port);
    }
    if (next.state != run_.state) {
        if (next.state == model::ToolState::Stopped) {
            LOG_INFO("trap: off, connections: {}, closed at once: {}", next.raised, next.dropped);
        } else if (next.state == model::ToolState::Failed) {
            LOG_INFO("trap: failed: {}", next.reason);
        }
    }
}

void TrapController::poll() {
    model::TrapRun next = run_;
    if (!runner_->take(next)) {
        return;
    }
    log_changes(next);
    if (next.raised > run_.raised && !next.hits.empty() && hit_listener_) {
        const int32_t count = next.raised - run_.raised;
        const std::vector<model::TrapHit> fresh(
            next.hits.begin(),
            next.hits.begin() + std::min<std::ptrdiff_t>(count, static_cast<std::ptrdiff_t>(next.hits.size())));
        const model::TrapHit& newest = fresh.front();
        const model::TrapService* service = model::trap_service(newest.port);
        hit_listener_(count == 1 ? newest.address + " tried " + (service ? service->word : "a port")
                                 : std::to_string(count) + " connections",
                      fresh);
    }
    run_ = std::move(next);
    view_model_.set_trap(run_);
}

} // namespace app
