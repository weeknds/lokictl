/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of reports (model/report.h, platform/report_files.h): every text
// from the network is escaped in the page and the devices table, the page
// loads and runs nothing, links go only to https pages, and saving,
// listing and reading keep to the reports folder with the right modes.
// Built on the Mac only, as lokictl_report_tests; exits 1 when a check
// fails. With a folder as its argument it also leaves the page made from
// the hostile values there (report-hostile.html), to open in a browser.

#include "report.h"
#include "report_files.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// The hostile texts every network-made field gets.
const std::string kScript = "<script>alert(1)</script>";
const std::string kQuote = "\"><img src=x onerror=alert(2)>";
const std::string kSingle = "' onmouseover='alert(3)";
const std::string kBroken = std::string("bad\xff\xfe utf8 \xc0\xaf end\x01\x1b[31m");
const std::string kFormula = "=HYPERLINK(\"http://198.51.100.9\")";

model::HeardDevice device(const std::string& address, const std::string& hardware, const std::string& name) {
    model::HeardDevice entry;
    entry.address = address;
    entry.hardware_address = hardware;
    entry.name = name;
    entry.kind = kSingle;
    entry.upnp = true;
    entry.ssdp_server = kQuote;
    return entry;
}

struct Values {
    model::HomeData home;
    model::ToolsData tools;
    model::WatchData watch;
    model::TrapRun trap;
};

Values hostile_values() {
    Values values;
    model::HomeData& home = values.home;
    home.sample = false;
    home.uplink_kind = model::LinkKind::Wifi;
    home.uplink_name = "wlan0";
    home.wifi_name = kScript;
    home.wifi_security = kQuote;
    home.uplink_address = "192.0.2.12";
    home.gateway_address = "192.0.2.1";
    home.details.dns = {"192.0.2.1", kSingle};
    home.ap_active = true;
    model::HomeClient client;
    client.name = kBroken;
    client.address = "198.51.100.20";
    client.hardware_address = "02:00:00:00:00:20";
    home.clients.push_back(client);

    model::ToolsData& tools = values.tools;
    tools.listen.state = model::ToolState::Done;
    tools.listen.interface_name = "wlan0";
    tools.listen.elapsed_s = 300;
    tools.listen.devices = {device("192.0.2.1", "02:00:00:00:00:01", kScript),
                            device("192.0.2.7", "02:00:00:00:00:07", kFormula),
                            device("192.0.2.8", "02:00:00:00:00:08", kBroken)};
    model::Neighbour neighbour;
    neighbour.protocol = "LLDP";
    neighbour.hardware_address = "02:00:00:00:00:30";
    neighbour.name = kScript;
    neighbour.port = kQuote;
    neighbour.model = kSingle;
    neighbour.address = "192.0.2.2";
    neighbour.vlan = 10;
    tools.listen.neighbours.push_back(neighbour);

    tools.ports.state = model::ToolState::Done;
    tools.ports.target = "192.0.2.7";
    tools.ports.total = 50;
    tools.ports.asked = 50;
    tools.ports.open = {22, 23, 80};
    tools.ports.banners = {{22, "OpenSSH_8.5p1 " + kScript}, {80, kQuote}};
    tools.ports.list_date = "2026-11-23";
    tools.ports.verdicts = {{22, 12, 1, true, ""}};
    model::ServiceFinding hole;
    hole.port = 22;
    hole.kind = model::FindingKind::Hole;
    hole.product = "openssh";
    hole.name = "OpenSSH";
    hole.version = "8.5p1";
    hole.cve = "CVE-2024-6387";
    hole.score = 81;
    hole.exploited = true;
    model::ServiceFinding fake = hole;
    fake.cve = "CVE-2024-1\"><script>x</script>";
    fake.exploited = false;
    fake.score = 95;
    model::ServiceFinding old = hole;
    old.kind = model::FindingKind::Outdated;
    old.cve.clear();
    old.link = "javascript:alert(4)";
    tools.ports.findings = {hole, fake, old};

    tools.health.state = model::ToolState::Done;
    tools.health.network = "192.0.2.0/24";
    tools.health.interface_name = "wlan0";
    tools.health.scanned = 3;
    model::HealthFinding finding;
    finding.issue = model::HealthIssue::Holes;
    finding.level = model::RiskLevel::Risk;
    finding.title = "3 known holes";
    finding.who = "192.0.2.7";
    finding.where = kScript;
    finding.fact = kQuote;
    finding.port = 22;
    tools.health.findings.push_back(finding);
    finding.issue = model::HealthIssue::Service;
    finding.level = model::RiskLevel::Warning;
    finding.title = "Telnet open";
    finding.port = 23;
    tools.health.findings.push_back(finding);

    tools.trace.state = model::ToolState::Done;
    tools.trace.target = "9.9.9.9";
    tools.trace.address = "9.9.9.9";
    tools.trace.end = model::TraceEnd::Reached;
    tools.trace.hops = {{1, "192.0.2.1", kScript, 1500}, {2, "", "", model::kPingLost}};

    tools.speed.state = model::ToolState::Done;
    tools.speed.server = "https://speed.example.com";
    tools.speed.down_kbps = 93400;
    tools.speed.up_kbps = 40100;

    model::WatchRun& run = values.watch.run;
    run.state = model::ToolState::Running;
    run.interface_name = "wlan0";
    run.elapsed_s = 125;
    model::WatchAlert alert;
    alert.kind = model::WatchAlertKind::FakeRouter;
    alert.time = "07:46";
    alert.address = "192.0.2.1";
    alert.hardware = "02:00:00:00:00:66";
    alert.before = kScript;
    run.alerts.push_back(alert);
    alert.kind = model::WatchAlertKind::Trap;  // listed under the trap only
    alert.address = "192.0.2.99";
    run.alerts.push_back(alert);
    values.watch.known = {"02:00:00:00:00:01"};

    values.trap.state = model::ToolState::Running;
    values.trap.raised = 1;
    model::TrapHit hit;
    hit.time = "07:50";
    hit.address = "192.0.2.50";
    hit.source_port = 51000;
    hit.port = 80;
    hit.first_bytes = "GET /" + kScript + kQuote;
    values.trap.hits.push_back(hit);
    return values;
}

