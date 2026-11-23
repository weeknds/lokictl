/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "mode_switch.h"

#include "data_source.h"

#include <cctype>
#include <fstream>

namespace platform {

std::string default_hotspot_name(const std::filesystem::path& root) {
    std::ifstream file(root / "sys/class/net" / kHotspotInterface / "address");
    std::string address;
    std::string digits;
    if (file >> address) {
        for (const char character : address) {
            if (std::isxdigit(static_cast<unsigned char>(character)) != 0) {
                digits += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            }
        }
    }
    return "LokiCTL-" + (digits.size() >= 4 ? digits.substr(digits.size() - 4) : std::string("0000"));
}

std::string router_mode_detail(const std::string& uplink) {
    return (uplink.empty() ? std::string("cable") : uplink) + " → wi-fi ap · nat · dhcp";
}

namespace {

void show_hotspot(model::HomeData& data, model::NetMode mode, const std::string& name, const std::string& security,
                  int32_t channel, int32_t uptime_seconds) {
    data.mode = mode;
    data.mode_detail = router_mode_detail(data.uplink_name);
    data.ap_active = true;
    data.ap_name = name;
    data.ap_security = security;
    data.ap_channel = channel;
    data.uptime_seconds = uptime_seconds;
}

} // namespace

void show_router(model::HomeData& data, const std::string& name, const std::string& security,
                 int32_t channel, int32_t uptime_seconds) {
    show_hotspot(data, model::NetMode::Router, name, security, channel, uptime_seconds);
}

void show_share(model::HomeData& data, const std::string& name, const std::string& security,
                int32_t channel, int32_t uptime_seconds) {
    show_hotspot(data, model::NetMode::Share, name, security, channel, uptime_seconds);
}

std::string vpn_mode_detail(const std::string& server) {
    return "wireguard → " + (server.empty() ? std::string("server") : server);
}

void show_vpn(model::HomeData& data, const model::VpnStatus& tunnel) {
    data.vpn.state = tunnel.state;
    data.vpn.file = tunnel.file;
    data.vpn.server = tunnel.server;
    data.vpn.reply_seconds = tunnel.reply_seconds;
    if (tunnel.state == model::VpnState::On && !data.ap_active) {
        data.mode = model::NetMode::Vpn;
        data.mode_detail = vpn_mode_detail(tunnel.server);
    }
}

void show_client(model::HomeData& data) {
    data.vpn.state = model::VpnState::Off;
    data.vpn.file.clear();
    data.vpn.server.clear();
    data.vpn.reply_seconds = model::kVpnReplyNone;
    data.mode = model::NetMode::Client;
    data.mode_detail.clear();
    data.ap_active = false;
    data.ap_name.clear();
    data.ap_security.clear();
    data.ap_channel = 0;
    data.uptime_seconds = 0;
    data.clients.clear();
}

namespace {

// The made-up server of a pretend VPN request that carries none (a
// documentation name).
constexpr const char* kPretendVpnServer = "vpn.example.net";
// A pretend tunnel's reply time, fixed so pictures repeat.
constexpr int32_t kPretendVpnReplySeconds = 4;

} // namespace

void PretendModeSwitch::request(const ModeRequest& request) {
    if (switching_) {
        return;
    }
    requested_ = true;
    requested_mode_ = request.mode;
    requested_name_ = model::is_hotspot_mode(request.mode) ? request.name : std::string();
    requested_restart_ = request.restart && model::is_hotspot_mode(request.mode);
    requested_vpn_ = model::VpnStatus{};
    if (request.mode == model::NetMode::Vpn) {
        requested_vpn_.state = model::VpnState::On;
        requested_vpn_.file = request.vpn_file;
        requested_vpn_.server = request.vpn_server.empty() ? kPretendVpnServer : request.vpn_server;
        requested_vpn_.reply_seconds = kPretendVpnReplySeconds;
    }
}

void PretendModeSwitch::show_mode(model::HomeData& data) const {
    if (!has_mode_) {
        return;
    }
    if (mode_ == model::NetMode::Vpn) {
        // Only reached through Client: a source's own hotspot goes first.
        if (model::is_hotspot_mode(data.mode)) {
            show_client(data);
        }
        show_vpn(data, vpn_);
    } else if (model::is_hotspot_mode(mode_)) {
        // A source that already shows the mode keeps its own values (the
        // sample's router, for one), apart from a name the switch carried.
        if (data.mode != mode_) {
            const model::HomeData sample = model::sample_home_data();
            show_hotspot(data, mode_, sample.ap_name, sample.ap_security, sample.ap_channel, 0);
        }
        if (!name_.empty()) {
            data.ap_name = name_;
        }
    } else if (data.mode != model::NetMode::Client) {
        show_client(data);
    }
}

void PretendModeSwitch::apply(model::HomeData& data, const PretendFailure& failure) {
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
            name_ = switching_name_;
            vpn_ = switching_vpn_;
        }
    }

    // The pretend mode, once a switch has finished.
    show_mode(data);

    // A new request starts at this reading, so its Switching is shown once.
    if (requested_) {
        requested_ = false;
        if (requested_restart_ && data.mode != requested_mode_) {
            // Nothing runs that could start again; the last status stays.
        } else {
            status_ = model::ModeSwitch{};
            status_.target = requested_mode_;
            if (requested_mode_ == data.mode && !requested_restart_) {
                // Already on: nothing to do.
            } else if ((!requested_restart_ && model::is_hotspot_mode(requested_mode_) &&
                        (model::is_hotspot_mode(data.mode) || data.mode == model::NetMode::Vpn)) ||
                       (requested_mode_ == model::NetMode::Vpn && model::is_hotspot_mode(data.mode))) {
                status_.state = model::SwitchState::Failed;
                status_.error = model::SwitchError::ClientFirst;
            } else if (requested_mode_ == model::NetMode::Router && !requested_restart_ &&
                       data.uplink_kind != model::LinkKind::Cable) {
                status_.state = model::SwitchState::Failed;
                status_.error = model::SwitchError::NoCable;
            } else if (requested_mode_ == model::NetMode::Share && !requested_restart_ &&
                       data.uplink_kind != model::LinkKind::Wifi) {
                status_.state = model::SwitchState::Failed;
                status_.error = model::SwitchError::NoWifi;
            } else {
                status_.state = model::SwitchState::Switching;
                switching_ = true;
                switching_name_ = requested_name_;
                switching_vpn_ = requested_vpn_;
            }
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
