/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Which software a port said it runs, and which version: read by hand from
// what the port said (service_banner.h), with a fixed table of the products
// LokiCTL's list of security holes knows (vuln_list.h). Nothing here reads
// a file or a socket.
namespace model {

// One known product a service named, with its version.
struct ServiceVersion {
    std::string product;  // the list's id: "openssh", "nginx", "apache"
    std::string name;     // as shown: "OpenSSH", "nginx", "Apache"
    std::string version;  // as it said: "9.6p1", "1.18.0"
};

bool operator==(const ServiceVersion& left, const ServiceVersion& right);
bool operator!=(const ServiceVersion& left, const ServiceVersion& right);

// What one port said, read.
struct ServiceInfo {
    std::vector<ServiceVersion> products;  // known products with a version, as they came; each once
    // The system that built it, when the text names one ("Debian" for
    // "OpenSSH_9.2p1 Debian-2+deb12u3", "Ubuntu" for "nginx/1.18.0
    // (Ubuntu)", "vendor" for a build tag such as "-fips"): such builds
    // often get fixes without a new version number. Empty when none.
    std::string vendor;
};

// A port says at most this many products ("Apache/2.4.41 (Ubuntu)
// OpenSSL/1.1.1f PHP/7.4.3").
constexpr std::size_t kServiceProductsMax = 4;

// Reads what a port said (printable ASCII): each known product with a
// version ("OpenSSH_9.6p1", "nginx/1.18.0", "(vsFTPd 2.3.4)", "Exim 4.92",
// "dnsmasq-2.85", "Jetty(9.4.44.v20210927)", "MikroTik 6.45.9", "BIND
// 9.18.24"), and the vendor. A product without a version is left out. Never
// fails: an unknown text gives nothing.
ServiceInfo read_service(std::string_view said);

// The version as the list compares it: OpenSSH without its portable
// release ("9.6p1" -> "9.6"), Jetty without its build date ("9.4.44.v2021"
// -> "9.4.44"), Boa's "rc" as NVD files it ("0.94.14rc21" ->
// "0.94.14.21"); others as they are.
std::string compared_version(std::string_view product, std::string_view version);

// The short form for a page: "OpenSSH 10.0", "nginx 1.18.0 · PHP 7.4.3";
// empty when no known product was read.
std::string service_short_text(const ServiceInfo& info);

// Compares two versions: below 0 when `left` is older, 0 when the same, above
// 0 when newer. Numbers compare as numbers, letters as text ("1.0.2k" is
// newer than "1.0.2"); "rc", "beta", "alpha", "pre" and "dev" come before
// the release they lead to ("0.94.14rc21" is older than "0.94.14"). Other
// characters only separate the parts.
int compare_versions(std::string_view left, std::string_view right);

// `said` without its last token (as read_service() splits a text into
// tokens) and the separators before it: for a line that was cut short, so
// a version cut short ("1.4.7" of "1.4.76") is not read as another,
// older version. A text that ends in a separator keeps every token.
std::string without_last_token(std::string_view said);

// `said` cut to at most `max` characters without a partly cut token: a cut
// that falls just before a separator keeps every token up to it, any other
// drops the token it falls in and the separators before it. Trailing
// spaces go.
std::string cut_at_token(std::string_view said, std::size_t max);

} // namespace model
