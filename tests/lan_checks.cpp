/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the LAN speed test's reading of iperf3 output (lan_speed.h):
// saved --json-stream output of iperf3 3.18 (tests/iperf3/, from tests on
// one machine, so the speeds are very high) as a client and as a server,
// its errors, output cut off at every byte, and hostile lines. Built on
// the Mac only, as lokictl_lan_tests, with the address and
// undefined-behaviour checkers; exits 1 when a check fails.

#include "lan_speed.h"

#include <cstdio>
#include <fstream>
#include <sstream>
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

std::string sample(const char* name) {
    std::ifstream file(std::string(IPERF3_SAMPLES_DIR) + "/" + name, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    expect(!text.str().empty(), std::string("sample ") + name + " is there");
    return text.str();
}

std::vector<model::IperfEvent> events_of(const std::string& bytes, std::size_t chunk) {
    model::IperfLines lines;
    std::vector<model::IperfEvent> events;
    for (std::size_t at = 0; at < bytes.size(); at += chunk) {
        lines.add(std::string_view(bytes).substr(at, chunk), events);
    }
    return events;
}

model::LanRun client_run(model::LanPhase phase) {
    model::LanRun run;
    run.state = model::ToolState::Running;
    run.mode = model::LanMode::Client;
    run.phase = phase;
    return run;
}

model::LanRun server_run() {
    model::LanRun run;
    run.state = model::ToolState::Running;
    run.mode = model::LanMode::Server;
    run.phase = model::LanPhase::Waiting;
    return run;
}

void feed_client(model::LanRun& run, const std::vector<model::IperfEvent>& events) {
    for (const model::IperfEvent& event : events) {
        model::lan_client_event(run, event);
    }
}

void feed_server(model::LanRun& run, const std::vector<model::IperfEvent>& events) {
    for (const model::IperfEvent& event : events) {
        model::lan_server_event(run, event);
    }
}

void check_chunks() {
    for (const char* name : {"client_download.jsonl", "client_upload.jsonl", "server_in.jsonl", "server_out.jsonl",
                             "server_both.jsonl", "client_refused.jsonl", "server_busy.jsonl"}) {
        const std::string bytes = sample(name);
        const std::size_t whole = events_of(bytes, bytes.size()).size();
        expect(whole >= 2, std::string(name) + ": events read");
        for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{4096}}) {
            expect(events_of(bytes, chunk).size() == whole,
                   std::string(name) + ": the same events in pieces of " + std::to_string(chunk));
        }
    }
}

void check_client() {
    model::LanRun run = client_run(model::LanPhase::Download);
    const std::vector<model::IperfEvent> down = events_of(sample("client_download.jsonl"), 4096);
    expect(down.front().kind == model::IperfEventKind::Start && down.front().reverse, "download: start, reverse");
    expect(down.back().kind == model::IperfEventKind::End, "download: ends with the end");
    feed_client(run, down);
    expect(run.state == model::ToolState::Running, "download: still running (the runner ends it)");
    expect(run.down_samples.size() == 4, "download: four half-second samples");
    expect(run.down_kbps == 123988509, "download: the receiver's speed");
    expect(run.down_bytes == 31016484864ULL, "download: the receiver's bytes");
    expect(run.up_kbps == model::kSpeedRateUnknown && run.up_samples.empty(), "download: up untouched");

    run.phase = model::LanPhase::Upload;
    const std::vector<model::IperfEvent> up = events_of(sample("client_upload.jsonl"), 4096);
    expect(!up.front().reverse, "upload: not reverse");
    feed_client(run, up);
    expect(run.up_samples.size() == 4, "upload: four samples");
    expect(run.up_kbps == 124879502, "upload: what reached the server");
    expect(run.up_bytes == 31399608320ULL, "upload: the bytes the server received");
    expect(run.down_kbps == 123988509, "upload: down kept");

    model::LanRun refused = client_run(model::LanPhase::Download);
    feed_client(refused, events_of(sample("client_refused.jsonl"), 4096));
    expect(refused.state == model::ToolState::Failed, "refused: fails");
    expect(refused.reason == "No iperf3 server there", "refused: plain words, not iperf3's: " + refused.reason);
    expect(refused.down_kbps == model::kSpeedRateUnknown, "refused: the empty end after the error changes nothing");
}

