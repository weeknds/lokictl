/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the web dashboard (assets/web, platform/remote): no file names
// anything outside the Cardputer, the page has no inline script or style,
// every answer of a real server on 127.0.0.1 carries exactly one
// Content-Security-Policy, the files go out with their exact types, the
// API needs the session, a state-changing request needs the request token,
// and texts from the network are escaped in the status JSON.
// Built on the Mac only, as lokictl_web_tests; exits 1 when a check fails.

#include "dashboard_status.h"
#include "health_check.h"
#include "remote/remote_http.h"
#include "remote/remote_keys.h"
#include "remote/remote_server.h"
#include "report.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef WEB_DIR
#error "WEB_DIR must point at assets/web"
#endif
#ifndef FONTS_DIR
#error "FONTS_DIR must point at assets/fonts"
#endif

namespace {

using platform::remote::BlockQueue;
using platform::remote::BlockRequest;
using platform::remote::KeyQueue;
using platform::remote::Server;
using platform::remote::ServerOptions;
using platform::remote::ServerPhase;

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

std::size_t count_of(const std::string& text, const std::string& part) {
    std::size_t count = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        ++count;
    }
    return count;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// ---------- The files ----------

// The only full URLs a file may hold: XML namespace names, which name
// nothing to fetch.
bool allowed_url(const std::string& text, std::size_t scheme_at) {
    static const char* const kNamespaces[] = {
        "http://www.w3.org/2000/svg",
        "http://www.w3.org/1999/xhtml",
        "http://www.w3.org/1998/Math/MathML",
        "http://www.w3.org/1999/xlink",
    };
    for (const char* name : kNamespaces) {
        if (text.compare(scheme_at, std::string(name).size(), name) == 0) {
            return true;
        }
    }
    return false;
}

void check_files() {
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(WEB_DIR)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        ++files;
        const std::string name = entry.path().filename().string();
        const std::string text = read_file(entry.path());
        for (std::size_t at = text.find("://"); at != std::string::npos; at = text.find("://", at + 3)) {
            std::size_t start = at;
            while (start > 0 && std::isalpha(static_cast<unsigned char>(text[start - 1]))) {
                --start;
            }
            expect(allowed_url(text, start), name + ": no outside URL (" + text.substr(start, 40) + ")");
        }
        // Protocol-relative URLs ("//host/...") in quotes or url().
        for (const char* opener : {"\"//", "'//", "(//", "`//"}) {
            expect(!contains(text, opener), name + ": no protocol-relative URL");
        }
        for (std::size_t at = text.find("url("); at != std::string::npos; at = text.find("url(", at + 4)) {
            // url(/...) from the Cardputer, or url(#...) inside the page (an SVG gradient).
            expect(text.compare(at, 5, "url(/") == 0 || text.compare(at, 5, "url(#") == 0,
                   name + ": url() only from the Cardputer");
        }
    }
    expect(files >= 7, "the web folder holds the dashboard's files");

    const std::string page = read_file(std::filesystem::path(WEB_DIR) / "index.html");
    expect(!contains(page, "<style"), "index.html: no inline style element");
    expect(!contains(page, " style="), "index.html: no style attribute");
    expect(count_of(page, "<script") == 1 && contains(page, "<script type=\"module\" src=\"/app.js\"></script>"),
           "index.html: one script, from the Cardputer");
    bool handler = false;
    for (std::size_t at = page.find(" on"); at != std::string::npos; at = page.find(" on", at + 3)) {
        std::size_t end = at + 3;
        while (end < page.size() && std::islower(static_cast<unsigned char>(page[end]))) {
            ++end;
        }
        handler = handler || (end > at + 3 && end < page.size() && page[end] == '=');
    }
    expect(!handler, "index.html: no event handler attributes");
}

// ---------- The status JSON ----------

