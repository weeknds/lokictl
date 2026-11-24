/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dashboard_findings.h"

#include "health_check.h"
#include "service_rules.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace model {
namespace {

// How a device or a thing is named in a sentence ("Smart TV has ...") and
// after a verb ("Update Smart TV", "Fix the router").
struct Subject {
    std::string key;     // one per subject: a device's id, "own", "wifi", "cable", "network" or an address
    std::string device;  // the device's id when it is one of the list's devices
    std::string label;   // "Smart TV", "Router", "The Cardputer", "Your Wi-Fi"
    std::string object;  // "Smart TV", "the router", "the Cardputer", "the Wi-Fi"
};

// One device while the list is put together.
struct Entry {
    DashDevice device;
    std::string hardware;
    std::string name;
    std::string kind;
    std::vector<uint16_t> open;
    int32_t seen_s{-1};  // seconds since it was last seen; -1 not known
    int32_t holes{0};
};

// One finding on its way to a thing.
struct Candidate {
    FindingTier tier{FindingTier::Know};
    Subject subject;
    std::string icon;
    std::string title;
    std::string hint;
    std::string action;
    std::string fix;
    std::string time;
};

std::string upper_first(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

std::string lower_first(std::string text) {
    if (text.size() > 1 && text[0] >= 'A' && text[0] <= 'Z' && text[1] >= 'a' && text[1] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'A' + 'a');
    }
    return text;
}

bool starts_with(const std::string& text, const char* start) {
    return text.rfind(start, 0) == 0;
}

// A rule's fix as a hint: lowercase, no full stop.
std::string hint_text(std::string fix) {
    while (!fix.empty() && fix.back() == '.') {
        fix.pop_back();
    }
    return lower_first(std::move(fix));
}

FindingTier tier_of(RiskLevel level) {
    switch (level) {
        case RiskLevel::Risk:
            return FindingTier::Fix;
        case RiskLevel::Warning:
            return FindingTier::Look;
        case RiskLevel::Info:
            return FindingTier::Know;
    }
    return FindingTier::Know;
}

// "a.b.c.d" as a number for sorting; anything else sorts last.
uint64_t address_order(const std::string& text) {
    if (!is_ipv4_text(text)) {
        return std::numeric_limits<uint64_t>::max();
    }
    uint64_t number = 0;
    uint64_t part = 0;
    for (const char character : text) {
        if (character == '.') {
            number = (number << 8) | part;
            part = 0;
        } else {
            part = part * 10 + static_cast<uint64_t>(character - '0');
        }
    }
    return (number << 8) | part;
}

// "21:44" as minutes since midnight; -1 when it is not that.
int32_t clock_minutes(const std::string& text) {
    if (text.size() != 5 || text[2] != ':') {
        return -1;
    }
    for (const std::size_t index : {0u, 1u, 3u, 4u}) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
    }
    const int32_t hours = (text[0] - '0') * 10 + (text[1] - '0');
    const int32_t minutes = (text[3] - '0') * 10 + (text[4] - '0');
    if (hours > 23 || minutes > 59) {
        return -1;
    }
    return hours * 60 + minutes;
}

std::string seen_text(int32_t seconds) {
    if (seconds < 0) {
        return "earlier";
    }
    if (seconds < 60) {
        return "now";
    }
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + " min";
    }
    return std::to_string(seconds / 3600) + " h";
}

// The number at the start of a title ("2 known holes"); 1 when there is
// none ("Hole attackers use").
int32_t leading_count(const std::string& title) {
    int32_t count = 0;
    std::size_t index = 0;
    while (index < title.size() && index < 4 && title[index] >= '0' && title[index] <= '9') {
        count = count * 10 + (title[index] - '0');
        ++index;
    }
    return index == 0 ? 1 : count;
}

bool wifi_issue(HealthIssue issue) {
    return issue == HealthIssue::OpenWifi || issue == HealthIssue::WepWifi || issue == HealthIssue::WpaWifi ||
           issue == HealthIssue::LookAlike;
}

