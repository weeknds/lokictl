/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_controller.h"

#include "base_viewmodel.h"
#include "linux_input.h"
#include "logger.h"
#include "mode_switch.h"
#include "remote/remote_frames.h"
#include "remote/remote_keys.h"
#include "remote/remote_pairing.h"
#include "remote/remote_server.h"
#include "remote/remote_switch.h"
#include "remote_mark.h"
#include "remote_panel.h"
#include "report_files.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

namespace app {
namespace {

using platform::remote::Server;
using platform::remote::ServerPhase;
using platform::remote::ServerStatus;
using platform::remote::StopReason;
using platform::remote::ViewState;

#if USE_DESKTOP
// The desktop timer also hands pictures over, so it runs more often.
constexpr uint32_t kTimerPeriodMs = 100;
// Desktop builds only: "on" switches Remote view on at start.
constexpr const char* kRemoteVariable = "LOKICTL_REMOTE";
#else
constexpr uint32_t kTimerPeriodMs = 250;
#endif

// The key timer presses one waiting key per tick while a browser is paired:
// up to about 33 a second, more than the 20 a second a browser may send.
constexpr uint32_t kKeyPeriodMs = 30;

// How long the panel shows why Remote view could not start.
constexpr uint32_t kMessageMs = 4000;

// Why the server ended, for the log.
const char* stop_text(StopReason reason) {
    switch (reason) {
        case StopReason::None:
        case StopReason::Asked:
            return "switched off";
        case StopReason::NoNetwork:
            return "no network";
        case StopReason::NotPrivate:
            return "not a private network";
        case StopReason::PortBusy:
            return "the port is busy";
        case StopReason::StartFailed:
            return "the port could not be opened";
        case StopReason::CodeUnused:
            return "the code was not used in time";
        case StopReason::WrongCodes:
            return "too many wrong codes";
        case StopReason::ViewerIdle:
            return "no request from the browser for 10 minutes";
        case StopReason::AddressChanged:
            return "the address went away or changed";
        case StopReason::HotspotClosed:
            return "the hotspot side closed";
    }
    return "switched off";
}

// What the panel says when a start fails; nullptr for the reasons that end
// a server that did start (the panel then just goes away).
const char* start_failure_message(StopReason reason) {
    switch (reason) {
        case StopReason::NoNetwork:
            return "Join a network first";
        case StopReason::NotPrivate:
            return "Only on a private network";
        case StopReason::PortBusy:
            return "Port 8780 is busy";
        case StopReason::StartFailed:
            return "Remote view could not start";
        default:
            return nullptr;
    }
}

#if USE_DESKTOP
// Blends `top` (ARGB8888, straight alpha) over `base` (ARGB8888) where they
// overlap; both are snapshots that start at the screen's top-left corner.
void blend_over(lv_draw_buf_t* base, const lv_draw_buf_t* top) {
    const uint32_t width = base->header.w < top->header.w ? base->header.w : top->header.w;
    const uint32_t height = base->header.h < top->header.h ? base->header.h : top->header.h;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* out = static_cast<uint8_t*>(base->data) + static_cast<std::size_t>(y) * base->header.stride;
        const uint8_t* in = static_cast<const uint8_t*>(top->data) + static_cast<std::size_t>(y) * top->header.stride;
        for (uint32_t x = 0; x < width; ++x, out += 4, in += 4) {
            const uint32_t alpha = in[3];
            if (alpha == 0) {
                continue;
            }
            for (int channel = 0; channel < 3; ++channel) {
                out[channel] = static_cast<uint8_t>((in[channel] * alpha + out[channel] * (255U - alpha) + 127U) / 255U);
            }
        }
    }
}
#endif

} // namespace

