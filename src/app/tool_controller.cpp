/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_controller.h"

#include "base_viewmodel.h"
#include "health_check.h"
#include "logger.h"
#include "sent_counter.h"

#include <cstddef>
#include <string>
#include <utility>

namespace app {
namespace {

const char* error_name(model::ToolError error) {
    switch (error) {
        case model::ToolError::None:
            return "none";
        case model::ToolError::NotAllowed:
            return "not allowed for this user";
        case model::ToolError::NoServer:
            return "no DNS server";
        case model::ToolError::NoSuchName:
            return "no such name";
        case model::ToolError::NoAnswer:
            return "no answer";
        case model::ToolError::NoAddress:
            return "no address";
        case model::ToolError::BadTarget:
            return "not a valid target";
        case model::ToolError::Failed:
            return "failed";
    }
    return "failed";
}

std::string why(model::ToolError error, const std::string& reason) {
    if (error == model::ToolError::Failed && !reason.empty()) {
        return reason;
    }
    return error_name(error);
}

// What the health check takes from the data layer as it starts: the
// uplink's address and router, the joined Wi-Fi (the uplink's, else the
// one the scan list marks as joined), NetworkManager's last scan list (the
// Health page has it read, see DataController) and whether the uplink is a
// cable.
model::HealthLocal health_local(const model::HomeData& home) {
    model::HealthLocal local;
    local.address = home.uplink_address;
    local.router = home.gateway_address;
    if (home.uplink_kind == model::LinkKind::Wifi) {
        local.wifi_name = home.wifi_name;
        local.wifi_security = home.wifi_security;
    }
    for (const model::WifiNetwork& network : home.wifi_scan.networks) {
        if (network.joined && local.wifi_security.empty()) {
            local.wifi_name = network.name;
            local.wifi_security = network.security;
        }
    }
    local.nearby = home.wifi_scan.networks;
    local.cable = home.uplink_kind == model::LinkKind::Cable;
    if (local.cable) {
        local.cable_name = home.uplink_name;
        local.cable_mbps = home.uplink_speed_mbps;
    }
    return local;
}

} // namespace

ToolController::ToolController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::ToolRunner> runner)
    : view_model_(view_model), runner_(std::move(runner)) {}

ToolController::~ToolController() {
    stop();
}

void ToolController::start() {
    if (poll_timer_ || stopped_ || !runner_) {
        return;
    }
    requests_seen_ = lv_subject_get_int(view_model_.tool_request_subject());
    request_observer_ = lv_subject_add_observer(view_model_.tool_request_subject(), request_cb, this);
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        LOG_WARN("tools: no timer to fetch the results; the tools stay off");
    }
    // The first take at once, so the pages built next find the list's date.
    poll();
}

void ToolController::stop() {
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
        const bool was_running = model::tool_running(data_);
        runner_->shutdown();
        if (was_running) {
            LOG_INFO("tools: runner stopped");
        }
    }
}

void ToolController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<ToolController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void ToolController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<ToolController*>(lv_observer_get_user_data(observer));
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

void ToolController::handle_request() {
    if (!poll_timer_) {
        return;  // nothing would show the run
    }
    if (view_model_.requested_capture_save()) {
        if (!runner_->save_capture()) {
            return;  // nothing to save, or saved already
        }
        LOG_INFO("tools: listen capture save asked");
    } else if (view_model_.requested_tool_stop()) {
        for (const model::ToolKind kind : {model::ToolKind::Ping, model::ToolKind::Dns, model::ToolKind::Trace,
                                           model::ToolKind::Listen, model::ToolKind::Probe,
                                           model::ToolKind::Ports, model::ToolKind::Speed,
                                           model::ToolKind::Health}) {
            if (model::tool_state(data_, kind) == model::ToolState::Running) {
                runner_->stop();
                LOG_INFO("tools: {} stopped by the user", model::tool_name(kind));
                break;
            }
        }
    } else {
        const model::ToolKind kind = view_model_.requested_tool();
        // The target is checked again here, whatever the page did.
        if (!model::valid_tool_target(kind, view_model_.requested_tool_target())) {
            LOG_WARN("tools: {} refused: not a valid target", model::tool_name(kind));
            return;
        }
        platform::ToolJob job{kind, view_model_.requested_tool_target(), {}};
        if (kind == model::ToolKind::Health) {
            job.health = health_local(view_model_.home());
        }
        if (!runner_->start(job)) {
            return;  // one runs already
        }
        if (kind == model::ToolKind::Health) {
            // Counts only: never a network's name.
            LOG_INFO("tools: health started, wifi networks heard: {}, joined wifi: {}, cable: {}",
                     job.health.nearby.size(), job.health.wifi_security.empty() ? "no" : "yes",
                     job.health.cable ? "yes" : "no");
        } else {
            LOG_INFO("tools: {} started", model::tool_name(kind));
        }
    }
    // The page shows the change at once, not at the next tick.
    poll();
}

void ToolController::poll() {
    model::ToolsData next = data_;
    runner_->take(next);
    next.sent = platform::sent_packets();
    if (next == data_) {
        return;
    }
    log_change(next);
    data_ = std::move(next);
    view_model_.set_tools(data_);
}

