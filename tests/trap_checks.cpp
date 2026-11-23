/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the trap's model (trap_data.h): the limits on what is logged,
// how what a connection sent is shown, the reading of the listening ports
// and the fixed port map. Built on the Mac only, as lokictl_trap_tests;
// exits 1 when a check fails.

#include "trap_data.h"

#include <cstdint>
#include <cstdio>
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

std::string shown(const std::string& bytes) {
    return model::trap_shown_text(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

void check_limits() {
    model::TrapLimiter limiter;
    // Five a minute from one source, the sixth refused.
    int allowed = 0;
    for (int index = 0; index < 6; ++index) {
        allowed += limiter.allow(1, index * 100) ? 1 : 0;
    }
    expect(allowed == 5, "one source: 5 a minute");
    // Twenty a minute in all, from many sources.
    model::TrapLimiter many;
    allowed = 0;
    for (uint32_t source = 0; source < 40; ++source) {
        allowed += many.allow(source + 10, 1000 + source) ? 1 : 0;
    }
    expect(allowed == 20, "many sources: 20 a minute");
    // A minute later there is room again.
    expect(many.allow(99, 1000 + 60000), "room again after a minute");
    expect(limiter.allow(1, 60000), "one source again after a minute");
    // A refused one does not count.
    model::TrapLimiter steady;
    for (int index = 0; index < 5; ++index) {
        steady.allow(7, 0);
    }
    for (int index = 0; index < 100; ++index) {
        steady.allow(7, 30000);
    }
    expect(steady.allow(7, 60000), "refused ones are not counted");
}

void check_shown_text() {
    expect(shown("GET / HTTP/1.1\r\nHost: a\r\n\r\n") == "GET / HTTP/1.1 Host: a", "line ends become one space");
    expect(shown("  \t root\r\n") == "root", "no space at either end");
    expect(shown(std::string("\x00\xffSMB", 5)) == "..SMB", "binary bytes become dots");
    expect(shown("<b>#ff0000 x#</b>") == "<b>#ff0000 x#</b>", "printable text stays as it is");
    const std::string long_text(200, 'A');
    expect(shown(long_text).size() == model::kTrapShownMax, "at most 60 characters");
    expect(model::trap_shown_text(nullptr, 10).empty(), "no bytes, no text");
    expect(shown("").empty(), "empty stays empty");
}

void check_listening_ports() {
    const std::string tcp =
        "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
        "   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1 1 0 100 0 0 10 0\n"
        "   1: 0100007F:0050 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 2 1 0 100 0 0 10 0\n"
        "   2: 2C01A8C0:0016 0101A8C0:D2F0 01 00000000:00000000 02:000A7B5D 00000000     0        0 3 4 0 20 4 30 10 -1\n"
        "   3: 00000000:2248 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 4 1 0 100 0 0 10 0\n"
        "garbage line\n";
    const std::vector<uint16_t> ports = model::parse_listening_ports(tcp);
    expect(ports == std::vector<uint16_t>({22, 80, 8776}), "listening ports only, sorted");
    const std::string tcp6 =
        "  sl  local_address                         remote_address                        st tx_queue rx_queue\n"
        "   0: 00000000000000000000000000000000:0015 00000000000000000000000000000000:0000 0A 00000000:00000000 00\n"
        "   1: 00000000000000000000000000000000:0015 00000000000000000000000000000000:0000 0A 00000000:00000000 00\n";
    expect(model::parse_listening_ports(tcp6) == std::vector<uint16_t>({21}), "IPv6 lines, each port once");
    expect(model::parse_listening_ports("").empty(), "no text, no ports");
    expect(model::parse_listening_ports("heads only\n").empty(), "only the column names");
}

void check_services() {
    const auto& services = model::trap_services();
    expect(services.size() == 6, "six fake services");
    for (const model::TrapService& service : services) {
        expect(service.port != 22 && service.listen_port != 22, "never the real SSH");
        expect(service.listen_port != 8780 && service.port != 8780, "never Remote view's port");
        expect(service.port < 1024 ? service.listen_port > 1024 && service.listen_port < 32768
                                   : service.listen_port == service.port,
               "low ports go to a fixed high port below the dynamic range");
        expect(std::string("tried ").size() + std::string(service.word).size() <= 13, "the row's word fits");
    }
    expect(model::trap_service(30080) && model::trap_service(30080)->port == 80, "found by its listen port");
    expect(model::trap_service(22) == nullptr, "no service on 22");
}

} // namespace

int main() {
    check_limits();
    check_shown_text();
    check_listening_ports();
    check_services();
    if (failures > 0) {
        std::printf("%d trap check(s) failed\n", failures);
        return 1;
    }
    std::printf("all trap checks passed\n");
    return 0;
}
