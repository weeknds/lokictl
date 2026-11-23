/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

// The check of the services a port scan found against LokiCTL's built-in
// list of known security holes (CVEs) and outdated versions
// (assets/data/vulndb.tsv, made by scripts/vulndb/build.py from NVD and
// CISA's Known Exploited Vulnerabilities catalog). The list is read line by
// line while a check runs and nothing of it is kept but what matched. It
// is a version check only: a device may have been fixed without a new
// version number.
namespace model {

struct PortRun;

// The list's place below the app's data folder.
constexpr const char* kVulnListFile = "data/vulndb.tsv";

// What a finding is.
enum class FindingKind : int32_t {
    Hole = 0,  // a known security hole (a CVE) in this version
    Outdated,  // this product line no longer gets security fixes
};

// One thing the check found about an open port.
struct ServiceFinding {
    uint16_t port{0};
    FindingKind kind{FindingKind::Hole};
    std::string product;  // the list's id of the product: "openssh"
    std::string name;     // the product as shown: "OpenSSH"
    std::string version;  // as the port said it: "9.6p1"
    std::string cve;      // Hole: "CVE-2024-6387"; empty for Outdated
    int32_t score{-1};    // Hole: the 0-10 severity score in tenths (81 is 8.1); -1 not known
    bool exploited{false};  // Hole: in CISA's list of holes attackers use
    // The system that built it, when the port named one ("Debian"): its
    // updates may already have fixed this. Empty when none was named.
    std::string vendor;
    std::string link;  // the page about it: NVD's for a hole, the source for an outdated line
};

bool operator==(const ServiceFinding& left, const ServiceFinding& right);
bool operator!=(const ServiceFinding& left, const ServiceFinding& right);

// What the check found for one port, in all (the kept findings are only
// the worst few).
struct PortVerdict {
    uint16_t port{0};
    int32_t holes{0};      // known holes in its version(s)
    int32_t exploited{0};  // of them, known to be used by attackers
    bool outdated{false};  // no longer gets security fixes
    std::string vendor;    // as ServiceFinding::vendor
};

bool operator==(const PortVerdict& left, const PortVerdict& right);
bool operator!=(const PortVerdict& left, const PortVerdict& right);

// A port keeps at most this many holes for the page, the worst first.
constexpr std::size_t kHolesKeptPerPort = 8;

// The result of one check.
struct VulnCheck {
    std::string date;                       // the list's date ("2026-11-23"); empty when no list was read
    std::vector<PortVerdict> verdicts;      // ports with any finding, ascending
    std::vector<ServiceFinding> findings;   // by port, ascending; each port's worst first, then its outdated line
};

// Checks what each open port of `run` said (run.banners) against the list
// read from `list`, one line at a time. A port with nothing known, an
// unknown product or no version gives nothing. Within a port, holes known
// to be used by attackers come first, then the higher score, then the
// newer hole.
VulnCheck check_services(std::istream& list, const PortRun& run);

// The list's date from its first lines; empty when it has none.
std::string vuln_list_date(std::istream& list);

// NVD's page about `cve`: "https://nvd.nist.gov/vuln/detail/CVE-2024-6387".
std::string cve_link(const std::string& cve);

} // namespace model
