/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <functional>
#include <string>

// The speed test of the Speed page: a 10 s download, then a 10 s upload,
// against a speed server with Cloudflare's endpoints (GET
// <server>/__down?bytes=N, POST <server>/__up).
namespace platform {

// The pretend runner's made-up test at `elapsed_ms` into the run, timed
// like a live one: a short connect, 10 s of download, a short pause while
// the upload connects, 10 s of upload. It opens no socket. `sent` is the
// number of requests made by then; `done` says the run has ended.
model::SpeedRun pretend_speed_test(uint32_t elapsed_ms, const std::string& server, uint32_t& sent, bool& done);

#if !USE_DESKTOP
// Runs one test against `server` (model::valid_speed_server()) on the
// calling thread, the tool runner's, and returns when it ends. libcurl is
// loaded the first time (dlopen of libcurl.so.4) and kept loaded; a device
// build without its headers, or a system without the library, fails the
// run with the reason.
//
// It makes one small request first (connect and TLS), then 25 MB downloads
// one after another until model::kSpeedPhaseMs passed since the first body
// byte, then 10 MB uploads the same way. One curl multi handle drives each
// request with waits of at most 100 ms, and `stopping` is asked after each
// wait, so a stop ends the run within about 0.1 s. Downloaded bytes are the
// body bytes received; uploaded bytes are those the server acknowledged
// (the kernel's TCP_INFO tcpi_bytes_acked), since bytes handed to the
// system wait in its send buffer first. Each request is counted once with
// count_sent(). Certificates are always checked; no redirect is followed.
// `publish` gets the run whenever it changes (at each sample, every
// model::kSpeedSampleMs, and at each change of state or phase), on this
// thread. Every handle and socket is freed on the way out, whatever ends
// the run. Nothing is logged.
void run_speed_test(const std::string& server,
                    const std::function<bool()>& stopping,
                    const std::function<void(const model::SpeedRun&)>& publish);
#endif

} // namespace platform
