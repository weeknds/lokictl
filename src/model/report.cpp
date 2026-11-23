/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "report.h"

#include "health_check.h"
#include "service_rules.h"
#include "vuln_list.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace model {
namespace {

// The page's look: plain, light, readable on any screen and on paper.
constexpr const char* kStyle =
    ":root{color-scheme:light}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:#fff;color:#1d2125;"
    "font:15px/1.5 -apple-system,'Segoe UI',Roboto,Helvetica,Arial,sans-serif}"
    "main{max-width:880px;margin:0 auto;padding:24px 16px 40px}"
    "h1{font-size:24px;margin:0 0 4px}"
    "h2{font-size:18px;margin:30px 0 8px;padding-bottom:4px;border-bottom:1px solid #d5dade}"
    "h3{font-size:15px;margin:16px 0 4px}"
    "p{margin:4px 0}"
    "ul{margin:6px 0;padding-left:22px}"
    "dt{font-weight:600;margin-top:6px}dd{margin:0 0 0 16px}"
    ".meta,.note{color:#56616b;font-size:13px}"
    ".sample{background:#fff4d6;border:1px solid #e3c25e;border-radius:6px;padding:6px 10px;margin:8px 0}"
    ".scroll{overflow-x:auto}"
    "table{width:100%;border-collapse:collapse;font-size:14px;margin:6px 0}"
    "th,td{text-align:left;vertical-align:top;padding:5px 8px;border-bottom:1px solid #e3e6e9}"
    "th{font-weight:600;background:#f2f4f6}"
    ".mono{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px;word-break:break-all}"
    ".item{border:1px solid #dde2e6;border-left:5px solid #8a949c;border-radius:6px;padding:8px 12px;margin:8px 0;"
    "break-inside:avoid}"
    ".item.risk{border-left-color:#c62828}.item.warn{border-left-color:#c98500}.item.info{border-left-color:#2f6fb3}"
    ".tag{display:inline-block;font-size:12px;font-weight:600;padding:0 6px;margin-right:8px;border:1px solid;"
    "border-radius:4px}"
    ".risk .tag{color:#b71c1c}.warn .tag{color:#7f5500}.info .tag{color:#1f5a99}"
    "a{color:#1f5a99}"
    "footer{margin-top:32px;color:#56616b;font-size:13px}"
    "@media print{body{font-size:12px}main{max-width:none;padding:0}a{color:inherit}"
    "tr,.item{break-inside:avoid}h2,h3{break-after:avoid}}";

// The replacement character, for bytes that are not UTF-8.
constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// The length of the UTF-8 sequence that starts at `at`, or 0 when the bytes
// there are not a well-formed one (no overlong forms, no surrogates,
// nothing above U+10FFFF).
std::size_t utf8_length(std::string_view text, std::size_t at) {
    const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(text[index]); };
    const unsigned char lead = byte(at);
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        low = lead == 0xE0 ? 0xA0 : 0x80;
        high = lead == 0xED ? 0x9F : 0xBF;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        low = lead == 0xF0 ? 0x90 : 0x80;
        high = lead == 0xF4 ? 0x8F : 0xBF;
    } else {
        return 0;
    }
    if (at + length > text.size()) {
        return 0;
    }
    if (byte(at + 1) < low || byte(at + 1) > high) {
        return 0;
    }
    for (std::size_t index = at + 2; index < at + length; ++index) {
        if (byte(index) < 0x80 || byte(index) > 0xBF) {
            return 0;
        }
    }
    return length;
}

// `text` as clean UTF-8: control characters become a space and broken
// bytes the replacement character; `escape` is called for every ASCII
// character that is kept, and appends it (or what stands for it).
template <typename Escape>
std::string cleaned(std::string_view text, Escape escape) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (std::size_t at = 0; at < text.size();) {
        const auto byte = static_cast<unsigned char>(text[at]);
        if (byte < 0x80) {
            if (byte < 0x20 || byte == 0x7F) {
                out += ' ';
            } else {
                escape(out, static_cast<char>(byte));
            }
            ++at;
            continue;
        }
        const std::size_t length = utf8_length(text, at);
        if (length == 0) {
            out += kReplacement;
            ++at;
            continue;
        }
        out.append(text.substr(at, length));
        at += length;
    }
    return out;
}

// Short for html_text(), used for every text that is not a fixed part of
// the page.
std::string esc(std::string_view text) {
    return html_text(text);
}

// "8.1"
std::string score_text(int32_t tenths) {
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

// "93.4 Mbit/s"
std::string mbit_text(int32_t kbps) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f Mbit/s", static_cast<double>(kbps) / 1000.0);
    return text;
}

// "40 s", "5 min", "1 h 5 min"
std::string duration_text(int32_t seconds) {
    if (seconds < 60) {
        return std::to_string(seconds < 0 ? 0 : seconds) + " s";
    }
    const int32_t minutes = seconds / 60;
    if (minutes < 60) {
        return std::to_string(minutes) + " min";
    }
    return std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
}

