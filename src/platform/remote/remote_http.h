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

// The limits of one request: the head (request line and headers, up to and
// including the empty line) and the body. Only a POST to kUploadPath may
// carry the larger body: a WireGuard file of at most 16 KiB.
constexpr std::size_t kMaxHeadBytes = 2048;
constexpr std::size_t kMaxBodyBytes = 256;
constexpr std::size_t kMaxUploadBytes = 16 * 1024;
constexpr std::string_view kUploadPath = "/api/v1/vpn/file";

// One HTTP/1.x request as the server reads it. Only the headers the server
// uses are kept.
struct HttpRequest {
    std::string method;          // "GET", "POST", ...
    std::string path;            // "/screen.png", without the query
    std::string query;           // after '?', may be empty
    std::string host;            // the Host header; empty when there is none
    std::string origin;          // the Origin header; empty when there is none
    std::string cookie;          // the Cookie header(s), joined by "; "
    std::string token;           // the X-LokiCTL-Token header; empty when there is none
    std::size_t content_length{0};
};

// The header that carries the dashboard's request token (see
// Pairing::request_token()) on every state-changing request.
constexpr std::string_view kTokenHeader = "X-LokiCTL-Token";

enum class HeadResult {
    Incomplete,  // the empty line has not arrived yet
    Complete,    // `request` and `head_size` are set
    Bad,         // not a request this server reads: answer 400
    TooLarge,    // the head is longer than kMaxHeadBytes: answer 431
    BodyTooLarge // Content-Length above the body's limit: answer 413
};

// The largest body `request` may carry: kMaxUploadBytes for a POST to
// kUploadPath, kMaxBodyBytes for anything else.
std::size_t body_limit(const HttpRequest& request);

// Reads the head at the start of `data` (what arrived so far). On Complete,
// `head_size` is the number of bytes of the head; the body follows. Refuses
// anything unusual: a target that is not a plain path, a version other
// than HTTP/1.0 or 1.1, a line without CRLF, folded or repeated Host,
// Origin, Content-Length or X-LokiCTL-Token headers, and any
// Transfer-Encoding.
HeadResult parse_request_head(std::string_view data, HttpRequest& request, std::size_t& head_size);

// The value of `name` in "a=1&b=2" (a query or a form body), without any
// decoding; empty when it is missing. `found` says whether it was there.
std::string form_value(std::string_view text, std::string_view name, bool* found = nullptr);

// Decodes a form value as a browser encodes it: "+" is a space and "%XX"
// one byte. Returns false for a '%' without two hex digits after it and for
// a decoded byte below 0x20 or 0x7F (a control character).
bool form_decode(std::string_view text, std::string& value);

// The value of the cookie `name` in a Cookie header ("a=1; b=2"); empty
// when it is missing.
std::string cookie_value(std::string_view header, std::string_view name);

// Reads a whole decimal number of 1 to 19 digits. Returns false for
// anything else.
bool parse_decimal(std::string_view text, uint64_t& value);

// The Content-Security-Policy of every answer that brings none of its own:
// the dashboard's. Files from the server itself only, nothing inline, no
// plugins, no frames, forms only to the server.
std::string_view default_security_policy();

// The start of an answer: the status line, the headers every answer has
// (Content-Type, Content-Length, Connection: close, Cache-Control (no-store
// unless `cache_control` says otherwise), X-Content-Type-Options: nosniff,
// Referrer-Policy: no-referrer, X-Frame-Options: DENY), then
// `extra_headers` (each line ending in CRLF), the default
// Content-Security-Policy unless `extra_headers` names one, and the empty
// line. So every answer carries exactly one policy.
std::string response_head(int status,
                          std::string_view content_type,
                          std::size_t content_length,
                          std::string_view extra_headers = {},
                          std::string_view cache_control = "no-store");

} // namespace platform::remote