void check_json() {
    using model::json_text;
    expect(json_text("plain") == "\"plain\"", "json_text: plain text");
    expect(json_text("a\"b\\c") == "\"a\\\"b\\\\c\"", "json_text: quote and backslash");
    expect(json_text("<script>&") == "\"\\u003cscript\\u003e\\u0026\"", "json_text: < > &");
    expect(json_text(std::string("x\x01y\n", 4)) == "\"x y \"", "json_text: control characters");
    expect(json_text("\xff\xc3") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\"", "json_text: broken UTF-8");
    expect(json_text("caf\xC3\xA9") == "\"caf\xC3\xA9\"", "json_text: UTF-8 kept");

    model::HomeData home = model::sample_home_data();
    home.uplink_kind = model::LinkKind::Wifi;
    home.wifi_name = "\"},\"evil\":{\"x\":\"<script>";
    home.battery_percent = model::kBatteryPercentUnknown;
    const std::string json = model::dashboard_status_json(home);
    expect(contains(json, "\"name\":\"\\\"},\\\"evil\\\":{\\\"x\\\":\\\"\\u003cscript\\u003e\""),
           "status JSON: the Wi-Fi name stays one escaped string");
    expect(contains(json, "\"battery\":null"), "status JSON: an unknown battery is null");
    expect(contains(json, "\"v\":1") && contains(json, "\"sample\":true"), "status JSON: version and sample flag");
    expect(!json.empty() && json.back() == '\n' && json[json.size() - 2] == '}', "status JSON: one closed object");
}

// ---------- The findings ----------

bool counts_add_up(const model::DashCounts& counts) {
    return counts.device_fix + counts.device_look + counts.fine + counts.unchecked == counts.devices;
}

