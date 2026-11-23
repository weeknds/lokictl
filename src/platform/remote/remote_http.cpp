/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote_http.h"

namespace platform::remote {
namespace {

constexpr std::size_t kMaxMethodChars = 16;
constexpr std::size_t kMaxDecimalDigits = 19;

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (lower(a[index]) != lower(b[index])) {
            return false;
        }
    }
    return true;
}

// A character allowed in a header name (an HTTP "token").
bool token_char(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

// Visible ASCII, no space: what a request target may hold.
bool visible_char(char c) {
    return c > 0x20 && c < 0x7F;
}

// A header value may hold visible ASCII, spaces and tabs.
bool value_char(char c) {
    return c == '\t' || (c >= 0x20 && c < 0x7F);
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

const char* status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Content Too Large";
        case 421: return "Misdirected Request";
        case 431: return "Request Header Fields Too Large";
        case 503: return "Service Unavailable";
        default: return "Internal Server Error";
    }
}

bool parse_request_line(std::string_view line, HttpRequest& request) {
    const std::size_t first = line.find(' ');
    if (first == std::string_view::npos) {
        return false;
    }
    const std::size_t second = line.find(' ', first + 1);
    if (second == std::string_view::npos || line.find(' ', second + 1) != std::string_view::npos) {
        return false;
    }

    const std::string_view method = line.substr(0, first);
    const std::string_view target = line.substr(first + 1, second - first - 1);
    const std::string_view version = line.substr(second + 1);
    if (method.empty() || method.size() > kMaxMethodChars) {
        return false;
    }
    for (const char c : method) {
        if (c < 'A' || c > 'Z') {
            return false;
        }
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return false;
    }
    // Only a plain path ("origin form"): no full URL, no '*', no fragment.
    if (target.empty() || target.front() != '/') {
        return false;
    }
    for (const char c : target) {
        if (!visible_char(c) || c == '#') {
            return false;
        }
    }

    request.method.assign(method);
    const std::size_t question = target.find('?');
    request.path.assign(target.substr(0, question));
    if (question != std::string_view::npos) {
        request.query.assign(target.substr(question + 1));
    }
    return true;
}

} // namespace

HeadResult parse_request_head(std::string_view data, HttpRequest& request, std::size_t& head_size) {
    const std::size_t end = data.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        return data.size() >= kMaxHeadBytes ? HeadResult::TooLarge : HeadResult::Incomplete;
    }
    if (end + 4 > kMaxHeadBytes) {
        return HeadResult::TooLarge;
    }

    request = HttpRequest{};
    head_size = end + 4;
    // Every line of the head, each ending in CRLF.
    const std::string_view head = data.substr(0, end + 2);

    std::size_t line_end = head.find("\r\n");
    if (!parse_request_line(head.substr(0, line_end), request)) {
        return HeadResult::Bad;
    }

    bool seen_host = false;
    bool seen_length = false;
    std::size_t position = line_end + 2;
    while (position < head.size()) {
        line_end = head.find("\r\n", position);
        const std::string_view line = head.substr(position, line_end - position);
        position = line_end + 2;

        // An empty line cannot come before the end, and a line that starts
        // with a space or tab is an old-style folded header.
        if (line.empty() || line.front() == ' ' || line.front() == '\t') {
            return HeadResult::Bad;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            return HeadResult::Bad;
        }
        const std::string_view name = line.substr(0, colon);
        for (const char c : name) {
            if (!token_char(c)) {
                return HeadResult::Bad;
            }
        }
        const std::string_view raw_value = line.substr(colon + 1);
        for (const char c : raw_value) {
            if (!value_char(c)) {
                return HeadResult::Bad;
            }
        }
        const std::string_view value = trim(raw_value);

        if (same_name(name, "host")) {
            if (seen_host) {
                return HeadResult::Bad;
            }
            seen_host = true;
            request.host.assign(value);
        } else if (same_name(name, "content-length")) {
            uint64_t length = 0;
            if (seen_length || !parse_decimal(value, length)) {
                return HeadResult::Bad;
            }
            seen_length = true;
            if (length > kMaxBodyBytes) {
                return HeadResult::BodyTooLarge;
            }
            request.content_length = static_cast<std::size_t>(length);
        } else if (same_name(name, "transfer-encoding")) {
            return HeadResult::Bad;
        } else if (same_name(name, "cookie")) {
            if (!request.cookie.empty()) {
                request.cookie += "; ";
            }
            request.cookie.append(value);
        }
    }
    return HeadResult::Complete;
}

std::string form_value(std::string_view text, std::string_view name, bool* found) {
    if (found) {
        *found = false;
    }
    while (!text.empty()) {
        const std::size_t amp = text.find('&');
        const std::string_view pair = text.substr(0, amp);
        const std::size_t equals = pair.find('=');
        if (pair.substr(0, equals) == name) {
            if (found) {
                *found = true;
            }
            return equals == std::string_view::npos ? std::string() : std::string(pair.substr(equals + 1));
        }
        if (amp == std::string_view::npos) {
            break;
        }
        text.remove_prefix(amp + 1);
    }
    return {};
}

std::string cookie_value(std::string_view header, std::string_view name) {
    while (!header.empty()) {
        const std::size_t semicolon = header.find(';');
        const std::string_view pair = trim(header.substr(0, semicolon));
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
            return std::string(pair.substr(equals + 1));
        }
        if (semicolon == std::string_view::npos) {
            break;
        }
        header.remove_prefix(semicolon + 1);
    }
    return {};
}

bool parse_decimal(std::string_view text, uint64_t& value) {
    if (text.empty() || text.size() > kMaxDecimalDigits) {
        return false;
    }
    uint64_t result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + static_cast<uint64_t>(c - '0');
    }
    value = result;
    return true;
}

std::string response_head(int status,
                          std::string_view content_type,
                          std::size_t content_length,
                          std::string_view extra_headers) {
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + status_text(status) + "\r\n";
    // A 204 answer has no body, so it names neither type nor length.
    if (status != 204) {
        head += "Content-Type: ";
        head.append(content_type);
        head += "\r\nContent-Length: " + std::to_string(content_length) + "\r\n";
    }
    head +=
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "X-Frame-Options: DENY\r\n";
    head.append(extra_headers);
    head += "\r\n";
    return head;
}

} // namespace platform::remote
