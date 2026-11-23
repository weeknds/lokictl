/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "trap_data.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The trap (model::TrapRun): fake services that log whoever connects.
namespace platform {

// The local TCP ports something listens on, from a file in the format of
// /proc/net/tcp or /proc/net/tcp6 (any address, any user); sorted, each
// once. An unreadable file gives none.
std::vector<uint16_t> read_listening_ports(const std::string& path);

// Runs the trap away from the UI thread and keeps its latest state for the
// screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first run and kept until shutdown(). A run:
// - leaves every port of model::trap_services() alone that something on
//   the Cardputer already listens on (/proc/net/tcp and tcp6), and listens
//   (IPv4, as the normal user) on the listen port of each other one;
// - for the low ports, runs the firewall helper's trap-on (platform::
//   trap_on()), which sends them on to the listen ports; a low port it did
//   not send on is closed again (in use, or no helper);
// - then accepts connections: within the limits (model::TrapLimiter) each
//   is logged at once, gets its service's harmless greeting (counted in
//   Sent), is read for at most model::kTrapReadSeconds and
//   model::kTrapReadMax bytes, and is closed; at most 8 are open at once.
//   Anything over the limits is closed at once and only counted; a
//   connection from the Cardputer itself (its own address or loopback) is
//   closed and not counted. Nothing received is ever run, stored on disk
//   or passed on.
// - every 10 s it checks again that nothing else began to listen on a low
//   port it sends on; if something did, trap-on runs again, which leaves
//   that port alone.
// A stop closes every socket and runs trap-off (a failure of that is left
// in helper_problem); every wait is at most 100 ms, so a stop takes effect
// within about 0.2 s plus the helper's run.
//
// The pretend runner (the Mac simulator and snapshots) has no thread and
// opens nothing: each take() adds the made-up connections up to the LVGL
// time since start(), so the same keys and settle time always give the
// same picture.
//
// All functions are called on the UI thread.
class TrapRunner {
public:
    virtual ~TrapRunner() = default;

    // Starts a run, which then reads Running at once. Returns false and
    // changes nothing while one runs or after shutdown().
    virtual bool start() = 0;
    // Asks the run to end; it then reads Stopped. Never waits.
    virtual void stop() = 0;
    // Copies the latest state into `run` and returns true, but only when it
    // changed since the last take().
    virtual bool take(model::TrapRun& run) = 0;
    // Stops the run (trap-off included) and ends the thread, waiting for
    // it. Safe to call more than once; start() refuses every run
    // afterwards.
    virtual void shutdown() = 0;
};

std::unique_ptr<TrapRunner> make_trap_runner(bool live);

} // namespace platform
