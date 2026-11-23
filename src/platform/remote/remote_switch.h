/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace platform::remote {

// What the Remote view switch shows on the Settings page.
enum class ViewState : int32_t {
    Off = 0,     // no server, no port open
    Waiting = 1, // the server listens and the Cardputer shows a code
    Viewed = 2,  // a browser is paired
};

// The module's own subjects, shared by the Settings row and
// app::RemoteController. They live outside the view model, so the row works
// in every run: in a snapshot run nothing answers a request and the row
// stays "off". Both are set up on first use and must only be used on the UI
// thread, after lv_init().

// The state, as a ViewState value. Only app::RemoteController sets it.
lv_subject_t* state_subject();
ViewState view_state();
void set_view_state(ViewState state);

// Counts the requests to switch Remote view on or off; every request adds
// one, so each one reaches the observers. The controller acts a moment
// later (see request_switch()).
lv_subject_t* switch_request_subject();

// Asks to switch Remote view on while it is off, and off while it is on.
// Safe to call from key and click callbacks: it only counts the request.
void request_switch();

} // namespace platform::remote