void check_findings() {
    // The made-up overview is the design's: 12 devices, 1 to fix, 2 worth
    // a look, 9 fine.
    const model::HomeData home = model::sample_home_data();
    const model::DashSample sample = model::sample_dashboard();
    const model::DashOverview overview = model::dashboard_overview(home, sample.tools, sample.watch);
    const model::DashCounts& counts = overview.counts;
    expect(counts.devices == 12 && counts.fix == 1 && counts.look == 2 && counts.fine == 9 && counts.unchecked == 0,
           "findings: the sample is 12 devices, 1 to fix, 2 worth a look, 9 fine");
    expect(counts_add_up(counts), "findings: the sample's device counts add up");
    expect(overview.checked && overview.checked_at == "07:44", "findings: the sample is checked");
    expect(!overview.things.empty() && overview.things[0].tier == model::FindingTier::Fix &&
               overview.things[0].title == "Smart TV has 2 security holes" &&
               overview.things[0].device == "10.0.0.43",
           "findings: the worst thing first, in plain words");
    expect(!overview.devices.empty() && overview.devices[0].tag == "2 holes", "findings: the worst device first, tagged");
    for (const model::DashDevice& device : overview.devices) {
        expect(device.address != home.uplink_address, "findings: the Cardputer is not one of the devices");
    }
    std::size_t look = 0;
    for (const model::DashThing& thing : overview.things) {
        look += thing.tier == model::FindingTier::Look ? 1 : 0;
    }
    expect(look == 2, "findings: one thing per subject (the NAS's alerts and its open port are one)");

    // Nothing checked: no device is fine, and the counts still add up.
    model::HomeData live = model::sample_home_data();
    live.sample = false;
    const model::DashOverview fresh = model::dashboard_overview(live, model::ToolsData(), model::WatchData());
    expect(!fresh.checked && fresh.counts.fine == 0 && fresh.counts.unchecked == 3 && counts_add_up(fresh.counts),
           "findings: unchecked hotspot devices are not fine");

    // Two hole findings on one device are one thing, its count the tag's;
    // a Wi-Fi without a password is a thing to fix but not a device.
    model::ToolsData holes = sample.tools;
    model::HealthFinding second = holes.health.findings[0];
    second.port = 8080;
    second.title = "3 known holes";
    holes.health.findings.push_back(second);
    model::HealthFinding wifi;
    wifi.issue = model::HealthIssue::OpenWifi;
    wifi.level = model::RiskLevel::Risk;
    wifi.title = "No Wi-Fi password";
    wifi.order = -1;
    holes.health.findings.push_back(wifi);
    model::sort_findings(holes.health.findings);
    const model::DashOverview both = model::dashboard_overview(home, holes, sample.watch);
    std::size_t tv_things = 0;
    for (const model::DashThing& thing : both.things) {
        tv_things += thing.device == "10.0.0.43" ? 1 : 0;
    }
    const auto tv = std::find_if(both.devices.begin(), both.devices.end(),
                                 [](const model::DashDevice& device) { return device.id == "10.0.0.43"; });
    const auto tv_thing = std::find_if(both.things.begin(), both.things.end(),
                                       [](const model::DashThing& thing) { return thing.device == "10.0.0.43"; });
    expect(tv_things == 1 && tv != both.devices.end() && tv->tag == "5 holes" && tv_thing != both.things.end() &&
               tv_thing->title == "Smart TV has 5 security holes",
           "findings: two hole findings on one device are one thing, counted as its tag");
    expect(both.counts.fix == 2 && both.counts.device_fix == 1 && counts_add_up(both.counts),
           "findings: the open Wi-Fi counts as a thing to fix, not as a device");

    // A hostile device name stays one escaped string.
    model::ToolsData tools = sample.tools;
    tools.health.devices[2].name = "\"}],\"evil\":\"<img>";
    model::DashboardParts parts;
    parts.overview = model::dashboard_overview(home, tools, sample.watch);
    const std::string json = model::dashboard_status_json(home, parts);
    expect(contains(json, "\"name\":\"\\\"}],\\\"evil\\\":\\\"\\u003cimg\\u003e\""),
           "status JSON: a device name stays one escaped string");
    expect(contains(json, "\"counts\":{\"devices\":12,\"security\":1}"), "status JSON: the sidebar's counts");
    expect(contains(json, "\"total\":12,") && count_of(json, "\"devices\":") == 2,
           "status JSON: the overview's count and list have their own names");
    expect(counts_add_up(parts.overview.counts), "findings: counts add up with a hostile name");

    // The Devices page's counts and a device's page.
    expect(counts.device_new == 2 && counts.hotspot == 3, "findings: the sample has 2 new devices and 3 on the hotspot");
    std::size_t listed_new = 0;
    std::size_t listed_hotspot = 0;
    for (const model::DashDevice& device : overview.devices) {
        listed_new += device.is_new ? 1 : 0;
        listed_hotspot += device.hotspot ? 1 : 0;
    }
    expect(listed_new == 2 && listed_hotspot == 3, "findings: the new and hotspot counts match the list");
    const model::DashDevice& smart_tv = overview.devices[0];
    const auto port_of = [](const model::DashDevice& device, uint16_t number) {
        return std::find_if(device.ports.begin(), device.ports.end(),
                            [number](const model::DashPort& port) { return port.port == number; });
    };
    const auto login = port_of(smart_tv, 22);
    const auto web = port_of(smart_tv, 8080);
    expect(login != smart_tv.ports.end() && login->name == "Remote login" && login->version == "Dropbear 2015.67" &&
               login->tag == "2 holes" && login->tier == model::FindingTier::Fix,
           "device page: the remote login, its version and its holes");
    expect(web != smart_tv.ports.end() && web->tag == "Old" && smart_tv.ports[0].port == 22,
           "device page: the old web page, the worst service first");
    expect(smart_tv.holes.size() == 2 && smart_tv.holes[0].cve == "CVE-2016-7406" && smart_tv.holes[0].score == 98 &&
               smart_tv.more_holes == 0 && smart_tv.holes[0].text == "A known hole in its remote login, Dropbear 2015.67.",
           "device page: the holes with their official numbers and scores");
    expect(smart_tv.issues.size() == 2 && smart_tv.issues[0].title == "2 security holes" &&
               smart_tv.issues[0].steps.size() == 3 && !smart_tv.issues[0].about.empty() &&
               smart_tv.issues[1].title == "Runs old software",
           "device page: the issues the worst first, with plain steps");
    const auto phone = std::find_if(overview.devices.begin(), overview.devices.end(),
                                    [](const model::DashDevice& device) { return device.id == "10.42.0.11"; });
    expect(phone != overview.devices.end() && phone->hotspot && !phone->blocked && phone->ports.size() == 1 &&
               phone->ports[0].name == "iPhone sync" && phone->issues.empty(),
           "device page: a fine hotspot device");

    // A newer Ports scan of the TV that finds nothing replaces the check's
    // holes; an older one changes nothing.
    model::ToolsData rescanned = sample.tools;
    rescanned.ports.state = model::ToolState::Done;
    rescanned.ports.target = "10.0.0.43";
    rescanned.ports.open = {8009};
    rescanned.latest_check = model::ToolKind::Ports;
    const model::DashOverview newer = model::dashboard_overview(home, rescanned, sample.watch);
    const auto tv_newer = std::find_if(newer.devices.begin(), newer.devices.end(),
                                       [](const model::DashDevice& device) { return device.id == "10.0.0.43"; });
    expect(tv_newer != newer.devices.end() && tv_newer->tier == model::FindingTier::Fine && tv_newer->holes.empty() &&
               tv_newer->ports.size() == 1 && newer.counts.fix == 0 && counts_add_up(newer.counts),
           "findings: a newer Ports scan replaces the check's findings about that device");
    rescanned.latest_check = model::ToolKind::Health;
    const model::DashOverview older = model::dashboard_overview(home, rescanned, sample.watch);
    expect(!older.devices.empty() && older.devices[0].id == "10.0.0.43" && older.devices[0].tag == "2 holes" &&
               older.devices[0].ports.size() == 3,
           "findings: an older Ports scan does not undo the check");

    // The page's parts in the status JSON, and a hotspot device blocked.
    model::HomeData blocked_home = home;
    blocked_home.clients[0].blocked = true;
    blocked_home.block_failures = 1;
    blocked_home.block_message = "Block failed: helper not installed";
    model::DashboardParts page_parts;
    page_parts.overview = model::dashboard_overview(blocked_home, sample.tools, sample.watch);
    const std::string page_json = model::dashboard_status_json(blocked_home, page_parts);
    expect(contains(page_json, "\"ports\":[{\"port\":22,\"name\":\"Remote login\",\"version\":\"Dropbear 2015.67\",\"tier\":\"fix\",\"tag\":\"2 holes\"}") &&
               contains(page_json, "\"cve\":\"CVE-2016-7406\",\"score\":98,\"used\":false") &&
               contains(page_json, "\"steps\":[\"") && contains(page_json, "\"device_new\":2,\"hotspot\":3,"),
           "status JSON: a device's services, holes, steps and the new counts");
    expect(contains(page_json, "\"hotspot\":true,\"blocked\":true") &&
               contains(page_json, "\"block\":{\"failures\":1,\"message\":\"Block failed: helper not installed\"}"),
           "status JSON: a blocked hotspot device and the last block failure");

    // The traffic log keeps the last minute.
    model::TrafficLog log;
    for (int32_t second = 0; second < 75; ++second) {
        log.push(second, -1);
    }
    expect(log.samples().size() == model::TrafficLog::kLength && log.samples().front()[0] == 15 &&
               log.samples().back()[1] == 0,
           "traffic log: the newest 60, unknown as 0");
}