std::string plural(int64_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

// Whether `text` is an official hole number: "CVE-", a four-digit year,
// "-" and 4 to 7 digits.
bool cve_id(std::string_view text) {
    if (text.size() < 13 || text.size() > 16 || text.substr(0, 4) != "CVE-" || text[8] != '-') {
        return false;
    }
    for (std::size_t index = 4; index < text.size(); ++index) {
        if (index != 8 && (text[index] < '0' || text[index] > '9')) {
            return false;
        }
    }
    return true;
}

// A link, only to an https:// page whose address is plain printable text;
// anything else is shown as text only.
std::string link(std::string_view href, std::string_view text) {
    bool safe = href.substr(0, 8) == "https://" && href.size() > 8 && href.size() <= 200;
    for (const char character : href) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20 || byte >= 0x7F || character == '"' || character == '\'' || character == '<' ||
            character == '>' || character == '\\' || character == '`') {
            safe = false;
        }
    }
    if (!safe) {
        return esc(text);
    }
    return "<a href=\"" + esc(href) + "\" rel=\"noreferrer\">" + esc(text) + "</a>";
}

struct LevelLook {
    const char* css;   // "risk", "warn", "info"
    const char* words; // the tag
};

LevelLook level_look(RiskLevel level) {
    switch (level) {
        case RiskLevel::Risk:
            return {"risk", "Fix this soon"};
        case RiskLevel::Warning:
            return {"warn", "Worth a look"};
        case RiskLevel::Info:
            return {"info", "Good to know"};
    }
    return {"info", "Good to know"};
}

// One finding's box: its tag and title, then lines that are already HTML.
void add_item(std::string& out, const char* css, const std::string& tag, const std::string& title,
              const std::vector<std::string>& html_lines) {
    out += "<div class=\"item ";
    out += css;
    out += "\"><p><span class=\"tag\">" + esc(tag) + "</span><b>" + esc(title) + "</b></p>\n";
    for (const std::string& line : html_lines) {
        out += line;
        out += '\n';
    }
    out += "</div>\n";
}

std::string para(const std::string& html, const char* css = nullptr) {
    return css ? "<p class=\"" + std::string(css) + "\">" + html + "</p>" : "<p>" + html + "</p>";
}

std::string todo(const std::string& text) {
    return "<p><b>What to do:</b> " + esc(text) + "</p>";
}

// A table: the header cells, then rows of cells that are already HTML.
void add_table(std::string& out, const std::vector<const char*>& head,
               const std::vector<std::vector<std::string>>& rows) {
    out += "<div class=\"scroll\"><table>\n<thead><tr>";
    for (const char* cell : head) {
        out += "<th>";
        out += cell;
        out += "</th>";
    }
    out += "</tr></thead>\n<tbody>\n";
    for (const auto& row : rows) {
        out += "<tr>";
        for (const std::string& cell : row) {
            out += "<td>" + cell + "</td>";
        }
        out += "</tr>\n";
    }
    out += "</tbody></table></div>\n";
}

std::string mono(std::string_view text) {
    return text.empty() ? std::string("-") : "<span class=\"mono\">" + esc(text) + "</span>";
}

std::string or_dash(std::string_view text) {
    return text.empty() ? std::string("-") : esc(text);
}

// The name Discover knows for `address`; empty when none.
std::string device_name(const std::vector<DiscoverDevice>& devices, const std::string& address) {
    for (const DiscoverDevice& entry : devices) {
        if (!address.empty() && entry.device.address == address) {
            return entry.device.name;
        }
    }
    return {};
}

// What a port scan's check found, in short.
struct HoleCounts {
    int32_t holes{0};
    int32_t exploited{0};
    int32_t outdated{0};
};

HoleCounts hole_counts(const PortRun& run) {
    HoleCounts counts;
    for (const PortVerdict& verdict : run.verdicts) {
        counts.holes += verdict.holes;
        counts.exploited += verdict.exploited;
        counts.outdated += verdict.outdated ? 1 : 0;
    }
    return counts;
}

// ---------------------------------------------------------------- sections

void add_head(std::string& out, const ReportSource& source) {
    out += "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    // Nothing may load or run, even if a text slipped through: only the
    // style below is allowed.
    out += "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; "
           "base-uri 'none'; form-action 'none'\">\n";
    out += "<meta name=\"referrer\" content=\"no-referrer\">\n";
    out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out += "<title>LokiCTL report " + esc(source.saved_at) + "</title>\n";
    out += "<style>";
    out += kStyle;
    out += "</style>\n</head>\n<body>\n<main>\n";
    out += "<header>\n<h1>Network report</h1>\n";
    out += para("Saved by LokiCTL " + esc(source.version) + " on " + esc(source.saved_at) +
                    " (the Cardputer's clock). It shows what LokiCTL had found by then.",
                "meta") +
           "\n";
    if (source.home.sample) {
        out += "<p class=\"sample\">These are made-up sample values, not a real network.</p>\n";
    }
    out += "</header>\n";
}

std::string network_line(const HomeData& home) {
    std::string line;
    switch (home.uplink_kind) {
        case LinkKind::None:
            return "Not connected to a network.";
        case LinkKind::Wifi:
            line = "Wi-Fi network <b>" + esc(home.wifi_name.empty() ? "(hidden)" : home.wifi_name) + "</b>";
            if (!home.wifi_security.empty()) {
                line += " (" + esc(home.wifi_security) + ")";
            }
            break;
        case LinkKind::Cable:
            line = "Network cable (" + esc(home.uplink_name);
            if (home.uplink_speed_mbps > 0) {
                line += ", " + std::to_string(home.uplink_speed_mbps) + " Mbit/s";
            }
            line += ")";
            break;
    }
    if (!home.uplink_address.empty()) {
        line += ". This Cardputer: " + esc(home.uplink_address);
    }
    if (!home.gateway_address.empty()) {
        line += ", router: " + esc(home.gateway_address);
    }
    line += ".";
    if (home.mode != NetMode::Client) {
        line += " Mode: " + esc(net_mode_name(home.mode)) + ".";
    }
    return line;
}

