/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <functional>
#include <string>

#include <netinet/in.h>

// The Wi-Fi sign-in check of the Sign-in page (F9.10): one plain-HTTP GET
// of model::kSigninCheckUrl, judged by model/signin_check.h. The live
// runner looks the host up with its own DNS question, then calls
// fetch_signin_head().
namespace platform {

// The pretend runner's made-up check at `elapsed_ms` into the run: with
// NetworkManager's state `portal` (local.internet) the network wants a
// sign-in and redirects to kPretendSigninTarget; without a link nothing
// answers (failed, "no network"); otherwise the internet is open. It
// opens no socket. `sent` is the packets sent by then (the DNS question
// and the request); `done` says the run has ended.
model::SigninRun pretend_signin_check(uint32_t elapsed_ms, const model::SigninLocal& local, uint32_t& sent,
                                      bool& done);

// The made-up sign-in page of pretend_signin_check(), on a documentation
// domain.
constexpr const char* kPretendSigninTarget = "http://portal.example.net/login?from=lobby";

// Sends "GET <path> HTTP/1.1" with Host: <host> to `server` (an IPv4
// address and port) on the calling thread, and reads the answer's head:
// the bytes up to the blank line, at most model::kSigninHeadMax (the
// connection is closed then, so the body is never read). It gives up
// after 8 s for the connection and 10 s in all, and asks `stopping` at
// least every 0.1 s. Returns an empty text with `head` filled, else a
// short reason ("stopped", "no answer", "connection refused", ...). It
// counts nothing, logs nothing and never follows a redirect. Plain POSIX
// sockets, so the Mac's tests run it too (tests/signin_checks.cpp).
std::string fetch_signin_head(const sockaddr_in& server, const std::string& host, const std::string& path,
                              const std::function<bool()>& stopping, std::string& head);

} // namespace platform
