/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "service_banner.h"

#include "service_version.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace model {
namespace {

// Telnet's command bytes: IAC starts a command, SB ... IAC SE is a
// sub-negotiation, WILL, WONT, DO and DONT take one option byte.
constexpr uint8_t kIac = 0xFF;
constexpr uint8_t kSb = 0xFA;
constexpr uint8_t kSe = 0xF0;
constexpr uint8_t kWill = 0xFB;
constexpr uint8_t kDont = 0xFE;

// A piece of text shorter than this is taken for noise.
constexpr std::size_t kTextMin = 3;

constexpr std::array<uint16_t, 15> kHttpPorts{80,   631,  1900, 3000, 5000,  7000,  7547, 8000,
                                              8008, 8080, 8123, 8888, 10000, 32400, 49152};
constexpr std::array<uint16_t, 7> kTlsPorts{443, 465, 853, 993, 995, 8443, 8883};
constexpr uint16_t kDnsPort = 53;

bool printable(uint8_t byte) {
    return byte >= 0x20 && byte <= 0x7E;
}

char lower(uint8_t byte) {
    return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte - 'A' + 'a' : byte);
}

std::string trimmed(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(' ') - begin + 1);
}

// At most kBannerKeptMax characters, without a token cut in two: a version
// cut short would be checked as another, older version.
std::string capped(std::string text) {
    if (text.size() > kBannerKeptMax) {
        text = trimmed(cut_at_token(text, kBannerKeptMax));
    }
    return text;
}

// The bytes without Telnet's commands; every other byte as it came.
std::string without_telnet_commands(const uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve(size);
    std::size_t index = 0;
    while (index < size) {
        const uint8_t byte = data[index];
        if (byte != kIac) {
            out.push_back(static_cast<char>(byte));
            ++index;
            continue;
        }
        if (index + 1 >= size) {
            break;
        }
        const uint8_t command = data[index + 1];
        if (command == kSb) {
            std::size_t end = index + 2;
            while (end + 1 < size && !(data[end] == kIac && data[end + 1] == kSe)) {
                ++end;
            }
            index = end + 2;  // past IAC SE, or past the end
        } else if (command >= kWill && command <= kDont) {
            index += 3;
        } else {
            index += 2;  // a two-byte command, or an escaped 0xFF (not text either)
        }
    }
    return out;
}

// The first run of printable characters that holds some text, trimmed;
// `whole` says something came after it (it is not cut by the end).
std::string first_text(const std::string& bytes, bool& whole) {
    whole = false;
    std::size_t index = 0;
    while (index < bytes.size()) {
        while (index < bytes.size() && !printable(static_cast<uint8_t>(bytes[index]))) {
            ++index;
        }
        const std::size_t begin = index;
        while (index < bytes.size() && printable(static_cast<uint8_t>(bytes[index]))) {
            ++index;
        }
        std::string text = trimmed(bytes.substr(begin, index - begin));
        if (text.size() >= kTextMin) {
            whole = index < bytes.size();
            return text;
        }
    }
    return {};
}

