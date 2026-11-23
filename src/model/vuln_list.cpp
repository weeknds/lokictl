/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "vuln_list.h"

#include "service_version.h"
#include "tool_data.h"

#include <algorithm>
#include <cstdint>
#include <istream>
#include <string_view>
#include <utility>

namespace model {
namespace {

// A line holds at most this many characters; a longer one is skipped.
constexpr std::size_t kLineMax = 64 * 1024;

// One service of the scan to check: a product with its version on a port.
struct Service {
    uint16_t port;
    std::string product;
    std::string name;
    std::string version;   // as said
    std::string compared;  // as compared (compared_version())
    std::string vendor;
};

// The tab-separated fields of `line`.
std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (true) {
        const std::size_t tab = line.find('\t', begin);
        fields.push_back(line.substr(begin, tab == std::string_view::npos ? std::string_view::npos : tab - begin));
        if (tab == std::string_view::npos) {
            return fields;
        }
        begin = tab + 1;
    }
}

// OpenSSH's portable release number: 2 for "9.3p2", 0 without one.
int32_t portable_release(const std::string& version) {
    const std::size_t p = version.rfind('p');
    if (p == std::string::npos || p == 0 || p + 1 >= version.size()) {
        return 0;
    }
    int32_t number = 0;
    for (std::size_t index = p + 1; index < version.size(); ++index) {
        if (version[index] < '0' || version[index] > '9' || number > 1000) {
            return 0;
        }
        number = number * 10 + (version[index] - '0');
    }
    return number;
}

// Whether the version of `service` is one `token` names: "*" (every
// version), "=x" (that one), "[a,b)" (from a to b, with "[" / "]"
// inclusive, "(" / ")" exclusive, an empty bound open). Ranges compare
// compared_version()s. OpenSSH's single versions keep their portable
// release: "=9.3p1" is 9.3p1 alone, "=9.3" is 9.3 and 9.3p1, so a fix
// release such as 9.3p2 is not taken for them.
bool token_matches(const Service& service, std::string_view token) {
    if (token == "*") {
        return true;
    }
    if (token.size() >= 2 && token[0] == '=') {
        const std::string named(token.substr(1));
        if (service.product == "openssh") {
            const int32_t release = portable_release(service.version);
            const int32_t wanted = portable_release(named);
            return compare_versions(service.compared, compared_version(service.product, named)) == 0 &&
                   (wanted > 0 ? release == wanted : release <= 1);
        }
        return compare_versions(service.compared, compared_version(service.product, named)) == 0;
    }
    if (token.size() < 3 || (token.front() != '[' && token.front() != '(') ||
        (token.back() != ']' && token.back() != ')')) {
        return false;
    }
    const std::size_t comma = token.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    const std::string_view low = token.substr(1, comma - 1);
    const std::string_view high = token.substr(comma + 1, token.size() - comma - 2);
    if (!low.empty()) {
        const int order = compare_versions(service.compared, compared_version(service.product, low));
        if (order < 0 || (order == 0 && token.front() == '(')) {
            return false;
        }
    }
    if (!high.empty()) {
        const int order = compare_versions(service.compared, compared_version(service.product, high));
        if (order > 0 || (order == 0 && token.back() == ')')) {
            return false;
        }
    }
    return true;
}

// Whether any of the space-separated tokens names the version.
bool any_token_matches(const Service& service, std::string_view tokens) {
    std::size_t begin = 0;
    while (begin < tokens.size()) {
        std::size_t end = tokens.find(' ', begin);
        if (end == std::string_view::npos) {
            end = tokens.size();
        }
        if (end > begin && token_matches(service, tokens.substr(begin, end - begin))) {
            return true;
        }
        begin = end + 1;
    }
    return false;
}

// "8.1" -> 81; -1 for "-" or anything else.
int32_t score_tenths(std::string_view text) {
    int32_t whole = 0;
    int32_t tenth = 0;
    std::size_t index = 0;
    if (text.empty() || text[0] < '0' || text[0] > '9') {
        return -1;
    }
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        whole = whole * 10 + (text[index] - '0');
        ++index;
        if (whole > 10) {
            return -1;
        }
    }
    if (index + 1 < text.size() && text[index] == '.' && text[index + 1] >= '0' && text[index + 1] <= '9') {
        tenth = text[index + 1] - '0';
    }
    return whole * 10 + tenth;
}

// "CVE-2024-6387" -> (2024, 6387), for the newer-first order.
std::pair<int64_t, int64_t> cve_order(const std::string& cve) {
    int64_t year = 0;
    int64_t number = 0;
    std::size_t index = 4;
    while (index < cve.size() && cve[index] >= '0' && cve[index] <= '9') {
        year = year * 10 + (cve[index++] - '0');
    }
    ++index;
    while (index < cve.size() && cve[index] >= '0' && cve[index] <= '9' && number < INT64_MAX / 10) {
        number = number * 10 + (cve[index++] - '0');
    }
    return {year, number};
}

bool worse(const ServiceFinding& left, const ServiceFinding& right) {
    if (left.exploited != right.exploited) {
        return left.exploited;
    }
    if (left.score != right.score) {
        return left.score > right.score;
    }
    return cve_order(left.cve) > cve_order(right.cve);
}

} // namespace