// ---------- A real server ----------

class StillFrames : public platform::remote::FrameSource {
public:
    bool grab(std::vector<uint8_t>& bytes, platform::remote::PixelLayout& layout) override {
        layout = {};
        layout.width = 4;
        layout.height = 2;
        layout.stride = 8;
        layout.bits_per_pixel = 16;
        layout.red = {11, 5};
        layout.green = {5, 6};
        layout.blue = {0, 5};
        bytes.assign(16, 0x5A);
        return true;
    }
};

struct Answer {
    int status{0};
    std::string head;
    std::string body;

    std::string header(const std::string& name) const {
        const std::string key = "\r\n" + name + ": ";
        const std::size_t at = head.find(key);
        if (at == std::string::npos) {
            return {};
        }
        const std::size_t start = at + key.size();
        return head.substr(start, head.find("\r\n", start) - start);
    }
};

Answer ask(uint16_t port, const std::string& request) {
    Answer answer;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return answer;
    }
    timeval wait{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7F000001U);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return answer;
    }
    ::send(fd, request.data(), request.size(), 0);
    std::string all;
    char chunk[16384];
    for (;;) {
        const ssize_t got = ::recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0) {
            break;
        }
        all.append(chunk, static_cast<std::size_t>(got));
    }
    ::close(fd);
    const std::size_t end = all.find("\r\n\r\n");
    if (all.size() < 12 || end == std::string::npos) {
        return answer;
    }
    answer.status = std::atoi(all.c_str() + 9);
    answer.head = all.substr(0, end + 2);
    answer.body = all.substr(end + 4);
    return answer;
}