void check_server() {
    model::LanRun run = server_run();
    const std::vector<model::IperfEvent> in = events_of(sample("server_in.jsonl"), 4096);
    expect(in.front().peer == "127.0.0.1", "server in: the peer");
    model::lan_server_event(run, in.front());
    expect(run.phase == model::LanPhase::Testing && run.served.size() == 1 && !run.served[0].done,
           "server in: a test runs");
    feed_server(run, std::vector<model::IperfEvent>(in.begin() + 1, in.end()));
    expect(run.phase == model::LanPhase::Waiting, "server in: waits again");
    expect(run.tests == 1 && run.served[0].done, "server in: one test done");
    expect(!run.served[0].to_peer && !run.served[0].both, "server in: came in");
    expect(run.served[0].kbps == 124879502, "server in: what it received");

    feed_server(run, events_of(sample("server_out.jsonl"), 4096));
    expect(run.tests == 2 && run.served.size() == 2, "server out: the second test on top");
    expect(run.served[0].to_peer, "server out: went out");
    expect(run.served[0].kbps == 123986758, "server out: what it sent (received is 0 here)");
    expect(run.served[1].kbps == 124879502, "server out: the first test kept below");

    feed_server(run, events_of(sample("server_both.jsonl"), 4096));
    expect(run.served[0].both && run.served[0].kbps == 116672455 && run.served[0].back_kbps == 116646246,
           "server both: in and out");

    feed_server(run, events_of(sample("server_in.jsonl"), 4096));
    expect(run.served.size() == model::kLanServedMax && run.tests == 4, "server: the list keeps the newest three");

    const std::vector<model::IperfEvent> busy = events_of(sample("server_busy.jsonl"), 4096);
    expect(busy.front().kind == model::IperfEventKind::Error, "busy: an error");
    expect(model::lan_server_cannot_listen(busy.front().error), "busy: cannot listen");
    expect(model::lan_error_words(busy.front().error, model::LanMode::Server) == "Port 5201 is busy",
           "busy: plain words");
    const std::vector<model::IperfEvent> stray = events_of(sample("server_not_a_test.jsonl"), 4096);
    expect(stray.front().kind == model::IperfEventKind::Error && !model::lan_server_cannot_listen(stray.front().error),
           "a connection that is not a test: the server starts again");
    model::LanRun after = run;
    feed_server(after, stray);
    expect(after.tests == run.tests && after.served == run.served, "a connection that is not a test: no change");

    // The server program ends in the middle of a test.
    model::LanRun cut = server_run();
    model::lan_server_event(cut, in[0]);
    model::lan_server_exit(cut);
    expect(cut.served.empty() && cut.tests == 0, "cut before any number: dropped");
    model::lan_server_event(cut, in[0]);
    model::lan_server_event(cut, in[1]);
    model::lan_server_exit(cut);
    expect(cut.served.size() == 1 && cut.served[0].done && cut.tests == 1 && cut.served[0].kbps > 0,
           "cut after a number: kept as it stood");
}

