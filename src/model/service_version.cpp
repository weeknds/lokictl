/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "service_version.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace model {
namespace {

// A name a product goes by in what a port says (lowercase), the list's id
// for it and how a page writes it.
struct ProductName {
    const char* alias;
    const char* product;
    const char* name;
};

constexpr std::array<ProductName, 29> kProductNames{{
    {"allegro-software-rompager", "rompager", "RomPager"},
    {"apache", "apache", "Apache"},
    {"bind", "bind", "BIND"},
    {"boa", "boa", "Boa"},
    {"busybox", "busybox", "BusyBox"},
    {"dnsmasq", "dnsmasq", "dnsmasq"},
    {"dropbear", "dropbear", "Dropbear"},
    {"exim", "exim", "Exim"},
    {"goahead", "goahead", "GoAhead"},
    {"goahead-http", "goahead", "GoAhead"},
    {"goahead-webs", "goahead", "GoAhead"},
    {"iis", "iis", "IIS"},
    {"jetty", "jetty", "Jetty"},
    {"lighttpd", "lighttpd", "lighttpd"},
    {"microsoft-iis", "iis", "IIS"},
    {"mikrotik", "routeros", "RouterOS"},
    {"mini_httpd", "mini_httpd", "mini_httpd"},
    {"miniupnpd", "miniupnpd", "MiniUPnPd"},
    {"nginx", "nginx", "nginx"},
    {"openssh", "openssh", "OpenSSH"},
    {"openssh_for_windows", "openssh", "OpenSSH"},
    {"openssl", "openssl", "OpenSSL"},
    {"php", "php", "PHP"},
    {"proftpd", "proftpd", "ProFTPD"},
    {"pure-ftpd", "pureftpd", "Pure-FTPd"},
    {"rompager", "rompager", "RomPager"},
    {"routeros", "routeros", "RouterOS"},
    {"thttpd", "thttpd", "thttpd"},
    {"vsftpd", "vsftpd", "vsftpd"},
}};

// Words in what a port says that name the system that built it, and the
// name a page uses. The first match counts, so Raspbian comes before Debian.
struct VendorWord {
    const char* word;
    const char* vendor;
};

constexpr std::array<VendorWord, 13> kVendorWords{{
    {"raspbian", "Raspbian"},
    {"ubuntu", "Ubuntu"},
    {"debian", "Debian"},
    {"+deb", "Debian"},
    {"~deb", "Debian"},
    {"red hat", "Red Hat"},
    {"redhat", "Red Hat"},
    {"rhel", "Red Hat"},
    {"centos", "CentOS"},
    {"fedora", "Fedora"},
    {"freebsd", "FreeBSD"},
    {"suse", "SUSE"},
    {"fips", "vendor"},
}};

// A version longer than this is not taken for one.
constexpr std::size_t kVersionMax = 24;

bool is_digit(char character) {
    return character >= '0' && character <= '9';
}

bool is_letter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

char lower(char character) {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

const ProductName* product_named(std::string_view name) {
    const std::string key = lowered(name);
    for (const ProductName& entry : kProductNames) {
        if (key == entry.alias) {
            return &entry;
        }
    }
    return nullptr;
}

// The version at the start of `text`: a digit (after an optional "v", as
// BusyBox writes it), then digits, letters and dots; "" when there is none
// or it is too long. "1.0.2k-fips" -> "1.0.2k", "9.18.24-1-Debian" ->
// "9.18.24", "2.3.4)" -> "2.3.4".
std::string version_at(std::string_view text) {
    std::size_t begin = 0;
    if (text.size() >= 2 && (text[0] == 'v' || text[0] == 'V') && is_digit(text[1])) {
        begin = 1;
    }
    if (begin >= text.size() || !is_digit(text[begin])) {
        return {};
    }
    std::size_t end = begin;
    while (end < text.size() && (is_digit(text[end]) || is_letter(text[end]) || text[end] == '.')) {
        ++end;
    }
    while (end > begin && text[end - 1] == '.') {
        --end;
    }
    if (end - begin > kVersionMax) {
        return {};
    }
    return std::string(text.substr(begin, end - begin));
}

bool separator(char character) {
    return character == ' ' || character == '(' || character == ')' || character == ',' || character == ';' ||
           character == '[' || character == ']' || character == '"' || character == '\t';
}

std::vector<std::string_view> tokens_of(std::string_view text) {
    std::vector<std::string_view> tokens;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && separator(text[index])) {
            ++index;
        }
        const std::size_t begin = index;
        while (index < text.size() && !separator(text[index])) {
            ++index;
        }
        if (index > begin) {
            tokens.push_back(text.substr(begin, index - begin));
        }
    }
    return tokens;
}