// Open ports in a few plain words: "remote login, web page, casting".
std::string services_text(const Entry& entry) {
    if (!entry.device.checked) {
        return "not checked";
    }
    std::vector<std::string> words;
    for (const uint16_t port : entry.open) {
        std::string word = port_plain(port);
        if (word.empty()) {
            word = "port " + std::to_string(port);
        }
        if (std::find(words.begin(), words.end(), word) == words.end()) {
            words.push_back(std::move(word));
        }
    }
    if (words.empty()) {
        return "none";
    }
    std::string text;
    const std::size_t shown = std::min<std::size_t>(words.size(), 3);
    for (std::size_t index = 0; index < shown; ++index) {
        text += (index == 0 ? "" : ", ") + words[index];
    }
    if (words.size() > shown) {
        text += " +" + std::to_string(words.size() - shown);
    }
    return text;
}

class DeviceList {
public:
    explicit DeviceList(std::string own_address) : own_(std::move(own_address)) {}

    // The entry of `address` (else `hardware`), made when there is none;
    // nullptr for the Cardputer itself and for neither.
    Entry* get(const std::string& address, const std::string& hardware) {
        if (!address.empty() && address == own_) {
            return nullptr;
        }
        if (Entry* found = find(address, hardware)) {
            if (found->device.address.empty() && !address.empty()) {
                found->device.address = address;
            }
            if (found->hardware.empty()) {
                found->hardware = hardware;
            }
            return found;
        }
        if (address.empty() && hardware.empty()) {
            return nullptr;
        }
        Entry entry;
        entry.device.address = address;
        entry.hardware = hardware;
        entries_.push_back(std::move(entry));
        return &entries_.back();
    }

    Entry* find(const std::string& address, const std::string& hardware) {
        for (Entry& entry : entries_) {
            if (!address.empty() && entry.device.address == address) {
                return &entry;
            }
        }
        for (Entry& entry : entries_) {
            if (!hardware.empty() && entry.hardware == hardware) {
                return &entry;
            }
        }
        return nullptr;
    }

    std::vector<Entry>& entries() { return entries_; }
    const std::string& own() const { return own_; }

private:
    std::string own_;
    std::vector<Entry> entries_;
};

void note_seen(Entry& entry, int32_t seconds) {
    if (seconds >= 0 && (entry.seen_s < 0 || seconds < entry.seen_s)) {
        entry.seen_s = seconds;
    }
}

void note_names(Entry& entry, const std::string& name, const std::string& kind) {
    if (entry.name.empty()) {
        entry.name = name;
    }
    if (entry.kind.empty()) {
        entry.kind = kind;
    }
}

// The subject of something about `address` (empty: not about one device).
Subject device_subject(DeviceList& list, const std::string& address, const std::string& hardware) {
    Subject subject;
    if (!address.empty() && address == list.own()) {
        subject.key = "own";
        subject.label = "The Cardputer";
        subject.object = "the Cardputer";
        return subject;
    }
    if (Entry* entry = list.find(address, hardware)) {
        subject.key = entry->device.id;
        subject.device = entry->device.id;
        subject.label = entry->device.name;  // a host name stays as the device gave it
        subject.object = entry->device.router ? std::string("the router") : entry->device.name;
        return subject;
    }
    subject.key = address.empty() ? hardware : address;
    subject.label = address.empty() ? std::string("A device") : address;
    subject.object = subject.label;
    return subject;
}