void check_cut_off() {
    // Output that stops at any byte: only whole lines count, nothing breaks.
    for (const char* name : {"client_download.jsonl", "server_both.jsonl"}) {
        const std::string bytes = sample(name);
        const std::size_t whole = events_of(bytes, bytes.size()).size();
        for (std::size_t size = 0; size <= bytes.size(); ++size) {
            const std::string cut = bytes.substr(0, size);
            const std::vector<model::IperfEvent> events = events_of(cut, 64);
            std::size_t lines = 0;
            for (const char c : cut) {
                lines += c == '\n' ? 1 : 0;
            }
            if (events.size() != lines || events.size() > whole) {
                expect(false, std::string(name) + ": cut at " + std::to_string(size));
                break;
            }
            model::LanRun run = client_run(model::LanPhase::Download);
            feed_client(run, events);
            model::LanRun server = server_run();
            feed_server(server, events);
            model::lan_server_exit(server);
        }
    }
    // A cut line on its own is refused.
    const std::string first = sample("client_download.jsonl").substr(0, 300);
    model::IperfEvent event;
    expect(!model::read_iperf_event(first, event), "a cut line is refused");
}

void check_hostile() {
    model::IperfEvent event;
    expect(!model::read_iperf_event("", event), "empty");
    expect(!model::read_iperf_event("not json", event), "not JSON");
    expect(!model::read_iperf_event("{\"event\":\"start\"}", event), "no data");
    expect(!model::read_iperf_event("{\"event\":5,\"data\":{}}", event), "event not a string");
    expect(!model::read_iperf_event("{\"event\":\"end\",\"data\":{}} x", event), "trailing bytes");
    expect(!model::read_iperf_event("{\"event\":\"end\",\"data\":{},}", event), "trailing comma");
    expect(!model::read_iperf_event("{\"event\":\"error\",\"data\":\"a\x01\"}", event), "raw control character");
    expect(!model::read_iperf_event("{\"event\":\"error\",\"data\":\"\\q\"}", event), "bad escape");
    expect(!model::read_iperf_event("{\"event\":\"error\",\"data\":\"\\u12\"}", event), "short \\u");
    expect(!model::read_iperf_event("{\"event\":\"end\",\"data\":{\"a\":01}}", event), "leading zero");
    expect(!model::read_iperf_event("{\"event\":\"end\",\"data\":{\"a\":-}}", event), "a lone minus");
    expect(!model::read_iperf_event("{\"event\":\"end\",\"data\":{\"a\":1.}}", event), "a dot without digits");

    std::string deep = "{\"event\":\"interval\",\"data\":{\"sum\":";
    for (int index = 0; index < 5000; ++index) {
        deep += '[';
    }
    expect(!model::read_iperf_event(deep, event), "deep nesting is refused, not followed");

    expect(model::read_iperf_event("{\"event\":\"error\",\"data\":\"x\\u0000\\ud800\\u00e9\\\"\"}", event) &&
               event.kind == model::IperfEventKind::Error && event.error == std::string("x\0?\xc3\xa9\"", 6),
           "escapes are read");
    expect(model::read_iperf_event("{\"event\":\"warning\",\"data\":{}}", event) &&
               event.kind == model::IperfEventKind::None,
           "an unknown event is ignored");

    // Numbers out of range are clamped or dropped.
    expect(model::read_iperf_event("{\"event\":\"interval\",\"data\":{\"sum\":{\"seconds\":0.5,\"bytes\":-5,"
                                   "\"bits_per_second\":1e400}}}",
                                   event) &&
               event.bytes == 0 && event.bps == 0,
           "a negative count and an endless rate read as 0");
    expect(model::read_iperf_event("{\"event\":\"interval\",\"data\":{\"sum\":{\"seconds\":0.5,\"bytes\":1e30,"
                                   "\"bits_per_second\":1e30}}}",
                                   event) &&
               event.bytes == model::kIperfBytesMax && event.bps == model::kIperfBpsMax,
           "huge numbers are clamped");
    model::LanRun run = client_run(model::LanPhase::Download);
    model::lan_client_event(run, event);
    expect(run.down_kbps == 2147483647 && run.down_bytes == model::kIperfBytesMax, "a huge rate fits");
    for (int index = 0; index < 100; ++index) {
        model::lan_client_event(run, event);
    }
    expect(run.down_samples.size() == model::kLanSamples && run.down_bytes == model::kIperfBytesMax,
           "many samples: only the last ones are kept, bytes stay clamped");
    // A short last piece is no sample.
    model::IperfEvent tail;
    tail.kind = model::IperfEventKind::Interval;
    tail.seconds = 0.01;
    tail.bps = 1000;
    model::LanRun short_run = client_run(model::LanPhase::Upload);
    model::lan_client_event(short_run, tail);
    expect(short_run.up_samples.empty(), "a piece of 0.01 s is left out of the samples");

    // Peers: only plain IPv4.
    expect(model::read_iperf_event("{\"event\":\"start\",\"data\":{\"connected\":[{\"remote_host\":"
                                   "\"::ffff:10.0.0.5\"}],\"test_start\":{\"reverse\":1}}}",
                                   event) &&
               event.peer == "10.0.0.5" && event.reverse,
           "an IPv4 address in IPv6 form");
    expect(model::read_iperf_event("{\"event\":\"start\",\"data\":{\"connected\":[{\"remote_host\":"
                                   "\"<b>phone</b>\"}]}}",
                                   event) &&
               event.peer.empty(),
           "a peer that is not an address is not shown");
    expect(model::read_iperf_event("{\"event\":\"start\",\"data\":{\"connected\":\"x\",\"test_start\":[]}}", event) &&
               event.peer.empty() && !event.reverse,
           "wrong types are ignored");

    // A line longer than the limit is skipped, the next one read.
    model::IperfLines lines;
    std::vector<model::IperfEvent> events;
    lines.add("{\"event\":\"error\",\"data\":\"", events);
    const std::string filler(model::kIperfLineMax, 'a');
    lines.add(filler, events);
    lines.add("\"}\n{\"event\":\"error\",\"data\":\"refused\"}\n", events);
    expect(events.size() == 1 && events[0].error == "refused", "an over-long line is dropped, the next is read");
}

