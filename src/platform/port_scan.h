/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <string>
#include <vector>

// The port scan of the Ports page: which addresses it may ask, and the
// pretend runner's made-up scan. Nothing here opens a socket; the tool
// runner connects (tool_runner.cpp).
namespace platform {

// One IPv4 address of the Cardputer with its netmask (host byte order).
struct OwnNetwork {
    uint32_t address{0};
    uint32_t netmask{0};
};

// The IPv4 addresses of the Cardputer's interfaces, without loopback, from
// the system (getifaddrs()).
std::vector<OwnNetwork> read_own_networks();

// Whether a scan may ask `target` (host byte order): it must be inside the
// subnet of one of `networks`, and not that subnet's network or broadcast
// address (for subnets with more than two addresses). One of the
// Cardputer's own addresses is allowed. Returns false with a short
// `reason` ("not on the Cardputer's networks") otherwise. The address must
// already pass model::port_scan_address().
bool port_scan_allowed(uint32_t target, const std::vector<OwnNetwork>& networks, std::string& reason);

// The pretend runner's made-up scan of `target`, at `elapsed_ms` into the
// run, timed as the live runner sends (one attempt every
// model::kPortIntervalMs; a port that answers settles at once, a silent one
// after model::kPortTimeoutMs). The Cardputer's made-up networks are those
// of the examples (10.0.0.0/24, 10.42.0.0/24 and 10.20.30.0/24); any other
// address fails at once, with nothing sent. The router (.1) and the
// Cardputer itself have their own sets of open ports; any other device
// gets one of three made-up sets, picked from its address. `sent` is the
// number of attempts made by then; `done` says the run has ended.
model::PortRun pretend_port_scan(uint32_t elapsed_ms, const std::string& target, uint32_t& sent, bool& done);

} // namespace platform