// `holes`: the device's known holes over all its ports (Entry::holes).
Candidate health_candidate(const HealthFinding& finding, DeviceList& list, const std::string& time, int32_t holes) {
    Candidate candidate;
    candidate.tier = tier_of(finding.level);
    candidate.time = time;
    candidate.hint = hint_text(health_fix(finding));
    if (finding.address.empty()) {
        candidate.subject.key = wifi_issue(finding.issue) ? "wifi" : "cable";
        candidate.subject.label = wifi_issue(finding.issue) ? "Your Wi-Fi" : "The cable link";
        candidate.subject.object = wifi_issue(finding.issue) ? "the Wi-Fi" : "the cable";
    } else {
        candidate.subject = device_subject(list, finding.address, std::string());
    }
    const std::string& label = candidate.subject.label;
    const std::string& object = candidate.subject.object;
    candidate.fix = "Fix " + object;
    switch (finding.issue) {
        case HealthIssue::Service:
            candidate.icon = "screen";
            if (finding.port == 9100) {
                candidate.title = label + " lets anyone print";
            } else if (starts_with(finding.title, "FTP") || starts_with(finding.title, "Telnet") ||
                       starts_with(finding.title, "Windows")) {
                candidate.title = label + " has " + finding.title;
            } else {
                candidate.title = label + " has " + lower_first(finding.title);
            }
            break;
        case HealthIssue::RouterPage:
            candidate.icon = "lock";
            candidate.title = "Router page isn't encrypted";
            break;
        case HealthIssue::Upnp:
            candidate.icon = "lock";
            candidate.title = label + " lets apps open ports";
            break;
        case HealthIssue::Holes:
            candidate.icon = "shield";
            if (finding.title.empty() || finding.title[0] < '0' || finding.title[0] > '9') {
                candidate.title = label + " has a hole attackers use";
            } else {
                const int32_t count = std::max(holes, leading_count(finding.title));
                candidate.title = label + " has " + std::to_string(count) + (count == 1 ? " security hole" : " security holes");
            }
            candidate.action = "Update " + object;
            break;
        case HealthIssue::Outdated:
            candidate.icon = "shield";
            candidate.title = label + " runs old software";
            candidate.action = "Update " + object;
            break;
        case HealthIssue::Check:
            candidate.icon = "shield";
            candidate.title = label + " may need updates";
            candidate.action = "Update " + object;
            break;
        case HealthIssue::ManyPorts:
            candidate.icon = "screen";
            candidate.title = label + " has many open ports";
            break;
        case HealthIssue::OpenWifi:
            candidate.icon = "wifi";
            candidate.title = "Your Wi-Fi has no password";
            candidate.action = "Lock your Wi-Fi";
            break;
        case HealthIssue::WepWifi:
            candidate.icon = "wifi";
            candidate.title = "Your Wi-Fi lock is weak";
            candidate.action = "Lock your Wi-Fi";
            break;
        case HealthIssue::WpaWifi:
            candidate.icon = "wifi";
            candidate.title = "Your Wi-Fi uses an old lock";
            candidate.action = "Lock your Wi-Fi";
            break;
        case HealthIssue::LookAlike:
            candidate.icon = "wifi";
            candidate.title = "A look-alike Wi-Fi is nearby";
            break;
        case HealthIssue::SlowCable:
            candidate.icon = "cable";
            candidate.title = "The cable link is slow";
            break;
    }
    if (candidate.action.empty()) {
        candidate.action = candidate.title;
    }
    return candidate;
}

FindingTier watch_tier(WatchAlertKind kind) {
    switch (kind) {
        case WatchAlertKind::FakeRouter:
            return FindingTier::Fix;
        case WatchAlertKind::NewDevice:
            return FindingTier::Know;
        case WatchAlertKind::SameAddress:
        case WatchAlertKind::SecondDhcp:
        case WatchAlertKind::Ipv6Router:
        case WatchAlertKind::Scan:
        case WatchAlertKind::ArpFlood:
        case WatchAlertKind::Trap:
            return FindingTier::Look;
    }
    return FindingTier::Look;
}

// What a thing says about an alert, and what the Watch feed says.
Candidate watch_candidate(const WatchAlert& alert, DeviceList& list) {
    Candidate candidate;
    candidate.tier = watch_tier(alert.kind);
    candidate.icon = "eye";
    candidate.time = alert.time;
    if (alert.kind == WatchAlertKind::ArpFlood) {
        candidate.subject.key = "network";
        candidate.subject.label = "Your network";
        candidate.subject.object = "your network";
    } else {
        candidate.subject = device_subject(list, alert.address, alert.hardware);
    }
    switch (alert.kind) {
        case WatchAlertKind::NewDevice:
            candidate.title = "New device: " + candidate.subject.label;
            break;
        case WatchAlertKind::FakeRouter:
            candidate.title = "Another device claims to be the router";
            candidate.hint = "someone may be listening in";
            break;
        case WatchAlertKind::SameAddress:
            candidate.title = "Two devices claim " + alert.address;
            break;
        case WatchAlertKind::SecondDhcp:
            candidate.title = "A second device hands out addresses";
            break;
        case WatchAlertKind::Ipv6Router:
            candidate.title = "An unknown IPv6 router spoke up";
            break;
        case WatchAlertKind::Scan:
            candidate.title = "Someone scanned the Cardputer";
            break;
        case WatchAlertKind::ArpFlood:
            candidate.title = "A flood of address requests";
            break;
        case WatchAlertKind::Trap:
            candidate.title = "Someone tried the trap";
            break;
    }
    candidate.action = candidate.title;
    candidate.fix = "Look at " + candidate.subject.object;
    return candidate;
}

