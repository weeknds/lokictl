/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the Wi-Fi sign-in check (signin_check.h, signin_fetch.h): the
// reading of answers and of redirect addresses, which address the QR code
// holds, and the fetch itself against a small test server on 127.0.0.1
// that answers like an open internet, a sign-in redirect, a network's own
// page, nothing at all, or too slowly; and when LokiCTL's own check after
// joining a Wi-Fi network is due. Built on the Mac only, as
// lokictl_signin_tests; exits 1 when a check fails.
//
// With a port as its argument it only fetches from 127.0.0.1:<port> (any
// local test server) and prints what it read.

#include "signin_check.h"
#include "signin_fetch.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

model::SigninRun judged(const std::string& head) {
    model::SigninRun run;
    model::judge_signin(run, model::read_signin_reply(head));
    return run;
}

void check_answers() {
    model::SigninRun run = judged("HTTP/1.1 204 No Content\r\nServer: x\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Clear && run.status == 204, "204 means no sign-in");

    run = judged("HTTP/1.1 302 Found\r\nContent-Type: text/html\r\nLocation: http://portal.example.net/login?ap=1\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Needed, "302 means a sign-in");
    expect(run.target == "http://portal.example.net/login?ap=1", "302 gives its Location");

    run = judged("HTTP/1.0 307 Temporary Redirect\nlocation:   HTTPS://Portal.Example.net/a  \n\n");
    expect(run.target == "https://Portal.Example.net/a", "a lowercase header name, LF lines, spaces, scheme case");

    run = judged("HTTP/1.1 302 Found\r\nLocation: /login\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Needed && run.target.empty(), "a relative Location is not shown");

    run = judged("HTTP/1.1 302 Found\r\nLocation: javascript:alert(1)\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Needed && run.target.empty(), "only http and https are shown");

    run = judged("HTTP/1.1 302 Found\r\nLocation: http://user@evil.example/\r\n\r\n");
    expect(run.target.empty(), "a user name in the address is not shown");

    run = judged("HTTP/1.1 302 Found\r\nLocation: http://portal.example.net/a b\r\n\r\n");
    expect(run.target.empty(), "a space in the address is not shown");

    run = judged("HTTP/1.1 302 Found\r\nLocation: http://portal.example.net/\x1b[2J\r\n\r\n");
    expect(run.target.empty(), "a control character in the address is not shown");

    run = judged("HTTP/1.1 302 Found\r\nLocation: http:///nohost\r\n\r\n");
    expect(run.target.empty(), "an address without a host is not shown");

    const std::string long_address = "http://portal.example.net/" + std::string(600, 'a');
    model::SigninReply reply = model::read_signin_reply("HTTP/1.1 302 Found\r\nLocation: " + long_address + "\r\n\r\n");
    expect(reply.location.empty() && reply.location_too_long, "a Location over the limit is dropped");

    run = judged("HTTP/1.1 302 Found\r\nX: 1\r\n\r\nLocation: http://body.example/\r\n");
    expect(run.target.empty(), "a Location after the head is not read");

    run = judged("HTTP/1.1 511 Network Authentication Required\r\nLocation: http://portal.example.net/\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Needed && !run.target.empty(), "511 means a sign-in");

    run = judged("HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\n<html>");
    expect(run.verdict == model::SigninVerdict::Needed && run.target.empty(), "200 means the network's own page");

    run = judged("HTTP/1.1 404 Not Found\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Unclear && run.status == 404, "404 is unclear");

    run = judged("SSH-2.0-OpenSSH_9.6\r\n");
    expect(run.verdict == model::SigninVerdict::Unclear && run.status == 0, "not HTTP is unclear");

    run = judged("HTTP/1.1 2040 Odd\r\n\r\n");
    expect(run.verdict == model::SigninVerdict::Unclear, "a four-digit status is not read");

    run = judged("HTTP/1.1 302 Found\r\nLocation: http://cut.example/");
    expect(run.target == "http://cut.example/", "a head cut short is still read");
}

void check_code_and_host() {
    model::SigninRun run;
    run.verdict = model::SigninVerdict::Clear;
    expect(model::signin_code_text(run).empty(), "no code without a sign-in");

    run.verdict = model::SigninVerdict::Needed;
    run.target = "http://portal.example.net/login";
    expect(model::signin_code_text(run) == run.target, "the code holds the sign-in address");

    run.target = "http://portal.example.net/" + std::string(model::kSigninCodeMax, 'a');
    expect(model::signin_code_text(run) == model::kSigninCheckUrl, "a long address gives the check page's code");

    run.target.clear();
    expect(model::signin_code_text(run) == model::kSigninCheckUrl, "no address gives the check page's code");

    expect(model::signin_host("http://portal.example.net:8080/x?y") == "portal.example.net", "host without port");
    expect(model::signin_host("https://[2001:db8::1]:8443/a") == "[2001:db8::1]", "IPv6 host");
    expect(model::signin_host("/relative").empty(), "no host in a relative address");
}

// A test server on 127.0.0.1 that answers each connection with the next
// canned answer: empty closes at once, "SLOW" never answers.
class TestServer {
public:
    TestServer() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(address);
        if (fd_ < 0 || ::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(fd_, 4) != 0 || ::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            std::printf("FAIL the test server could not start\n");
            std::exit(1);
        }
        port_ = ntohs(address.sin_port);
    }
    ~TestServer() {
        ::close(fd_);
    }

    // Serves one connection with `answer` on a thread; the request it got
    // is kept in request().
    void serve(std::string answer) {
        if (thread_.joinable()) {
            thread_.join();
        }
        request_.clear();
        thread_ = std::thread([this, answer = std::move(answer)] {
            const int client = ::accept(fd_, nullptr, nullptr);
            if (client < 0) {
                return;
            }
            char buffer[2048];
            const ssize_t size = ::recv(client, buffer, sizeof(buffer), 0);
            if (size > 0) {
                request_.assign(buffer, static_cast<std::size_t>(size));
            }
            if (answer == "SLOW") {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
            } else if (!answer.empty()) {
                ::send(client, answer.data(), answer.size(), 0);
            }
            ::close(client);
        });
    }
    void finish() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    uint16_t port() const {
        return port_;
    }
    const std::string& request() const {
        return request_;
    }

private:
    int fd_{-1};
    uint16_t port_{0};
    std::thread thread_;
    std::string request_;
};

sockaddr_in local_server(uint16_t port) {
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return server;
}

std::string fetch(uint16_t port, std::string& head, const std::function<bool()>& stopping = [] { return false; }) {
    return platform::fetch_signin_head(local_server(port), model::kSigninCheckHost, model::kSigninCheckPath,
                                       stopping, head);
}

void check_fetch() {
    TestServer server;
    std::string head;

    // A sign-in redirect with a body that must not be read.
    server.serve("HTTP/1.1 302 Moved\r\nLocation: http://portal.example.net/login?ap=lobby\r\nContent-Length: 9000\r\n\r\n" +
                 std::string(9000, 'x'));
    std::string reason = fetch(server.port(), head);
    server.finish();
    model::SigninRun run;
    model::judge_signin(run, model::read_signin_reply(head));
    expect(reason.empty() && run.verdict == model::SigninVerdict::Needed &&
               run.target == "http://portal.example.net/login?ap=lobby",
           "the test server's redirect is read");
    expect(head.size() <= model::kSigninHeadMax, "no more than the head limit is read");
    expect(server.request().rfind("GET /generate_204 HTTP/1.1\r\n", 0) == 0 &&
               server.request().find("\r\nHost: cp.cloudflare.com\r\n") != std::string::npos,
           "the request is a GET of the check page with its Host");

    // An open internet.
    server.serve("HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n");
    reason = fetch(server.port(), head);
    server.finish();
    model::judge_signin(run, model::read_signin_reply(head));
    expect(reason.empty() && run.verdict == model::SigninVerdict::Clear, "the test server's 204 is read");

    // The network's own page.
    server.serve("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html>Welcome</html>");
    reason = fetch(server.port(), head);
    server.finish();
    model::judge_signin(run, model::read_signin_reply(head));
    expect(reason.empty() && run.verdict == model::SigninVerdict::Needed && run.target.empty(),
           "the test server's own page is read");

    // A head with no end: the read stops at the limit.
    server.serve("HTTP/1.1 302 Found\r\nX-Pad: " + std::string(8000, 'p'));
    reason = fetch(server.port(), head);
    server.finish();
    expect(reason.empty() && head.size() == model::kSigninHeadMax, "an endless head stops at the limit");

    // Closed without an answer.
    server.serve("");
    reason = fetch(server.port(), head);
    server.finish();
    expect(reason == "no answer", "a closed connection gives no answer");

    // A stop while the server is silent.
    server.serve("SLOW");
    const auto started = std::chrono::steady_clock::now();
    reason = fetch(server.port(), head, [started] {
        return std::chrono::steady_clock::now() - started > std::chrono::milliseconds(200);
    });
    const auto took = std::chrono::steady_clock::now() - started;
    server.finish();
    expect(reason == "stopped" && took < std::chrono::milliseconds(450), "a stop ends the fetch quickly");

    // Nothing listens on port 1.
    reason = fetch(1, head);
    expect(reason == "connection refused", "a closed port is refused");
}

int fetch_only(const char* port_text) {
    const int port = std::atoi(port_text);
    if (port <= 0 || port > 65535) {
        std::printf("usage: lokictl_signin_tests [port]\n");
        return 2;
    }
    std::string head;
    const std::string reason = fetch(static_cast<uint16_t>(port), head);
    if (!reason.empty()) {
        std::printf("no answer: %s\n", reason.c_str());
        return 1;
    }
    model::SigninRun run;
    model::judge_signin(run, model::read_signin_reply(head));
    const char* verdict = run.verdict == model::SigninVerdict::Clear    ? "no sign-in needed"
                          : run.verdict == model::SigninVerdict::Needed ? "sign-in needed"
                                                                        : "unclear";
    std::printf("status=%d verdict=%s target=%s code=%s\n", run.status, verdict,
                run.target.empty() ? "-" : run.target.c_str(), model::signin_code_text(run).c_str());
    return 0;
}

} // namespace

// When the own check after joining a Wi-Fi network is due.
void check_due() {
    model::HomeData home;
    home.uplink_kind = model::LinkKind::Wifi;
    home.uplink_name = "wlan0";
    home.uplink_address = "192.0.2.10";
    home.wifi_name = "Harbour-Cafe";
    home.internet_check = false;
    std::string last;
    expect(model::signin_check_due(home, last) && last == "Harbour-Cafe", "joining counts, the first reading too");
    expect(!model::signin_check_due(home, last), "once per network");
    home.wifi_name = "Hotel-Lobby";
    expect(model::signin_check_due(home, last) && last == "Hotel-Lobby", "another network checks again");
    home.uplink_kind = model::LinkKind::Cable;
    home.wifi_name.clear();
    expect(!model::signin_check_due(home, last) && last.empty(), "the cable never checks and resets");
    home.uplink_kind = model::LinkKind::Wifi;
    home.wifi_name = "Hotel-Lobby";
    expect(model::signin_check_due(home, last), "joining the same network again checks again");
    home.uplink_address.clear();
    expect(!model::signin_check_due(home, last) && last.empty(), "no address yet: not joined");
    home.uplink_address = "192.0.2.10";
    home.internet_check = true;
    expect(!model::signin_check_due(home, last) && last.empty(), "NetworkManager's own check on: no own check");
    home.internet_check = false;
    expect(model::signin_check_due(home, last), "its check found off later: checks then");
    home.wifi_name.clear();
    expect(!model::signin_check_due(home, last) && last.empty(), "no name: not joined");
}

int main(int argc, char** argv) {
    if (argc > 1) {
        return fetch_only(argv[1]);
    }
    check_answers();
    check_code_and_host();
    check_fetch();
    check_due();
    if (failures == 0) {
        std::printf("signin checks: all checks passed\n");
        return 0;
    }
    return 1;
}
