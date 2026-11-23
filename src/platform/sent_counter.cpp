/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "sent_counter.h"

#include <atomic>

namespace platform {
namespace {

std::atomic<uint64_t> sent_count{0};

} // namespace

void count_sent(uint32_t packets) {
    sent_count.fetch_add(packets, std::memory_order_relaxed);
}

uint64_t sent_packets() {
    return sent_count.load(std::memory_order_relaxed);
}

} // namespace platform