std::string watch_line(const WatchAlert& alert, DeviceList& list) {
    switch (alert.kind) {
        case WatchAlertKind::NewDevice:
            return "New device: " + device_subject(list, alert.address, alert.hardware).label;
        case WatchAlertKind::FakeRouter:
            return "Another device claims to be the router";
        case WatchAlertKind::SameAddress:
            return "Two devices claim " + alert.address;
        case WatchAlertKind::SecondDhcp:
            return "A second device hands out addresses";
        case WatchAlertKind::Ipv6Router:
            return "An unknown IPv6 router spoke up";
        case WatchAlertKind::Scan:
            return alert.address.empty() ? std::string("Someone scanned the Cardputer")
                                         : "Someone scanned the Cardputer from " + alert.address;
        case WatchAlertKind::ArpFlood:
            return "A flood of " + std::to_string(alert.count) + " address requests a second";
        case WatchAlertKind::Trap: {
            const std::string plain = alert.count > 0 ? port_plain(static_cast<uint16_t>(alert.count)) : "";
            return plain.empty() ? std::string("The trap caught a visit")
                                 : "The trap caught a visit to its " + plain;
        }
    }
    return "Alert";
}

} // namespace

const char* finding_tier_name(FindingTier tier) {
    switch (tier) {
        case FindingTier::Fine:
            return "ok";
        case FindingTier::Know:
            return "know";
        case FindingTier::Look:
            return "look";
        case FindingTier::Fix:
            return "fix";
    }
    return "ok";
}