void add_glance(std::string& out, const ReportSource& source) {
    const ToolsData& tools = source.tools;
    std::vector<std::string> lines;
    lines.push_back("<b>Network:</b> " + network_line(source.home));

    const std::vector<DiscoverDevice> devices = discover_devices(tools.listen, tools.probe);
    if (tools.listen.state == ToolState::Idle && tools.probe.state == ToolState::Idle) {
        lines.push_back("<b>Devices:</b> Discover has not run.");
    } else {
        lines.push_back("<b>Devices:</b> " + plural(static_cast<int64_t>(devices.size()), "device", "devices") +
                        " found.");
    }

    const HealthRun& health = tools.health;
    if (health.state == ToolState::Idle) {
        lines.push_back("<b>Health check:</b> not run.");
    } else {
        const HealthCounts counts = health_counts(health.findings);
        std::string text;
        const auto add = [&text](int32_t count, const char* words) {
            if (count > 0) {
                text += (text.empty() ? "" : ", ") + std::to_string(count) + " " + words;
            }
        };
        add(counts.risks, "to fix soon");
        add(counts.warnings, "worth a look");
        add(counts.infos, "good to know");
        if (text.empty()) {
            text = health.state == ToolState::Failed ? "could not run" : "nothing found";
        }
        if (health.state == ToolState::Running || health.state == ToolState::Stopped) {
            text += " (not finished)";
        }
        lines.push_back("<b>Health check:</b> " + text + ".");
    }

    const PortRun& ports = tools.ports;
    if (ports.state == ToolState::Idle) {
        lines.push_back("<b>Security holes:</b> no device scanned with Ports yet.");
    } else if (ports.list_date.empty()) {
        lines.push_back("<b>Security holes:</b> versions not checked on " + esc(ports.target) + ".");
    } else {
        const HoleCounts counts = hole_counts(ports);
        std::string text;
        if (counts.holes == 0 && counts.outdated == 0) {
            text = "none found";
        } else {
            if (counts.holes > 0) {
                text = plural(counts.holes, "known security hole", "known security holes");
                if (counts.exploited > 0) {
                    text += " (" + std::to_string(counts.exploited) + " that attackers are using)";
                }
            }
            if (counts.outdated > 0) {
                text += (text.empty() ? "" : ", ") + plural(counts.outdated, "old version", "old versions");
            }
        }
        lines.push_back("<b>Security holes:</b> " + text + " on " + esc(ports.target) + ".");
    }

    int32_t alerts = 0;
    for (const WatchAlert& alert : source.watch.run.alerts) {
        alerts += alert.kind == WatchAlertKind::Trap ? 0 : 1;
    }
    std::string alert_text = source.watch.run.state == ToolState::Idle ? std::string("Watch has not run")
                                                                       : plural(alerts, "alert", "alerts");
    alert_text += source.trap.state == ToolState::Idle
                      ? ", the trap was not set."
                      : ", " + plural(static_cast<int64_t>(source.trap.hits.size()), "visit", "visits") +
                            " to the trap.";
    lines.push_back("<b>Alerts:</b> " + alert_text);

    const SpeedRun& speed = tools.speed;
    if (speed.state == ToolState::Done && speed.down_kbps >= 0 && speed.up_kbps >= 0) {
        lines.push_back("<b>Speed:</b> " + mbit_text(speed.down_kbps) + " down, " + mbit_text(speed.up_kbps) + " up.");
    } else {
        lines.push_back(std::string("<b>Speed:</b> ") +
                        (speed.state == ToolState::Idle ? "not measured." : "no complete test."));
    }

    out += "<section>\n<h2>At a glance</h2>\n<ul>\n";
    for (const std::string& line : lines) {
        out += "<li>" + line + "</li>\n";
    }
    out += "</ul>\n</section>\n";
}

void add_health(std::string& out, const HealthRun& run) {
    out += "<section>\n<h2>Health check</h2>\n";
    switch (run.state) {
        case ToolState::Idle:
            out += para("Not run. It looks for weak spots on your own network: on the Cardputer, Tools, key 7 "
                        "(Check). Then save a new report.") +
                   "\n</section>\n";
            return;
        case ToolState::Failed:
            out += para("Could not run" + (run.reason.empty() ? std::string(".") : ": " + esc(run.reason) + ".")) +
                   "\n</section>\n";
            return;
        case ToolState::Running:
            out += para("Still running when this report was saved: not every device was checked.", "note") + "\n";
            break;
        case ToolState::Stopped:
            out += para("Stopped before the end: not every device was checked.", "note") + "\n";
            break;
        case ToolState::Done:
            break;
    }
    std::string facts = "Checked";
    if (!run.network.empty()) {
        facts += " " + esc(run.network);
    }
    if (!run.interface_name.empty()) {
        facts += " from " + esc(run.interface_name);
    }
    facts += ": " + plural(run.scanned, "device", "devices") + " scanned, in " + duration_text(run.elapsed_s) +
             ". The worst come first.";
    for (const HealthFinding& finding : run.findings) {
        if (finding.issue == HealthIssue::Holes || finding.issue == HealthIssue::Outdated ||
            finding.issue == HealthIssue::Check) {
            facts += " Each known hole has an official number (CVE): scan that device with Ports, then save a new "
                     "report to see them.";
            break;
        }
    }
    out += para(facts, "note") + "\n";
    if (run.findings.empty()) {
        out += para("Nothing found.") + "\n";
    }
    for (const HealthFinding& finding : run.findings) {
        const LevelLook look = level_look(finding.level);
        std::vector<std::string> lines;
        std::string where = finding.where;
        if (!finding.fact.empty()) {
            where += (where.empty() ? "" : " · ") + finding.fact;
        }
        if (!where.empty()) {
            lines.push_back(para(esc(where), "meta"));
        }
        const std::string about = health_about(finding);
        if (!about.empty()) {
            lines.push_back(para(esc(about)));
        }
        const std::string fix = health_fix(finding);
        if (!fix.empty()) {
            lines.push_back(todo(fix));
        }
        add_item(out, look.css, look.words, finding.title, lines);
    }
    out += "</section>\n";
}