RemoteController::RemoteController(view::ThemeEngine& theme_engine, const viewmodel::BaseViewModel& view_model)
    : view_model_(view_model),
      panel_(std::make_unique<view::widgets::RemotePanel>(lv_layer_top(), theme_engine)),
      mark_(std::make_unique<view::widgets::RemoteMark>(lv_layer_top(), theme_engine)),
      keys_(std::make_unique<platform::remote::KeyQueue>()) {
    panel_->build();
    mark_->build();

    lv_subject_t* requests = platform::remote::switch_request_subject();
    // LVGL calls the observer once while it is added. Remembering the count
    // first makes that call a no-op.
    requests_seen_ = lv_subject_get_int(requests);
    request_observer_ = lv_subject_add_observer(requests, request_cb, this);
    lv_subject_t* hotspot_requests = platform::remote::hotspot_request_subject();
    hotspot_requests_seen_ = lv_subject_get_int(hotspot_requests);
    hotspot_observer_ = lv_subject_add_observer(hotspot_requests, hotspot_request_cb, this);

    timer_ = lv_timer_create(timer_cb, kTimerPeriodMs, this);
    if (timer_) {
        lv_timer_pause(timer_);
    }
    key_timer_ = lv_timer_create(key_timer_cb, kKeyPeriodMs, this);
    if (key_timer_) {
        lv_timer_pause(key_timer_);
    }
    platform::remote::set_view_state(ViewState::Off);
    platform::remote::set_hotspot_allowed(false);

#if USE_DESKTOP
    const char* value = std::getenv(kRemoteVariable);
    if (value && std::strcmp(value, "on") == 0) {
        LOG_INFO("remote view: switched on by {}; the code goes to standard error", kRemoteVariable);
        print_code_ = true;
        switch_on();
    }
#endif
}

RemoteController::~RemoteController() {
    stop();
}

void RemoteController::stop() {
    if (stopped_) {
        return;
    }
    stopped_ = true;

    lv_async_call_cancel(apply_requests_async, this);
    if (request_observer_) {
        lv_observer_remove(request_observer_);
        request_observer_ = nullptr;
    }
    if (hotspot_observer_) {
        lv_observer_remove(hotspot_observer_);
        hotspot_observer_ = nullptr;
    }
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (key_timer_) {
        lv_timer_delete(key_timer_);
        key_timer_ = nullptr;
    }
    key_timer_running_ = false;

    // Both servers are told first, so they wind down together; destroying
    // them then waits for their threads.
    const bool was_on = server_ != nullptr;
    if (server_) {
        server_->request_stop();
    }
    if (retiring_) {
        retiring_->request_stop();
    }
    hand_off_ = nullptr;
    server_.reset();
    retiring_.reset();
    if (was_on) {
        LOG_INFO("remote view: off (the app quits)");
    }

    panel_.reset();
    mark_.reset();
    set_hotspot(false, false);
    platform::remote::set_view_state(ViewState::Off);
}

void RemoteController::request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<RemoteController*>(lv_observer_get_user_data(observer));
    if (!controller || controller->stopped_) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->requests_seen_) {
        return;
    }
    controller->requests_pending_ += count - controller->requests_seen_;
    controller->requests_seen_ = count;
    controller->schedule_apply();
}

void RemoteController::hotspot_request_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* controller = static_cast<RemoteController*>(lv_observer_get_user_data(observer));
    if (!controller || controller->stopped_) {
        return;
    }
    const int32_t count = lv_subject_get_int(subject);
    if (count == controller->hotspot_requests_seen_) {
        return;
    }
    controller->hotspot_requests_pending_ += count - controller->hotspot_requests_seen_;
    controller->hotspot_requests_seen_ = count;
    controller->schedule_apply();
}

void RemoteController::schedule_apply() {
    // The request arrives inside a key callback; the switch runs a moment
    // later from lv_timer_handler(), like a theme switch.
    if (!apply_scheduled_) {
        apply_scheduled_ = lv_async_call(apply_requests_async, this) == LV_RESULT_OK;
        if (!apply_scheduled_) {
            LOG_ERROR("remote view: could not schedule the switch");
            requests_pending_ = 0;
            hotspot_requests_pending_ = 0;
        }
    }
}

void RemoteController::apply_requests_async(void* user_data) {
    auto* controller = static_cast<RemoteController*>(user_data);
    if (controller) {
        controller->apply_requests();
    }
}

void RemoteController::apply_requests() {
    apply_scheduled_ = false;
    // Two presses before the switch ran cancel each other out.
    const bool toggle = requests_pending_ % 2 != 0;
    const bool toggle_hotspot = hotspot_requests_pending_ % 2 != 0;
    requests_pending_ = 0;
    hotspot_requests_pending_ = 0;
    if (stopped_) {
        return;
    }
    if (toggle) {
        if (server_) {
            switch_off();
        } else {
            switch_on();
        }
    }
    // Hotspot devices can only be allowed while Remote view is on.
    if (toggle_hotspot && server_) {
        set_hotspot(!hotspot_allowed_, true);
        resume_timer();
    }
}

