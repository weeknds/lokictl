/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <vector>

// The well-known services that deserve a word of warning when they are
// open, each with a level and one plain line. The Ports page's host
// detail shows them under the open port; the health check uses the same
// table.
namespace model {

// How much a finding matters.
enum class RiskLevel : int32_t {
    Info = 0,  // good to know
    Warning,   // worth a look
    Risk,      // should be fixed
};

const char* risk_level_name(RiskLevel level);  // "info", "warning", "risk"

// One rule: an open TCP port and what to say about it.
struct ServiceRule {
    uint16_t port;
    RiskLevel level;
    const char* note;  // one short line, without the service's name ("sends passwords in plain text")
};

// Every rule, by port, ascending.
const std::vector<ServiceRule>& service_rules();
// The rule of `port`; nullptr when it has none.
const ServiceRule* service_rule(uint16_t port);

} // namespace model
