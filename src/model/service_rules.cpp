/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "service_rules.h"

namespace model {

const char* risk_level_name(RiskLevel level) {
    switch (level) {
        case RiskLevel::Info:
            return "info";
        case RiskLevel::Warning:
            return "warning";
        case RiskLevel::Risk:
            return "risk";
    }
    return "info";
}

const std::vector<ServiceRule>& service_rules() {
    static const std::vector<ServiceRule> rules{
        {21, RiskLevel::Warning, "sends passwords in plain text",
         "FTP open",
         "Turn FTP off, or use SFTP instead."},
        {23, RiskLevel::Risk, "sends passwords in plain text",
         "Telnet open",
         "Turn Telnet off; use SSH instead."},
        {110, RiskLevel::Info, "mail login may be unencrypted",
         "Plain mail login",
         "Use encrypted mail (port 995)."},
        {111, RiskLevel::Info, "lists other services; rarely needed",
         "Service list open",
         "Turn it off unless you share folders."},
        {135, RiskLevel::Info, "Windows remote calls; keep it local",
         "Windows calls open",
         "Keep it blocked from other networks."},
        {139, RiskLevel::Info, "old Windows sharing; keep it local",
         "Old file sharing",
         "Turn SMB1 off if nothing needs it."},
        {143, RiskLevel::Info, "mail login may be unencrypted",
         "Plain mail login",
         "Use encrypted mail (port 993)."},
        {445, RiskLevel::Info, "file sharing; keep it local",
         "File sharing open",
         "Share less, and use a password."},
        {1080, RiskLevel::Warning, "relays traffic; check it needs a password",
         "Traffic relay open",
         "Turn it off, or set a password."},
        {1883, RiskLevel::Warning, "unencrypted, often no password",
         "Message hub open",
         "Set a password and encryption."},
        {1900, RiskLevel::Warning, "UPnP: may let apps open router ports",
         "Apps can open ports",
         "Turn UPnP off if nothing needs it."},
        {2049, RiskLevel::Warning, "shares files; check who may mount",
         "Shared folders open",
         "Let only your devices use it."},
        {3306, RiskLevel::Warning, "a database open to the network",
         "Database open",
         "Keep it local, set a password."},
        {3389, RiskLevel::Warning, "remote desktop; often attacked",
         "Remote desktop open",
         "Turn it off, or use it via a VPN."},
        {5432, RiskLevel::Warning, "a database open to the network",
         "Database open",
         "Keep it local, set a password."},
        {5555, RiskLevel::Risk, "debug access; anyone may take control",
         "Debug access open",
         "Turn debug access (ADB) off."},
        {5900, RiskLevel::Warning, "remote screen; check its password",
         "Remote screen open",
         "Set a password, or turn it off."},
        {6379, RiskLevel::Risk, "a database that often has no password",
         "Database open",
         "Keep it local, set a password."},
        {7547, RiskLevel::Warning, "remote control by the provider",
         "Provider access",
         "Ask your provider to turn it off."},
        {9100, RiskLevel::Info, "anyone on the network can print",
         "Anyone can print",
         "Turn it off on shared networks."},
        {49152, RiskLevel::Warning, "UPnP: may let apps open router ports",
         "Apps can open ports",
         "Turn UPnP off if nothing needs it."},
    };
    return rules;
}

const ServiceRule* service_rule(uint16_t port) {
    for (const ServiceRule& rule : service_rules()) {
        if (rule.port == port) {
            return &rule;
        }
    }
    return nullptr;
}

} // namespace model