void RemoteController::set_hotspot(bool allowed, bool log) {
    if (allowed == hotspot_allowed_) {
        return;
    }
    hotspot_allowed_ = allowed;
    platform::remote::set_hotspot_allowed(allowed);
    if (!allowed && server_) {
        server_->set_hotspot_interface({});
    }
    if (log) {
        LOG_INFO("remote view: hotspot devices {}", allowed ? "allowed" : "not allowed");
    }
}

std::string RemoteController::hotspot_interface() const {
    if (!hotspot_allowed_) {
        return {};
    }
    const model::HomeData& home = view_model_.home();
    if (home.sample || !home.ap_active) {
        return {};
    }
    switch (home.mode) {
        case model::NetMode::Router:
            return platform::kHotspotInterface;
        case model::NetMode::Share:
            return platform::kShareInterface;
        case model::NetMode::Client:
        case model::NetMode::Vpn:
            break;
    }
    return {};
}

void RemoteController::switch_on() {
    if (server_ || stopped_) {
        return;
    }
    // A server that is still winding down holds the port for a moment more.
    if (retiring_) {
        retiring_->join();
        retiring_.reset();
    }
    if (panel_) {
        panel_->hide();
    }
    message_shown_ = false;
    keys_->clear();
    set_hotspot(false, false);
    hotspot_open_seen_ = false;

    platform::remote::ServerOptions options;
    options.keys = keys_.get();
    options.reports_folder = platform::report_folder();
    std::unique_ptr<platform::remote::FrameSource> frames;
#if USE_DESKTOP
    options.loopback_only = true;
    auto hand_off = std::make_unique<platform::remote::HandOffSource>();
    hand_off_ = hand_off.get();
    frames = std::move(hand_off);
#else
    frames = platform::remote::make_framebuffer_source(APP_FRAMEBUFFER_DEVICE);
#endif
    server_ = std::make_unique<Server>(options, std::move(frames));
    wrong_codes_seen_ = 0;
    listening_seen_ = false;
    paired_seen_ = false;
    server_->start();
    platform::remote::set_view_state(ViewState::Waiting);
    resume_timer();
}

void RemoteController::switch_off() {
    if (!server_) {
        return;
    }
    server_->request_stop();
    if (retiring_) {
        retiring_->join();
    }
    retiring_ = std::move(server_);
    hand_off_ = nullptr;
    run_key_timer(false);
    set_hotspot(false, false);
    hotspot_open_seen_ = false;
    if (panel_) {
        panel_->hide();
    }
    if (mark_) {
        mark_->hide();
    }
    message_shown_ = false;
    platform::remote::set_view_state(ViewState::Off);
    LOG_INFO("remote view: off ({})", stop_text(StopReason::Asked));
    // The timer ends the retiring server's thread once it is done.
    resume_timer();
}

void RemoteController::resume_timer() {
    if (timer_) {
        lv_timer_resume(timer_);
    }
}

void RemoteController::run_key_timer(bool run) {
    if (run == key_timer_running_) {
        return;
    }
    key_timer_running_ = run;
    if (!run) {
        keys_->clear();
    }
    if (key_timer_) {
        if (run) {
            lv_timer_resume(key_timer_);
        } else {
            lv_timer_pause(key_timer_);
        }
    }
}

void RemoteController::key_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<RemoteController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->press_waiting_key();
    }
}

void RemoteController::press_waiting_key() {
    if (!server_ || !key_timer_running_ || stopped_) {
        return;
    }
    uint32_t key = 0;
    if (!keys_->pop(key)) {
        return;
    }
    // Press and release together: every key acts on its press (ESC on its
    // release), so nothing is lost, and a key that is never held can't
    // become a long press (a held ESC quits the app) or stay down when the
    // key itself switches Remote view off. One key per tick, so the next
    // one finds the page this one asked for.
    platform::inject_key(key, true);
    platform::inject_key(key, false);
}

void RemoteController::timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<RemoteController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