std::string get(const std::string& host, const std::string& path, const std::string& extra = {}) {
    return "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra + "\r\n";
}

std::string post(const std::string& host, const std::string& path, const std::string& body, const std::string& extra = {}) {
    return "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra +
           "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}

// Exactly one Content-Security-Policy, and the dashboard's unless `own`.
void expect_policy(const Answer& answer, const std::string& what, bool own = false) {
    expect(count_of(answer.head, "\r\nContent-Security-Policy: ") == 1, what + ": exactly one policy");
    const std::string policy = answer.header("Content-Security-Policy");
    if (!own) {
        expect(policy == std::string(platform::remote::default_security_policy()), what + ": the dashboard's policy");
        expect(contains(policy, "default-src 'self'") && !contains(policy, "unsafe") && !contains(policy, "http"),
               what + ": strict policy");
    }
    expect(contains(answer.head, "\r\nX-Content-Type-Options: nosniff"), what + ": nosniff");
}

void check_server() {
    using platform::remote::dotted_ipv4;
    expect(dotted_ipv4("10.42.0.11") && dotted_ipv4("255.255.255.255") && dotted_ipv4("0.0.0.0"),
           "dotted_ipv4: plain addresses");
    expect(!dotted_ipv4("10.42.0") && !dotted_ipv4("10.42.0.256") && !dotted_ipv4("10.042.0.1") &&
               !dotted_ipv4("10.42.0.1.") && !dotted_ipv4("a.b.c.d") && !dotted_ipv4("") && !dotted_ipv4("10.42.0.1%3A"),
           "dotted_ipv4: anything else");

    KeyQueue keys;
    BlockQueue blocks;
    ServerOptions options;
    options.loopback_only = true;
    options.port = 18780;
    options.keys = &keys;
    options.blocks = &blocks;
    options.web_folder = WEB_DIR;
    options.font_folder = FONTS_DIR;
    Server server(options, std::make_unique<StillFrames>());
    server.start();
    for (int tries = 0; tries < 100 && server.status().phase == ServerPhase::Starting; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (server.status().phase != ServerPhase::Listening) {
        expect(false, "the test server listens on 127.0.0.1:18780");
        return;
    }
    const std::string code = server.status().code;
    const uint16_t port = options.port;
    const std::string host = "127.0.0.1:18780";
    const std::string origin = "Origin: http://" + host + "\r\n";

    struct File {
        const char* path;
        const char* type;
    };
    const File files[] = {
        {"/", "text/html; charset=utf-8"},
        {"/app.css", "text/css; charset=utf-8"},
        {"/app.js", "text/javascript; charset=utf-8"},
        {"/pair.js", "text/javascript; charset=utf-8"},
        {"/shell.js", "text/javascript; charset=utf-8"},
        {"/ui.js", "text/javascript; charset=utf-8"},
        {"/home.js", "text/javascript; charset=utf-8"},
        {"/devices.js", "text/javascript; charset=utf-8"},
        {"/preact.module.js", "text/javascript; charset=utf-8"},
        {"/icon.svg", "image/svg+xml"},
        {"/fonts/Inter-Regular.ttf", "font/ttf"},
        {"/fonts/Inter-SemiBold.ttf", "font/ttf"},
    };
    for (const File& file : files) {
        const Answer answer = ask(port, get(host, file.path));
        const std::string what = std::string("GET ") + file.path;
        expect(answer.status == 200, what + ": 200");
        expect(answer.header("Content-Type") == file.type, what + ": exact type");
        expect(answer.header("Content-Length") == std::to_string(answer.body.size()) && !answer.body.empty(),
               what + ": the whole file");
        expect_policy(answer, what);
        const bool font = contains(file.path, "/fonts/");
        expect(answer.header("Cache-Control") == (font ? "private, max-age=604800" : "no-store"), what + ": caching");
    }
    expect(ask(port, get(host, "/")).body == read_file(std::filesystem::path(WEB_DIR) / "index.html"),
           "GET /: index.html as it is");

    // Answers that are not files carry the policy too.
    const Answer missing = ask(port, get(host, "/../app.js"));
    expect(missing.status == 404, "a path outside the table: 404");
    expect_policy(missing, "404");
    expect_policy(ask(port, get(host, "/nothing.js")), "404 for an unknown file");
    const Answer wrong_method = ask(port, post(host, "/", "x=1"));
    expect(wrong_method.status == 405, "POST /: 405");
    expect_policy(wrong_method, "405");
    const Answer wrong_host = ask(port, get("evil.example:18780", "/"));
    expect(wrong_host.status == 421, "another Host: 421");
    expect_policy(wrong_host, "421");
    const Answer bad = ask(port, get(host, "/", "X-LokiCTL-Token: a\r\nX-LokiCTL-Token: b\r\n"));
    expect(bad.status == 400, "a repeated token header: 400");
    expect_policy(bad, "400");
    for (const char* path : {"/api/v1/session", "/api/v1/status", "/reports", "/screen.png"}) {
        const Answer answer = ask(port, get(host, path));
        expect(answer.status == 401, std::string(path) + " without a session: 401");
        expect_policy(answer, std::string(path) + " 401");
    }
    const Answer screen = ask(port, get(host, "/screen"));
    expect(screen.status == 200 && contains(screen.body, "X-LokiCTL-Token"), "GET /screen: the screen page, sending the token");
    expect_policy(screen, "GET /screen", true);
    expect(contains(screen.header("Content-Security-Policy"), "frame-ancestors 'none'"), "GET /screen: its own policy");

    // Pair, then the session and the status.
    const Answer paired = ask(port, post(host, "/pair", "code=" + code));
    expect(paired.status == 200, "POST /pair with the code: 200");
    expect_policy(paired, "POST /pair");
    const std::string set_cookie = paired.header("Set-Cookie");
    expect(contains(set_cookie, "HttpOnly") && contains(set_cookie, "SameSite=Strict"), "the session cookie is HttpOnly and Strict");
    const std::string cookie = "Cookie: " + set_cookie.substr(0, set_cookie.find(';')) + "\r\n";

    const Answer session = ask(port, get(host, "/api/v1/session", cookie));
    expect(session.status == 200 && session.header("Content-Type") == "application/json; charset=utf-8",
           "GET /api/v1/session with the cookie: JSON");
    expect_policy(session, "GET /api/v1/session");
    const std::size_t start = session.body.find("\"token\":\"");
    const std::string token = start == std::string::npos ? std::string() : session.body.substr(start + 9, 32);
    expect(token.size() == 32 && !contains(set_cookie, token), "the request token is its own, not the cookie's");

    expect(ask(port, get(host, "/api/v1/status", cookie)).status == 503, "GET /api/v1/status before the app's JSON: 503");
    server.set_status_json("{\"v\":1}\n");
    const Answer status = ask(port, get(host, "/api/v1/status", cookie));
    expect(status.status == 200 && status.body == "{\"v\":1}\n", "GET /api/v1/status: the app's JSON");
    expect_policy(status, "GET /api/v1/status");

    // A key: Origin, session and request token.
    const std::string key = "k=c54";
    const Answer no_origin = ask(port, post(host, "/key", key, cookie + "X-LokiCTL-Token: " + token + "\r\n"));
    expect(no_origin.status == 403, "POST /key without Origin: 403");
    expect_policy(no_origin, "POST /key 403");
    expect(ask(port, post(host, "/key", key, origin + "X-LokiCTL-Token: " + token + "\r\n")).status == 401,
           "POST /key without the cookie: 401");
    expect(ask(port, post(host, "/key", key, origin + cookie)).status == 403, "POST /key without the token: 403");
    expect(ask(port, post(host, "/key", key, origin + cookie + "X-LokiCTL-Token: " + std::string(32, '0') + "\r\n"))
                   .status == 403,
           "POST /key with a wrong token: 403");
    const Answer pressed = ask(port, post(host, "/key", key, origin + cookie + "X-LokiCTL-Token: " + token + "\r\n"));
    expect(pressed.status == 204, "POST /key with Origin, cookie and token: 204");
    expect_policy(pressed, "POST /key 204");
    uint32_t pressed_key = 0;
    expect(keys.pop(pressed_key) && !keys.pop(pressed_key), "exactly one key was queued");

    // Block and Allow: Origin, session and request token, as a key.
    const std::string with_token = "X-LokiCTL-Token: " + token + "\r\n";
    const std::string block = "id=10.42.0.11&on=1";
    const Answer block_no_origin = ask(port, post(host, "/api/v1/block", block, cookie + with_token));
    expect(block_no_origin.status == 403, "POST /api/v1/block without Origin: 403");
    expect_policy(block_no_origin, "POST /api/v1/block 403");
    expect(ask(port, post(host, "/api/v1/block", block, origin + with_token)).status == 401,
           "POST /api/v1/block without the cookie: 401");
    expect(ask(port, post(host, "/api/v1/block", block, origin + cookie)).status == 403,
           "POST /api/v1/block without the token: 403");
    expect(ask(port, post(host, "/api/v1/block", "id=10.42.0.x&on=1", origin + cookie + with_token)).status == 400,
           "POST /api/v1/block with a bad id: 400");
    expect(ask(port, post(host, "/api/v1/block", "id=10.42.0.11&on=2", origin + cookie + with_token)).status == 400,
           "POST /api/v1/block with a bad on: 400");
    expect(ask(port, get(host, "/api/v1/block", cookie)).status == 405, "GET /api/v1/block: 405");
    const Answer blocked = ask(port, post(host, "/api/v1/block", block, origin + cookie + with_token));
    expect(blocked.status == 204, "POST /api/v1/block with Origin, cookie and token: 204");
    expect_policy(blocked, "POST /api/v1/block 204");
    BlockRequest asked;
    expect(blocks.pop(asked) && asked.address == "10.42.0.11" && asked.block && !blocks.pop(asked),
           "exactly one block request was queued, as asked");
    for (std::size_t index = 0; index < BlockQueue::kMaxWaiting; ++index) {
        ask(port, post(host, "/api/v1/block", "id=10.42.0.11&on=0", origin + cookie + with_token));
    }
    expect(ask(port, post(host, "/api/v1/block", block, origin + cookie + with_token)).status == 429,
           "POST /api/v1/block with a full queue: 429");
    blocks.clear();

    server.request_stop();
    server.join();
}

} // namespace

int main() {
    check_files();
    check_json();
    check_findings();
    check_server();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("web checks passed\n");
    return 0;
}
