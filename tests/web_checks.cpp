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
#include "remote/remote_http.h"
#include "remote/remote_keys.h"
#include "remote/remote_server.h"
#include "report.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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
            expect(text.compare(at, 5, "url(/") == 0, name + ": url() only from the Cardputer");
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
    KeyQueue keys;
    ServerOptions options;
    options.loopback_only = true;
    options.port = 18780;
    options.keys = &keys;
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

    server.request_stop();
    server.join();
}

} // namespace

int main() {
    check_files();
    check_json();
    check_server();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("web checks passed\n");
    return 0;
}
