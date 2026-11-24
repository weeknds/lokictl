/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "talk_controller.h"

#include "base_viewmodel.h"
#include "logger.h"
#include "mode_switch.h"

#include <utility>

namespace app {

TalkController::TalkController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::TalkRunner> runner)
    : view_model_(view_model), runner_(std::move(runner)) {}

TalkController::~TalkController() {
    stop();
}

void TalkController::start() {
    if (poll_timer_ || stopped_ || !runner_) {
        return;
    }
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        LOG_WARN("talk: no timer to follow the hotspot; names are not shown");
        return;
    }
    poll();
}

void TalkController::stop() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    if (runner_ && !stopped_) {
        stopped_ = true;
        runner_->shutdown();
        if (started_ && run_.state == model::ToolState::Running) {
            LOG_INFO("talk: runner stopped");
        }
    }
}

void TalkController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<TalkController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

std::string TalkController::hotspot_interface() const {
    const model::HomeData& home = view_model_.home();
    if (!home.ap_active) {
        return {};
    }
    switch (home.mode) {
        case model::NetMode::Router:
            return platform::kHotspotInterface;
        case model::NetMode::Share:
            return platform::kShareInterface;
        case model::NetMode::Client:
        case model::NetMode::Vpn:
        case model::NetMode::Laptop:
            break;
    }
    return {};
}

void TalkController::poll() {
    if (stopped_ || !runner_) {
        return;
    }
    const std::string wanted = hotspot_interface();
    if (wanted != interface_) {
        // The hotspot stopped or moved: forget everything heard.
        if (started_) {
            runner_->stop();
            LOG_INFO("talk: stopped, lookups: {}", run_.lookups);
        }
        interface_ = wanted;
        started_ = false;
        run_ = model::TalkRun{};
        view_model_.set_talk(run_);
    }
    if (!interface_.empty() && !started_) {
        // Refused while the last run is still ending: tried again next time.
        if (runner_->start(interface_)) {
            started_ = true;
            LOG_INFO("talk: started on {}", interface_);
        }
    }
    model::TalkRun next;
    if (!runner_->take(next) || !started_) {
        return;  // nothing new, or what a stopped run left: dropped
    }
    if (next.state == model::ToolState::Failed && run_.state != model::ToolState::Failed) {
        LOG_WARN("talk: failed: {}", next.reason.empty() ? std::string("not allowed") : next.reason);
    }
    run_ = std::move(next);
    view_model_.set_talk(run_);
}

} // namespace app