void add_hole(std::string& out, const ServiceFinding& finding) {
    std::string product = finding.name.empty() ? finding.product : finding.name;
    if (!finding.version.empty()) {
        product += " " + finding.version;
    }
    const std::string title = product + " on port " + std::to_string(finding.port);
    const std::string vendor = finding.vendor == "vendor" ? std::string("Its maker") : finding.vendor;
    std::vector<std::string> lines;
    if (finding.kind == FindingKind::Outdated) {
        lines.push_back(para("Old version, no more security fixes."));
        if (!vendor.empty()) {
            lines.push_back(para(esc(vendor) + " may still fix it with its own updates."));
        }
        lines.push_back(todo(vendor.empty() ? "Update the device's software, or replace the device."
                                            : "Install the device's updates; if it gets none, replace it."));
        if (!finding.link.empty()) {
            lines.push_back(para("More: " + link(finding.link, finding.link), "meta"));
        }
        add_item(out, "warn", "Old version", title, lines);
        return;
    }
    const char* severity = finding.score >= 90 ? "Very serious hole"
                           : finding.score >= 70 ? "Serious hole"
                                                 : "Known hole";
    const char* tag = finding.exploited ? "Hole attackers use" : severity;
    const char* css = !vendor.empty() ? "warn" : (finding.exploited || finding.score >= 90) ? "risk" : "warn";
    std::string sentence = std::string("Known security hole") +
                           (finding.score >= 90 ? ", very serious" : finding.score >= 70 ? ", serious" : "");
    if (finding.score >= 0) {
        sentence += " (" + score_text(finding.score) + " of 10)";
    }
    sentence += ".";
    if (finding.exploited) {
        sentence += " Attackers are using it.";
    }
    lines.push_back(para(sentence));
    if (!vendor.empty()) {
        lines.push_back(para(esc(vendor) + " may already have fixed it with its updates."));
    }
    lines.push_back(todo(vendor.empty() ? "Update the device's software (its firmware)."
                                        : "Install the device's updates."));
    if (cve_id(finding.cve)) {
        lines.push_back(para("Official number: " + link(cve_link(finding.cve), finding.cve), "meta"));
    } else if (!finding.cve.empty()) {
        lines.push_back(para("Official number: " + esc(finding.cve), "meta"));
    }
    add_item(out, css, tag, title, lines);
}

void add_holes(std::string& out, const PortRun& run, const std::vector<DiscoverDevice>& devices) {
    out += "<section>\n<h2>Security holes and old software</h2>\n";
    if (run.state == ToolState::Idle) {
        out += para("No device scanned with Ports yet. On the Cardputer: Tools, Ports, then save a new report.") +
               "\n</section>\n";
        return;
    }
    std::string target = esc(run.target);
    const std::string name = device_name(devices, run.target);
    if (!name.empty()) {
        target += " (" + esc(name) + ")";
    }
    if (run.list_date.empty()) {
        out += para("From the last port scan, of " + target +
                    ". Versions were not checked: the scan did not finish, or the list of holes was missing.") +
               "\n</section>\n";
        return;
    }
    out += para("From the last port scan, of " + target + ", checked against LokiCTL's built-in list from " +
                    esc(run.list_date) +
                    ". It compares version numbers only, so a device that was fixed without a new number can "
                    "still show up.",
                "note") +
           "\n";
    if (run.findings.empty()) {
        out += para("No known security holes or old versions found.") + "\n";
    }
    for (const ServiceFinding& finding : run.findings) {
        add_hole(out, finding);
    }
    // Only the worst few holes per port are kept; say how many more there are.
    for (const PortVerdict& verdict : run.verdicts) {
        int32_t kept = 0;
        for (const ServiceFinding& finding : run.findings) {
            kept += finding.port == verdict.port && finding.kind == FindingKind::Hole ? 1 : 0;
        }
        if (verdict.holes > kept) {
            out += para("Port " + std::to_string(verdict.port) + " has " +
                            plural(verdict.holes - kept, "more known hole", "more known holes") +
                            " not listed here; the update fixes them too.",
                        "note") +
                   "\n";
        }
    }
    out += "</section>\n";
}

std::string port_note(const PortRun& run, uint16_t port) {
    std::string note;
    if (const ServiceRule* rule = service_rule(port)) {
        note = capitalized(rule->note) + ".";
    }
    for (const PortVerdict& verdict : run.verdicts) {
        if (verdict.port != port) {
            continue;
        }
        std::string found;
        if (verdict.holes > 0) {
            found = plural(verdict.holes, "known hole", "known holes");
            if (verdict.exploited > 0) {
                found += ", attackers use " + std::to_string(verdict.exploited);
            }
        }
        if (verdict.outdated) {
            found += (found.empty() ? "" : "; ") + std::string("old version");
        }
        if (!found.empty()) {
            note += (note.empty() ? "" : " ") + capitalized(found) + " (see above).";
        }
    }
    return note;
}

