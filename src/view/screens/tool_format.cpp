/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_format.h"

#include "home_format.h"
#include "service_banner.h"
#include "service_rules.h"
#include "service_version.h"

#include <algorithm>
#include <cstdio>

namespace screen::tools {
namespace {

constexpr const char* kSeparator = " · ";

std::string failed_text(const std::string& reason) {
    return "Failed: " + (reason.empty() ? std::string("unknown error") : reason);
}

// The texts for the errors both tools share.
ToneText error_status(model::ToolError error, const std::string& reason) {
    switch (error) {
        case model::ToolError::NotAllowed:
            return {"Ping not allowed for this user", kToneError};
        case model::ToolError::NoServer:
            return {"No DNS server", kToneError};
        case model::ToolError::NoSuchName:
            return {"No such name", kToneWarning};
        case model::ToolError::NoAnswer:
            return {"No answer", kToneWarning};
        case model::ToolError::NoAddress:
            return {"No address for this name", kToneWarning};
        case model::ToolError::BadTarget:
            return {"Not a valid name or address", kToneError};
        case model::ToolError::Failed:
        case model::ToolError::None:
            break;
    }
    return {failed_text(reason), kToneError};
}

} // namespace

std::string time_text(int32_t micros) {
    if (micros < 0) {
        return "--";
    }
    char text[16];
    if (micros < 10000) {
        std::snprintf(text, sizeof(text), "%d.%02d", micros / 1000, (micros % 1000) / 10);
    } else if (micros < 100000) {
        std::snprintf(text, sizeof(text), "%d.%d", micros / 1000, (micros % 1000) / 100);
    } else {
        std::snprintf(text, sizeof(text), "%d", micros / 1000);
    }
    return text;
}

ToneText reply_text(const model::PingReply& reply, bool running) {
    if (reply.time_us == model::kPingLost) {
        return {"lost", kToneWarning};
    }
    if (reply.time_us == model::kPingWaiting) {
        return {running ? "…" : "--", kToneMuted};
    }
    return {time_text(reply.time_us), kToneText};
}

std::string ping_counts_text(const model::PingRun& run) {
    if (run.sent == 0 && run.lost == 0) {
        return {};
    }
    const int32_t settled = run.received + run.lost;
    std::string text = std::to_string(run.sent) + " sent" + kSeparator + std::to_string(run.received) + " received";
    if (settled > 0) {
        text += kSeparator + std::to_string(run.lost * 100 / settled) + "% lost";
    }
    return text;
}

std::string ping_times_text(const model::PingRun& run) {
    if (run.received == 0) {
        return {};
    }
    return "min " + time_text(run.min_us) + kSeparator + "avg " + time_text(run.avg_us) + kSeparator + "max " +
           time_text(run.max_us) + " ms";
}

TraceRow trace_row(const model::TraceHop& hop, bool running) {
    TraceRow row;
    row.number = std::to_string(hop.ttl);
    if (hop.time_us == model::kPingWaiting) {
        row.address = {running ? "…" : "*", kToneMuted};
        row.time = {"", kToneMuted};
    } else if (hop.time_us == model::kPingLost || hop.address.empty()) {
        row.address = {"*", kToneMuted};
        row.name = "no answer";
        row.time = {"", kToneMuted};
    } else {
        row.address = {hop.address, kToneText};
        row.name = hop.name;
        row.time = {time_text(hop.time_us) + "ms", kToneText};
    }
    return row;
}

ToneText ping_status(const model::PingRun& run) {
    switch (run.state) {
        case model::ToolState::Idle:
            return {"10 echoes, one a second", kToneMuted};
        case model::ToolState::Running:
            if (run.address.empty()) {
                return {"Looking up the name…", kToneText};
            }
            // The newest echo is the last one of the table.
            return {"Echo " + std::to_string(run.replies.empty() ? 0 : run.replies.back().sequence) + " of " +
                        std::to_string(model::kPingCount),
                    kToneText};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            return error_status(run.error, run.reason);
        case model::ToolState::Done:
            break;
    }
    if (run.received == 0) {
        if (!run.reason.empty()) {
            return {failed_text(run.reason), kToneError};
        }
        return {"No replies", kToneWarning};
    }
    return {"Done", kToneSuccess};
}

ToneText dns_status(const model::DnsRun& run) {
    switch (run.state) {
        case model::ToolState::Idle:
            return {"IPv4 and IPv6 addresses", kToneMuted};
        case model::ToolState::Running:
            return {"Looking up…", kToneText};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            return error_status(run.error, run.reason);
        case model::ToolState::Done:
            break;
    }
    if (run.error != model::ToolError::None) {
        return error_status(run.error, run.reason);
    }
    const std::size_t count = run.addresses.size();
    return {std::to_string(count) + (count == 1 ? " address" : " addresses"), kToneSuccess};
}

ToneText trace_status(const model::TraceRun& run) {
    const std::string hops = std::to_string(run.hops.size());
    switch (run.state) {
        case model::ToolState::Idle:
            return {"One probe per hop, up to " + std::to_string(model::kTraceMaxHops), kToneMuted};
        case model::ToolState::Running:
            if (run.address.empty()) {
                return {"Looking up the name…", kToneText};
            }
            if (run.end == model::TraceEnd::None) {
                return {"Hop " + hops + "…", kToneText};
            }
            return {"Looking up names…", kToneText};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                return {"Trace not allowed for this user", kToneError};
            }
            if (run.hops.empty()) {
                return error_status(run.error, run.reason);
            }
            return {failed_text(run.reason) + " at hop " + std::to_string(run.hops.size() + 1), kToneError};
        case model::ToolState::Done:
            break;
    }
    switch (run.end) {
        case model::TraceEnd::Reached:
            return {"Reached in " + hops + (run.hops.size() == 1 ? " hop" : " hops"), kToneSuccess};
        case model::TraceEnd::Unreachable:
            return {"Unreachable at hop " + hops, kToneWarning};
        case model::TraceEnd::MaxHops:
        case model::TraceEnd::None:
            break;
    }
    return {"Not reached in " + hops + " hops", kToneWarning};
}

