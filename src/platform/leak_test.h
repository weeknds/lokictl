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

// The VPN leak test of the Leak page (F9.8): with the VPN on, it checks that
// name lookups and IPv6 go by the tunnel, which public address the internet
// sees, and, in the VPN gateway, that the kill switch is armed. The verdicts
// and words are in model/leak_check.h.
namespace platform {

// The pretend runner's made-up test at `elapsed_ms` into the run: the
// system's checks at once, the address after a short wait. With the VPN on
// everything passes, except with the made-up file "travel": then name
// lookups and IPv6 go around the tunnel, and in the gateway the kill switch
// is missing. With the VPN off it only notes the address. Documentation
// addresses only; it opens no socket. `without_vpn` is the address a run
// with the VPN off saw (empty: none); `sent` is the requests made by then
// (one); `done` says the run has ended.
model::LeakRun pretend_leak_test(uint32_t elapsed_ms, const model::LeakLocal& local, const std::string& without_vpn,
                                 uint32_t& sent, bool& done);

// The made-up file whose tunnel leaks in pretend_leak_test().
constexpr const char* kPretendLeakyFile = "travel";

#if !USE_DESKTOP
// Runs one test on the calling thread, the tool runner's, and returns when
// it ends. With the VPN on (local.vpn_on):
// - name lookups: each nameserver of /etc/resolv.conf the resolver uses
//   (at most three, IPv4 and IPv6) gets an unconnected UDP socket that is
//   connected to it, which sends nothing; the interface of the source
//   address the system picks is the way the lookups go;
// - IPv6: when an interface other than the tunnel has a global IPv6
//   address, the same for a public IPv6 address;
// - in the gateway (local.gateway): the helper's `list` says whether the
//   kill chains are there;
// - the public address (fetch_public_address(), speed_test.h), the only
//   thing sent: one counted HTTPS request.
// With the VPN off only the kill switch (in the gateway) and the address
// are checked, and the address is kept in `without_vpn` for the next runs'
// comparison (in memory only). `publish` gets the run at each change, on
// this thread; `stopping` is asked between the checks and during the
// request. Nothing is logged.
void run_leak_test(const model::LeakLocal& local,
                   std::string& without_vpn,
                   const std::function<bool()>& stopping,
                   const std::function<void(const model::LeakRun&)>& publish);
#endif

} // namespace platform
