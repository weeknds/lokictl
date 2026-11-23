/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "signin_check.h"

#include <cctype>

namespace model {
namespace {

char lower(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

bool same_text_any_case(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_digit(char character) {
    return character >= '0' && character <= '9';
}

// The length of the scheme part ("http://" or "https://", any case) that
// starts `address`; 0 when it starts with neither.
std::size_t scheme_length(std::string_view address) {
    for (const std::string_view scheme : {std::string_view("http://"), std::string_view("https://")}) {
        if (address.size() >= scheme.size() && same_text_any_case(address.substr(0, scheme.size()), scheme)) {
            return scheme.size();
        }
    }
    return 0;
}

// The part between the scheme and the path, query or fragment.
std::string_view authority_of(std::string_view address) {
    const std::size_t start = scheme_length(address);
    if (start == 0) {
        return {};
    }
    std::string_view rest = address.substr(start);
    const std::size_t end = rest.find_first_of("/?#");
    return end == std::string_view::npos ? rest : rest.substr(0, end);
}

} // namespace

SigninReply read_signin_reply(std::string_view head) {
    SigninReply reply;
    // "HTTP/1.x NNN ...": the version, one space, three digits.
    constexpr std::string_view kVersion = "HTTP/1.";
    if (head.size() < kVersion.size() + 5 || head.substr(0, kVersion.size()) != kVersion ||
        !is_digit(head[kVersion.size()]) || head[kVersion.size() + 1] != ' ') {
        return reply;
    }
    const std::size_t code = kVersion.size() + 2;
    if (!is_digit(head[code]) || !is_digit(head[code + 1]) || !is_digit(head[code + 2]) ||
        (head.size() > code + 3 && head[code + 3] != ' ' && head[code + 3] != '\r' && head[code + 3] != '\n')) {
        return reply;
    }
    reply.http = true;
    reply.status = (head[code] - '0') * 100 + (head[code + 1] - '0') * 10 + (head[code + 2] - '0');

    std::size_t start = head.find('\n');
    while (start != std::string_view::npos && start + 1 < head.size()) {
        ++start;
        const std::size_t end = head.find('\n', start);
        std::string_view line = head.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            break;  // the blank line ends the head
        }
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && same_text_any_case(trim(line.substr(0, colon)), "location")) {
            const std::string_view value = trim(line.substr(colon + 1));
            if (value.size() > kSigninTargetMax) {
                reply.location_too_long = true;
            } else {
                reply.location = std::string(value);
            }
            break;  // the first one counts
        }
        start = end;
    }
    return reply;
}

std::string signin_target(std::string_view location) {
    if (location.empty() || location.size() > kSigninTargetMax) {
        return {};
    }
    for (const char character : location) {
        const auto code = static_cast<unsigned char>(character);
        if (code <= 0x20 || code >= 0x7f) {
            return {};
        }
    }
    const std::size_t scheme = scheme_length(location);
    if (scheme == 0) {
        return {};
    }
    const std::string_view authority = authority_of(location);
    if (authority.empty() || authority.find('@') != std::string_view::npos || authority.front() == ':') {
        return {};
    }
    std::string target(location);
    for (std::size_t index = 0; index < scheme; ++index) {
        target[index] = lower(target[index]);
    }
    return target;
}

void judge_signin(SigninRun& run, const SigninReply& reply) {
    run.status = reply.http ? reply.status : 0;
    run.target.clear();
    if (!reply.http) {
        run.verdict = SigninVerdict::Unclear;
        return;
    }
    switch (reply.status) {
        case 204:
            run.verdict = SigninVerdict::Clear;
            return;
        case 301:
        case 302:
        case 303:
        case 307:
        case 308:
        case 511:
            run.verdict = SigninVerdict::Needed;
            run.target = signin_target(reply.location);
            return;
        default:
            break;
    }
    run.verdict = reply.status >= 200 && reply.status < 300 ? SigninVerdict::Needed : SigninVerdict::Unclear;
}

std::string signin_code_text(const SigninRun& run) {
    if (run.verdict != SigninVerdict::Needed) {
        return {};
    }
    if (!run.target.empty() && run.target.size() <= kSigninCodeMax) {
        return run.target;
    }
    return kSigninCheckUrl;
}

std::string signin_host(std::string_view address) {
    std::string_view authority = authority_of(address);
    if (!authority.empty() && authority.front() == '[') {
        // An IPv6 address in brackets keeps them.
        const std::size_t close = authority.find(']');
        return close == std::string_view::npos ? std::string() : std::string(authority.substr(0, close + 1));
    }
    const std::size_t colon = authority.find(':');
    if (colon != std::string_view::npos) {
        authority = authority.substr(0, colon);
    }
    return std::string(authority);
}

SigninLocal signin_local(const HomeData& home) {
    SigninLocal local;
    local.link = home.uplink_kind != LinkKind::None;
    local.internet = home.internet;
    return local;
}

bool signin_check_due(const HomeData& home, std::string& last) {
    if (home.uplink_kind != LinkKind::Wifi || home.uplink_address.empty() || home.wifi_name.empty()) {
        last.clear();
        return false;
    }
    if (home.internet_check || home.wifi_name == last) {
        return false;
    }
    last = home.wifi_name;
    return true;
}

} // namespace model