void add_ports(std::string& out, const PortRun& run) {
    out += "<section>\n<h2>Open ports and services</h2>\n";
    if (run.state == ToolState::Idle) {
        out += para("No port scan yet.") + "\n</section>\n";
        return;
    }
    if (run.state == ToolState::Failed) {
        out += para("The last port scan could not run" +
                    (run.reason.empty() ? std::string(".") : ": " + esc(run.reason) + ".")) +
               "\n</section>\n";
        return;
    }
    std::string facts = esc(run.target) + ": " + std::to_string(run.asked) + " of " + std::to_string(run.total) +
                        " common ports tried, " + std::to_string(run.open.size()) + " open.";
    if (run.state == ToolState::Running || run.state == ToolState::Stopped) {
        facts += " The scan had not finished.";
    }
    out += para(facts, "note") + "\n";
    if (run.open.empty()) {
        out += para("No open ports found.") + "\n</section>\n";
        return;
    }
    std::vector<std::vector<std::string>> rows;
    for (const uint16_t port : run.open) {
        std::string number = std::to_string(port);
        const std::string service = port_service(port);
        if (!service.empty()) {
            number += " <span class=\"meta\">" + esc(service) + "</span>";
        }
        rows.push_back({number, or_dash(port_plain(port)), mono(port_banner(run, port)), or_dash(port_note(run, port))});
    }
    add_table(out, {"Port", "What it is for", "What it said", "Note"}, rows);
    out += "</section>\n";
}

// The names each hotspot device looked up (TalkRun), newest first, with
// how often. Only while the hotspot runs: the names are kept in memory
// only and go when it stops.
void add_talk(std::string& out, const ReportSource& source) {
    const HomeData& home = source.home;
    const TalkRun& talk = source.talk;
    if (!home.ap_active || talk.devices.empty()) {
        return;
    }
    out += "<h3>Names the hotspot's devices looked up</h3>\n";
    out += para("Newest first, with how often. LokiCTL keeps them in memory only and forgets them when the "
                "hotspot stops; this report has them because you saved it.",
                "note") +
           "\n";
    std::vector<std::vector<std::string>> rows;
    for (const TalkDevice& device : talk.devices) {
        std::string who;
        for (const HomeClient& client : home.clients) {
            if (client.hardware_address == device.hardware && !client.name.empty()) {
                who = esc(client.name) + "<br>";
                break;
            }
        }
        who += mono(device.address) + "<br>" + mono(device.hardware);
        std::string names;
        for (const TalkName& name : device.names) {
            if (!names.empty()) {
                names += ", ";
            }
            names += mono(name.name) + (name.count > 1 ? " &times;" + std::to_string(name.count) : std::string());
        }
        rows.push_back({who, names.empty() ? std::string("-") : names});
    }
    add_table(out, {"Device", "Names looked up"}, rows);
}

void add_devices(std::string& out, const ReportSource& source, const std::vector<DiscoverDevice>& devices) {
    const ToolsData& tools = source.tools;
    out += "<section>\n<h2>Devices</h2>\n";
    if (tools.listen.state == ToolState::Idle && tools.probe.state == ToolState::Idle) {
        out += para("Discover has not run. On the Cardputer: home page, key 5 (Scan), then save a new report.") +
               "\n";
    } else {
        std::string facts;
        if (tools.listen.state != ToolState::Idle) {
            facts += "Heard while listening on " + esc(tools.listen.interface_name) + " for " +
                     duration_text(tools.listen.elapsed_s) + ". ";
        }
        if (tools.probe.state != ToolState::Idle) {
            facts += "Probed " + esc(tools.probe.network.empty() ? tools.probe.interface_name : tools.probe.network) +
                     ": " + plural(tools.probe.replied, "device", "devices") + " answered. ";
        }
        if (!source.watch.known.empty()) {
            facts += "Known: on your list of known devices (Watch).";
        }
        out += para(facts, "note") + "\n";
        if (devices.empty()) {
            out += para("No devices found.") + "\n";
        } else {
            std::vector<std::vector<std::string>> rows;
            for (const DiscoverDevice& entry : devices) {
                const HeardDevice& device = entry.device;
                std::vector<std::string> notes;
                if (!device.own_port.empty()) {
                    notes.push_back("This Cardputer (" + esc(device.own_port) + ")");
                } else if (!device.address.empty() && device.address == source.home.gateway_address) {
                    notes.push_back("Your router");
                }
                if (!source.watch.known.empty() && !device.hardware_address.empty()) {
                    notes.push_back(watch_known(source.watch, device.hardware_address) ? "Known" : "Not known");
                }
                if (device.upnp) {
                    notes.push_back("UPnP on (apps can open ports)");
                }
                if (!device.ssdp_server.empty()) {
                    notes.push_back("Says: " + mono(device.ssdp_server));
                }
                std::string joined;
                for (const std::string& note : notes) {
                    joined += (joined.empty() ? "" : "; ") + note;
                }
                rows.push_back({or_dash(device.name), or_dash(device.address), mono(device.hardware_address),
                                or_dash(device.kind), joined.empty() ? std::string("-") : joined});
            }
            add_table(out, {"Name", "Address", "Hardware address", "What it is", "Notes"}, rows);
        }
    }

    const HomeData& home = source.home;
    if (home.ap_active && !home.clients.empty()) {
        out += "<h3>On the Cardputer's hotspot</h3>\n";
        std::vector<std::vector<std::string>> rows;
        for (const HomeClient& client : home.clients) {
            const std::string signal =
                client.signal_dbm != kSignalUnknown ? std::to_string(client.signal_dbm) + " dBm" : std::string("-");
            rows.push_back({or_dash(client.name), or_dash(client.address), mono(client.hardware_address), signal,
                            client.blocked ? "Blocked" : "-"});
        }
        add_table(out, {"Name", "Address", "Hardware address", "Signal", "Blocked"}, rows);
    }
    add_talk(out, source);
    out += "</section>\n";
}

