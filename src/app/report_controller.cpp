/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "report_controller.h"

#include "base_viewmodel.h"
#include "logger.h"
#include "report.h"
#include "report_files.h"

#include <ctime>
#include <utility>

namespace app {

ReportController::ReportController(viewmodel::BaseViewModel& view_model, std::string folder)
    : view_model_(view_model), folder_(std::move(folder)) {}

ReportController::~ReportController() {
    stop();
}

void ReportController::set_listener(Listener listener) {
    listener_ = std::move(listener);
}

void ReportController::start() {
    if (request_observer_) {
        return;
    }
    requests_seen_ = lv_subject_get_int(view_model_.report_request_subject());
    request_observer_ = lv_subject_add_observer(view_model_.report_request_subject(), request_cb, this);
}

void ReportController::stop() {
    if (request_observer_) {
        lv_observer_remove(request_observer_);
        request_observer_ = nullptr;
    }
}

void ReportController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<ReportController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->requests_seen_) {
        return;
    }
    controller->requests_seen_ = count;
    controller->save();
}

void ReportController::save() {
    const auto tell = [this](bool saved, const std::string& text) {
        if (listener_) {
            listener_(saved, text);
        }
    };

    char stamp[32] = {};
    char saved_at[32] = {};
    char clock[16] = {};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (!localtime_r(&now, &local) || std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local) == 0 ||
        std::strftime(saved_at, sizeof(saved_at), "%Y-%m-%d %H:%M", &local) == 0 ||
        std::strftime(clock, sizeof(clock), "%H:%M:%S", &local) == 0) {
        LOG_WARN("report: not saved (no clock)");
        tell(false, "no clock");
        return;
    }
    if (folder_.empty()) {
        LOG_WARN("report: not saved (no folder; set {})", platform::kReportFolderVariable);
        tell(false, "no folder");
        return;
    }

    const model::ReportSource source{view_model_.home(),
                                     view_model_.tools(),
                                     view_model_.watch(),
                                     view_model_.trap(),
                                     saved_at,
                                     APP_VERSION};
    const std::string html = model::report_html(source);
    const std::string csv = model::report_devices_csv(source);

    platform::SavedReport saved;
    std::string reason;
    if (!platform::save_report(folder_, stamp, html, csv, saved, reason)) {
        LOG_WARN("report: not saved ({})", reason);
        tell(false, reason);
        return;
    }
    LOG_INFO("report: saved {} ({} bytes)", saved.page, html.size());
    tell(true, clock);
}

} // namespace app