void RemoteController::poll() {
    if (retiring_ && retiring_->finished()) {
        retiring_->join();
        retiring_.reset();
    }

    if (server_) {
        server_->set_hotspot_interface(hotspot_interface());
        const ServerStatus status = server_->status();
        const bool hotspot_open = !status.hotspot_url.empty();
        if (hotspot_open != hotspot_open_seen_ && status.phase != ServerPhase::Stopped) {
            hotspot_open_seen_ = hotspot_open;
            LOG_INFO("remote view: hotspot side {}", hotspot_open ? "open" : "closed");
        }
        if (status.wrong_codes > wrong_codes_seen_) {
            wrong_codes_seen_ = status.wrong_codes;
            LOG_INFO("remote view: wrong code ({} of {})",
                     wrong_codes_seen_,
                     platform::remote::Pairing::kMaxWrongCodes);
        }
        switch (status.phase) {
            case ServerPhase::Starting:
                break;
            case ServerPhase::Listening: {
                if (!listening_seen_) {
                    listening_seen_ = true;
                    LOG_INFO("remote view: on, port {}", platform::remote::kPort);
#if USE_DESKTOP
                    if (print_code_) {
                        print_code_ = false;
                        std::fprintf(stderr, "remote view: code %s at %s\n", status.code.c_str(), status.url.c_str());
                        std::fflush(stderr);
                    }
#endif
                }
                const int64_t left_ms = status.code_deadline_ms - Server::now_ms();
                if (panel_) {
                    panel_->show_code(status.url,
                                      status.hotspot_url,
                                      status.code,
                                      static_cast<int32_t>((left_ms + 999) / 1000));
                }
                if (mark_) {
                    mark_->hide();
                }
                platform::remote::set_view_state(ViewState::Waiting);
                run_key_timer(false);
                break;
            }
            case ServerPhase::Paired:
                if (!paired_seen_) {
                    paired_seen_ = true;
                    LOG_INFO("remote view: paired");
                }
                if (panel_) {
                    panel_->hide();
                }
                if (mark_) {
                    mark_->show();
                }
                platform::remote::set_view_state(ViewState::Viewed);
                run_key_timer(true);
                break;
            case ServerPhase::Stopped: {
                server_->join();
                const char* message = listening_seen_ ? nullptr : start_failure_message(status.reason);
                if (message && panel_) {
                    LOG_WARN("remote view: could not start ({})", stop_text(status.reason));
                    panel_->show_message(message);
                    message_shown_ = true;
                    message_started_ = lv_tick_get();
                } else {
                    LOG_INFO("remote view: off ({})", stop_text(status.reason));
                    if (panel_) {
                        panel_->hide();
                    }
                }
                if (mark_) {
                    mark_->hide();
                }
                hand_off_ = nullptr;
                server_.reset();
                run_key_timer(false);
                set_hotspot(false, false);
                hotspot_open_seen_ = false;
                platform::remote::set_view_state(ViewState::Off);
                break;
            }
        }
        if (server_ && hand_off_) {
            hand_off_frame();
        }
    }

    if (message_shown_ && lv_tick_elaps(message_started_) >= kMessageMs) {
        message_shown_ = false;
        if (panel_) {
            panel_->hide();
        }
    }

    if (!server_ && !retiring_ && !message_shown_ && timer_) {
        lv_timer_pause(timer_);
    }
}

void RemoteController::hand_off_frame() {
#if USE_DESKTOP
    if (!hand_off_ || !hand_off_->wanted()) {
        return;
    }
    lv_obj_t* screen = lv_screen_active();
    if (!screen) {
        return;
    }
    lv_draw_buf_t* snapshot = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);
    if (!snapshot) {
        return;
    }
    // A snapshot of the screen leaves out the top layer (help, messages,
    // the panel and the mark), which the device's pictures have: it is
    // taken on its own, on a clear ground, and blended over.
    lv_draw_buf_t* top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
    if (top) {
        blend_over(snapshot, top);
        lv_draw_buf_destroy(top);
    }
    // ARGB8888 is stored as B, G, R, A: as a 32-bit word, red sits at bit
    // 16, green at bit 8 and blue at bit 0.
    platform::remote::PixelLayout layout;
    layout.width = static_cast<int32_t>(snapshot->header.w);
    layout.height = static_cast<int32_t>(snapshot->header.h);
    layout.stride = static_cast<int32_t>(snapshot->header.stride);
    layout.bits_per_pixel = 32;
    layout.red = {16, 8};
    layout.green = {8, 8};
    layout.blue = {0, 8};
    hand_off_->deliver(static_cast<const uint8_t*>(snapshot->data), layout);
    lv_draw_buf_destroy(snapshot);
#endif
}

} // namespace app