void ToolController::log_change(const model::ToolsData& next) const {
    const model::PingRun& ping = next.ping;
    if (ping.state != data_.ping.state) {
        if (ping.state == model::ToolState::Done) {
            LOG_INFO("tools: ping done, {}/{} replies", ping.received, ping.sent);
        } else if (ping.state == model::ToolState::Failed) {
            LOG_INFO("tools: ping failed: {}", why(ping.error, ping.reason));
        }
    }
    const model::DnsRun& dns = next.dns;
    if (dns.state != data_.dns.state) {
        if (dns.state == model::ToolState::Done && dns.error == model::ToolError::None) {
            LOG_INFO("tools: dns done, {} addresses", dns.addresses.size());
        } else if (dns.state == model::ToolState::Done || dns.state == model::ToolState::Failed) {
            LOG_INFO("tools: dns failed: {}", why(dns.error, dns.reason));
        }
    }
    const model::TraceRun& trace = next.trace;
    if (trace.state != data_.trace.state) {
        const std::size_t hops = trace.hops.size();
        if (trace.state == model::ToolState::Done) {
            switch (trace.end) {
                case model::TraceEnd::Reached:
                    LOG_INFO("tools: trace done, reached in {} hops", hops);
                    break;
                case model::TraceEnd::Unreachable:
                    LOG_INFO("tools: trace done, unreachable at hop {}", hops);
                    break;
                case model::TraceEnd::MaxHops:
                case model::TraceEnd::None:
                    LOG_INFO("tools: trace done, not reached in {} hops", hops);
                    break;
            }
        } else if (trace.state == model::ToolState::Failed) {
            LOG_INFO("tools: trace failed: {}", why(trace.error, trace.reason));
        }
    }
    // Counts only: never a hardware address, an address or a name.
    const model::ListenRun& listen = next.listen;
    if (listen.state != data_.listen.state) {
        if (listen.state == model::ToolState::Done) {
            LOG_INFO("tools: listen done, devices heard: {}, frames: {}", listen.devices.size(), listen.frames);
        } else if (listen.state == model::ToolState::Stopped) {
            LOG_INFO("tools: listen ended, devices heard: {}", listen.devices.size());
        } else if (listen.state == model::ToolState::Failed) {
            LOG_INFO("tools: listen failed: {}", why(listen.error, listen.reason));
        }
    }
    // Counts only, as for listening.
    const model::ProbeRun& probe = next.probe;
    if (probe.state != data_.probe.state) {
        if (probe.state == model::ToolState::Done) {
            LOG_INFO("tools: probe done, asked: {}, replied: {}, devices found: {}", probe.asked, probe.replied,
                     probe.devices.size());
        } else if (probe.state == model::ToolState::Stopped) {
            LOG_INFO("tools: probe ended, asked: {} of {}, devices found: {}", probe.asked, probe.total,
                     probe.devices.size());
        } else if (probe.state == model::ToolState::Failed) {
            LOG_INFO("tools: probe failed: {}", why(probe.error, probe.reason));
        }
    }
    // Attempt counts only: never the target, a port or how many were open.
    const model::PortRun& ports = next.ports;
    if (ports.state != data_.ports.state) {
        if (ports.state == model::ToolState::Done) {
            LOG_INFO("tools: ports done, asked: {}", ports.asked);
        } else if (ports.state == model::ToolState::Stopped) {
            LOG_INFO("tools: ports ended, asked: {} of {}", ports.asked, ports.total);
        } else if (ports.state == model::ToolState::Failed) {
            LOG_INFO("tools: ports failed: {}", why(ports.error, ports.reason));
        }
    }
    // Speeds and sizes only: never the server.
    const model::SpeedRun& speed = next.speed;
    if (speed.state != data_.speed.state) {
        if (speed.state == model::ToolState::Done) {
            LOG_INFO("tools: speed done, down {} kbit/s ({} bytes), up {} kbit/s ({} bytes)", speed.down_kbps,
                     speed.down_bytes, speed.up_kbps, speed.up_bytes);
        } else if (speed.state == model::ToolState::Failed) {
            LOG_INFO("tools: speed failed: {}", why(speed.error, speed.reason));
        }
    }
    // Counts only: never an address, a name or what was found where.
    const model::HealthRun& health = next.health;
    if (health.state != data_.health.state) {
        const model::HealthCounts counts = model::health_counts(health.findings);
        if (health.state == model::ToolState::Done) {
            LOG_INFO("tools: health done, asked: {}, devices: {}, risks: {}, warnings: {}, info: {}, {} s",
                     health.asked, health.scanned, counts.risks, counts.warnings, counts.infos, health.elapsed_s);
        } else if (health.state == model::ToolState::Stopped) {
            LOG_INFO("tools: health ended, asked: {} of {}, devices: {} of {}", health.asked, health.total,
                     health.scanned, health.hosts);
        } else if (health.state == model::ToolState::Failed) {
            LOG_INFO("tools: health failed: {}", why(health.error, health.reason));
        }
    }
    // Sizes only: never the file's path.
    if (listen.save != data_.listen.save) {
        if (listen.save == model::CaptureSave::Saved) {
            LOG_INFO("tools: listen capture saved, {} bytes{}", listen.save_bytes, listen.save_full ? ", full" : "");
        } else if (listen.save == model::CaptureSave::Failed) {
            LOG_INFO("tools: listen capture save failed: {}", listen.save_reason);
        }
    }
}

} // namespace app