ToneText ping_tag(const model::PingRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            if (run.received > 0) {
                return {time_text(run.avg_us) + " ms", kToneMuted};
            }
            return {"no reply", kToneWarning};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

ToneText trace_tag(const model::TraceRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            if (run.end == model::TraceEnd::Reached) {
                return {std::to_string(run.hops.size()) + (run.hops.size() == 1 ? " hop" : " hops"), kToneMuted};
            }
            return {"not reached", kToneWarning};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

ToneText dns_tag(const model::DnsRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            if (run.error == model::ToolError::None) {
                return {std::to_string(run.addresses.size()) + " found", kToneMuted};
            }
            return {"none", kToneWarning};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

std::vector<std::string> open_port_items(const model::PortRun& run) {
    std::vector<std::string> items;
    items.reserve(run.open.size());
    for (const uint16_t port : run.open) {
        const char* service = model::port_service(port);
        items.push_back(std::to_string(port) + (service[0] != '\0' ? std::string(" ") + service : std::string()));
    }
    return items;
}

std::string ports_counts_text(const model::PortRun& run) {
    const int32_t answered = static_cast<int32_t>(run.open.size()) + run.closed + run.silent;
    if (answered == 0) {
        return {};
    }
    return std::to_string(run.open.size()) + " open · " + std::to_string(run.closed) + " closed · " +
           std::to_string(run.silent) + " no answer";
}

namespace {

const model::PortVerdict* verdict_of(const model::PortRun& run, uint16_t port) {
    for (const model::PortVerdict& verdict : run.verdicts) {
        if (verdict.port == port) {
            return &verdict;
        }
    }
    return nullptr;
}

// "Debian may have fixed these", or "Its maker may have fixed these" for a
// build tag that names no system.
std::string vendor_note(const std::string& vendor, const char* what) {
    return (vendor == "vendor" ? std::string("Its maker") : vendor) + " may have fixed " + what;
}

// A hole's words on the host detail's line.
ToneText hole_words(const model::ServiceFinding& finding) {
    const int32_t tone = !finding.vendor.empty() ? kToneWarning
                         : finding.exploited || finding.score >= 90 ? kToneError
                                                                     : kToneWarning;
    if (finding.exploited) {
        return {"Hole attackers use", tone};
    }
    if (finding.score >= 90) {
        return {"Very serious hole", tone};
    }
    if (finding.score >= 70) {
        return {"Serious hole", tone};
    }
    return {"Known hole", tone};
}

// "8.1"
std::string score_text(int32_t tenths) {
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

std::string without_scheme(const std::string& link) {
    static constexpr char kScheme[] = "https://";
    return link.compare(0, sizeof(kScheme) - 1, kScheme) == 0 ? link.substr(sizeof(kScheme) - 1) : link;
}

} // namespace

ToneText port_tag(const model::PortRun& run, uint16_t port) {
    const model::PortVerdict* verdict = verdict_of(run, port);
    if (verdict == nullptr) {
        return {};
    }
    if (!verdict->vendor.empty()) {
        return {"Check", kToneWarning};
    }
    if (verdict->exploited > 0) {
        return {"Attacked", kToneError};
    }
    if (verdict->holes > 0) {
        return {std::to_string(verdict->holes) + (verdict->holes == 1 ? " hole" : " holes"), kToneWarning};
    }
    if (verdict->outdated) {
        return {"Old", kToneWarning};
    }
    return {};
}

std::vector<PortItem> port_items(const model::PortRun& run) {
    const std::vector<std::string> texts = open_port_items(run);
    std::vector<PortItem> items;
    items.reserve(texts.size());
    for (std::size_t index = 0; index < texts.size() && index < run.open.size(); ++index) {
        items.push_back({run.open[index], texts[index], port_tag(run, run.open[index])});
    }
    return items;
}

std::vector<DetailLine> host_detail_lines(const model::PortRun& run,
                                          const std::string& name,
                                          const std::string& hardware_address) {
    std::vector<DetailLine> lines;
    if (!name.empty()) {
        lines.push_back({"name", {name, kToneText}});
    }
    if (!hardware_address.empty()) {
        lines.push_back({"MAC", {hardware_address, kToneText}});
    }
    const std::vector<std::string> items = open_port_items(run);
    for (std::size_t index = 0; index < run.open.size() && index < items.size(); ++index) {
        const uint16_t port = run.open[index];
        // Everyday words first, then what the port said: in short when a
        // known product was read ("OpenSSH 10.0"), "encrypted" for a TLS
        // port.
        const std::string plain = model::port_plain(port);
        const std::string said = model::port_banner(run, port);
        std::string shown = said;
        if (!said.empty() && model::banner_probe(port) == model::BannerProbe::Tls) {
            shown = "encrypted (" + said + ")";
        }
        const model::ServiceInfo info = model::read_service(said);
        const std::string short_text = model::service_short_text(info);
        if (!short_text.empty()) {
            shown = short_text;
        }
        DetailLine line{items[index], {plain, kToneText}};
        if (!shown.empty()) {
            line.right.text = plain.empty() ? shown : plain + " · " + shown;
            if (!plain.empty()) {
                line.shorter.push_back(shown);
                line.shorter.push_back(plain);
            }
        }
        lines.push_back(line);
        // The whole text when it says more than the short form (more words
        // than products, as "OpenSSH_10.0p2 Debian-7"), and fits.
        std::size_t words = 0;
        for (std::size_t at = 0; at < said.size(); ++at) {
            words += (said[at] != ' ' && (at == 0 || said[at - 1] == ' ')) ? 1 : 0;
        }
        if (!short_text.empty() && words > info.products.size()) {
            DetailLine whole{"", {said, kToneMuted}};
            whole.optional = true;
            lines.push_back(whole);
        }
        if (const model::ServiceRule* rule = model::service_rule(port)) {
            const int32_t tone = rule->level == model::RiskLevel::Risk      ? kToneError
                                 : rule->level == model::RiskLevel::Warning ? kToneWarning
                                                                            : kToneMuted;
            lines.push_back({"", {rule->note, tone}});
        }
        const model::PortVerdict* verdict = verdict_of(run, port);
        if (verdict == nullptr) {
            continue;
        }
        if (!verdict->vendor.empty()) {
            lines.push_back({"", {vendor_note(verdict->vendor, "these"), kToneMuted}});
        }
        int32_t kept = 0;
        for (std::size_t place = 0; place < run.findings.size(); ++place) {
            const model::ServiceFinding& finding = run.findings[place];
            if (finding.port != port) {
                continue;
            }
            DetailLine row;
            row.finding = static_cast<int32_t>(place);
            if (finding.kind == model::FindingKind::Hole) {
                row.right = hole_words(finding);
                row.aside = finding.cve;
                ++kept;
            } else {
                row.right = {"Old: no more security fixes", kToneWarning};
                row.shorter.push_back("Old version");
            }
            if (finding.kind == model::FindingKind::Outdated && verdict->holes > kept) {
                const int32_t more = verdict->holes - kept;
                lines.push_back({"", {"+" + std::to_string(more) + (more == 1 ? " more hole" : " more holes"),
                                      kToneMuted}});
                kept = verdict->holes;
            }
            lines.push_back(row);
        }
        if (verdict->holes > kept) {
            const int32_t more = verdict->holes - kept;
            lines.push_back(
                {"", {"+" + std::to_string(more) + (more == 1 ? " more hole" : " more holes"), kToneMuted}});
        }
    }
    if (run.open.empty()) {
        lines.push_back({"", {"No open ports", kToneMuted}});
    }
    if (run.state == model::ToolState::Done) {
        if (run.list_date.empty()) {
            lines.push_back({"", {"No holes list: versions not checked", kToneMuted}, {"Versions not checked"}});
        } else {
            lines.push_back({"", {"Holes list from " + run.list_date, kToneMuted}});
        }
    }
    return lines;
}

FindingView finding_view(const model::PortRun& run, std::size_t index) {
    FindingView view;
    if (run.findings.empty()) {
        return view;
    }
    index = std::min(index, run.findings.size() - 1);
    const model::ServiceFinding& finding = run.findings[index];
    view.place = std::to_string(index + 1) + " of " + std::to_string(run.findings.size()) + " · port " +
                 std::to_string(finding.port);
    model::ServiceInfo info;
    info.products.push_back({finding.product, finding.name, finding.version});
    view.product = model::service_short_text(info);
    view.link = finding.link;
    view.link_text = without_scheme(finding.link);
    if (finding.kind == model::FindingKind::Outdated) {
        view.severity = {"Old version", kToneWarning};
        view.sentences.push_back({"No more security fixes", kToneWarning});
        view.sentences.push_back(finding.vendor.empty() ? ToneText{"Fix: update or replace it", kToneText}
                                                        : ToneText{vendor_note(finding.vendor, "it"), kToneMuted});
        view.official = "Read more on this page";
        return view;
    }
    const ToneText words = hole_words(finding);
    std::string severity = finding.score >= 90 ? "Very serious hole" : finding.score >= 70 ? "Serious hole" : "Known hole";
    if (finding.score >= 0) {
        severity += ", " + score_text(finding.score) + " of 10";
    }
    view.severity = {severity, words.tone};
    if (finding.exploited) {
        // A vendor's build may already be fixed, so it is a warning there.
        view.sentences.push_back({"Attackers are using it", finding.vendor.empty() ? kToneError : kToneWarning});
    }
    if (!finding.vendor.empty()) {
        view.sentences.push_back({vendor_note(finding.vendor, "it"), kToneMuted});
    }
    if (view.sentences.size() < 2) {
        view.sentences.push_back({finding.vendor.empty() ? "Fix: update its software" : "Fix: install its updates",
                                  kToneText});
    }
    view.official = "Official number " + finding.cve;
    return view;
}

ToneText ports_status(const model::PortRun& run) {
    const std::string progress = std::to_string(run.asked) + " of " + std::to_string(run.total);
    switch (run.state) {
        case model::ToolState::Idle:
            return {std::to_string(model::common_ports().size()) + " common ports of one device", kToneMuted};
        case model::ToolState::Running:
            return {run.total > 0 ? "Scanning " + progress : std::string("Scanning"), kToneAccent};
        case model::ToolState::Done:
            return {"Scanned " + std::to_string(run.asked) + ", " + std::to_string(run.open.size()) + " open",
                    run.open.empty() ? kToneMuted : kToneSuccess};
        case model::ToolState::Stopped:
            return {"Scan stopped at " + progress, kToneMuted};
        case model::ToolState::Failed:
            if (run.error == model::ToolError::BadTarget) {
                return {"Not a private address", kToneError};
            }
            if (!run.reason.empty()) {
                std::string text = run.reason;
                if (text[0] >= 'a' && text[0] <= 'z') {
                    text[0] = static_cast<char>(text[0] - 'a' + 'A');
                }
                return {text, kToneError};
            }
            return error_status(run.error, run.reason);
    }
    return {};
}

namespace {

// Whole megabytes: "50 MB".
std::string megabytes_text(uint64_t bytes) {
    return std::to_string(bytes / 1000000) + " MB";
}

} // namespace

std::string speed_cap_text() {
    return "up to " + megabytes_text(model::kSpeedPhaseBytesMax);
}

ToneText speed_status(const model::SpeedRun& run) {
    const std::string seconds = std::to_string(run.phase_ms / 1000) + " of " +
                                std::to_string(model::kSpeedPhaseMs / 1000) + " s";
    switch (run.state) {
        case model::ToolState::Idle:
            return {"Uses up to " + megabytes_text(2 * model::kSpeedPhaseBytesMax) + " of data", kToneMuted};
        case model::ToolState::Running:
            switch (run.phase) {
                case model::SpeedPhase::Connect:
                    return {"Connecting", kToneAccent};
                case model::SpeedPhase::Download:
                    return {"Download " + seconds, kToneAccent};
                case model::SpeedPhase::Upload:
                    return {"Upload " + seconds, kToneAccent};
            }
            break;
        case model::ToolState::Done:
            return {"Done", kToneSuccess};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            if (run.error == model::ToolError::BadTarget) {
                return {"Not a usable server address", kToneError};
            }
            if (!run.reason.empty()) {
                std::string text = run.reason;
                if (text[0] >= 'a' && text[0] <= 'z') {
                    text[0] = static_cast<char>(text[0] - 'a' + 'A');
                }
                return {text, kToneError};
            }
            return error_status(run.error, run.reason);
    }
    return {};
}

std::string bytes_text(uint64_t bytes) {
    constexpr uint64_t kKilo = 1000;
    constexpr uint64_t kMega = 1000 * kKilo;
    constexpr uint64_t kGiga = 1000 * kMega;
    char text[24];
    if (bytes < kMega) {
        std::snprintf(text, sizeof(text), "%u kB", static_cast<unsigned>((bytes + kKilo / 2) / kKilo));
    } else if (bytes < 100 * kMega) {
        const uint64_t tenths = (bytes + kMega / 20) / (kMega / 10);
        std::snprintf(text, sizeof(text), "%u.%u MB", static_cast<unsigned>(tenths / 10),
                      static_cast<unsigned>(tenths % 10));
    } else if (bytes < kGiga) {
        std::snprintf(text, sizeof(text), "%u MB", static_cast<unsigned>((bytes + kMega / 2) / kMega));
    } else {
        const uint64_t tenths = (bytes + kGiga / 20) / (kGiga / 10);
        std::snprintf(text, sizeof(text), "%llu.%u GB", static_cast<unsigned long long>(tenths / 10),
                      static_cast<unsigned>(tenths % 10));
    }
    return text;
}

ToneText speed_tag(const model::SpeedRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            return {home::rate_text(run.down_kbps) + " / " + home::rate_text(run.up_kbps), kToneMuted};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

ToneText ports_tag(const model::PortRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            return {std::to_string(run.open.size()) + " open", kToneMuted};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

} // namespace screen::tools