// What to do about each kind of alert, in one short sentence.
const char* alert_todo(WatchAlertKind kind) {
    switch (kind) {
        case WatchAlertKind::NewDevice:
            return "If you know it, mark it known on the Watch page (key 7). If not, find out what it is, and "
                   "change your Wi-Fi password if it should not be there.";
        case WatchAlertKind::FakeRouter:
            return "Unplug devices you don't know and watch whether it happens again. Avoid logging in to "
                   "anything until it stops.";
        case WatchAlertKind::SameAddress:
            return "Look for two devices set to the same address, or a device you don't know.";
        case WatchAlertKind::SecondDhcp:
            return "Find the second device that hands out addresses and switch that function off.";
        case WatchAlertKind::Ipv6Router:
            return "Find the device at that address. If it is not a router you own, unplug it.";
        case WatchAlertKind::Scan:
            return "Find the device at that address. If you don't know it, remove it from your network.";
        case WatchAlertKind::ArpFlood:
            return "Look for a broken or unknown device. Restarting the router helps if the network is slow.";
        case WatchAlertKind::Trap:
            return "Find the device at that address. If you don't know it, remove it from your network.";
    }
    return "";
}

std::string alert_details(const WatchAlert& alert) {
    switch (alert.kind) {
        case WatchAlertKind::FakeRouter:
            return alert.before.empty() ? std::string() : "Was " + alert.before;
        case WatchAlertKind::SameAddress:
            return alert.before.empty() ? std::string() : "Also " + alert.before;
        case WatchAlertKind::SecondDhcp:
            return alert.before.empty() ? std::string() : "First server " + alert.before;
        case WatchAlertKind::Scan:
            return "Tried " + std::to_string(alert.count) + " ports";
        case WatchAlertKind::ArpFlood:
            return std::to_string(alert.count) + " in one second";
        case WatchAlertKind::NewDevice:
        case WatchAlertKind::Ipv6Router:
        case WatchAlertKind::Trap:
            return {};
    }
    return {};
}

void add_alerts(std::string& out, const WatchData& watch) {
    const WatchRun& run = watch.run;
    out += "<section>\n<h2>Alerts (Watch)</h2>\n";
    switch (run.state) {
        case ToolState::Idle:
            out += para("Watch has not run. It only listens and warns about suspicious things: on the Cardputer, "
                        "Tools, key 8 (Watch).") +
                   "\n</section>\n";
            return;
        case ToolState::Failed:
            out += para("Watch could not run" + (run.reason.empty() ? std::string(".") : ": " + esc(run.reason) + ".")) +
                   "\n";
            break;
        case ToolState::Running:
            out += para("Watch was listening on " + esc(run.interface_name) + " for " + duration_text(run.elapsed_s) +
                            " when this report was saved.",
                        "note") +
                   "\n";
            break;
        case ToolState::Stopped:
        case ToolState::Done:
            out += para("Watch listened on " + esc(run.interface_name) + " for " + duration_text(run.elapsed_s) + ".",
                        "note") +
                   "\n";
            break;
    }
    std::vector<std::vector<std::string>> rows;
    bool seen[kWatchAlertKindCount] = {};
    for (const WatchAlert& alert : run.alerts) {
        if (alert.kind == WatchAlertKind::Trap) {
            continue;  // listed under the trap
        }
        seen[static_cast<std::size_t>(alert.kind)] = true;
        std::string who = esc(alert.address);
        if (!alert.hardware.empty()) {
            who += (who.empty() ? "" : "<br>") + mono(alert.hardware);
        }
        rows.push_back({or_dash(alert.time), esc(watch_alert_name(alert.kind)), who.empty() ? std::string("-") : who,
                        or_dash(alert_details(alert))});
    }
    if (rows.empty()) {
        out += para("No alerts.") + "\n</section>\n";
        return;
    }
    out += para("The newest first.", "note") + "\n";
    add_table(out, {"Time", "What happened", "Who", "Details"}, rows);
    out += "<h3>What these mean</h3>\n<dl>\n";
    for (std::size_t kind = 0; kind < kWatchAlertKindCount; ++kind) {
        if (!seen[kind]) {
            continue;
        }
        const auto alert_kind = static_cast<WatchAlertKind>(kind);
        out += "<dt>" + esc(watch_alert_name(alert_kind)) + "</dt>\n<dd>" + esc(watch_alert_about(alert_kind)) +
               " <b>What to do:</b> " + esc(alert_todo(alert_kind)) + "</dd>\n";
    }
    out += "</dl>\n</section>\n";
}

