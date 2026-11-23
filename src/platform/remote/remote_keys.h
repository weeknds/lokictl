/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string_view>

namespace platform::remote {

// The key a browser names in POST /key (k=<name>), as the key path knows it
// (an LV_KEY_* code, platform::kKeyHelp or a printable ASCII character):
// - "c32" to "c126": that ASCII character by its code. Form bodies are not
//   decoded, so characters such as '&', '=', '+' or '%' can't be sent as
//   themselves.
// - "esc", "enter", "backspace", "up", "down", "left", "right", "help".
// Returns false for anything else. There is no name for a held key or for
// fn+J: a browser can only press and let go.
bool key_from_name(std::string_view name, uint32_t& key);

// The keys a paired browser sent and the UI thread has not pressed yet. The
// server's thread adds them, the UI thread takes them, both through the
// mutex. At most kMaxWaiting wait, and at most kMaxPerSecond are taken in
// any one second (counted in fixed one-second windows).
class KeyQueue {
public:
    static constexpr std::size_t kMaxWaiting = 16;
    static constexpr int kMaxPerSecond = 20;

    enum class Push {
        Added,
        Full,    // kMaxWaiting keys wait already
        TooFast, // kMaxPerSecond keys came in this second already
    };

    // Server thread. `now_ms` is on the steady clock.
    Push push(uint32_t key, int64_t now_ms);
    // UI thread: the oldest waiting key. Returns false when none waits.
    bool pop(uint32_t& key);
    // Forgets the waiting keys and the count of this second.
    void clear();

private:
    std::mutex mutex_;
    std::deque<uint32_t> keys_;
    int64_t window_start_ms_{0};
    int window_count_{0};
};

} // namespace platform::remote
