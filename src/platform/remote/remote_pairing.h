/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace platform::remote {

// Fills `out` with `size` bytes from the kernel's random source
// (getrandom() on Linux, getentropy() on the Mac). Returns false when the
// kernel gave none; nothing else is ever used in their place.
bool random_bytes(uint8_t* out, std::size_t size);

// A one-time code of six digits, 000000 to 999999, each equally likely.
// Returns false (and leaves `code` empty) without random bytes.
bool make_pairing_code(std::string& code);

// A session token: 128 random bits as 32 lowercase hex digits. Returns false
// (and leaves `token` empty) without random bytes.
bool make_session_token(std::string& token);

// True when `a` and `b` hold the same bytes. For equal lengths the time it
// takes does not depend on where they differ.
bool equal_in_constant_time(std::string_view a, std::string_view b);

// The pairing rules of Remote view, without any network or clock of their
// own: the server hands in the time (steady milliseconds) with every call
// and keeps everything in memory only.
// - The code is single use and lasts kCodeLifetimeMs from start().
// - kMaxWrongCodes wrong codes end the session.
// - One browser at a time: while one is paired, try_code() answers Busy and
//   that does not count as a wrong code.
// - A paired browser that sends nothing for kViewerIdleMs ends the session.
// - Pairing makes two tokens: the session token (the HttpOnly cookie, which
//   page scripts never see) and the request token, which the page asks for
//   (GET /api/v1/session) and sends in the X-LokiCTL-Token header with
//   every state-changing request (check_change()).
// A session that ended stays ended; a new code needs a new Pairing.
class Pairing {
public:
    static constexpr int64_t kCodeLifetimeMs = 2 * 60 * 1000;
    static constexpr int64_t kViewerIdleMs = 10 * 60 * 1000;
    static constexpr int kMaxWrongCodes = 5;

    enum class Attempt {
        Paired, // the right code: `token` holds the new session token
        Wrong,  // a wrong code; the session goes on
        Busy,   // a browser is paired already; not counted
        Ended,  // the session ended (this attempt may have ended it)
    };

    // What check_change() decided.
    enum class Change {
        Allowed,    // the paired browser's session and request token
        NoSession,  // no session, or not this one: answer 401
        WrongToken, // the session is right, the request token is not: 403
    };

    enum class End {
        None,
        CodeUnused, // the code was not used in time
        WrongCodes, // kMaxWrongCodes wrong codes
        ViewerIdle, // the paired browser sent nothing for too long
        NoRandom,   // the kernel gave no random bytes
    };

    // Makes the code. Without random bytes the session ends at once
    // (End::NoRandom).
    explicit Pairing(int64_t now_ms);

    const std::string& code() const { return code_; }
    int64_t code_deadline_ms() const { return code_deadline_ms_; }
    bool paired() const { return !token_.empty(); }
    int wrong_codes() const { return wrong_codes_; }
    End ended() const { return end_; }

    // A code sent by a browser. Spaces and '+' (a space in a form) are
    // ignored, so "482 913" counts as "482913".
    Attempt try_code(std::string_view code, int64_t now_ms, std::string& token);
    // A request that counts as wrong although it named no code (a body
    // without code=). Returns the same as a wrong try_code().
    Attempt count_wrong(int64_t now_ms);

    // True when `token` is the paired browser's; it then counts as a sign
    // of life from that browser.
    bool check_token(std::string_view token, int64_t now_ms);

    // A state-changing request: `session` is the cookie's token and
    // `request` the X-LokiCTL-Token header. Only an allowed request counts
    // as a sign of life.
    Change check_change(std::string_view session, std::string_view request, int64_t now_ms);

    // The request token of the paired browser; empty while none is paired.
    // Handed out only to the paired browser.
    const std::string& request_token() const { return request_token_; }

    // Ends the session when its time ran out. Returns ended().
    End check_time(int64_t now_ms);

private:
    std::string code_;
    std::string token_;
    std::string request_token_;
    int64_t code_deadline_ms_{0};
    int64_t last_seen_ms_{0};
    int wrong_codes_{0};
    End end_{End::None};
};

} // namespace platform::remote
