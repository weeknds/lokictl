/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <memory>
#include <string>

namespace platform {

// One job for a ToolRunner: a ping run, a DNS lookup or a trace of
// `target`, or listening on the interface `target`.
struct ToolJob {
    model::ToolKind kind{model::ToolKind::Ping};
    std::string target;  // checked with model::valid_tool_target()
};

// Runs the tools (ping, DNS lookup, trace, listening) away from the UI
// thread, one job at a time, and keeps the latest run of each for the screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first job and kept until shutdown(); it never touches the view
// model, LVGL or the log. Every wait in it is at most 100 ms, so a stop is
// seen quickly: a job ends within about 0.1 s of stop(), and shutdown()
// returns within about 0.2 s (a listen takes up to 0.1 s more, while the
// helper ends). It sends only what the tools need: echoes on
// an unprivileged ICMP datagram socket (a trace sets each echo's time to
// live and reads the hops' answers from the socket's error queue) and
// questions to the IPv4 nameservers of /etc/resolv.conf (the first one;
// the retry goes to the next one), each counted with count_sent(). A listen
// sends nothing: it runs the capture helper (platform::CaptureProcess,
// which alone may open the packet socket and only receives) and reads its
// stream into the device list (listen.h). It never changes a network
// setting.
//
// The pretend runner (the Mac simulator, snapshots, and the made-up data
// sources) has no thread and never opens a socket: each take() works out
// the run from the LVGL time since the job started, with fixed made-up
// results (documentation addresses only; a listen reads a made-up capture,
// pretend_capture()), so the same keys and settle time always give the same
// picture. It still adds the packets it pretends to send to the Sent count
// (none for a listen).
//
// All functions are called on the UI thread.
class ToolRunner {
public:
    virtual ~ToolRunner() = default;

    // Starts `job`, whose run then reads Running at once. Returns false and
    // changes nothing while a job runs or when the target is not valid for
    // the kind (model::valid_tool_target()).
    virtual bool start(const ToolJob& job) = 0;
    // Asks the running job to end; it then reads Stopped. Never waits; does
    // nothing without a job.
    virtual void stop() = 0;
    // Copies the latest runs into data.ping, data.dns, data.trace and
    // data.listen (data.sent is left alone) and returns true, but only when any changed
    // since the last take(); otherwise leaves `data` alone and returns
    // false.
    virtual bool take(model::ToolsData& data) = 0;
    // Stops the job and ends the thread, waiting for it. Safe to call more
    // than once; start() refuses every job afterwards.
    virtual void shutdown() = 0;
};

// The live runner on device builds when `live` is true; otherwise the
// pretend runner.
std::unique_ptr<ToolRunner> make_tool_runner(bool live);

} // namespace platform
