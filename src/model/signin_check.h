/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The Wi-Fi sign-in check's rules (model::SigninRun in tool_data.h): what
// the check page's answer means and which sign-in address may be shown.
// The runners fetch; the functions here read and decide, so the rules can
// be checked without a network (tests/signin_checks.cpp).
namespace model {

// At most this much of the answer is read: its status line and headers.
constexpr std::size_t kSigninHeadMax = 4096;
// A redirect address longer than this is not shown at all.
constexpr std::size_t kSigninTargetMax = 512;
// A sign-in address up to this long goes into the QR code; a longer one
// would make the code's dots too small for a phone camera on this screen,
// so the code holds kSigninCheckUrl instead (opened on a phone on the same
// network, the network sends it to its sign-in page too).
constexpr std::size_t kSigninCodeMax = 200;

// The parts of an answer's head the check needs.
struct SigninReply {
    bool http{false};      // it starts with an HTTP/1.x status line
    int32_t status{0};     // the status code; 0 when not http
    std::string location;  // the first Location header's value, trimmed; empty when none
                           // or when longer than kSigninTargetMax
    bool location_too_long{false};
};

// Reads the head of an HTTP/1.x answer (the bytes up to the blank line, or
// as much of them as came): the status line and the first Location
// header (its name in any case). Lines may end in CRLF or LF. Never throws.
SigninReply read_signin_reply(std::string_view head);

// `location` as a sign-in address that may be shown: an absolute http://
// or https:// address (the scheme in any case; returned in lowercase) of
// 1 to kSigninTargetMax printable ASCII characters (no spaces, no control
// characters), with a host and no user name ('@' before the path).
// Anything else, a relative address included, gives an empty text.
std::string signin_target(std::string_view location);

// Judges `reply` into `run` (verdict, status and target):
// - 204: Clear, the internet is open;
// - 301, 302, 303, 307, 308 and 511 (network sign-in needed): Needed, with
//   the Location's address when signin_target() allows it;
// - any other 2xx: Needed (the network answered with its own page);
// - anything else, or no HTTP answer: Unclear.
void judge_signin(SigninRun& run, const SigninReply& reply);

// What the Sign-in page's QR code holds: with the verdict Needed, the
// sign-in address when there is one of at most kSigninCodeMax characters,
// else kSigninCheckUrl; otherwise empty (no code).
std::string signin_code_text(const SigninRun& run);

// The host of an http(s) address ("portal.example.net"), without the
// scheme, a port, the path or the query; empty when there is none.
std::string signin_host(std::string_view address);

// The data layer's state for the check (see SigninLocal).
SigninLocal signin_local(const HomeData& home);

} // namespace model
