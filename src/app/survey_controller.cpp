/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "survey_controller.h"

#include "base_viewmodel.h"
#include "logger.h"

#include <string>
#include <utility>

namespace app {

SurveyController::SurveyController(viewmodel::BaseViewModel& view_model,
                                   std::unique_ptr<platform::SurveyRunner> runner)
    : view_model_(view_model), runner_(std::move(runner)) {}

SurveyController::~SurveyController() {
    stop();
}

void SurveyController::start() {
    if (poll_timer_ || stopped_ || !runner_) {
        return;
    }
    runner_->recover();
    requests_seen_ = lv_subject_get_int(view_model_.survey_request_subject());
    request_observer_ = lv_subject_add_observer(view_model_.survey_request_subject(), request_cb, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        LOG_WARN("survey: no timer to fetch the results; the survey stays off");
    }
    poll();
}

void SurveyController::begin_stop() {
    if (runner_ && !stopped_) {
        runner_->stop();
    }
}

void SurveyController::stop() {
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
            LOG_INFO("survey: runner stopped");
        }
    }
}

void SurveyController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<SurveyController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void SurveyController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<SurveyController*>(lv_observer_get_user_data(observer));
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

void SurveyController::handle_request() {
    if (stopped_ || !runner_) {
        return;
    }
    if (view_model_.requested_survey() == viewmodel::BaseViewModel::SurveyRequest::Stop) {
        if (run_.state == model::ToolState::Running) {
            runner_->stop();
        }
        poll();
        return;
    }
    if (run_.state == model::ToolState::Running) {
        return;
    }
    std::string why;
    const std::string stick = model::survey_stick(view_model_.home(), why);
    if (stick.empty()) {
        LOG_INFO("survey: no stick to use ({})", why);
        return;
    }
    // Refused while the last run (or putting back a stick) is still ending:
    // the next press tries again.
    if (runner_->start(stick)) {
        stick_gone_logged_ = false;
        LOG_INFO("survey: started on {}", stick);
    }
    poll();
}

void SurveyController::poll() {
    if (stopped_ || !runner_) {
        return;
    }
    model::SurveyRun next;
    if (runner_->take(next)) {
        const bool ended = run_.state == model::ToolState::Running && next.state != model::ToolState::Running;
        run_ = std::move(next);
        if (ended) {
            std::string name;
            std::string security;
            if (model::survey_joined_wifi(view_model_.home(), name, security)) {
                joined_name_ = name;
                joined_security_ = security;
            }
            log_end(run_);
        }
    }
    // The stick was pulled out, or is no longer free: the survey ends.
    if (run_.state == model::ToolState::Running) {
        std::string why;
        if (model::survey_stick(view_model_.home(), why) != run_.interface_name) {
            runner_->stop();
            if (!stick_gone_logged_) {
                stick_gone_logged_ = true;
                LOG_INFO("survey: stick gone, stopping");
            }
        }
    }
    std::string name;
    std::string security;
    if (model::survey_joined_wifi(view_model_.home(), name, security)) {
        joined_name_ = name;
        joined_security_ = security;
    }
    model::SurveyRun shown = run_;
    model::mark_survey_look_alikes(shown, joined_name_, joined_security_);
    view_model_.set_survey(shown);
}

void SurveyController::log_end(const model::SurveyRun& run) const {
    model::SurveyRun marked = run;
    model::mark_survey_look_alikes(marked, joined_name_, joined_security_);
    std::string how = "done";
    if (run.state == model::ToolState::Stopped) {
        how = "stopped";
    } else if (run.state == model::ToolState::Failed) {
        how = "failed: " + (run.error == model::ToolError::NotAllowed ? std::string("not allowed") : run.reason);
    }
    LOG_INFO("survey: {}, networks: {}, warnings: {}, frames: {}", how, run.networks_seen,
             model::survey_warnings(marked), run.frames);
}

} // namespace app
