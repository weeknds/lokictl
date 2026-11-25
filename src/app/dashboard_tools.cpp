/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "dashboard_tools.h"

#include "base_viewmodel.h"
#include "command_bar.h"
#include "health_check.h"
#include "home_format.h"
#include "lan_speed.h"
#include "report.h"
#include "survey_data.h"
#include "tool_format.h"
#include "trap_data.h"
#include "watch_data.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace app {
namespace {

namespace tools = screen::tools;

// The most of each list the pages show.
constexpr std::size_t kAlertsShown = 30;
constexpr std::size_t kHitsShown = 30;
constexpr std::size_t kNetworksShown = 24;

const char* state_word(model::ToolState state) {
    switch (state) {
        case model::ToolState::Idle: return "idle";
        case model::ToolState::Running: return "running";
        case model::ToolState::Done: return "done";
        case model::ToolState::Stopped: return "stopped";
        case model::ToolState::Failed: return "failed";
    }
    return "idle";
}

// The tones of tool_format.h, as the page's colour words.
const char* tone_word(int32_t tone) {
    switch (tone) {
        case tools::kToneMuted: return "muted";
        case tools::kToneSuccess: return "ok";
        case tools::kToneWarning: return "warn";
        case tools::kToneError: return "bad";
        case tools::kToneAccent: return "accent";
        default: return "";
    }
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

// "4:05" or "1:02:03", as the Watch and Trap pages write a run's time.
std::string duration_text(int32_t seconds) {
    seconds = std::max<int32_t>(seconds, 0);
    const int32_t hours = seconds / 3600;
    const int32_t minutes = (seconds / 60) % 60;
    const int32_t rest = seconds % 60;
    std::string text;
    if (hours > 0) {
        text = std::to_string(hours) + (minutes < 10 ? ":0" : ":");
    }
    text += std::to_string(minutes) + (rest < 10 ? ":0" : ":") + std::to_string(rest);
    return text;
}

// "4:05", as the Survey page writes a time in milliseconds.
std::string minutes_text(int32_t ms) {
    const int32_t seconds = std::max<int32_t>(ms, 0) / 1000;
    const int32_t rest = seconds % 60;
    return std::to_string(seconds / 60) + (rest < 10 ? ":0" : ":") + std::to_string(rest);
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
    void text(const char* name, const std::string& value) { key(name) += model::json_text(value); }
    void word(const char* name, const char* value) { key(name).append("\"").append(value).append("\""); }
    void number(const char* name, int64_t value) { key(name) += std::to_string(value); }
    void boolean(const char* name, bool value) { key(name) += value ? "true" : "false"; }
    // The status line and its colour.
    void line(const tools::ToneText& value) {
        text("line", value.text);
        word("tone", tone_word(value.tone));
    }
    // Whether it may start now, and why not.
    void gate(const std::string& why) {
        boolean("can", why.empty());
        text("why", why);
    }

private:
    std::string& out_;
    bool first_{true};
};

// "[a,b,...]" from `count` items, each written by `write(out, index)`.
template <typename Write>
void list(std::string& out, std::size_t count, Write write) {
    out += '[';
    for (std::size_t index = 0; index < count; ++index) {
        if (index > 0) {
            out += ',';
        }
        write(out, index);
    }
    out += ']';
}

void numbers(std::string& out, const std::vector<int32_t>& values) {
    list(out, values.size(), [&](std::string& text, std::size_t index) { text += std::to_string(values[index]); });
}

void write_ping(Fields& tool, const model::PingRun& run) {
    const bool running = run.state == model::ToolState::Running;
    tool.line(tools::ping_status(run));
    tool.text("target", run.target);
    tool.text("address", run.address);
    tool.text("counts", tools::ping_counts_text(run));
    tool.text("times", tools::ping_times_text(run));
    list(tool.key("replies"), run.replies.size(), [&](std::string& out, std::size_t index) {
        const model::PingReply& reply = run.replies[index];
        const tools::ToneText text = tools::reply_text(reply, running);
        Fields item(out);
        item.number("seq", reply.sequence);
        item.text("text", text.text);
        item.word("tone", tone_word(text.tone));
    });
}

void write_dns(Fields& tool, const model::DnsRun& run) {
    tool.line(tools::dns_status(run));
    tool.text("target", run.name);
    tool.text("server", run.server);
    tool.number("time_ms", run.time_ms);
    list(tool.key("addresses"), run.addresses.size(), [&](std::string& out, std::size_t index) {
        out += model::json_text(run.addresses[index]);
    });
}

void write_trace(Fields& tool, const model::TraceRun& run) {
    const bool running = run.state == model::ToolState::Running;
    tool.line(tools::trace_status(run));
    tool.text("target", run.target);
    tool.text("address", run.address);
    list(tool.key("hops"), run.hops.size(), [&](std::string& out, std::size_t index) {
        const tools::TraceRow row = tools::trace_row(run.hops[index], running);
        Fields item(out);
        item.text("n", row.number);
        item.text("address", row.address.text);
        item.word("address_tone", tone_word(row.address.tone));
        item.text("name", row.name);
        item.text("time", row.time.text);
        item.word("time_tone", tone_word(row.time.tone));
    });
}

void write_ports(Fields& tool, const model::PortRun& run) {
    tool.line(tools::ports_status(run));
    tool.text("target", run.target);
    tool.text("counts", tools::ports_counts_text(run));
    tool.number("asked", run.asked);
    tool.number("total", run.total);
    const std::vector<tools::PortItem> items = tools::port_items(run);
    list(tool.key("open"), items.size(), [&](std::string& out, std::size_t index) {
        const tools::PortItem& port = items[index];
        Fields item(out);
        item.number("port", port.port);
        item.text("text", port.text);
        item.text("plain", model::port_plain(port.port));
        item.text("said", model::port_banner(run, port.port));
        item.text("tag", port.tag.text);
        item.word("tag_tone", tone_word(port.tag.tone));
    });
}

void write_speed(Fields& tool, const model::SpeedRun& run, const std::string& server) {
    tool.line(tools::speed_status(run));
    // The run's server, else the one a run would use (the settings').
    tool.text("server", model::speed_server_host(run.server.empty() ? server : run.server));
    tool.word("phase", run.phase == model::SpeedPhase::Download ? "download"
                       : run.phase == model::SpeedPhase::Upload ? "upload"
                                                                : "connect");
    tool.text("down", screen::home::rate_text(run.down_kbps));
    tool.text("up", screen::home::rate_text(run.up_kbps));
    tool.text("down_bytes", tools::bytes_text(run.down_bytes));
    tool.text("up_bytes", tools::bytes_text(run.up_bytes));
    numbers(tool.key("down_samples"), run.down_samples);
    numbers(tool.key("up_samples"), run.up_samples);
}

// The LAN page's status line on its test side, as the page writes it.
tools::ToneText lan_line(const model::LanRun& run, const model::ToolsData& data) {
    const bool mine = run.mode == model::LanMode::Client;
    const model::ToolState state = mine ? run.state : model::ToolState::Idle;
    if (state != model::ToolState::Running && model::tool_running(data)) {
        return {model::other_tool_text(data), tools::kToneMuted};
    }
    if (state != model::ToolState::Running && run.state == model::ToolState::Running) {
        return {"The server is on", tools::kToneMuted};
    }
    if (state == model::ToolState::Failed) {
        return {run.reason.empty() ? std::string("The test failed") : run.reason, tools::kToneError};
    }
    const std::string seconds =
        std::to_string(run.phase_ms / 1000) + " of " + std::to_string(model::kLanPhaseSeconds) + " s";
    switch (state) {
        case model::ToolState::Running:
            return run.phase == model::LanPhase::Download ? tools::ToneText{"Download " + seconds, tools::kToneAccent}
                   : run.phase == model::LanPhase::Upload ? tools::ToneText{"Upload " + seconds, tools::kToneAccent}
                                                          : tools::ToneText{"Connecting", tools::kToneAccent};
        case model::ToolState::Done:
            return {"Done", tools::kToneSuccess};
        case model::ToolState::Stopped:
            return {"Stopped", tools::kToneMuted};
        case model::ToolState::Idle:
        case model::ToolState::Failed:
            break;
    }
    return {"Tests 10 s each way", tools::kToneMuted};
}

void write_lan(Fields& tool, const viewmodel::BaseViewModel& view_model) {
    const model::LanRun& run = view_model.lan();
    tool.word("mode", run.mode == model::LanMode::Server ? "server" : "client");
    tool.line(lan_line(run, view_model.tools()));
    tool.text("target", run.mode == model::LanMode::Client ? run.target : std::string());
    tool.text("down", screen::home::rate_text(run.down_kbps));
    tool.text("up", screen::home::rate_text(run.up_kbps));
    numbers(tool.key("down_samples"), run.down_samples);
    numbers(tool.key("up_samples"), run.up_samples);
    tool.gate(browser_lan_refusal(view_model));
}

const char* load_word(model::ChannelLoad load) {
    switch (load) {
        case model::ChannelLoad::Quiet: return "quiet";
        case model::ChannelLoad::InUse: return "in use";
        case model::ChannelLoad::Busy: return "busy";
        case model::ChannelLoad::Unknown: break;
    }
    return "";
}

void write_survey(Fields& tool, const viewmodel::BaseViewModel& view_model) {
    const model::SurveyRun& run = view_model.survey();
    std::string why;
    const std::string stick = model::survey_stick(view_model.home(), why);
    tools::ToneText status;
    switch (run.state) {
        case model::ToolState::Running:
            status = {"Listening, " + minutes_text(model::kSurveySeconds * 1000 - run.elapsed_ms) + " left",
                      tools::kToneAccent};
            break;
        case model::ToolState::Done:
            status = {"Done, listened " + minutes_text(run.elapsed_ms), tools::kToneMuted};
            break;
        case model::ToolState::Stopped:
            status = {"Stopped after " + minutes_text(run.elapsed_ms), tools::kToneMuted};
            break;
        case model::ToolState::Failed:
            status = run.error == model::ToolError::NotAllowed
                         ? tools::ToneText{"Survey not allowed", tools::kToneError}
                         : tools::ToneText{capitalized(run.reason.empty() ? std::string("survey failed") : run.reason),
                                           tools::kToneError};
            break;
        case model::ToolState::Idle: {
            const std::string refusal = browser_survey_refusal(view_model);
            status = refusal.empty() ? tools::ToneText{"Ready on " + stick + ", sends nothing", tools::kToneMuted}
                                     : tools::ToneText{refusal, tools::kToneWarning};
            break;
        }
    }
    tool.line(status);
    tool.text("stick", stick.empty() ? run.interface_name : stick);
    tool.gate(run.state == model::ToolState::Running ? std::string() : browser_survey_refusal(view_model));
    tool.number("frames", run.frames);
    tool.number("networks_seen", run.networks_seen);
    tool.number("best", model::survey_best_channel(run));
    list(tool.key("channels"), static_cast<std::size_t>(model::kSurveyChannels), [&](std::string& out, std::size_t index) {
        const auto channel = static_cast<int32_t>(index + 1);
        Fields item(out);
        item.number("level", model::survey_channel_level(run, channel));
        item.word("load", load_word(model::survey_channel_load(run, channel)));
        item.number("networks", model::survey_channel_networks(run, channel));
        item.number("kickoffs", run.channels[index].kickoffs);
    });
    const std::vector<model::SurveyFinding> findings = model::survey_findings(run);
    list(tool.key("findings"), findings.size(), [&](std::string& out, std::size_t index) {
        const model::SurveyFinding& finding = findings[index];
        Fields item(out);
        item.word("kind", finding.kind == model::SurveyFindingKind::Kickoff     ? "kickoff"
                          : finding.kind == model::SurveyFindingKind::LookAlike ? "look-alike"
                                                                                : "busy");
        item.text("text", finding.text);
        item.number("channel", finding.channel);
    });
    // The strongest first, as many as the page shows.
    std::vector<const model::SurveyNetwork*> networks;
    for (const model::SurveyNetwork& network : run.networks) {
        networks.push_back(&network);
    }
    std::stable_sort(networks.begin(), networks.end(), [](const model::SurveyNetwork* a, const model::SurveyNetwork* b) {
        return a->signal_dbm > b->signal_dbm;
    });
    const std::size_t shown = std::min(networks.size(), kNetworksShown);
    list(tool.key("networks"), shown, [&](std::string& out, std::size_t index) {
        const model::SurveyNetwork& network = *networks[index];
        Fields item(out);
        item.text("name", network.name);
        item.boolean("hidden", network.hidden);
        item.number("channel", network.channel);
        item.text("security", network.security);
        item.number("signal", network.signal_dbm);
        item.boolean("look_alike", network.look_alike);
    });
    tool.number("more_networks", static_cast<int64_t>(networks.size() - shown));
}

// Who an alert row names, as the Watch page's row: its IPv4 address, else
// its hardware address. A fake router's row names both, as the Cardputer's
// Watch detail does: the router's address and the claimant's hardware
// address.
std::string alert_who(const model::WatchAlert& alert) {
    if (alert.kind == model::WatchAlertKind::FakeRouter && !alert.hardware.empty()) {
        return alert.address.empty() ? alert.hardware : alert.address + " " + alert.hardware;
    }
    if (!alert.address.empty() && alert.address.find(':') == std::string::npos) {
        return alert.address;
    }
    return alert.hardware.empty() ? alert.address : alert.hardware;
}

// The Watch page detail's line of facts.
std::string alert_fact(const model::WatchAlert& alert, bool known) {
    switch (alert.kind) {
        case model::WatchAlertKind::NewDevice:
            return known ? "Marked as known" : "Not on your known list";
        case model::WatchAlertKind::FakeRouter:
            return "Router was " + alert.before;
        case model::WatchAlertKind::SameAddress:
            return "Also " + alert.before;
        case model::WatchAlertKind::SecondDhcp:
            return "First server " + alert.before;
        case model::WatchAlertKind::Ipv6Router:
            return alert.hardware.empty() ? std::string("Router announcement") : "Sent by " + alert.hardware;
        case model::WatchAlertKind::Scan:
            return "Tried " + std::to_string(alert.count) + " ports";
        case model::WatchAlertKind::ArpFlood:
            return std::to_string(alert.count) + " ARP frames in 1 s";
        case model::WatchAlertKind::Trap: {
            const model::TrapService* service = model::trap_service(static_cast<uint16_t>(alert.count));
            return std::string("Tried ") + (service ? service->word : "a port") + ", port " +
                   std::to_string(alert.count);
        }
    }
    return {};
}

void write_watch(Fields& tool,
                 const viewmodel::BaseViewModel& view_model,
                 const model::WatchData& shown,
                 const std::vector<std::string>& lines) {
    const model::WatchRun& run = view_model.watch().run;
    tools::ToneText status;
    switch (run.state) {
        case model::ToolState::Running:
            status = run.learn_left_s > 0
                         ? tools::ToneText{"Learning devices, " + duration_text(run.learn_left_s) + " left",
                                           tools::kToneAccent}
                         : tools::ToneText{"Watching " + duration_text(run.elapsed_s) + ", sends none",
                                           tools::kToneAccent};
            break;
        case model::ToolState::Stopped:
        case model::ToolState::Done:
            status = {"Stopped after " + duration_text(run.elapsed_s), tools::kToneMuted};
            break;
        case model::ToolState::Failed:
            status = run.error == model::ToolError::NotAllowed
                         ? tools::ToneText{"Capture not allowed", tools::kToneError}
                         : tools::ToneText{capitalized(run.reason.empty() ? std::string("watch failed") : run.reason),
                                           tools::kToneError};
            break;
        case model::ToolState::Idle: {
            const std::string refusal = browser_watch_refusal(view_model);
            status = refusal.empty() ? tools::ToneText{"Listens only, sends nothing", tools::kToneMuted}
                                     : tools::ToneText{refusal, tools::kToneWarning};
            break;
        }
    }
    tool.word("state", state_word(run.state));
    tool.line(status);
    tool.gate(run.state == model::ToolState::Running ? std::string() : browser_watch_refusal(view_model));
    // Alerts raised in all, so the page can tell new ones from those it has seen.
    tool.number("raised", shown.run.raised);
    const std::size_t count = std::min(shown.run.alerts.size(), kAlertsShown);
    list(tool.key("alerts"), count, [&](std::string& out, std::size_t index) {
        const model::WatchAlert& alert = shown.run.alerts[index];
        Fields item(out);
        item.text("time", alert.time);
        item.text("name", model::watch_alert_name(alert.kind));
        item.text("text", index < lines.size() ? lines[index] : std::string(model::watch_alert_name(alert.kind)));
        item.text("who", alert_who(alert));
        item.text("fact", alert_fact(alert, model::watch_known(shown, alert.hardware)));
        item.text("about", model::watch_alert_about(alert.kind));
        item.boolean("serious", alert.kind != model::WatchAlertKind::NewDevice);
    });
}

const char* port_state_word(model::TrapPortState state) {
    switch (state) {
        case model::TrapPortState::Open: return "open";
        case model::TrapPortState::InUse: return "in use";
        case model::TrapPortState::NoHelper: return "no helper";
        case model::TrapPortState::Off: break;
    }
    return "off";
}

void write_trap(Fields& tool, const model::TrapRun& run, const model::TrapRun& shown) {
    tools::ToneText status;
    const auto open = model::trap_open_ports(run);
    const auto all = static_cast<int32_t>(model::kTrapServiceCount);
    switch (run.state) {
        case model::ToolState::Running: {
            const std::string time = "On " + duration_text(run.elapsed_s) + ", ";
            if (!run.helper_problem.empty()) {
                status = {time + std::to_string(open) + " of " + std::to_string(all) + " (no helper)",
                          tools::kToneWarning};
            } else if (open == all) {
                status = {time + std::to_string(open) + " fake ports", tools::kToneAccent};
            } else {
                status = {time + std::to_string(open) + " of " + std::to_string(all) + " ports", tools::kToneAccent};
            }
            break;
        }
        case model::ToolState::Stopped:
        case model::ToolState::Done:
            status = {"Off after " + duration_text(run.elapsed_s), tools::kToneMuted};
            break;
        case model::ToolState::Failed:
            status = {capitalized(run.reason.empty() ? std::string("trap failed") : run.reason), tools::kToneError};
            break;
        case model::ToolState::Idle:
            status = {"Off, no port open", tools::kToneMuted};
            break;
    }
    tool.word("state", state_word(run.state));
    tool.line(status);
    tool.gate({});
    tool.text("helper", run.helper_problem);
    tool.number("raised", shown.raised);
    // Every fake service, with its state in the run shown.
    list(tool.key("ports"), model::kTrapServiceCount, [&](std::string& out, std::size_t index) {
        const model::TrapService& service = model::trap_services()[index];
        model::TrapPortState state = model::TrapPortState::Off;
        for (const model::TrapPort& port : shown.ports) {
            if (port.port == service.port) {
                state = port.state;
            }
        }
        Fields item(out);
        item.number("port", service.port);
        item.text("name", service.name);
        item.word("state", port_state_word(state));
    });
    const std::size_t count = std::min(shown.hits.size(), kHitsShown);
    list(tool.key("hits"), count, [&](std::string& out, std::size_t index) {
        const model::TrapHit& hit = shown.hits[index];
        const model::TrapService* service = model::trap_service(hit.port);
        Fields item(out);
        item.text("time", hit.time);
        item.text("who", hit.address);
        item.number("from_port", hit.source_port);
        item.number("port", hit.port);
        item.text("word", service ? service->word : "a port");
        item.text("name", service ? service->name : "Fake service");
        item.text("sent", hit.first_bytes);
    });
}

} // namespace

std::string browser_tool_refusal(model::ToolKind kind, const viewmodel::BaseViewModel& view_model) {
    return tool_command_refusal(kind, view_model.tools(), view_model.home());
}

std::string browser_lan_refusal(const viewmodel::BaseViewModel& view_model) {
    const model::LanRun& run = view_model.lan();
    if (run.state == model::ToolState::Running) {
        return run.mode == model::LanMode::Server ? "The server is on" : "A test is running";
    }
    if (model::tool_running(view_model.tools())) {
        return model::other_tool_text(view_model.tools());
    }
    return {};
}

bool browser_lan_target(const std::string& address, const viewmodel::BaseViewModel& view_model) {
    if (!model::valid_lan_target(address) || address == view_model.home().uplink_address) {
        return false;
    }
    for (const model::LanPlace& place : view_model.lan().places) {
        if (place.address == address) {
            return false;  // the Cardputer itself
        }
    }
    return true;
}

std::string browser_survey_refusal(const viewmodel::BaseViewModel& view_model) {
    std::string why;
    if (!model::survey_stick(view_model.home(), why).empty()) {
        return {};
    }
    if (why == "managed") {
        return "Stick is in use";
    }
    if (why == "unknown") {
        return "Can't check the stick";
    }
    return "Needs a USB Wi-Fi stick";
}

std::string browser_watch_refusal(const viewmodel::BaseViewModel& view_model) {
    return model::valid_interface_name(view_model.home().uplink_name) ? std::string() : "Join a network first";
}

std::string dashboard_tools_json(const viewmodel::BaseViewModel& view_model,
                                 const model::DashSample* sample,
                                 const std::vector<std::string>& watch_lines) {
    const model::ToolsData& data = view_model.tools();
    const model::HomeData& home = view_model.home();
    std::string json;
    {
        Fields root(json);
        // The tool that runs now (only one does), by its name; empty for none.
        std::string running;
        for (std::size_t index = 0; index < model::kToolKindCount; ++index) {
            const auto kind = static_cast<model::ToolKind>(index);
            if (model::tool_state(data, kind) == model::ToolState::Running) {
                running = model::tool_name(kind);
            }
        }
        root.text("running", running);
        root.text("router", model::is_ipv4_text(home.gateway_address) ? home.gateway_address : std::string());
        root.number("max", static_cast<int64_t>(model::kToolTargetMax));
        root.text("data_note", tools::speed_status(model::SpeedRun{}).text);
        {
            Fields presets(root.key("presets"));
            list(presets.key("ping"), std::size(model::kPingPresets), [](std::string& out, std::size_t index) {
                out += model::json_text(model::kPingPresets[index]);
            });
            list(presets.key("dns"), std::size(model::kDnsPresets), [](std::string& out, std::size_t index) {
                out += model::json_text(model::kDnsPresets[index]);
            });
        }
        const auto write_tool = [&](const char* name, model::ToolKind kind, auto write) {
            Fields tool(root.key(name));
            tool.word("state", state_word(model::tool_state(data, kind)));
            tool.gate(browser_tool_refusal(kind, view_model));
            write(tool);
        };
        write_tool("ping", model::ToolKind::Ping, [&](Fields& tool) { write_ping(tool, data.ping); });
        write_tool("dns", model::ToolKind::Dns, [&](Fields& tool) { write_dns(tool, data.dns); });
        write_tool("trace", model::ToolKind::Trace, [&](Fields& tool) { write_trace(tool, data.trace); });
        write_tool("ports", model::ToolKind::Ports, [&](Fields& tool) { write_ports(tool, data.ports); });
        write_tool("speed", model::ToolKind::Speed, [&](Fields& tool) { write_speed(tool, data.speed, view_model.speed_server()); });
        {
            Fields tool(root.key("lan"));
            tool.word("state", state_word(view_model.lan().state));
            write_lan(tool, view_model);
        }
        {
            Fields tool(root.key("survey"));
            tool.word("state", state_word(view_model.survey().state));
            write_survey(tool, view_model);
        }
        {
            Fields tool(root.key("watch"));
            write_watch(tool, view_model, sample ? sample->watch : view_model.watch(), watch_lines);
        }
        {
            Fields tool(root.key("trap"));
            write_trap(tool, view_model.trap(), sample ? sample->trap : view_model.trap());
        }
    }
    return json;
}

} // namespace app
