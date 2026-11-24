/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <string>

namespace model {

// The web dashboard's GET /api/v1/status: what the home page already knows,
// as one JSON object. Built on the UI thread from the data the app has; it
// never asks the system for anything. Every text from the network (the
// Wi-Fi name) goes through json_text(). Unknown numbers are null.
//
// {"v":1,"sample":false,
//  "cardputer":{"address":"10.0.0.12","battery":87,"charging":false},
//  "network":{"name":"Home","link":"wifi","internet":"full"},
//  "mode":{"id":"router","name":"Router","hotspot":true,"hotspot_devices":3,
//          "vpn":"off","kill_switch":false},
//  "traffic":{"down_kbps":1200,"up_kbps":80}}
//
// "link" is "wifi", "cable" or "none"; "internet" is internet_name();
// "id" is "client", "router", "share" or "vpn"; "vpn" is vpn_state_name().
std::string dashboard_status_json(const HomeData& home);

} // namespace model