bool starts_with(const std::string& text, const char* prefix) {
    return text.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

// A greeting without its protocol's own prefix.
std::string without_prefix(std::string text) {
    if (starts_with(text, "SSH-")) {
        // "SSH-2.0-OpenSSH_9.6": the software after the protocol version.
        const std::size_t dash = text.find('-', 4);
        if (dash != std::string::npos) {
            text = text.substr(dash + 1);
        }
    } else if (text.size() >= 4 && text[0] >= '0' && text[0] <= '9' && text[1] >= '0' && text[1] <= '9' &&
               text[2] >= '0' && text[2] <= '9' && (text[3] == ' ' || text[3] == '-')) {
        text = text.substr(4);  // FTP and SMTP: "220 "
    } else if (starts_with(text, "+OK")) {
        text = text.substr(3);  // POP3
    } else if (starts_with(text, "* OK")) {
        text = trimmed(text.substr(4));  // IMAP, maybe with "[CAPABILITY ...]" first
        if (!text.empty() && text[0] == '[') {
            const std::size_t close = text.find(']');
            text = close == std::string::npos ? std::string() : text.substr(close + 1);
        }
    }
    return trimmed(text);
}

std::string listen_text(const uint8_t* data, std::size_t size, bool& whole) {
    return first_text(without_telnet_commands(data, size), whole);
}

// Whether the bytes start an HTTP answer ("HTTP/"); `maybe` when they are
// too few to tell yet.
bool http_answer(const uint8_t* data, std::size_t size, bool& maybe) {
    static constexpr char kStart[] = "HTTP/";
    constexpr std::size_t kStartSize = sizeof(kStart) - 1;
    const std::size_t count = std::min(size, kStartSize);
    for (std::size_t index = 0; index < count; ++index) {
        if (data[index] != static_cast<uint8_t>(kStart[index])) {
            maybe = false;
            return false;
        }
    }
    maybe = size < kStartSize;
    return size >= kStartSize;
}

// The value of the header line `name` ("server:", lowercase with its
// colon) of an HTTP answer, printable characters only; false without one.
// `whole` says the line ended within the bytes.
bool header_line(const uint8_t* data, std::size_t size, std::string_view name, std::string& value, bool& whole) {
    const std::size_t kNameSize = name.size();
    std::size_t start = 0;
    while (start < size) {
        std::size_t end = start;
        while (end < size && data[end] != '\n') {
            ++end;
        }
        if (end - start >= kNameSize) {
            bool match = true;
            for (std::size_t index = 0; index < kNameSize && match; ++index) {
                match = lower(data[start + index]) == name[index];
            }
            if (match) {
                std::string text;
                for (std::size_t index = start + kNameSize; index < end; ++index) {
                    if (printable(data[index])) {
                        text.push_back(static_cast<char>(data[index]));
                    }
                }
                value = trimmed(text);
                // A CR at the very end still ends the line (CR LF cut in two).
                whole = end < size || data[end - 1] == '\r';
                return true;
            }
        }
        start = end + 1;
    }
    return false;
}

bool headers_end(const uint8_t* data, std::size_t size) {
    for (std::size_t index = 0; index + 1 < size; ++index) {
        if (data[index] == '\n' && (data[index + 1] == '\n' ||
                                    (data[index + 1] == '\r' && index + 2 < size && data[index + 2] == '\n'))) {
            return true;
        }
    }
    return false;
}

} // namespace

BannerProbe banner_probe(uint16_t port) {
    if (port == kDnsPort) {
        return BannerProbe::Dns;
    }
    if (std::find(kTlsPorts.begin(), kTlsPorts.end(), port) != kTlsPorts.end()) {
        return BannerProbe::Tls;
    }
    if (std::find(kHttpPorts.begin(), kHttpPorts.end(), port) != kHttpPorts.end()) {
        return BannerProbe::Http;
    }
    return BannerProbe::Listen;
}

std::string http_head_request(const std::string& host, uint16_t port) {
    std::string request = "HEAD / HTTP/1.0\r\nHost: " + host;
    if (port != 80) {
        request += ":" + std::to_string(port);
    }
    request += "\r\n\r\n";
    return request;
}

std::string banner_text(BannerProbe probe, const uint8_t* data, std::size_t size) {
    if (probe == BannerProbe::Tls) {
        return "TLS";
    }
    if (data == nullptr || size == 0 || probe == BannerProbe::Dns) {
        return {};
    }
    bool whole = false;
    if (probe == BannerProbe::Http) {
        bool maybe = false;
        if (http_answer(data, size, maybe)) {
            std::string server;
            std::string powered;
            // A line cut by the end of what was read (kBannerBytesMax) loses
            // its last token, which may be a version cut short.
            if (header_line(data, size, "server:", server, whole) && !whole) {
                server = without_last_token(server);
            }
            if (header_line(data, size, "x-powered-by:", powered, whole) && !whole) {
                powered = without_last_token(powered);
            }
            if (!server.empty() && !powered.empty()) {
                server += "; ";
            }
            return capped(server + powered);
        }
        if (maybe) {
            return {};
        }
        // Not a web server after all: what it said by itself.
    }
    return capped(without_prefix(listen_text(data, size, whole)));
}

bool banner_complete(BannerProbe probe, const uint8_t* data, std::size_t size) {
    if (probe == BannerProbe::Tls) {
        return true;
    }
    if (data == nullptr || size == 0) {
        return false;
    }
    if (probe == BannerProbe::Dns) {
        return size >= 2 && size >= 2 + ((static_cast<std::size_t>(data[0]) << 8) | data[1]);
    }
    bool whole = false;
    if (probe == BannerProbe::Http) {
        bool maybe = false;
        if (http_answer(data, size, maybe)) {
            return headers_end(data, size);
        }
        if (maybe) {
            return false;
        }
    }
    return !listen_text(data, size, whole).empty() && whole;
}

std::string dns_version_text(const std::string& answer) {
    std::string text;
    for (const char character : answer) {
        if (printable(static_cast<uint8_t>(character))) {
            text.push_back(character);
        }
    }
    text = trimmed(text);
    if (!text.empty() && text[0] >= '0' && text[0] <= '9') {
        text = "BIND " + text;
    }
    return capped(text);
}

} // namespace model
