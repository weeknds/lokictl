/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_pairing.h"

#include <cerrno>
#include <cstring>

#include <sys/random.h>
#include <sys/types.h>
#include <unistd.h>

namespace platform::remote {
namespace {

constexpr std::size_t kCodeDigits = 6;
constexpr uint32_t kCodeRange = 1000000;
// The largest multiple of kCodeRange that fits in 32 bits: values at or
// above it are drawn again, so every code is equally likely.
constexpr uint32_t kCodeLimit = (UINT32_MAX / kCodeRange) * kCodeRange;
constexpr std::size_t kTokenBytes = 16;

// The code a browser sent, without spaces and '+'; empty when anything else
// in it is not a digit or it does not hold exactly six digits.
std::string clean_code(std::string_view sent) {
    std::string digits;
    for (const char c : sent) {
        if (c == ' ' || c == '+') {
            continue;
        }
        if (c < '0' || c > '9' || digits.size() == kCodeDigits) {
            return {};
        }
        digits.push_back(c);
    }
    return digits.size() == kCodeDigits ? digits : std::string();
}

} // namespace

bool random_bytes(uint8_t* out, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
#if defined(__linux__)
        const ssize_t got = getrandom(out + done, size - done, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        done += static_cast<std::size_t>(got);
#else
        // getentropy() gives at most 256 bytes per call.
        const std::size_t chunk = size - done < 256 ? size - done : 256;
        if (getentropy(out + done, chunk) != 0) {
            return false;
        }
        done += chunk;
#endif
    }
    return true;
}

bool make_pairing_code(std::string& code) {
    code.clear();
    uint32_t value = 0;
    do {
        uint8_t bytes[4];
        if (!random_bytes(bytes, sizeof(bytes))) {
            return false;
        }
        value = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
                (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
    } while (value >= kCodeLimit);
    value %= kCodeRange;

    code.assign(kCodeDigits, '0');
    for (std::size_t index = kCodeDigits; index > 0; --index) {
        code[index - 1] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    return true;
}

bool make_session_token(std::string& token) {
    token.clear();
    uint8_t bytes[kTokenBytes];
    if (!random_bytes(bytes, sizeof(bytes))) {
        return false;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    token.reserve(kTokenBytes * 2);
    for (const uint8_t byte : bytes) {
        token.push_back(kHex[byte >> 4]);
        token.push_back(kHex[byte & 0x0F]);
    }
    std::memset(bytes, 0, sizeof(bytes));
    return true;
}

bool equal_in_constant_time(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        difference |= static_cast<unsigned char>(a[index] ^ b[index]);
    }
    return difference == 0;
}

Pairing::Pairing(int64_t now_ms) : code_deadline_ms_(now_ms + kCodeLifetimeMs), last_seen_ms_(now_ms) {
    if (!make_pairing_code(code_)) {
        end_ = End::NoRandom;
    }
}

Pairing::Attempt Pairing::try_code(std::string_view code, int64_t now_ms, std::string& token) {
    token.clear();
    if (check_time(now_ms) != End::None) {
        return Attempt::Ended;
    }
    if (paired()) {
        return Attempt::Busy;
    }
    const std::string sent = clean_code(code);
    // Compared even when the sent text was not six digits, so a wrong code
    // takes the same path every time.
    const bool right = equal_in_constant_time(sent.empty() ? std::string(kCodeDigits, 'x') : sent, code_);
    if (!right || sent.empty()) {
        return count_wrong(now_ms);
    }
    std::string new_token;
    std::string new_request_token;
    if (!make_session_token(new_token) || !make_session_token(new_request_token)) {
        end_ = End::NoRandom;
        return Attempt::Ended;
    }
    token_ = new_token;
    request_token_ = new_request_token;
    token = new_token;
    // Single use: the code is gone once it paired.
    code_.assign(code_.size(), '\0');
    code_.clear();
    last_seen_ms_ = now_ms;
    return Attempt::Paired;
}

Pairing::Attempt Pairing::count_wrong(int64_t now_ms) {
    if (check_time(now_ms) != End::None) {
        return Attempt::Ended;
    }
    if (paired()) {
        return Attempt::Busy;
    }
    ++wrong_codes_;
    if (wrong_codes_ >= kMaxWrongCodes) {
        end_ = End::WrongCodes;
        return Attempt::Ended;
    }
    return Attempt::Wrong;
}

bool Pairing::check_token(std::string_view token, int64_t now_ms) {
    if (end_ != End::None || token_.empty() || !equal_in_constant_time(token, token_)) {
        return false;
    }
    last_seen_ms_ = now_ms;
    return true;
}

Pairing::Change Pairing::check_change(std::string_view session, std::string_view request, int64_t now_ms) {
    if (end_ != End::None || token_.empty() || !equal_in_constant_time(session, token_)) {
        return Change::NoSession;
    }
    if (!equal_in_constant_time(request, request_token_)) {
        return Change::WrongToken;
    }
    last_seen_ms_ = now_ms;
    return Change::Allowed;
}

Pairing::End Pairing::check_time(int64_t now_ms) {
    if (end_ != End::None) {
        return end_;
    }
    if (!paired() && now_ms >= code_deadline_ms_) {
        end_ = End::CodeUnused;
    } else if (paired() && now_ms - last_seen_ms_ >= kViewerIdleMs) {
        end_ = End::ViewerIdle;
    }
    return end_;
}

} // namespace platform::remote
