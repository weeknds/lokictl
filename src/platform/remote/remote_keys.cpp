/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote/remote_keys.h"

#include "linux_input.h"
#include "lvgl.h"

namespace platform::remote {
namespace {

constexpr int64_t kWindowMs = 1000;

struct NamedKey {
    std::string_view name;
    uint32_t key;
};

constexpr NamedKey kNamedKeys[] = {
    {"esc", LV_KEY_ESC},
    {"enter", LV_KEY_ENTER},
    {"backspace", LV_KEY_BACKSPACE},
    {"up", LV_KEY_UP},
    {"down", LV_KEY_DOWN},
    {"left", LV_KEY_LEFT},
    {"right", LV_KEY_RIGHT},
    {"help", platform::kKeyHelp},
};

} // namespace

bool key_from_name(std::string_view name, uint32_t& key) {
    for (const NamedKey& entry : kNamedKeys) {
        if (entry.name == name) {
            key = entry.key;
            return true;
        }
    }

    // "c" and two or three digits, without leading zeros: c32 to c126.
    if (name.size() < 3 || name.size() > 4 || name.front() != 'c' || name[1] == '0') {
        return false;
    }
    uint32_t code = 0;
    for (const char c : name.substr(1)) {
        if (c < '0' || c > '9') {
            return false;
        }
        code = code * 10 + static_cast<uint32_t>(c - '0');
    }
    if (code < 32 || code > 126) {
        return false;
    }
    key = code;
    return true;
}

KeyQueue::Push KeyQueue::push(uint32_t key, int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (window_count_ == 0 || now_ms - window_start_ms_ >= kWindowMs) {
        window_start_ms_ = now_ms;
        window_count_ = 0;
    }
    if (window_count_ >= kMaxPerSecond) {
        return Push::TooFast;
    }
    if (keys_.size() >= kMaxWaiting) {
        return Push::Full;
    }
    keys_.push_back(key);
    ++window_count_;
    return Push::Added;
}

bool KeyQueue::pop(uint32_t& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keys_.empty()) {
        return false;
    }
    key = keys_.front();
    keys_.pop_front();
    return true;
}

void KeyQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_.clear();
    window_start_ms_ = 0;
    window_count_ = 0;
}

} // namespace platform::remote
