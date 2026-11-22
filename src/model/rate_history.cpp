/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "rate_history.h"

#include <algorithm>
#include <cstddef>

namespace model {

void RateHistory::clear() {
    kbps_.fill(0);
}

void RateHistory::push(int32_t kbps) {
    std::rotate(kbps_.begin(), kbps_.begin() + 1, kbps_.end());
    kbps_.back() = std::max<int32_t>(kbps, 0);
}

std::array<uint8_t, kHomeHistoryLength> RateHistory::shares() const {
    const int64_t peak = *std::max_element(kbps_.begin(), kbps_.end());
    const int64_t scale = std::max<int64_t>(peak, kRateGraphFloorKbps);
    std::array<uint8_t, kHomeHistoryLength> result{};
    for (std::size_t index = 0; index < kbps_.size(); ++index) {
        const int64_t share = (static_cast<int64_t>(kbps_[index]) * 100 + scale / 2) / scale;
        result[index] = static_cast<uint8_t>(std::clamp<int64_t>(share, 0, 100));
    }
    return result;
}

} // namespace model