void add_trap(std::string& out, const TrapRun& run) {
    out += "<section>\n<h2>Trap (honeypot)</h2>\n";
    if (run.state == ToolState::Idle) {
        out += para("The trap was not set. It pretends to offer a few services and catches whoever connects: on "
                    "the Cardputer, Watch, key 8 (Trap).") +
               "\n</section>\n";
        return;
    }
    std::string facts = run.state == ToolState::Running ? "Set for " + duration_text(run.elapsed_s) + " when saved: "
                                                        : "Was set for " + duration_text(run.elapsed_s) + ": ";
    facts += plural(run.raised, "connection", "connections") + " logged";
    if (run.dropped > 0) {
        facts += ", " + std::to_string(run.dropped) + " more closed at once (too many)";
    }
    facts += ".";
    if (run.state == ToolState::Failed && !run.reason.empty()) {
        facts += " It stopped: " + esc(run.reason) + ".";
    }
    out += para(facts, "note") + "\n";
    if (run.hits.empty()) {
        out += para("Nobody connected.") + "\n</section>\n";
        return;
    }
    out += para("Nothing real runs on these ports, so whoever connects is probing your network. <b>What to "
                "do:</b> find the device at that address. If you don't know it, remove it from your network and "
                "change your Wi-Fi password.") +
           "\n";
    std::vector<std::vector<std::string>> rows;
    for (const TrapHit& hit : run.hits) {
        const TrapService* service = trap_service(hit.port);
        std::string tried = std::to_string(hit.port);
        if (service) {
            tried += " (" + esc(service->name) + ")";
        }
        rows.push_back({or_dash(hit.time), esc(hit.address) + (hit.source_port ? ":" + std::to_string(hit.source_port) : ""),
                        tried, mono(hit.first_bytes)});
    }
    add_table(out, {"Time", "Who", "Tried", "What it sent first"}, rows);
    out += "</section>\n";
}

void add_connection(std::string& out, const ReportSource& source, const std::vector<DiscoverDevice>& devices) {
    const HomeData& home = source.home;
    const ToolsData& tools = source.tools;
    out += "<section>\n<h2>How you are connected</h2>\n";
    out += para(network_line(home)) + "\n";
    if (!home.details.dns.empty()) {
        std::string dns;
        for (const std::string& server : home.details.dns) {
            dns += (dns.empty() ? "" : ", ") + esc(server);
        }
        out += para("Name servers (DNS): " + dns + ".") + "\n";
    }

    std::string router_hardware;
    for (const DiscoverDevice& entry : devices) {
        if (!home.gateway_address.empty() && entry.device.address == home.gateway_address) {
            router_hardware = entry.device.hardware_address;
        }
    }
    const std::vector<Neighbour>& neighbours = tools.listen.neighbours;
    if (neighbours.empty()) {
        out += para("No switch said what it is. Discover listens for that on a network cable.", "note") + "\n";
    } else {
        out += "<h3>Plugged into</h3>\n<ul>\n";
        for (const Neighbour& neighbour : neighbours) {
            const bool router = neighbour_is_router(neighbour, home.gateway_address, router_hardware);
            std::string name = !neighbour.name.empty()      ? neighbour.name
                               : !neighbour.chassis.empty() ? neighbour.chassis
                                                            : neighbour.hardware_address;
            std::string line = "<b>" + esc(name) + "</b> (" + (router ? "your router" : "a switch or access point");
            const std::string port = neighbour_port_text(neighbour);
            if (!port.empty()) {
                line += ", " + esc(port);
            }
            line += ")";
            if (!neighbour.model.empty()) {
                line += ". " + esc(neighbour.model);
            }
            if (!neighbour.address.empty()) {
                line += ". Address " + esc(neighbour.address);
            }
            const std::string vlan = neighbour_vlan_text(neighbour, 60);
            if (!vlan.empty()) {
                line += ". " + esc(vlan);
            }
            line += ". <span class=\"meta\">Said by " + esc(neighbour.protocol) + ".</span>";
            out += "<li>" + line + "</li>\n";
        }
        out += "</ul>\n";
    }

    const TraceRun& trace = tools.trace;
    if (trace.state == ToolState::Idle || trace.hops.empty()) {
        out += para("No route traced. On the Cardputer: Map, key 8, or Tools, Trace.", "note") + "\n";
    } else {
        std::string facts = "Route to " + esc(trace.target);
        if (!trace.address.empty() && trace.address != trace.target) {
            facts += " (" + esc(trace.address) + ")";
        }
        facts += ": " + plural(static_cast<int64_t>(trace.hops.size()), "step", "steps");
        switch (trace.end) {
            case TraceEnd::Reached:
                facts += ", reached.";
                break;
            case TraceEnd::Unreachable:
                facts += ", a step said it can't be reached.";
                break;
            case TraceEnd::MaxHops:
                facts += ", not reached within " + std::to_string(kTraceMaxHops) + ".";
                break;
            case TraceEnd::None:
                facts += ".";
                break;
        }
        out += "<h3>Route to the internet</h3>\n" + para(facts, "note") + "\n";
        std::vector<std::vector<std::string>> rows;
        for (const TraceHop& hop : trace.hops) {
            std::string time = "-";
            if (hop.time_us >= 0) {
                char text[32];
                std::snprintf(text, sizeof(text), "%.1f ms", static_cast<double>(hop.time_us) / 1000.0);
                time = text;
            } else if (hop.time_us == kPingLost) {
                time = "no answer";
            }
            rows.push_back({std::to_string(hop.ttl), or_dash(hop.address), or_dash(hop.name), time});
        }
        add_table(out, {"Step", "Address", "Name", "Time"}, rows);
    }
    out += "</section>\n";
}