void check_escaping(const std::string& out_folder) {
    const Values values = hostile_values();
    const model::ReportSource source{values.home, values.tools, values.watch, values.trap, "2026-11-23 07:51",
                                     "1.0.0" + kScript};
    const std::string html = model::report_html(source);
    const std::string low = lower(html);

    expect(!contains(low, "<script"), "no script tag in the page");
    expect(!contains(low, "<img"), "no img tag in the page");
    expect(!contains(low, "onerror=alert(2)>"), "no live attribute from a quote");
    expect(contains(html, "&lt;script&gt;alert(1)&lt;/script&gt;"), "a hostile name shows as text");
    expect(contains(html, "&quot;&gt;&lt;img src=x onerror=alert(2)&gt;"), "a hostile quote shows as text");
    expect(contains(html, "&#39; onmouseover=&#39;alert(3)"), "single quotes escaped");
    expect(!contains(html, "\xff") && !contains(html, "\xc0\xaf") && !contains(html, "\x1b"),
           "broken bytes and control characters are gone");
    expect(contains(html, "bad\xEF\xBF\xBD\xEF\xBF\xBD utf8"), "broken bytes become the replacement character");
    expect(contains(html, "Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'"),
           "the page forbids loading and running anything");
    expect(!contains(low, "href=\"javascript"), "a javascript: link is never made");
    expect(!contains(html, "src=\"") && !contains(low, "<link") && !contains(low, "@import") && !contains(low, "url("),
           "the page loads nothing");
    // Every link is to an https page; the made-up CVE gets none.
    std::size_t at = 0;
    int links = 0;
    while ((at = html.find("href=\"", at)) != std::string::npos) {
        at += 6;
        ++links;
        expect(html.compare(at, 8, "https://") == 0, "a link that is not https: " + html.substr(at, 40));
    }
    expect(links == 1, "one link: the real CVE's (" + std::to_string(links) + ")");
    expect(contains(html, "href=\"https://nvd.nist.gov/vuln/detail/CVE-2024-6387\""), "the CVE links to NVD");
    expect(contains(html, "Attackers are using it"), "plain words for an exploited hole");
    expect(contains(html, "Old version, no more security fixes"), "plain words for an old version");
    expect(contains(html, "What to do:"), "what to do is said");
    expect(contains(html, "10 more known holes"), "holes not kept are counted");
    expect(contains(html, "1 visit to the trap"), "the trap's visit is counted once");
    expect(!contains(html, "192.0.2.99"), "a trap alert in Watch's log is not listed twice");
    // The tags stay balanced: as many opening as closing table and div tags.
    const auto count = [&low](const std::string& part) {
        int n = 0;
        for (std::size_t pos = 0; (pos = low.find(part, pos)) != std::string::npos; pos += part.size()) {
            ++n;
        }
        return n;
    };
    expect(count("<table>") == count("</table>"), "tables are closed");
    expect(count("<div") == count("</div>"), "boxes are closed");
    expect(count("<section>") == count("</section>"), "sections are closed");
    expect(count("<tr>") == count("</tr>"), "rows are closed");

    const std::string csv = model::report_devices_csv(source);
    expect(contains(csv, "\"'=HYPERLINK(\"\"http://198.51.100.9\"\")\""), "a formula is neutralised and quoted");
    expect(contains(csv, "\"<script>alert(1)</script>\""), "the CSV keeps names as text");
    expect(!contains(csv, "\xff") && !contains(csv, "\x01"), "the CSV has no broken bytes");
    expect(contains(csv, "\"hotspot\""), "hotspot devices are in the CSV");
    expect(csv.find("\r\n") != std::string::npos, "the CSV uses CRLF line ends");

    expect(model::csv_field("-1") == "\"'-1\"" && model::csv_field("@x") == "\"'@x\"" &&
               model::csv_field("+1") == "\"'+1\"" && model::csv_field("a\"b") == "\"a\"\"b\"",
           "CSV fields");
    expect(model::html_text("a&b") == "a&amp;b", "ampersand escaped");

    // Empty values: every section says it did not run, nothing breaks.
    const model::HomeData home;
    const model::ToolsData tools;
    const model::WatchData watch;
    const model::TrapRun trap;
    const std::string empty = model::report_html({home, tools, watch, trap, "2026-11-23 07:51", "1.0.0"});
    expect(contains(empty, "made-up sample values"), "sample values are marked");
    expect(contains(empty, "Discover has not run"), "an empty Discover says so");
    expect(contains(empty, "Watch has not run"), "an empty Watch says so");
    expect(contains(empty, "No device scanned with Ports yet"), "an empty port scan says so");

    if (!out_folder.empty()) {
        std::ofstream(out_folder + "/report-hostile.html", std::ios::binary) << html;
        std::ofstream(out_folder + "/report-hostile.csv", std::ios::binary) << csv;
    }
}

