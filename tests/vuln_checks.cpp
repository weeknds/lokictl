/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of how LokiCTL reads what a port says (service_version.h,
// service_banner.h) and of the check against the shipped list of known
// security holes (vuln_list.h, assets/data/vulndb.tsv). Built on the Mac
// only, as lokictl_tests; exits 1 when a check fails.
//
//   lokictl_tests                  runs every check
//   lokictl_tests "<what it said>" prints what the list says about one text

#include "dns_message.h"
#include "service_banner.h"
#include "service_version.h"
#include "tool_data.h"
#include "vuln_list.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

model::VulnCheck check_text(uint16_t port, const std::string& said) {
    model::PortRun run;
    run.state = model::ToolState::Done;
    run.open.push_back(port);
    run.banners.push_back({port, said});
    std::ifstream list(VULN_LIST_PATH);
    return model::check_services(list, run);
}

bool has_cve(const model::VulnCheck& check, const char* cve) {
    for (const model::ServiceFinding& finding : check.findings) {
        if (finding.cve == cve) {
            return true;
        }
    }
    return false;
}

int holes_of(const model::VulnCheck& check) {
    return check.verdicts.empty() ? 0 : check.verdicts.front().holes;
}

bool outdated_of(const model::VulnCheck& check) {
    return !check.verdicts.empty() && check.verdicts.front().outdated;
}

// Whether CVE-2024-6387 is among a port's holes: the kept findings show
// only the worst few, so the test asks the list with a run of its own.
bool regresshion(const std::string& said) {
    const model::VulnCheck check = check_text(22, said);
    if (has_cve(check, "CVE-2024-6387")) {
        return true;
    }
    // Not among the worst kept: look through the whole list for the row.
    std::ifstream list(VULN_LIST_PATH);
    std::string line;
    const model::ServiceInfo info = model::read_service(said);
    if (info.products.empty()) {
        return false;
    }
    while (std::getline(list, line)) {
        if (line.find("\tCVE-2024-6387\t") != std::string::npos) {
            // Ask with a list of that row alone.
            std::istringstream one("date\t2026-01-01\n" + line + "\n");
            model::PortRun run;
            run.open.push_back(22);
            run.banners.push_back({22, said});
            return has_cve(model::check_services(one, run), "CVE-2024-6387");
        }
    }
    return false;
}

void version_checks() {
    expect(model::compare_versions("1.0.2", "1.0.2k") < 0, "1.0.2 < 1.0.2k");
    expect(model::compare_versions("1.0.2z", "1.0.2za") < 0, "1.0.2z < 1.0.2za");
    expect(model::compare_versions("0.94.14rc21", "0.94.14") < 0, "0.94.14rc21 < 0.94.14");
    expect(model::compare_versions("4.100", "4.99") > 0, "4.100 > 4.99");
    expect(model::compare_versions("2.4.41", "2.4.41") == 0, "2.4.41 == 2.4.41");
    expect(model::compare_versions("10.0", "9.8") > 0, "10.0 > 9.8");
    expect(model::compared_version("openssh", "9.7p1") == "9.7", "OpenSSH 9.7p1 compares as 9.7");
    expect(model::compared_version("boa", "0.94.14rc21") == "0.94.14.21", "Boa rc");
    expect(model::compared_version("jetty", "9.4.44.v20210927") == "9.4.44", "Jetty build date");
}

void reading_checks() {
    model::ServiceInfo info = model::read_service("Apache/2.4.41 (Ubuntu) OpenSSL/1.1.1f PHP/7.4.3");
    expect(info.products.size() == 3 && info.products[0].product == "apache" && info.products[1].product == "openssl" &&
               info.products[2].version == "7.4.3",
           "Apache, OpenSSL and PHP in one Server line");
    expect(info.vendor == "Ubuntu", "(Ubuntu) names the vendor");
    info = model::read_service("OpenSSH_10.0p2 Debian-7");
    expect(info.products.size() == 1 && info.products[0].version == "10.0p2" && info.vendor == "Debian",
           "OpenSSH with a Debian revision");
    expect(model::service_short_text(info) == "OpenSSH 10.0", "short form OpenSSH 10.0");
    expect(model::read_service("(vsFTPd 2.3.4)").products.size() == 1, "vsFTPd in brackets");
    info = model::read_service("BIND 9.18.24-1-Debian");
    expect(info.products.size() == 1 && info.products[0].version == "9.18.24", "BIND from version.bind");
    expect(model::read_service("dnsmasq-2.85").products.at(0).version == "2.85", "dnsmasq-2.85");
    expect(model::read_service("Jetty(9.4.44.v20210927)").products.at(0).product == "jetty", "Jetty(...)");
    info = model::read_service("MikroTik FTP server (MikroTik 6.45.9) ready");
    expect(info.products.size() == 1 && info.products[0].version == "6.45.9", "MikroTik FTP greeting");
    expect(model::read_service("mail.example.com ESMTP Exim 4.92 #3").products.at(0).version == "4.92", "Exim");
    expect(model::read_service("BusyBox v1.22.1").products.at(0).version == "1.22.1", "BusyBox v1.22.1");
    expect(model::read_service("nginx").products.empty(), "a product without a version gives nothing");
    expect(model::read_service("MediaRenderer/3.2").products.empty(), "an unknown product gives nothing");
    expect(model::read_service("").products.empty(), "an empty text gives nothing");
    const std::string junk("\x01\xff/\x7f_9 -1 (((( ////", 20);
    expect(model::read_service(junk).products.empty(), "junk gives nothing");

    const char http[] = "HTTP/1.1 200 OK\r\nServer: nginx/1.18.0\r\nX-Powered-By: PHP/7.4.33\r\n\r\n";
    const auto* bytes = reinterpret_cast<const uint8_t*>(http);
    expect(model::banner_text(model::BannerProbe::Http, bytes, sizeof(http) - 1) == "nginx/1.18.0; PHP/7.4.33",
           "Server and X-Powered-By");
    // A Server line cut by the 512-byte read loses the word the cut falls
    // in: "lighttpd/1.4.7" of "lighttpd/1.4.76" would be an older version.
    const std::string head = "HTTP/1.1 200 OK\r\nX-Filler: " + std::string(461, 'a') + "\r\nServer: lighttpd/1.4.76\r\n\r\n";
    const auto* head_bytes = reinterpret_cast<const uint8_t*>(head.data());
    const std::size_t cut = head.find("1.4.76") + 5;
    expect(cut == model::kBannerBytesMax &&
               model::banner_text(model::BannerProbe::Http, head_bytes, model::kBannerBytesMax).empty(),
           "a Server line cut in its version keeps no version");
    expect(model::banner_text(model::BannerProbe::Http, head_bytes, head.size()) == "lighttpd/1.4.76",
           "the whole Server line keeps its version");
    expect(model::banner_text(model::BannerProbe::Http, head_bytes, head.find("\r\n\r\n") + 1) == "lighttpd/1.4.76",
           "a Server line cut between CR and LF is whole");
    // A text over 120 characters loses the word the cut falls in.
    const std::string server = "Apache/2.4.62 (Debian) OpenSSL/3.0.15 mod_fcgid/2.3.9 mod_wsgi/4.9.4 Python/3.11 "
                               "mod_perl/2.0.12 Perl/v5.36.0";
    const std::string long_head = "HTTP/1.1 200 OK\r\nServer: " + server + "\r\nX-Powered-By: PHP/8.4.14\r\n\r\n";
    expect(server.size() == 109 &&
               model::banner_text(model::BannerProbe::Http, reinterpret_cast<const uint8_t*>(long_head.data()),
                                  long_head.size()) == server,
           "a version cut at 120 characters is dropped, not shortened");
    expect(model::cut_at_token("Exim 4.92", 7) == "Exim" && model::cut_at_token("Exim 4.92 #3", 9) == "Exim 4.92" &&
               model::cut_at_token("nginx/1.18.0 (Ubuntu)", 14) == "nginx/1.18.0" &&
               model::cut_at_token("PHP/8.4.14", 9).empty() && model::cut_at_token("PHP/8.4.14", 10) == "PHP/8.4.14",
           "cut_at_token keeps whole words only");
    expect(model::banner_probe(53) == model::BannerProbe::Dns, "port 53 is asked its version");
    expect(model::dns_version_text("9.18.24-1-Debian") == "BIND 9.18.24-1-Debian", "a number alone is BIND");
    expect(model::dns_version_text("dnsmasq-2.90") == "dnsmasq-2.90", "dnsmasq says its name");
}

// The version question a name server gets on port 53, and the reading of
// its answer.
void dns_checks() {
    std::vector<uint8_t> question;
    expect(platform::dns::build_version_query(0x1234, question), "the version question is built");
    const std::vector<uint8_t> expected{0x12, 0x34, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        7,    'v',  'e',  'r',  's',  'i',  'o',  'n',  4,    'b',  'i',  'n',
                                        'd',  0,    0x00, 0x10, 0x00, 0x03};
    expect(question == expected, "version.bind TXT CH, no recursion");

    std::vector<uint8_t> answer = question;
    answer[2] = 0x84;  // a response, authoritative
    answer[7] = 1;     // one answer
    const std::string text = "dnsmasq-2.90";
    const std::vector<uint8_t> record{0xC0, 0x0C, 0x00, 0x10, 0x00, 0x03, 0, 0, 0, 0, 0,
                                      static_cast<uint8_t>(text.size() + 1), static_cast<uint8_t>(text.size())};
    answer.insert(answer.end(), record.begin(), record.end());
    answer.insert(answer.end(), text.begin(), text.end());
    expect(platform::dns::version_answer(answer.data(), answer.size(), 0x1234) == text, "the answer's text is read");
    expect(platform::dns::version_answer(answer.data(), answer.size(), 0x4321).empty(), "another id is refused");
    expect(platform::dns::version_answer(answer.data(), answer.size() - 3, 0x1234).empty(), "a cut answer is refused");
    std::vector<uint8_t> framed{0, static_cast<uint8_t>(answer.size())};
    framed.insert(framed.end(), answer.begin(), answer.end());
    expect(model::banner_complete(model::BannerProbe::Dns, framed.data(), framed.size()), "a whole TCP answer");
    expect(!model::banner_complete(model::BannerProbe::Dns, framed.data(), framed.size() - 1), "not yet whole");
}

void list_checks() {
    std::ifstream list(VULN_LIST_PATH);
    expect(static_cast<bool>(list), "the list " VULN_LIST_PATH " opens");
    const std::string date = model::vuln_list_date(list);
    expect(date.size() == 10, "the list has a date");

    for (const char* said : {"OpenSSH_8.5p1", "OpenSSH_8.6p1", "OpenSSH_8.9p1", "OpenSSH_9.0p1", "OpenSSH_9.3p2",
                             "OpenSSH_9.6p1", "OpenSSH_9.7p1"}) {
        expect(regresshion(said), std::string(said) + " has CVE-2024-6387");
    }
    expect(!regresshion("OpenSSH_8.4p1"), "OpenSSH_8.4p1 does not have CVE-2024-6387");
    // CVE-2023-38408 is filed as "before 9.3, 9.3 and 9.3p1": 9.3p2 fixed it.
    expect(has_cve(check_text(22, "OpenSSH_9.3p1"), "CVE-2023-38408") ||
               holes_of(check_text(22, "OpenSSH_9.3p1")) > holes_of(check_text(22, "OpenSSH_9.3p2")),
           "OpenSSH 9.3p1 has CVE-2023-38408");
    expect(holes_of(check_text(22, "OpenSSH_9.3p1")) == holes_of(check_text(22, "OpenSSH_9.3p2")) + 1,
           "OpenSSH 9.3p2, its fix release, does not");

    model::VulnCheck check = check_text(21, "(vsFTPd 2.3.4)");
    expect(has_cve(check, "CVE-2011-2523"), "vsFTPd 2.3.4 has CVE-2011-2523");
    expect(check.date == date, "the check gives the list's date");
    expect(!check.findings.empty() && check.findings.front().vendor.empty(), "a plain vsFTPd is no vendor build");
    expect(!check.findings.empty() && check.findings.front().link == "https://nvd.nist.gov/vuln/detail/" +
                                                                         check.findings.front().cve,
           "a hole links to NVD");

    check = check_text(22, "OpenSSH_9.2p1 Debian-2+deb12u3");
    expect(holes_of(check) > 0, "OpenSSH 9.2p1 has holes");
    bool all_vendor = !check.findings.empty();
    for (const model::ServiceFinding& finding : check.findings) {
        all_vendor = all_vendor && finding.vendor == "Debian";
    }
    expect(all_vendor, "a Debian OpenSSH banner is \"may already be fixed by Debian\"");

    check = check_text(8008, "MediaRenderer/3.2");
    expect(check.findings.empty() && check.verdicts.empty() && check.date == date, "an unknown product: nothing");

    check = check_text(22, "OpenSSH_7.4");
    expect(outdated_of(check), "OpenSSH 7.4 is outdated");
    check = check_text(80, "Apache/2.2.34");
    expect(outdated_of(check), "Apache 2.2 is outdated");
    expect(!outdated_of(check_text(80, "Apache/2.4.66")), "Apache 2.4 is not outdated");

    // At most kHolesKeptPerPort kept, the exploited ones first, then the
    // higher score.
    check = check_text(80, "PHP/5.4.0");
    expect(holes_of(check) > static_cast<int>(model::kHolesKeptPerPort), "PHP 5.4.0 has many holes");
    std::size_t kept = 0;
    bool ordered = true;
    const model::ServiceFinding* last = nullptr;
    for (const model::ServiceFinding& finding : check.findings) {
        if (finding.kind != model::FindingKind::Hole) {
            continue;
        }
        ++kept;
        if (last != nullptr && ((!last->exploited && finding.exploited) ||
                                (last->exploited == finding.exploited && last->score < finding.score))) {
            ordered = false;
        }
        last = &finding;
    }
    expect(kept == model::kHolesKeptPerPort, "PHP 5.4.0 keeps the worst 8");
    expect(ordered, "worst first");

    std::istringstream empty;
    model::PortRun run;
    run.open.push_back(22);
    run.banners.push_back({22, "OpenSSH_7.4"});
    check = model::check_services(empty, run);
    expect(check.date.empty() && check.findings.empty(), "no list: no date, nothing found");
}

void print_one(const std::string& said) {
    const model::ServiceInfo info = model::read_service(said);
    std::printf("read: %s (vendor %s)\n", model::service_short_text(info).c_str(),
                info.vendor.empty() ? "none" : info.vendor.c_str());
    const model::VulnCheck check = check_text(1, said);
    for (const model::PortVerdict& verdict : check.verdicts) {
        std::printf("holes %d, exploited %d, outdated %s\n", verdict.holes, verdict.exploited,
                    verdict.outdated ? "yes" : "no");
    }
    for (const model::ServiceFinding& finding : check.findings) {
        std::printf("  %s %s %s score %d %s\n", finding.kind == model::FindingKind::Hole ? "hole" : "old ",
                    finding.name.c_str(), finding.cve.c_str(), finding.score, finding.exploited ? "exploited" : "");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        print_one(argv[1]);
        return 0;
    }
    version_checks();
    reading_checks();
    dns_checks();
    list_checks();
    std::printf("%s (%d failed)\n", failures == 0 ? "all checks passed" : "checks failed", failures);
    return failures == 0 ? 0 : 1;
}