DashOverview dashboard_overview(const HomeData& home, const ToolsData& tools, const WatchData& watch) {
    DashOverview overview;
    const HealthRun& health = tools.health;
    overview.checking = health.state == ToolState::Running;
    overview.checked = health.state == ToolState::Done;
    overview.checked_at = health.finished;
    const bool wifi_uplink = home.uplink_kind == LinkKind::Wifi;
    const bool hotspot = home.ap_active && is_hotspot_mode(home.mode);

    // The devices: the health check's, then Discover's, then the hotspot's.
    DeviceList list(home.uplink_address);
    const int32_t checked_minutes = clock_minutes(health.finished);
    for (const HealthDevice& scanned : health.devices) {
        if (scanned.own) {
            continue;
        }
        Entry* entry = list.get(scanned.address, std::string());
        if (entry == nullptr) {
            continue;
        }
        note_names(*entry, scanned.name, scanned.kind);
        entry->device.router = entry->device.router || scanned.router;
        entry->device.checked = true;
        entry->open = scanned.open;
        if (overview.checking) {
            note_seen(*entry, 0);
        } else if (checked_minutes >= 0) {
            // A clock read a moment before the check ended can be a minute behind.
            int32_t minutes = ((home.clock_minutes - checked_minutes) % 1440 + 1440) % 1440;
            if (minutes > 1440 - 5) {
                minutes = 0;
            }
            note_seen(*entry, minutes * 60);
        }
    }
    const bool discovering = tools.listen.state == ToolState::Running || tools.probe.state == ToolState::Running;
    for (const DiscoverDevice& heard : discover_devices(tools.listen, tools.probe)) {
        if (!heard.device.own_port.empty()) {
            continue;
        }
        Entry* entry = list.get(heard.device.address, heard.device.hardware_address);
        if (entry == nullptr) {
            continue;
        }
        note_names(*entry, heard.device.name, heard.device.kind);
        if (discovering) {
            note_seen(*entry, heard.age_s);
        }
    }
    if (tools.ports.state == ToolState::Done) {
        if (Entry* entry = list.get(tools.ports.target, std::string())) {
            entry->device.checked = true;
            for (const uint16_t port : tools.ports.open) {
                if (std::find(entry->open.begin(), entry->open.end(), port) == entry->open.end()) {
                    entry->open.push_back(port);
                }
            }
            std::sort(entry->open.begin(), entry->open.end());
        }
    }
    if (hotspot) {
        for (const HomeClient& client : home.clients) {
            Entry* entry = list.get(client.address, client.hardware_address);
            if (entry == nullptr) {
                continue;
            }
            note_names(*entry, client.name, std::string());
            note_seen(*entry, 0);
            entry->device.wireless = true;
            entry->device.is_new = entry->device.is_new || client.is_new;
        }
    }
    for (Entry& entry : list.entries()) {
        DashDevice& device = entry.device;
        device.id = device.address.empty() ? entry.hardware : device.address;
        if (!home.gateway_address.empty() && device.address == home.gateway_address) {
            device.router = true;
        }
        device.wireless = device.wireless || wifi_uplink;
        if (!entry.name.empty()) {
            device.name = entry.name;
        } else if (device.router) {
            device.name = "Router";
        } else if (!entry.kind.empty()) {
            device.name = upper_first(entry.kind);
        } else {
            device.name = device.id;
        }
        device.seen = seen_text(entry.seen_s);
    }

    // Every finding, as a candidate thing: the health check's (the worst
    // first), the Ports page's that the check did not find, then Watch's
    // alerts, the newest first.
    std::vector<Candidate> candidates;
    std::vector<HealthFinding> findings = health.findings;
    if (tools.ports.state == ToolState::Done && is_ipv4_text(tools.ports.target)) {
        HealthHost host;
        host.address = tools.ports.target;
        host.router = tools.ports.target == home.gateway_address;
        if (Entry* entry = list.find(host.address, std::string())) {
            host.name = entry->name;
            host.kind = entry->kind;
        }
        std::vector<HealthFinding> extra = host_findings(host, tools.ports, std::numeric_limits<int32_t>::max());
        for (HealthFinding& finding : extra) {
            const bool known = std::any_of(findings.begin(), findings.end(), [&finding](const HealthFinding& other) {
                return other.address == finding.address && other.issue == finding.issue && other.port == finding.port;
            });
            if (!known) {
                findings.push_back(std::move(finding));
            }
        }
        sort_findings(findings);
    }
    for (const HealthFinding& finding : findings) {
        if (finding.issue == HealthIssue::Holes) {
            if (Entry* entry = list.find(finding.address, std::string())) {
                entry->holes += leading_count(finding.title);
            }
        }
    }
    const std::string health_time = overview.checked ? health.finished : std::string();
    for (const HealthFinding& finding : findings) {
        const Entry* entry = finding.address.empty() ? nullptr : list.find(finding.address, std::string());
        candidates.push_back(health_candidate(finding, list, health_time, entry ? entry->holes : 0));
    }
    std::vector<std::pair<WatchAlertKind, std::string>> alerted;
    for (const WatchAlert& alert : watch.run.alerts) {
        Candidate candidate = watch_candidate(alert, list);
        const std::pair<WatchAlertKind, std::string> key{alert.kind, candidate.subject.key};
        if (std::find(alerted.begin(), alerted.end(), key) != alerted.end()) {
            continue;
        }
        alerted.push_back(key);
        if (alert.kind == WatchAlertKind::NewDevice) {
            if (Entry* entry = list.find(alert.address, alert.hardware)) {
                entry->device.is_new = true;
            }
        }
        candidates.push_back(std::move(candidate));
    }
    for (const WatchAlert& alert : watch.run.alerts) {
        if (overview.watch_lines.size() >= 5) {
            break;
        }
        overview.watch_lines.push_back(watch_line(alert, list));
        overview.watch_times.push_back(alert.time);
    }

    // One thing per subject, at its worst candidate (the first of them).
    std::vector<std::string> subjects;
    for (const Candidate& candidate : candidates) {
        if (std::find(subjects.begin(), subjects.end(), candidate.subject.key) != subjects.end()) {
            continue;
        }
        subjects.push_back(candidate.subject.key);
        const Candidate* worst = &candidate;
        for (const Candidate& other : candidates) {
            if (other.subject.key == candidate.subject.key && other.tier > worst->tier) {
                worst = &other;
            }
        }
        DashThing thing;
        thing.tier = worst->tier;
        thing.icon = worst->icon;
        thing.title = worst->title;
        thing.hint = worst->hint;
        thing.action = worst->action;
        thing.fix = worst->fix;
        thing.device = worst->subject.device;
        thing.time = worst->time;
        overview.things.push_back(std::move(thing));
    }
    std::stable_sort(overview.things.begin(), overview.things.end(),
                     [](const DashThing& left, const DashThing& right) { return left.tier > right.tier; });

    // Each device's tier and tag, then the counts.
    for (Entry& entry : list.entries()) {
        DashDevice& device = entry.device;
        for (const DashThing& thing : overview.things) {
            if (thing.device == device.id && thing.tier > device.tier) {
                device.tier = thing.tier;
            }
        }
        device.services = services_text(entry);
        if (device.tier == FindingTier::Fix) {
            device.tag = entry.holes > 0 ? std::to_string(entry.holes) + (entry.holes == 1 ? " hole" : " holes") : "Fix";
        } else if (device.tier == FindingTier::Look) {
            device.tag = "Check";
        } else if (device.is_new) {
            device.tag = "New";
        } else {
            device.tag = device.checked ? "OK" : "Not checked";
        }
    }
    DashCounts& counts = overview.counts;
    for (const DashThing& thing : overview.things) {
        if (thing.tier == FindingTier::Fix) {
            ++counts.fix;
        } else if (thing.tier == FindingTier::Look) {
            ++counts.look;
        } else if (thing.tier == FindingTier::Know) {
            ++counts.know;
        }
    }
    for (const Entry& entry : list.entries()) {
        ++counts.devices;
        if (entry.device.tier == FindingTier::Fix) {
            ++counts.device_fix;
        } else if (entry.device.tier == FindingTier::Look) {
            ++counts.device_look;
        } else if (entry.device.checked) {
            ++counts.fine;
        } else {
            ++counts.unchecked;
        }
    }

    std::vector<Entry>& entries = list.entries();
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.device.tier != right.device.tier) {
            return left.device.tier > right.device.tier;
        }
        if (left.device.is_new != right.device.is_new) {
            return left.device.is_new;
        }
        const uint64_t left_order = address_order(left.device.address);
        const uint64_t right_order = address_order(right.device.address);
        if (left_order != right_order) {
            return left_order < right_order;
        }
        return left.device.id < right.device.id;
    });
    for (Entry& entry : entries) {
        if (overview.devices.size() >= kDashMaxDevices) {
            break;
        }
        overview.devices.push_back(std::move(entry.device));
    }
    if (overview.things.size() > kDashMaxThings) {
        overview.things.resize(kDashMaxThings);
    }
    return overview;
}

