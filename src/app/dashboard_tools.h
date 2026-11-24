/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dashboard_findings.h"
#include "tool_data.h"

#include <string>
#include <vector>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The web dashboard's Tools and Watch pages: what may start now, and what
// the runs show. Every gate here is the one the Cardputer's own page (or
// its `:` command) uses, so the browser can never start more than a key
// press could; the Remote view controller asks the same functions before
// it applies a request, and the status says the same to the page.

// Why the Ping, DNS, Trace, Ports or Speed page could not start `kind` now
// (tool_command_refusal(): another tool runs, no network for Speed), in
// the device's words; empty when it could. The target is checked apart,
// with model::valid_tool_target().
std::string browser_tool_refusal(model::ToolKind kind, const viewmodel::BaseViewModel& view_model);

// The LAN page's rules for a test: only while neither a test nor the
// server runs ("The server is on") and no other tool does ("Ping is
// running"). The server side stays on the Cardputer: it listens only while
// its LAN page is shown.
std::string browser_lan_refusal(const viewmodel::BaseViewModel& view_model);
// Whether `address` may be tested against as the LAN page lets it: a local
// address (model::valid_lan_target()) that is not one of the Cardputer's
// own.
bool browser_lan_target(const std::string& address, const viewmodel::BaseViewModel& view_model);

// The Survey page's rule: a survey starts only with a free USB Wi-Fi stick
// ("Needs a USB Wi-Fi stick", "Stick is in use", "Can't check the stick").
std::string browser_survey_refusal(const viewmodel::BaseViewModel& view_model);

// The Watch page's rule: Watch starts only with a usable uplink ("Join a
// network first").
std::string browser_watch_refusal(const viewmodel::BaseViewModel& view_model);

// The "tools" object of GET /api/v1/status (see docs/remote.md): each
// tool's state, its page's status line and results in the page's own
// texts (tool_format.h), the gates above as "can"/"why", and Watch and the
// trap with their newest alerts and connections. The switches' states
// always come from the real runs; the lists of Watch and the trap come
// from `sample` when it is given (made-up data, as the overview's), and
// `watch_lines` are the overview's plain words for those alerts, in the
// same order. Built on the UI thread from what the view model holds.
std::string dashboard_tools_json(const viewmodel::BaseViewModel& view_model,
                                 const model::DashSample* sample,
                                 const std::vector<std::string>& watch_lines);

} // namespace app
