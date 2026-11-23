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
        {21, RiskLevel::Warning, "sends passwords in plain text"},
        {23, RiskLevel::Risk, "sends passwords in plain text"},
        {110, RiskLevel::Info, "mail login may be unencrypted"},
        {111, RiskLevel::Info, "lists other services; rarely needed"},
        {135, RiskLevel::Info, "Windows remote calls; keep it local"},
        {139, RiskLevel::Info, "old Windows sharing; keep it local"},
        {143, RiskLevel::Info, "mail login may be unencrypted"},
        {445, RiskLevel::Info, "file sharing; keep it local"},
        {1080, RiskLevel::Warning, "relays traffic; check it needs a password"},
        {1883, RiskLevel::Warning, "unencrypted, often no password"},
        {1900, RiskLevel::Warning, "UPnP: may let apps open router ports"},
        {2049, RiskLevel::Warning, "shares files; check who may mount"},
        {3306, RiskLevel::Warning, "a database open to the network"},
        {3389, RiskLevel::Warning, "remote desktop; often attacked"},
        {5432, RiskLevel::Warning, "a database open to the network"},
        {5555, RiskLevel::Risk, "debug access; anyone may take control"},
        {5900, RiskLevel::Warning, "remote screen; check its password"},
        {6379, RiskLevel::Risk, "a database that often has no password"},
        {7547, RiskLevel::Warning, "remote control by the provider"},
        {9100, RiskLevel::Info, "anyone on the network can print"},
        {49152, RiskLevel::Warning, "UPnP: may let apps open router ports"},
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