bool operator==(const ServiceFinding& left, const ServiceFinding& right) {
    return left.port == right.port && left.kind == right.kind && left.product == right.product &&
           left.name == right.name &&
           left.version == right.version && left.cve == right.cve && left.score == right.score &&
           left.exploited == right.exploited && left.vendor == right.vendor && left.link == right.link;
}

bool operator!=(const ServiceFinding& left, const ServiceFinding& right) {
    return !(left == right);
}

bool operator==(const PortVerdict& left, const PortVerdict& right) {
    return left.port == right.port && left.holes == right.holes && left.exploited == right.exploited &&
           left.outdated == right.outdated && left.vendor == right.vendor;
}

bool operator!=(const PortVerdict& left, const PortVerdict& right) {
    return !(left == right);
}

std::string cve_link(const std::string& cve) {
    return "https://nvd.nist.gov/vuln/detail/" + cve;
}

std::string vuln_list_date(std::istream& list) {
    std::string line;
    // The date comes right after the comment lines at the top.
    for (int lines = 0; lines < 64 && std::getline(list, line); ++lines) {
        if (line.compare(0, 5, "date\t") == 0) {
            return line.substr(5, 10);
        }
        if (!line.empty() && line[0] != '#') {
            break;
        }
    }
    return {};
}

VulnCheck check_services(std::istream& list, const PortRun& run) {
    VulnCheck check;
    std::vector<Service> services;
    for (const PortBanner& banner : run.banners) {
        const ServiceInfo info = read_service(banner.text);
        for (const ServiceVersion& entry : info.products) {
            services.push_back({banner.port, entry.product, entry.name, entry.version,
                                compared_version(entry.product, entry.version), info.vendor});
        }
    }

    std::vector<ServiceFinding> holes;
    std::vector<ServiceFinding> outdated;
    std::string line;
    while (std::getline(list, line)) {
        if (line.empty() || line[0] == '#' || line.size() > kLineMax) {
            continue;
        }
        if (line.compare(0, 5, "date\t") == 0) {
            check.date = line.substr(5, 10);
            continue;
        }
        if (services.empty() || line.size() < 3 || line[1] != '\t' || (line[0] != 'v' && line[0] != 'o')) {
            continue;
        }
        // The product first, so lines of other products cost little.
        const std::size_t product_end = line.find('\t', 2);
        if (product_end == std::string::npos) {
            continue;
        }
        const std::string_view product(line.data() + 2, product_end - 2);
        if (std::none_of(services.begin(), services.end(),
                         [product](const Service& service) { return service.product == product; })) {
            continue;
        }
        const std::vector<std::string_view> fields = fields_of(line);
        if (line[0] == 'o' && fields.size() >= 4) {
            for (const Service& service : services) {
                if (service.product == product && token_matches(service, fields[2])) {
                    ServiceFinding finding;
                    finding.port = service.port;
                    finding.kind = FindingKind::Outdated;
                    finding.product = service.product;
                    finding.name = service.name;
                    finding.version = service.version;
                    finding.vendor = service.vendor;
                    finding.link = std::string(fields[3]);
                    outdated.push_back(std::move(finding));
                }
            }
        } else if (line[0] == 'v' && fields.size() >= 6) {
            for (const Service& service : services) {
                if (service.product != product || !any_token_matches(service, fields[5])) {
                    continue;
                }
                ServiceFinding finding;
                finding.port = service.port;
                finding.product = service.product;
                finding.name = service.name;
                finding.version = service.version;
                finding.cve = std::string(fields[2]);
                finding.score = score_tenths(fields[3]);
                finding.exploited = fields[4] == "K";
                finding.vendor = service.vendor;
                finding.link = cve_link(finding.cve);
                const bool repeated = std::any_of(holes.begin(), holes.end(), [&finding](const ServiceFinding& kept) {
                    return kept.port == finding.port && kept.cve == finding.cve;
                });
                if (!repeated) {
                    holes.push_back(std::move(finding));
                }
            }
        }
    }

    // Per port, ascending: its worst holes, then its outdated line.
    std::vector<uint16_t> ports;
    for (const ServiceFinding& finding : holes) {
        ports.push_back(finding.port);
    }
    for (const ServiceFinding& finding : outdated) {
        ports.push_back(finding.port);
    }
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    for (const uint16_t port : ports) {
        PortVerdict verdict;
        verdict.port = port;
        std::vector<ServiceFinding> mine;
        for (const ServiceFinding& finding : holes) {
            if (finding.port == port) {
                mine.push_back(finding);
                ++verdict.holes;
                verdict.exploited += finding.exploited ? 1 : 0;
                verdict.vendor = finding.vendor;
            }
        }
        std::sort(mine.begin(), mine.end(), worse);
        if (mine.size() > kHolesKeptPerPort) {
            mine.resize(kHolesKeptPerPort);
        }
        for (const ServiceFinding& finding : outdated) {
            if (finding.port == port && !verdict.outdated) {
                verdict.outdated = true;
                verdict.vendor = verdict.vendor.empty() ? finding.vendor : verdict.vendor;
                mine.push_back(finding);
            }
        }
        check.verdicts.push_back(verdict);
        check.findings.insert(check.findings.end(), mine.begin(), mine.end());
    }
    return check;
}

} // namespace model
