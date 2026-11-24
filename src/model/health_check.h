/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The rules of the health check (the Health page): which devices one run
// scans, and what it says about what it found, in plain words. Nothing here
// sends or reads anything; the tool runner sweeps and scans
// (tool_runner.cpp) and hands the results to these functions. What each
// rule looks at is written up in docs/checks.md.
namespace model {

// The words of one kind of finding. HealthIssue::Service has none here:
// its words are its port's rule in service_rules().
struct HealthRule {
    HealthIssue issue;
    RiskLevel level;    // Holes and the Wi-Fi rules may pick their own (see host_findings())
    const char* title;  // the list's short name, at most 20 characters
    const char* about;  // what it means: one or two short plain sentences
    const char* fix;    // what to do: one short sentence
};

// Every rule but Service, in the order of HealthIssue.
const std::vector<HealthRule>& health_rules();
// The rule of `issue`; nullptr for Service.
const HealthRule* health_rule(HealthIssue issue);

// What the detail says a finding means, and what to do about it.
std::string health_about(const HealthFinding& finding);
std::string health_fix(const HealthFinding& finding);

// A level's tag in the list: "Risk", "Warn", "Info".
const char* health_level_tag(RiskLevel level);

// One device a health check scans. A text that is not known is empty.
struct HealthHost {
    std::string address;      // IPv4, dotted
    std::string name;         // its host name
    std::string kind;         // what it is ("printer", "router")
    bool router{false};       // the uplink's router
    bool own{false};          // the Cardputer itself (the uplink's address)
    bool upnp{false};         // it answered or announced itself over SSDP
    std::string ssdp_server;  // what its SSDP SERVER line said
};

// Which devices a check scans, at most kHealthMaxHosts: the router first,
// then the Cardputer itself (local.address), then every other device of
// `devices` with an IPv4 address, by address. Only addresses inside the
// network `network`/`netmask` (host byte order) are kept, never its
// network or broadcast address, never a device that is one of the
// Cardputer's own ports (HeardDevice::own_port), and each address once (a
// name, a kind, the SSDP facts of any entry for it are kept).
std::vector<HealthHost> health_hosts(const HealthLocal& local,
                                     const std::vector<HeardDevice>& devices,
                                     uint32_t network,
                                     uint32_t netmask);

// The findings about one scanned device `host`, placed at `order`, from
// its finished port scan `scan` (open ports, what they said, and the check
// against the list of known holes):
// - Service: each open port with a rule in service_rules(), at the rule's
//   level (UPnP's ports 1900 and 49152 only through Upnp);
// - RouterPage: the router with an open plain web port;
// - Upnp: the router (or a device that says it is one) answering SSDP or
//   with port 1900 or 49152 open;
// - Holes (Risk), Outdated (Warning) or, for a version built by a vendor
//   ("Debian"), Check (Info): one per port the check flagged;
// - ManyPorts: more than kHealthManyPorts open ports.
std::vector<HealthFinding> host_findings(const HealthHost& host, const PortRun& scan, int32_t order);

// The findings about the Cardputer's own links:
// - OpenWifi (Risk), WepWifi (Risk) or WpaWifi (Warning): the joined
//   Wi-Fi's security is open, WEP or WPA;
// - LookAlike: a nearby network, not the joined one, with the joined one's
//   name and weak security (open, OWE, WEP or WPA) that is weaker than the
//   joined one's; one finding, about the weakest;
// - SlowCable: the cable link at kHealthSlowCableMbps or slower, or half
//   duplex.
// They take the order -1, so they come before any device's of the same
// level.
std::vector<HealthFinding> local_findings(const HealthLocal& local);

// Sorts `findings` the worst first, then by order, issue and port, and
// keeps the first kHealthMaxFindings.
void sort_findings(std::vector<HealthFinding>& findings);

// How strong a Wi-Fi security, as WifiNetwork::security writes it, is: 0
// for "open", 1 for "OWE" and "WEP", 2 for "WPA" and "WPA-EAP", 3 for
// "WPA2" and "WPA2-EAP", 4 for "WPA2/3", 5 for "WPA3" and "WPA3-EAP"; -1
// for anything else.
int32_t wifi_security_rank(std::string_view security);

// How many findings there are of each level.
struct HealthCounts {
    int32_t risks{0};
    int32_t warnings{0};
    int32_t infos{0};
};

HealthCounts health_counts(const std::vector<HealthFinding>& findings);

// The counts in a line: "2 risks, 3 warnings, 1 info" (levels with none
// left out); "Nothing found" without any.
std::string health_summary(const HealthCounts& counts);

// ---------- Starting a check ----------

// Whether a health check may start now. The Health page and the web
// dashboard ask the same gate, so both refuse the same networks.
enum class HealthGate : int32_t {
    Open = 0,    // it may start
    Running,     // a health check runs now
    OtherTool,   // another tool runs (one tool at a time)
    NoNetwork,   // no uplink with a valid interface name
    NoAddress,   // the uplink has no IPv4 address yet
    NotPrivate,  // the uplink's address is not a private one
};

struct HealthStart {
    HealthGate gate{HealthGate::NoNetwork};
    std::string interface_name;  // the uplink a check runs on; empty when there is none
    std::string asks;            // what the sweep asks, "10.0.0.1-254"; empty unless the address is private
};

// The uplink a check runs on: HomeData::uplink_name when it is a valid
// interface name, else empty.
std::string health_uplink(const HomeData& home);
// The uplink's address when it is a private IPv4 address (10.0.0.0/8,
// 172.16.0.0/12 or 192.168.0.0/16), else empty: a check only runs there.
std::string health_home_address(const HomeData& home);
// The gate, the uplink and what the sweep asks (a check sweeps the
// address's /24).
HealthStart health_start(const HomeData& home, const ToolsData& tools);
// Why the gate is closed, in a few words for a person ("Join a network
// first"); empty for Open.
std::string health_gate_text(HealthGate gate, const ToolsData& tools);
// What runs instead of a health check, in a few words: "Ping is running".
const char* other_tool_text(const ToolsData& data);

} // namespace model
