/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "watch_controller.h"

#include "base_viewmodel.h"
#include "logger.h"

#include <algorithm>
#include <utility>

namespace app {

WatchController::WatchController(viewmodel::BaseViewModel& view_model,
                                 std::unique_ptr<platform::WatchRunner> runner,
                                 std::vector<std::string> known)
    : view_model_(view_model), runner_(std::move(runner)), known_(std::move(known)) {
    if (known_.size() > model::kWatchKnownMax) {
        known_.erase(known_.begin(),
                     known_.begin() + static_cast<std::ptrdiff_t>(known_.size() - model::kWatchKnownMax));
    }
}

WatchController::~WatchController() {
    stop();
}

void WatchController::set_alert_listener(AlertListener listener) {
    alert_listener_ = std::move(listener);
}

void WatchController::set_known_saver(KnownSaver saver) {
    known_saver_ = std::move(saver);
}

void WatchController::add_alert(const model::WatchAlert& alert) {
    others_.insert(others_.begin(), OtherAlert{alert, run_.raised});
    if (others_.size() > model::kWatchMaxAlerts) {
        others_.pop_back();
    }
    ++others_raised_;
    publish();
}

void WatchController::start() {
    if (poll_timer_ || stopped_ || !runner_) {
        return;
    }
    requests_seen_ = lv_subject_get_int(view_model_.watch_request_subject());
    request_observer_ = lv_subject_add_observer(view_model_.watch_request_subject(), request_cb, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        LOG_WARN("watch: no timer to fetch the alerts; Watch stays off");
    }
    publish();
}

void WatchController::stop() {
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
            LOG_INFO("watch: runner stopped");
        }
        // Quitting: what was learned is kept only after the whole minute.
        keep_learned(run_);
    }
}

void WatchController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<WatchController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void WatchController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<WatchController*>(lv_observer_get_user_data(observer));
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

void WatchController::handle_request() {
    if (!poll_timer_) {
        return;  // nothing would show the run
    }
    using Request = viewmodel::BaseViewModel::WatchRequest;
    switch (view_model_.requested_watch()) {
        case Request::Start: {
            const model::HomeData& home = view_model_.home();
            platform::WatchJob job;
            job.interface_name = home.uplink_name;
            job.own_address = home.uplink_address;
            job.gateway_address = home.gateway_address;
            job.known = known_;
            if (!runner_->start(job)) {
                return;  // one runs already, or no usable uplink
            }
            run_ = model::WatchRun{};
            learned_taken_ = 0;
            // The alerts from elsewhere stay, older than the new run's.
            for (OtherAlert& other : others_) {
                other.after = 0;
            }
            LOG_INFO("watch: started");
            break;
        }
        case Request::Stop:
            if (run_.state == model::ToolState::Running) {
                runner_->stop();
                LOG_INFO("watch: stopped by the user");
            }
            break;
        case Request::MarkKnown: {
            const std::string& hardware = view_model_.requested_watch_hardware();
            if (hardware.empty() || std::find(known_.begin(), known_.end(), hardware) != known_.end()) {
                return;
            }
            add_known(hardware);
            LOG_INFO("watch: device marked known");
            save_known();
            break;
        }
    }
    // The page shows the change at once, not at the next tick.
    poll();
    publish();
}

void WatchController::add_known(const std::string& hardware) {
    if (known_.size() >= model::kWatchKnownMax) {
        known_.erase(known_.begin());
    }
    known_.push_back(hardware);
}

// Devices learned on a first run join the known list and are saved, but
// only once the whole learning minute has passed: a run stopped sooner
// keeps nothing, so the next run learns again.
void WatchController::keep_learned(const model::WatchRun& run) {
    if (run.elapsed_s < model::kWatchLearnSeconds || run.learned.size() <= learned_taken_) {
        return;
    }
    for (std::size_t index = learned_taken_; index < run.learned.size(); ++index) {
        if (std::find(known_.begin(), known_.end(), run.learned[index]) == known_.end()) {
            add_known(run.learned[index]);
        }
    }
    learned_taken_ = run.learned.size();
    save_known();
    LOG_INFO("watch: learned devices: {}", learned_taken_);
}

void WatchController::save_known() {
    if (known_saver_) {
        known_saver_(known_);
    }
}

void WatchController::poll() {
    model::WatchRun next = run_;
    if (!runner_->take(next)) {
        return;
    }
    keep_learned(next);
    // New alerts: one message for all that came since the last take.
    if (next.raised > run_.raised && !next.alerts.empty()) {
        const int32_t fresh = next.raised - run_.raised;
        for (int32_t index = 0; index < fresh && index < static_cast<int32_t>(next.alerts.size()); ++index) {
            LOG_INFO("watch: alert: {}", model::watch_alert_log_name(next.alerts[static_cast<std::size_t>(index)].kind));
        }
        if (alert_listener_) {
            alert_listener_(fresh == 1 ? std::string(model::watch_alert_name(next.alerts.front().kind))
                                       : std::to_string(fresh) + " new alerts");
        }
    }
    if (next.state != run_.state) {
        if (next.state == model::ToolState::Stopped) {
            LOG_INFO("watch: ended, alerts: {}, frames: {}", next.raised, next.frames);
        } else if (next.state == model::ToolState::Failed) {
            LOG_INFO("watch: failed: {}",
                     next.error == model::ToolError::NotAllowed ? std::string("not allowed for this user")
                                                                : next.reason);
        }
    }
    run_ = std::move(next);
    publish();
}

void WatchController::publish() {
    model::WatchData data;
    data.run = run_;
    data.known = known_;
    if (!others_.empty()) {
        // Both lists are the newest first; run_'s alert at `index` was
        // raised as number run_.raised - index.
        std::vector<model::WatchAlert> merged;
        std::size_t index = 0;
        std::size_t other = 0;
        while (merged.size() < model::kWatchMaxAlerts && (index < run_.alerts.size() || other < others_.size())) {
            const int32_t number = run_.raised - static_cast<int32_t>(index);
            if (index < run_.alerts.size() && (other >= others_.size() || number > others_[other].after)) {
                merged.push_back(run_.alerts[index++]);
            } else {
                merged.push_back(others_[other++].alert);
            }
        }
        data.run.alerts = std::move(merged);
        data.run.raised = run_.raised + others_raised_;
    }
    view_model_.set_watch(data);
}

} // namespace app
