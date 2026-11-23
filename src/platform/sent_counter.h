/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

// The app-wide count of packets LokiCTL has sent in this run of the app:
// every ping echo, every DNS question (retries included) and, in later
// features, every other packet a tool sends. The pages show it as "Sent N".
// Safe to use from any thread.
namespace platform {

// Adds `packets` to the count. Call it once per packet handed to the
// system (a send that succeeded).
void count_sent(uint32_t packets = 1);

// The count so far.
uint64_t sent_packets();

} // namespace platform
