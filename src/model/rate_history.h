/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <array>
#include <cstdint>

namespace model {

// The lowest peak a traffic graph is scaled to, kbit/s. On a quiet link a
// few stray bytes (a name lookup, a keep-alive) are the peak; against this
// floor they stay low bars instead of filling the graph. It is 0.1 Mbit/s,
// the smallest rate the page writes as more than "0.0".
constexpr int32_t kRateGraphFloorKbps = 100;

// The last kHomeHistoryLength rates of one direction, oldest first, and the
// graph they make (HomeData::down_history and up_history).
class RateHistory {
public:
    // Forgets every rate: the graph is empty (all 0).
    void clear();

    // Adds `kbps` as the newest rate and drops the oldest one. A negative
    // rate counts as 0.
    void push(int32_t kbps);

    // Each rate as a share (0-100, rounded) of the largest of them, or of
    // kRateGraphFloorKbps when the largest is lower.
    std::array<uint8_t, kHomeHistoryLength> shares() const;

private:
    std::array<int32_t, kHomeHistoryLength> kbps_{};
};

} // namespace model