// A token that holds a name and a version: "nginx/1.18.0" (at the first
// '/'), "OpenSSH_9.6p1" (at the last '_' before a digit), "dnsmasq-2.85" (at
// the first '-' before a digit). False when it holds no such split.
bool split_token(std::string_view token, std::string_view& name, std::string_view& rest) {
    const std::size_t slash = token.find('/');
    if (slash != std::string_view::npos) {
        name = token.substr(0, slash);
        rest = token.substr(slash + 1);
        return true;
    }
    for (std::size_t index = token.size(); index-- > 1;) {
        if (token[index - 1] == '_' && is_digit(token[index])) {
            name = token.substr(0, index - 1);
            rest = token.substr(index);
            return true;
        }
    }
    for (std::size_t index = 1; index < token.size(); ++index) {
        if (token[index - 1] == '-' && is_digit(token[index])) {
            name = token.substr(0, index - 1);
            rest = token.substr(index);
            return true;
        }
    }
    return false;
}

bool pre_release(std::string_view word) {
    const std::string key = lowered(word);
    return key == "rc" || key == "alpha" || key == "beta" || key == "pre" || key == "dev";
}

// One part of a version: a number or a run of letters.
struct Part {
    bool number{false};
    uint64_t value{0};
    std::string_view text;
};

// The next part of `text` from `index`; false at the end.
bool next_part(std::string_view text, std::size_t& index, Part& part) {
    while (index < text.size() && !is_digit(text[index]) && !is_letter(text[index])) {
        ++index;
    }
    if (index >= text.size()) {
        return false;
    }
    const std::size_t begin = index;
    part.number = is_digit(text[index]);
    if (part.number) {
        part.value = 0;
        while (index < text.size() && is_digit(text[index])) {
            if (part.value < UINT64_MAX / 10) {
                part.value = part.value * 10 + static_cast<uint64_t>(text[index] - '0');
            }
            ++index;
        }
    } else {
        while (index < text.size() && is_letter(text[index])) {
            ++index;
        }
    }
    part.text = text.substr(begin, index - begin);
    return true;
}

} // namespace

bool operator==(const ServiceVersion& left, const ServiceVersion& right) {
    return left.product == right.product && left.name == right.name && left.version == right.version;
}

bool operator!=(const ServiceVersion& left, const ServiceVersion& right) {
    return !(left == right);
}

ServiceInfo read_service(std::string_view said) {
    ServiceInfo info;
    const std::vector<std::string_view> tokens = tokens_of(said);
    const auto add = [&info](const ProductName& entry, const std::string& version) {
        if (version.empty() || info.products.size() >= kServiceProductsMax) {
            return;
        }
        for (const ServiceVersion& known : info.products) {
            if (known.product == entry.product) {
                return;  // each product once: the first version counts
            }
        }
        info.products.push_back({entry.product, entry.name, version});
    };
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        std::string_view name;
        std::string_view rest;
        if (split_token(tokens[index], name, rest)) {
            if (const ProductName* entry = product_named(name)) {
                add(*entry, version_at(rest));
            }
            continue;
        }
        // A name alone, with the version as the next word: "vsFTPd 2.3.4".
        if (const ProductName* entry = product_named(tokens[index])) {
            if (index + 1 < tokens.size()) {
                add(*entry, version_at(tokens[index + 1]));
            }
        }
    }
    const std::string text = lowered(said);
    for (const VendorWord& entry : kVendorWords) {
        if (text.find(entry.word) != std::string::npos) {
            info.vendor = entry.vendor;
            break;
        }
    }
    return info;
}

