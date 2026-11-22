/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "mode_switch.h"

#include "data_source.h"

namespace platform {

std::string router_mode_detail(const std::string& uplink) {
    return (uplink.empty() ? std::string("cable") : uplink) + " → wi-fi ap · nat · dhcp";
}

void show_router(model::HomeData& data, const std::string& name, const std::string& security,
                 int32_t uptime_seconds) {
    data.mode = model::NetMode::Router;
    data.mode_detail = router_mode_detail(data.uplink_name);
    data.ap_active = true;
    data.ap_name = name;
    data.ap_security = security;
    data.uptime_seconds = uptime_seconds;
}

void show_client(model::HomeData& data) {
    data.mode = model::NetMode::Client;
    data.mode_detail.clear();
    data.ap_active = false;
    data.ap_name.clear();
    data.ap_security.clear();
    data.uptime_seconds = 0;
    data.clients.clear();
}

void PretendModeSwitch::request(model::NetMode mode) {
    if (switching_ || (mode != model::NetMode::Client && mode != model::NetMode::Router)) {
        return;
    }
    requested_ = true;
    requested_mode_ = mode;
}

void PretendModeSwitch::apply(model::HomeData& data, const PretendFailure& failure) {
    // The pretend mode, once a switch has finished. A source that already
    // shows that mode keeps its own values (the sample's router, for one).
    if (has_mode_ && data.mode != mode_) {
        if (mode_ == model::NetMode::Router) {
            const model::HomeData sample = model::sample_home_data();
            show_router(data, sample.ap_name, sample.ap_security, 0);
        } else {
            show_client(data);
        }
    }

    // A switch that runs ends at this reading.
    if (switching_) {
        switching_ = false;
        if (failure.error != model::SwitchError::None) {
            status_.state = model::SwitchState::Failed;
            status_.error = failure.error;
            status_.reason = failure.error == model::SwitchError::Failed ? failure.reason : std::string();
        } else {
            status_.state = model::SwitchState::Idle;
            has_mode_ = true;
            mode_ = status_.target;
            if (mode_ == model::NetMode::Router) {
                const model::HomeData sample = model::sample_home_data();
                show_router(data, sample.ap_name, sample.ap_security, 0);
            } else {
                show_client(data);
            }
        }
    }

    // A new request starts at this reading, so its Switching is shown once.
    if (requested_) {
        requested_ = false;
        status_ = model::ModeSwitch{};
        status_.target = requested_mode_;
        if (requested_mode_ == data.mode) {
            // Already on: nothing to do.
        } else if (requested_mode_ == model::NetMode::Router && data.uplink_kind != model::LinkKind::Cable) {
            status_.state = model::SwitchState::Failed;
            status_.error = model::SwitchError::NoCable;
        } else {
            status_.state = model::SwitchState::Switching;
            switching_ = true;
        }
    }

    data.mode_switch = status_;
}

std::chrono::milliseconds PretendModeSwitch::next_delay() const {
    if (requested_) {
        return std::chrono::milliseconds(0);
    }
    if (switching_) {
        return kPretendTime;
    }
    return DataSource::kNever;
}

} // namespace platform
