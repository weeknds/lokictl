/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_switch.h"

namespace platform::remote {
namespace {

struct Subjects {
    lv_subject_t state{};
    lv_subject_t requests{};

    Subjects() {
        lv_subject_init_int(&state, static_cast<int32_t>(ViewState::Off));
        lv_subject_init_int(&requests, 0);
    }
};

Subjects& subjects() {
    static Subjects instance;
    return instance;
}

} // namespace

lv_subject_t* state_subject() {
    return &subjects().state;
}

ViewState view_state() {
    switch (lv_subject_get_int(state_subject())) {
        case static_cast<int32_t>(ViewState::Waiting):
            return ViewState::Waiting;
        case static_cast<int32_t>(ViewState::Viewed):
            return ViewState::Viewed;
        default:
            return ViewState::Off;
    }
}

void set_view_state(ViewState state) {
    if (view_state() != state) {
        lv_subject_set_int(state_subject(), static_cast<int32_t>(state));
    }
}

lv_subject_t* switch_request_subject() {
    return &subjects().requests;
}

void request_switch() {
    lv_subject_t* requests = switch_request_subject();
    lv_subject_set_int(requests, lv_subject_get_int(requests) + 1);
}

} // namespace platform::remote