std::string compared_version(std::string_view product, std::string_view version) {
    if (product == "openssh") {
        // "9.6p1": the portable release of 9.6.
        const std::size_t p = version.find('p');
        if (p != std::string_view::npos && p > 0 && p + 1 < version.size() && is_digit(version[p + 1])) {
            return std::string(version.substr(0, p));
        }
    } else if (product == "boa") {
        // "0.94.14rc21" is filed as "0.94.14.21".
        const std::size_t rc = version.find("rc");
        if (rc != std::string_view::npos && rc + 2 < version.size() && is_digit(version[rc + 2])) {
            return std::string(version.substr(0, rc)) + "." + std::string(version.substr(rc + 2));
        }
    } else if (product == "jetty") {
        // "9.4.44.v20210927": the build date of 9.4.44.
        const std::size_t v = version.find(".v");
        if (v != std::string_view::npos && v + 2 < version.size() && is_digit(version[v + 2])) {
            return std::string(version.substr(0, v));
        }
    }
    return std::string(version);
}

std::string service_short_text(const ServiceInfo& info) {
    std::string text;
    for (const ServiceVersion& entry : info.products) {
        if (!text.empty()) {
            text += " · ";
        }
        // OpenSSH's portable release says little to a reader: "OpenSSH 10.0".
        text += entry.name + std::string(" ") +
                (entry.product == "openssh" ? compared_version(entry.product, entry.version) : entry.version);
    }
    return text;
}

int compare_versions(std::string_view left, std::string_view right) {
    std::size_t left_index = 0;
    std::size_t right_index = 0;
    while (true) {
        Part a;
        Part b;
        const bool has_a = next_part(left, left_index, a);
        const bool has_b = next_part(right, right_index, b);
        if (!has_a && !has_b) {
            return 0;
        }
        if (!has_a) {
            // "1.0.2" against "1.0.2k": older, unless "rc" and the like follow.
            return !b.number && pre_release(b.text) ? 1 : -1;
        }
        if (!has_b) {
            return !a.number && pre_release(a.text) ? -1 : 1;
        }
        if (a.number && b.number) {
            if (a.value != b.value) {
                return a.value < b.value ? -1 : 1;
            }
            continue;
        }
        if (a.number != b.number) {
            // A number against letters: a release is newer than its "rc".
            return a.number ? 1 : -1;
        }
        const std::string a_text = lowered(a.text);
        const std::string b_text = lowered(b.text);
        if (a_text != b_text) {
            const bool a_pre = pre_release(a_text);
            const bool b_pre = pre_release(b_text);
            if (a_pre != b_pre) {
                return a_pre ? -1 : 1;
            }
            // OpenSSL: "z" < "za"; otherwise in alphabetical order.
            return a_text < b_text ? -1 : 1;
        }
    }
}

std::string without_last_token(std::string_view said) {
    std::size_t end = said.size();
    if (end > 0 && !separator(said[end - 1])) {
        while (end > 0 && !separator(said[end - 1])) {
            --end;
        }
        while (end > 0 && separator(said[end - 1])) {
            --end;
        }
    }
    return std::string(said.substr(0, end));
}

std::string cut_at_token(std::string_view said, std::size_t max) {
    if (said.size() <= max) {
        return std::string(said);
    }
    std::size_t end = max;
    if (!separator(said[max])) {
        while (end > 0 && !separator(said[end - 1])) {
            --end;  // the token the cut falls in
        }
        while (end > 0 && separator(said[end - 1])) {
            --end;  // and the separators before it
        }
    }
    while (end > 0 && said[end - 1] == ' ') {
        --end;
    }
    return std::string(said.substr(0, end));
}

} // namespace model