void check_files() {
    expect(platform::valid_report_name("lokictl-20261123-075130.html"), "a page name");
    expect(platform::valid_report_name("lokictl-20261123-075130-2-devices.csv"), "a second devices name");
    for (const char* bad : {"../lokictl-20261123-075130.html", "lokictl-20261123-075130.html/", "lokictl-2026112-075130.html",
                            "lokictl-20261123-075130-1.html", "lokictl-20261123-075130.htm", "x.html", "",
                            "lokictl-20261123-07513a.html", "lokictl-20261123-075130.html.csv"}) {
        expect(!platform::valid_report_name(bad), std::string("refused name: ") + bad);
    }

    char pattern[] = "/tmp/lokictl-report-test-XXXXXX";
    const char* temp = ::mkdtemp(pattern);
    expect(temp != nullptr, "temporary folder");
    if (!temp) {
        return;
    }
    const std::string folder = std::string(temp) + "/LokiCTL/reports";
    platform::SavedReport first;
    platform::SavedReport second;
    std::string reason;
    expect(platform::save_report(folder, "20261123-075130", "<p>one</p>", "a\r\n", first, reason), "first save");
    expect(platform::save_report(folder, "20261123-075130", "<p>two</p>", "b\r\n", second, reason), "second save");
    expect(first.page == "lokictl-20261123-075130.html" && first.devices == "lokictl-20261123-075130-devices.csv",
           "first names");
    expect(second.page == "lokictl-20261123-075130-2.html", "second name: " + second.page);
    expect(!platform::save_report(folder, "bad", "", "", first, reason), "a bad stamp is refused");

    struct stat info{};
    expect(::stat(folder.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700, "reports folder is 0700");
    expect(::stat((std::string(temp) + "/LokiCTL").c_str(), &info) == 0 && (info.st_mode & 0777) == 0700,
           "LokiCTL folder is 0700");
    expect(::stat((folder + "/" + second.page).c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
           "report file is 0600");

    // A link with a report's name is neither listed nor read.
    const std::string link = folder + "/lokictl-20261124-000000.html";
    expect(::symlink("/etc/hosts", link.c_str()) == 0, "made a link");
    const std::vector<platform::ReportFile> files = platform::list_reports(folder);
    expect(files.size() == 4, "four files listed (" + std::to_string(files.size()) + ")");
    expect(!files.empty() && files[0].name == second.page, "the newest page first");
    expect(files.size() == 4 && files[1].name == second.devices, "then its devices");
    std::string content;
    expect(platform::read_report(folder, second.page, content) && content == "<p>two</p>", "read a report");
    expect(!platform::read_report(folder, "lokictl-20261124-000000.html", content), "a link is not read");
    expect(!platform::read_report(folder, "../../../etc/hosts", content), "a path is not read");

    std::error_code error;
    std::filesystem::remove_all(temp, error);
}

} // namespace

int main(int argc, char** argv) {
    check_escaping(argc > 1 ? argv[1] : "");
    check_files();
    if (failures == 0) {
        std::printf("report checks passed\n");
        return 0;
    }
    std::printf("%d report checks failed\n", failures);
    return 1;
}
