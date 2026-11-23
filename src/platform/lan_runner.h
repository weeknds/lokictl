/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lan_speed.h"

#include <memory>
#include <string>

// The LAN speed test's runs (model::LanRun): iperf3 started as a child
// process, its --json-stream output read as it comes.
namespace platform {

// Where Debian's iperf3 package puts the program.
constexpr const char* kIperfPath = "/usr/bin/iperf3";

// Runs one LAN run at a time away from the UI thread and keeps its latest
// state for the screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first run and kept until shutdown(). It never runs iperf3 as
// root, always with a fixed argument list, no shell and an empty
// environment, its output read from a pipe and its other output thrown
// away; the child gets no file of LokiCTL's but the pipe and is killed
// when LokiCTL's runner thread ends, also when LokiCTL is killed.
// - A client run is two iperf3 runs against the other machine's port
//   model::kLanPort: model::kLanPhaseSeconds with the other end sending
//   (-R), then as long with the Cardputer sending, each reporting every
//   model::kLanSampleMs. The first error ends the run.
// - A server run listens on one of the Cardputer's own IPv4 addresses
//   (-B, port model::kLanPort) for one test at a time (-1) and starts again
//   after each; a test longer than model::kLanServeTestMaxSeconds is ended.
//   It ends only when stopped, or when iperf3 cannot listen there.
// Each test counts once in the Sent count. Every wait is at most 100 ms,
// so a stop ends the child (SIGTERM, then SIGKILL) within about half a
// second; nothing is left running.
//
// The pretend runner (the Mac simulator and snapshots) has no thread and
// starts nothing: each take() makes up the run up to the LVGL time since
// it started.
//
// All functions are called on the UI thread.
class LanRunner {
public:
    virtual ~LanRunner() = default;

    // Starts a client run against `target` (model::valid_lan_target()); it
    // then reads Running at once. Returns false and changes nothing while a
    // run is under way or ending, after shutdown() and for a bad target.
    virtual bool start_client(const std::string& target) = 0;
    // Starts a server run on `address`, one of the Cardputer's own IPv4
    // addresses (the caller checks that). Returns false as above.
    virtual bool start_server(const std::string& address) = 0;
    // Asks the run to end; it then reads Stopped. Never waits.
    virtual void stop() = 0;
    // Copies the latest state into `run` (all but model::LanRun::places)
    // and returns true, but only when it changed since the last take().
    virtual bool take(model::LanRun& run) = 0;
    // Stops the run and ends the thread, waiting for it. Safe to call more
    // than once; every start is refused afterwards.
    virtual void shutdown() = 0;
    // The first IPv4 address of `interface_name` (the hotspot's), empty
    // when it has none. The pretend runner gives the examples' hotspot
    // address.
    virtual std::string interface_address(const std::string& interface_name) = 0;
};

std::unique_ptr<LanRunner> make_lan_runner(bool live);

} // namespace platform