void add_speed(std::string& out, const SpeedRun& run) {
    out += "<section>\n<h2>Speed test</h2>\n";
    const std::string host = run.server.empty() ? std::string() : speed_server_host(run.server);
    const std::string through = host.empty() ? std::string(".") : ", through " + esc(host) + ".";
    const std::string down = run.down_kbps >= 0 ? mbit_text(run.down_kbps) : std::string("not measured");
    const std::string up = run.up_kbps >= 0 ? mbit_text(run.up_kbps) : std::string("not measured");
    switch (run.state) {
        case ToolState::Idle:
            out += para("Not run. On the Cardputer: Tools, Speed.");
            break;
        case ToolState::Done:
            out += para("Download <b>" + down + "</b>, upload <b>" + up + "</b>" + through);
            break;
        case ToolState::Running:
        case ToolState::Stopped:
            out += para("Not finished: download " + down + ", upload " + up + through);
            break;
        case ToolState::Failed:
            out += para("Could not run" + (run.reason.empty() ? std::string(".") : ": " + esc(run.reason) + "."));
            break;
    }
    out += "\n</section>\n";
}

void add_words(std::string& out) {
    out += "<section>\n<h2>Words used</h2>\n<dl>\n"
           "<dt>Security hole</dt><dd>A known weakness in a version of some software. Attackers can use it to "
           "get into the device.</dd>\n"
           "<dt>Official number (CVE)</dt><dd>The number every known hole gets, such as CVE-2024-6387. Its link "
           "opens the official page about it (nvd.nist.gov).</dd>\n"
           "<dt>Score</dt><dd>How bad a hole is, from 0 to 10 (10 is the worst).</dd>\n"
           "<dt>Old version</dt><dd>A version that no longer gets security fixes.</dd>\n"
           "<dt>Port</dt><dd>A numbered door on a device where a service waits for connections, such as 22 for "
           "remote login.</dd>\n"
           "<dt>Hardware address</dt><dd>The fixed address of a device's network chip (its MAC address).</dd>\n"
           "<dt>UPnP</dt><dd>Lets apps on your network open doors (ports) in your router by themselves.</dd>\n"
           "<dt>Trap (honeypot)</dt><dd>Fake services on the Cardputer. Nothing real runs there, so anyone who "
           "connects is probing your network.</dd>\n"
           "</dl>\n</section>\n";
}

} // namespace

std::string html_text(std::string_view text) {
    return cleaned(text, [](std::string& out, char character) {
        switch (character) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out += character;
                break;
        }
    });
}

std::string csv_field(std::string_view text) {
    std::string clean = cleaned(text, [](std::string& out, char character) {
        out += character;
        if (character == '"') {
            out += '"';
        }
    });
    if (!clean.empty() && (clean[0] == '=' || clean[0] == '+' || clean[0] == '-' || clean[0] == '@')) {
        clean.insert(clean.begin(), '\'');
    }
    return "\"" + clean + "\"";
}

std::string report_html(const ReportSource& source) {
    const std::vector<DiscoverDevice> devices = discover_devices(source.tools.listen, source.tools.probe);
    std::string out;
    out.reserve(32 * 1024);
    add_head(out, source);
    add_glance(out, source);
    add_health(out, source.tools.health);
    add_holes(out, source.tools.ports, devices);
    add_ports(out, source.tools.ports);
    add_devices(out, source, devices);
    add_alerts(out, source.watch);
    add_trap(out, source.trap);
    add_connection(out, source, devices);
    add_speed(out, source.tools.speed);
    add_words(out);
    out += "<footer>Made by LokiCTL from what it saw on the network. Use it only on networks you own or may "
           "test.</footer>\n</main>\n</body>\n</html>\n";
    return out;
}

std::string report_devices_csv(const ReportSource& source) {
    std::string out = "\"name\",\"address\",\"hardware_address\",\"what_it_is\",\"where\",\"notes\"\r\n";
    const auto row = [&out](const std::vector<std::string>& fields) {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            out += (index == 0 ? "" : ",") + csv_field(fields[index]);
        }
        out += "\r\n";
    };
    for (const DiscoverDevice& entry : discover_devices(source.tools.listen, source.tools.probe)) {
        const HeardDevice& device = entry.device;
        std::string notes;
        const auto note = [&notes](const std::string& text) { notes += (notes.empty() ? "" : "; ") + text; };
        if (!device.own_port.empty()) {
            note("this Cardputer (" + device.own_port + ")");
        } else if (!device.address.empty() && device.address == source.home.gateway_address) {
            note("router");
        }
        if (!source.watch.known.empty() && !device.hardware_address.empty()) {
            note(watch_known(source.watch, device.hardware_address) ? "known" : "not known");
        }
        if (device.upnp) {
            note("UPnP on");
        }
        if (!device.ssdp_server.empty()) {
            note("says: " + device.ssdp_server);
        }
        row({device.name, device.address, device.hardware_address, device.kind, "network", notes});
    }
    if (source.home.ap_active) {
        for (const HomeClient& client : source.home.clients) {
            row({client.name, client.address, client.hardware_address, "", "hotspot", client.blocked ? "blocked" : ""});
        }
    }
    return out;
}

} // namespace model
