/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dashboard_status.h"

#include "health_check.h"
#include "report.h"

#include <cstdint>

namespace model {
namespace {

const char* mode_id(NetMode mode) {
    switch (mode) {
        case NetMode::Client: return "client";
        case NetMode::Router: return "router";
        case NetMode::Share: return "share";
        case NetMode::Vpn: return "vpn";
    }
    return "client";
}

const char* state_word(ToolState state) {
    switch (state) {
        case ToolState::Idle: return "idle";
        case ToolState::Running: return "running";
        case ToolState::Done: return "done";
        case ToolState::Stopped: return "stopped";
        case ToolState::Failed: return "failed";
    }
    return "idle";
}

const char* flag(bool value) {
    return value ? "true" : "false";
}

// A number, or null when it is negative (not known).
std::string number_or_null(int32_t value) {
    return value < 0 ? std::string("null") : std::to_string(value);
}

// "name":value pairs in one object, with the commas between them.
class Fields {
public:
    explicit Fields(std::string& out) : out_(out) { out_ += '{'; }
    ~Fields() { out_ += '}'; }
    Fields(const Fields&) = delete;
    Fields& operator=(const Fields&) = delete;

    std::string& key(const char* name) {
        out_ += first_ ? "\"" : ",\"";
        first_ = false;
        out_ += name;
        out_ += "\":";
        return out_;
    }
    void text(const char* name, const std::string& value) { key(name) += json_text(value); }
    void word(const char* name, const char* value) { key(name).append("\"").append(value).append("\""); }
    void number(const char* name, int32_t value) { key(name) += std::to_string(value); }
    void boolean(const char* name, bool value) { key(name) += flag(value); }

private:
    std::string& out_;
    bool first_{true};
};

// A device's page: its services, holes and issues.
void write_page(Fields& item, std::string& out, const DashDevice& device) {
    item.key("ports") += '[';
    for (std::size_t index = 0; index < device.ports.size(); ++index) {
        const DashPort& port = device.ports[index];
        if (index > 0) {
            out += ',';
        }
        Fields entry(out);
        entry.number("port", port.port);
        entry.text("name", port.name);
        entry.text("version", port.version);
        entry.word("tier", finding_tier_name(port.tier));
        entry.text("tag", port.tag);
    }
    out += ']';
    item.number("more_ports", device.more_ports);
    item.key("holes") += '[';
    for (std::size_t index = 0; index < device.holes.size(); ++index) {
        const DashHole& hole = device.holes[index];
        if (index > 0) {
            out += ',';
        }
        Fields entry(out);
        entry.number("port", hole.port);
        entry.text("cve", hole.cve);
        entry.key("score") += number_or_null(hole.score);
        entry.boolean("used", hole.used);
        entry.text("text", hole.text);
    }
    out += ']';
    item.number("more_holes", device.more_holes);
    item.key("issues") += '[';
    for (std::size_t index = 0; index < device.issues.size(); ++index) {
        const DashIssue& issue = device.issues[index];
        if (index > 0) {
            out += ',';
        }
        Fields entry(out);
        entry.word("tier", finding_tier_name(issue.tier));
        entry.text("title", issue.title);
        entry.text("about", issue.about);
        std::string& steps = entry.key("steps");
        steps += '[';
        for (std::size_t step = 0; step < issue.steps.size(); ++step) {
            steps += step > 0 ? "," : "";
            steps += json_text(issue.steps[step]);
        }
        steps += ']';
        entry.number("port", issue.port);
    }
    out += ']';
}

void write_check(std::string& out, const DashCheck& check) {
    Fields fields(out);
    fields.word("state", state_word(check.state));
    fields.word("step", check.step == HealthStep::Ports ? "ports" : "sweep");
    fields.number("asked", check.asked);
    fields.number("total", check.total);
    fields.number("scanned", check.scanned);
    fields.number("hosts", check.hosts);
    fields.number("elapsed_s", check.elapsed_s);
    fields.text("reason", check.reason);
    fields.boolean("can", check.can);
    fields.text("why", check.why);
    fields.text("asks", check.asks);
    fields.number("max_devices", static_cast<int32_t>(kHealthMaxHosts));
    fields.number("max_ports", static_cast<int32_t>(common_ports().size()));
}

void write_overview(std::string& out, const DashOverview& overview) {
    Fields fields(out);
    fields.boolean("checked", overview.checked);
    fields.boolean("checking", overview.checking);
    fields.text("checked_at", overview.checked_at);
    fields.key("took_s") += number_or_null(overview.took_s);
    const DashCounts& counts = overview.counts;
    fields.number("fix", counts.fix);
    fields.number("look", counts.look);
    fields.number("know", counts.know);
    fields.number("total", counts.devices);
    fields.number("device_fix", counts.device_fix);
    fields.number("device_look", counts.device_look);
    fields.number("fine", counts.fine);
    fields.number("unchecked", counts.unchecked);
    fields.number("device_new", counts.device_new);
    fields.number("hotspot", counts.hotspot);
    fields.text("list_date", overview.list_date);
    fields.key("things") += '[';
    for (std::size_t index = 0; index < overview.things.size(); ++index) {
        const DashThing& thing = overview.things[index];
        if (index > 0) {
            out += ',';
        }
        Fields item(out);
        item.text("id", thing.id);
        item.word("tier", finding_tier_name(thing.tier));
        item.text("icon", thing.icon);
        item.text("title", thing.title);
        item.text("hint", thing.hint);
        item.text("action", thing.action);
        item.text("fix", thing.fix);
        item.text("device", thing.device);
        item.text("time", thing.time);
        item.text("about", thing.about);
        std::string& steps = item.key("steps");
        steps += '[';
        for (std::size_t step = 0; step < thing.steps.size(); ++step) {
            steps += step > 0 ? "," : "";
            steps += json_text(thing.steps[step]);
        }
        steps += ']';
        item.text("where", thing.where);
        std::string& also = item.key("also");
        also += '[';
        for (std::size_t other = 0; other < thing.also.size(); ++other) {
            also += other > 0 ? "," : "";
            also += json_text(thing.also[other]);
        }
        also += ']';
    }
    out += ']';
    fields.key("devices") += '[';
    for (std::size_t index = 0; index < overview.devices.size(); ++index) {
        const DashDevice& device = overview.devices[index];
        if (index > 0) {
            out += ',';
        }
        Fields item(out);
        item.text("id", device.id);
        item.text("name", device.name);
        item.text("address", device.address);
        item.word("ring", device.wireless ? "wireless" : "cable");
        item.boolean("router", device.router);
        item.boolean("new", device.is_new);
        item.boolean("checked", device.checked);
        item.word("tier", finding_tier_name(device.tier));
        item.text("tag", device.tag);
        item.text("services", device.services);
        item.text("seen", device.seen);
        item.text("kind", device.kind);
        item.boolean("hotspot", device.hotspot);
        item.boolean("blocked", device.blocked);
        write_page(item, out, device);
    }
    out += ']';
}

// The whole object, closed when `root` goes out of scope.
void write_status(std::string& out, const HomeData& home, const DashboardParts& parts) {
    const bool wifi = home.uplink_kind == LinkKind::Wifi;
    const bool hotspot = home.ap_active && is_hotspot_mode(home.mode);
    Fields root(out);
    root.number("v", 1);
    root.boolean("sample", home.sample);
    {
        Fields cardputer(root.key("cardputer"));
        cardputer.text("address", home.uplink_address);
        cardputer.key("battery") += number_or_null(home.battery_percent);
        cardputer.boolean("charging", home.battery_state == BatteryState::Charging);
    }
    {
        Fields network(root.key("network"));
        network.text("name", wifi ? home.wifi_name : std::string());
        network.word("link", link_kind_name(home.uplink_kind));
        network.word("internet", internet_name(home.internet));
    }
    {
        Fields mode(root.key("mode"));
        mode.word("id", mode_id(home.mode));
        mode.text("name", net_mode_name(home.mode));
        mode.boolean("hotspot", hotspot);
        mode.number("hotspot_devices", hotspot ? static_cast<int32_t>(home.clients.size()) : 0);
        mode.text("hotspot_name", hotspot ? home.ap_name : std::string());
        mode.word("vpn", vpn_state_name(home.vpn.state));
        mode.key("vpn_reply_s") += home.vpn.state == VpnState::On ? number_or_null(home.vpn.reply_seconds) : "null";
        mode.boolean("kill_switch", home.vpn.kill_switch);
        mode.boolean("keep", parts.keep_mode);
    }
    {
        Fields block(root.key("block"));
        block.number("failures", home.block_failures);
        block.text("message", home.block_message);
    }
    {
        Fields traffic(root.key("traffic"));
        traffic.key("down_kbps") += number_or_null(home.down_kbps);
        traffic.key("up_kbps") += number_or_null(home.up_kbps);
        std::string& history = traffic.key("history");
        history += '[';
        for (std::size_t index = 0; index < parts.traffic.size(); ++index) {
            if (index > 0) {
                history += ',';
            }
            history.append("[")
                .append(std::to_string(parts.traffic[index][0]))
                .append(",")
                .append(std::to_string(parts.traffic[index][1]))
                .append("]");
        }
        history += ']';
    }
    {
        Fields counts(root.key("counts"));
        counts.number("devices", parts.overview.counts.devices);
        counts.number("security", parts.overview.counts.fix);
    }
    write_check(root.key("check"), parts.check);
    write_overview(root.key("overview"), parts.overview);
    {
        Fields watch(root.key("watch"));
        watch.boolean("on", parts.watching);
        watch.boolean("trap", parts.trap_on);
        watch.number("trap_ports", parts.trap_ports);
        std::string& feed = watch.key("feed");
        feed += '[';
        for (std::size_t index = 0; index < parts.overview.watch_lines.size(); ++index) {
            if (index > 0) {
                feed += ',';
            }
            Fields line(feed);
            line.text("time", index < parts.overview.watch_times.size() ? parts.overview.watch_times[index] : "");
            line.text("text", parts.overview.watch_lines[index]);
        }
        feed += ']';
    }
}

} // namespace

void TrafficLog::clear() {
    samples_.clear();
}

void TrafficLog::push(int32_t down_kbps, int32_t up_kbps) {
    if (samples_.size() >= kLength) {
        samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(samples_.size() - kLength + 1));
    }
    samples_.push_back({down_kbps < 0 ? 0 : down_kbps, up_kbps < 0 ? 0 : up_kbps});
}

DashCheck dashboard_check(const HomeData& home, const ToolsData& tools) {
    DashCheck check;
    const HealthRun& run = tools.health;
    check.state = run.state;
    check.step = run.step;
    check.asked = run.asked;
    check.total = run.total;
    check.scanned = run.scanned;
    check.hosts = run.hosts;
    check.elapsed_s = run.elapsed_s;
    if (run.state == ToolState::Failed) {
        check.reason = run.error == ToolError::NotAllowed ? std::string("the sweep isn't allowed here")
                       : run.reason.empty()                ? std::string("check failed")
                                                           : run.reason;
    }
    const HealthStart start = health_start(home, tools);
    check.can = start.gate == HealthGate::Open;
    check.why = health_gate_text(start.gate, tools);
    check.asks = start.asks;
    return check;
}

std::string dashboard_status_json(const HomeData& home, const DashboardParts& parts) {
    std::string out;
    out.reserve(1280 + parts.overview.devices.size() * 400 + parts.overview.things.size() * 700);
    write_status(out, home, parts);
    out += '\n';
    return out;
}

} // namespace model
