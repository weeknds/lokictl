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

// The speed test of the Speed page: a download, then an upload, each for
// model::kSpeedPhaseMs or model::kSpeedPhaseBytesMax, whichever comes
// first, against a speed server with Cloudflare's endpoints (GET
// <server>/__down?bytes=N, POST <server>/__up).
namespace platform {

// The pretend runner's made-up test at `elapsed_ms` into the run, timed
// like a live one: a short connect, the download (about 89 Mbit/s, so it
// ends at the byte cap after about 4.8 s), a short pause while the upload
// connects, 10 s of upload (about 18 Mbit/s). It opens no socket. `sent`
// is the number of requests made by then; `done` says the run has ended.
model::SpeedRun pretend_speed_test(uint32_t elapsed_ms, const std::string& server, uint32_t& sent, bool& done);

#if !USE_DESKTOP
// Runs one test against `server` (model::valid_speed_server()) on the
// calling thread, the tool runner's, and returns when it ends. libcurl is
// loaded for the run (dlopen of libcurl.so.4) and let go after it, unless a
// name lookup may still run inside it (then it stays loaded for the
// session); freed memory goes back to the system. A device build without
// its headers, or a system without the library, fails the run with the
// reason.
//
// It makes one small request first (connect and TLS), then downloads of at
// most 25 MB one after another until model::kSpeedPhaseMs passed since the
// first body byte or the phase moved model::kSpeedPhaseBytesMax, then
// uploads of at most 10 MB the same way. Requests never ask for more than
// the phase may still move; a phase cut short by the cap ends with one
// shorter sample. One curl multi handle drives each request with waits of
// at most 100 ms, and `stopping` is asked after each wait, so a stop ends
// the run within about 0.1 s. Downloaded bytes are the body bytes
// received; uploaded bytes are those the server acknowledged (the kernel's
// TCP_INFO tcpi_bytes_acked, never more than the body bytes handed over),
// since bytes handed to the system wait in its send buffer first. Each
// request is counted once with count_sent(). Certificates are always
// checked; no redirect is followed. `publish` gets the run whenever it
// changes (at each sample, every model::kSpeedSampleMs, and at each change
// of state or phase), on this thread. Every handle and socket is freed on
// the way out, whatever ends the run. Nothing is logged.
void run_speed_test(const std::string& server,
                    const std::function<bool()>& stopping,
                    const std::function<void(const model::SpeedRun&)>& publish);

// The leak test's public address (F9.8): one GET of model::kLeakTraceUrl
// over HTTPS and IPv4 only, on the calling thread (the tool runner's),
// through libcurl loaded the same way as for the speed test. The answer is
// read up to 4 KB; its "ip=" line gives `address` (an IPv4 address in
// dotted form). The request is counted once with count_sent(); it gives
// up after 15 s, and `stopping` is asked at least every 0.1 s. Returns an
// empty text with `address` set, else a short reason ("stopped" after a
// stop). Certificates are checked; no redirect is followed; nothing is
// logged.
std::string fetch_public_address(const std::function<bool()>& stopping, std::string& address);
#endif

} // namespace platform
