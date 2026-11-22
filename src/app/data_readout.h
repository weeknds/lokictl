/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_source.h"

#include <cstdint>
#include <string>

namespace app {

// --print-data: takes the first reading from the source and then `steps`
// further ones, one after the other on this thread, and prints the newest
// values as name=value lines on standard output, in this order:
//   source=<sample|demo|live|wifi|cable|offline|tour>
//   sample=<yes|no>
//   clock=<HH:MM>
//   battery_percent=<0-100|unknown>
//   battery_state=<charging|discharging|idle|unknown>
//   mode=<client|router|share|vpn>
//   link=<none|wifi|cable>
//   interface=<name of the uplink's interface|unknown>
//   address=<IPv4 address of the uplink|unknown>
//   router=<IPv4 address of the router the uplink leads to|unknown>
//   speed_mbps=<link speed in Mbit/s|unknown>
//   network=<name of the Wi-Fi network joined|unknown>
//   security=<security of that Wi-Fi network|unknown>
//   signal_dbm=<received signal in dBm, for example -60|unknown>
//   access_point=<yes|no>
//   clients=<number of connected clients>
//   down_kbps=<download rate in kbit/s|unknown>
//   up_kbps=<upload rate in kbit/s|unknown>
//   down_history=<24 shares of 0-100, oldest first, by commas>
//   up_history=<the same for the upload>
// A value that is not known (an empty text, an unknown speed or signal, a
// negative rate) prints `unknown`. The first five lines came first and
// stay first. With the live source on the real system (an empty
// `data_root`) and a link, it waits one traffic interval (1 s) after the
// first reading and takes one more before the steps, so the rates are real. It starts no screen, no logger and no background thread,
// never reads or writes the settings file, and prints nothing else.
// `data_root` is handed to the source (see platform::make_data_source()).
// Returns the process exit code.
int run_data_readout(platform::DataSourceKind kind, uint32_t steps, const std::string& data_root);

} // namespace app