void check_words() {
    const char* errors[] = {
        "unable to connect to server - server may have stopped running or use a different port, firewall issue, "
        "etc.: Connection refused",
        "unable to connect to server - server may have stopped running or use a different port, firewall issue, "
        "etc.: Connection timed out",
        "unable to connect to server: No route to host",
        "the server is busy running a test. try again later",
        "error - unable to start listener for connections: Address already in use",
        "error - unable to start listener for connections: Cannot assign requested address",
        "control socket has closed unexpectedly",
        "something new",
        "",
    };
    for (const char* error : errors) {
        for (const model::LanMode mode : {model::LanMode::Client, model::LanMode::Server}) {
            const std::string words = model::lan_error_words(error, mode);
            expect(!words.empty() && words.size() <= 30, "short words for: " + std::string(error));
            expect(words.find("iperf3 -") == std::string::npos && words.find(':') == std::string::npos,
                   "never iperf3's own text: " + words);
        }
    }
    expect(model::lan_error_words(errors[1], model::LanMode::Client) == "No answer from there", "timed out");
    expect(model::lan_error_words(errors[3], model::LanMode::Client) == "Their server is busy", "busy");
    expect(model::lan_error_words(errors[5], model::LanMode::Server) == "That address is gone", "address gone");
    expect(model::lan_server_cannot_listen(errors[5]) && !model::lan_server_cannot_listen(errors[6]),
           "which server errors end the run");
}

void check_targets() {
    expect(model::valid_lan_target("192.168.1.20"), "a home address");
    expect(model::valid_lan_target("10.42.0.11"), "a hotspot address");
    expect(!model::valid_lan_target("8.8.8.8"), "not the internet");
    expect(!model::valid_lan_target("router.lan"), "not a name");
    expect(!model::valid_lan_target(""), "not empty");
}

} // namespace

int main() {
    check_chunks();
    check_client();
    check_server();
    check_cut_off();
    check_hostile();
    check_words();
    check_targets();
    if (failures > 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("lan checks passed\n");
    return 0;
}