namespace {

HealthDevice sample_device(const char* address, const char* name, const char* kind, std::vector<uint16_t> open) {
    HealthDevice device;
    device.address = address;
    device.name = name;
    device.kind = kind;
    device.open = std::move(open);
    return device;
}

HealthFinding sample_finding(HealthIssue issue,
                             RiskLevel level,
                             const char* title,
                             const char* address,
                             const char* where,
                             const char* fact,
                             uint16_t port,
                             int32_t order) {
    HealthFinding finding;
    finding.issue = issue;
    finding.level = level;
    finding.title = title;
    finding.who = address;
    finding.where = where;
    finding.fact = fact;
    finding.port = port;
    finding.order = order;
    finding.address = address;
    return finding;
}

WatchAlert sample_alert(WatchAlertKind kind, const char* time, const char* address, const char* hardware, int32_t count) {
    WatchAlert alert;
    alert.kind = kind;
    alert.time = time;
    alert.address = address;
    alert.hardware = hardware;
    alert.count = count;
    return alert;
}

} // namespace

DashSample sample_dashboard() {
    DashSample sample;
    // A finished health check of the sample's cable network (10.0.0.0/24,
    // the Cardputer at 10.0.0.12) and, made up, the hotspot's three
    // devices too, so every device has been looked at.
    HealthRun& health = sample.tools.health;
    health.state = ToolState::Done;
    health.interface_name = "eth0";
    health.network = "10.0.0.0/24";
    health.step = HealthStep::Ports;
    health.total = 253;
    health.asked = 253;
    health.elapsed_s = 94;
    health.finished = "07:44";
    HealthDevice router = sample_device("10.0.0.1", "", "router", {53, 80, 443});
    router.router = true;
    HealthDevice own = sample_device("10.0.0.12", "", "", {22});
    own.own = true;
    health.devices = {
        router,
        own,
        sample_device("10.0.0.20", "Desktop", "", {}),
        sample_device("10.0.0.31", "Speaker", "", {7000}),
        sample_device("10.0.0.43", "Smart TV", "", {22, 8009, 8080}),
        sample_device("10.0.0.52", "Doorbell", "", {443}),
        sample_device("10.0.0.57", "Thermostat", "", {}),
        sample_device("10.0.0.60", "", "printer", {80, 631, 9100}),
        sample_device("10.0.0.66", "Camera", "", {554}),
        sample_device("10.0.0.75", "NAS", "", {445, 3389}),
        sample_device("10.42.0.11", "phone", "", {62078}),
        sample_device("10.42.0.14", "laptop", "", {}),
        sample_device("10.42.0.23", "tv", "", {8009}),
    };
    health.hosts = static_cast<int32_t>(health.devices.size());
    health.scanned = health.hosts;
    health.findings = {
        sample_finding(HealthIssue::Holes, RiskLevel::Risk, "2 known holes", "10.0.0.43", "Smart TV 10.0.0.43",
                       "Port 22, Dropbear 2015.67, worst 9.8", 22, 4),
        sample_finding(HealthIssue::RouterPage, RiskLevel::Warning, "Plain router page", "10.0.0.1",
                       "router 10.0.0.1", "Port 80, web page, lighttpd", 80, 0),
        sample_finding(HealthIssue::Service, RiskLevel::Warning, "Remote desktop open", "10.0.0.75", "NAS 10.0.0.75",
                       "Port 3389, remote desktop", 3389, 9),
        sample_finding(HealthIssue::Service, RiskLevel::Info, "File sharing open", "10.0.0.75", "NAS 10.0.0.75",
                       "Port 445, file sharing", 445, 9),
        sample_finding(HealthIssue::Check, RiskLevel::Info, "Check for updates", "10.0.0.12",
                       "this device 10.0.0.12", "Port 22, OpenSSH 10.0p2, Debian build", 22, 1),
        sample_finding(HealthIssue::Service, RiskLevel::Info, "Anyone can print", "10.0.0.60", "printer 10.0.0.60",
                       "Port 9100, printing", 9100, 7),
    };
    health.findings[0].who = "10.0.0.43";
    health.findings[1].who = "router";
    health.findings[4].who = "this device";
    sort_findings(health.findings);

    // Watch is listening; the trap is set with its six fake services.
    WatchRun& run = sample.watch.run;
    run.state = ToolState::Running;
    run.interface_name = "eth0";
    run.elapsed_s = 3120;
    run.frames = 5211;
    run.alerts = {
        sample_alert(WatchAlertKind::Scan, "07:42", "10.0.0.75", "02:5e:11:7a:d0:75", 20),
        sample_alert(WatchAlertKind::NewDevice, "07:31", "10.0.0.60", "02:5e:11:7a:d0:60", 0),
        sample_alert(WatchAlertKind::Trap, "06:58", "10.0.0.75", "", 80),
    };
    run.raised = static_cast<int32_t>(run.alerts.size());
    TrapRun& trap = sample.trap;
    trap.state = ToolState::Running;
    trap.elapsed_s = 3120;
    for (const TrapService& service : trap_services()) {
        TrapPort port;
        port.port = service.port;
        port.state = TrapPortState::Open;
        trap.ports.push_back(port);
    }
    trap.raised = 1;
    return sample;
}

} // namespace model
