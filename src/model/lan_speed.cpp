/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "lan_speed.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace model {

bool operator==(const LanServed& left, const LanServed& right) {
    return left.peer == right.peer && left.to_peer == right.to_peer && left.both == right.both &&
           left.kbps == right.kbps && left.back_kbps == right.back_kbps && left.bytes == right.bytes &&
           left.done == right.done;
}

bool operator!=(const LanServed& left, const LanServed& right) {
    return !(left == right);
}

bool operator==(const LanPlace& left, const LanPlace& right) {
    return left.address == right.address && left.label == right.label;
}

bool operator!=(const LanPlace& left, const LanPlace& right) {
    return !(left == right);
}

bool operator==(const LanRun& left, const LanRun& right) {
    return left.state == right.state && left.mode == right.mode && left.reason == right.reason &&
           left.target == right.target && left.phase == right.phase && left.phase_ms == right.phase_ms &&
           left.down_bytes == right.down_bytes && left.up_bytes == right.up_bytes &&
           left.down_kbps == right.down_kbps && left.up_kbps == right.up_kbps &&
           left.down_samples == right.down_samples && left.up_samples == right.up_samples &&
           left.served == right.served && left.tests == right.tests && left.places == right.places;
}

bool operator!=(const LanRun& left, const LanRun& right) {
    return !(left == right);
}

bool valid_lan_target(std::string_view text) {
    return port_scan_address(text);
}

namespace {

// ---------------------------------------------------------------------------
// A small JSON reader: enough for iperf3's lines, strict about the grammar,
// with a depth limit, so a hostile line (the server's peer shapes part of
// the output) can only be refused, never crash or grow without bound.

struct JsonValue {
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    bool boolean{false};
    double number{0};
    std::string text;               // String
    std::vector<JsonValue> items;   // Array items, or Object values
    std::vector<std::string> keys;  // Object keys, one per item

    const JsonValue* member(std::string_view key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        for (std::size_t index = 0; index < keys.size() && index < items.size(); ++index) {
            if (keys[index] == key) {
                return &items[index];
            }
        }
        return nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    bool read(JsonValue& out) {
        skip_space();
        if (!value(out, 0)) {
            return false;
        }
        skip_space();
        return pos_ == text_.size();
    }

private:
    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skip_space() {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n')) {
            ++pos_;
        }
    }

    bool word(std::string_view expected) {
        if (text_.substr(pos_, expected.size()) != expected) {
            return false;
        }
        pos_ += expected.size();
        return true;
    }

    bool value(JsonValue& out, int32_t depth) {
        if (depth > kIperfDepthMax || at_end()) {
            return false;
        }
        switch (peek()) {
            case '{':
                return object(out, depth);
            case '[':
                return array(out, depth);
            case '"':
                out.type = JsonValue::Type::String;
                return string(out.text);
            case 't':
                out.type = JsonValue::Type::Bool;
                out.boolean = true;
                return word("true");
            case 'f':
                out.type = JsonValue::Type::Bool;
                out.boolean = false;
                return word("false");
            case 'n':
                out.type = JsonValue::Type::Null;
                return word("null");
            default:
                out.type = JsonValue::Type::Number;
                return number(out.number);
        }
    }

    bool object(JsonValue& out, int32_t depth) {
        out.type = JsonValue::Type::Object;
        ++pos_;  // {
        skip_space();
        if (!at_end() && peek() == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_space();
            if (at_end() || peek() != '"') {
                return false;
            }
            std::string key;
            if (!string(key)) {
                return false;
            }
            skip_space();
            if (at_end() || peek() != ':') {
                return false;
            }
            ++pos_;
            skip_space();
            JsonValue item;
            if (!value(item, depth + 1)) {
                return false;
            }
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(item));
            skip_space();
            if (at_end()) {
                return false;
            }
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool array(JsonValue& out, int32_t depth) {
        out.type = JsonValue::Type::Array;
        ++pos_;  // [
        skip_space();
        if (!at_end() && peek() == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_space();
            JsonValue item;
            if (!value(item, depth + 1)) {
                return false;
            }
            out.items.push_back(std::move(item));
            skip_space();
            if (at_end()) {
                return false;
            }
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    static int hex_digit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    static void append_utf8(std::string& out, uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool string(std::string& out) {
        ++pos_;  // "
        while (!at_end()) {
            const char c = peek();
            ++pos_;
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;  // a raw control character is not JSON
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_end()) {
                return false;
            }
            const char escape = peek();
            ++pos_;
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    out += escape;
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    if (text_.size() - pos_ < 4) {
                        return false;
                    }
                    uint32_t code = 0;
                    for (int index = 0; index < 4; ++index) {
                        const int digit = hex_digit(text_[pos_ + static_cast<std::size_t>(index)]);
                        if (digit < 0) {
                            return false;
                        }
                        code = code * 16 + static_cast<uint32_t>(digit);
                    }
                    pos_ += 4;
                    // Half of a surrogate pair stands alone here: '?'.
                    append_utf8(out, (code >= 0xD800 && code <= 0xDFFF) ? '?' : code);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;  // cut off inside the string
    }

    bool number(double& out) {
        const std::size_t start = pos_;
        bool negative = false;
        if (!at_end() && peek() == '-') {
            negative = true;
            ++pos_;
        }
        if (at_end() || peek() < '0' || peek() > '9') {
            return false;
        }
        double value = 0;
        if (peek() == '0') {
            ++pos_;
        } else {
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                value = value * 10 + (peek() - '0');
                ++pos_;
            }
        }
        if (!at_end() && peek() == '.') {
            ++pos_;
            if (at_end() || peek() < '0' || peek() > '9') {
                return false;
            }
            double scale = 0.1;
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                value += (peek() - '0') * scale;
                scale /= 10;
                ++pos_;
            }
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            bool exponent_negative = false;
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                exponent_negative = peek() == '-';
                ++pos_;
            }
            if (at_end() || peek() < '0' || peek() > '9') {
                return false;
            }
            int32_t exponent = 0;
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                exponent = std::min<int32_t>(exponent * 10 + (peek() - '0'), 400);
                ++pos_;
            }
            value *= std::pow(10.0, exponent_negative ? -exponent : exponent);
        }
        if (pos_ == start) {
            return false;
        }
        out = negative ? -value : value;
        return true;
    }

    std::string_view text_;
    std::size_t pos_{0};
};

const JsonValue* object_member(const JsonValue* value, std::string_view key) {
    return value ? value->member(key) : nullptr;
}

// A count or a rate: 0 unless it is a finite number of 0 or more; at most
// `most`.
double plain_number(const JsonValue* value, double most) {
    if (!value || value->type != JsonValue::Type::Number || !std::isfinite(value->number) || value->number <= 0) {
        return 0;
    }
    return std::min(value->number, most);
}

uint64_t byte_count(const JsonValue* value) {
    return static_cast<uint64_t>(plain_number(value, static_cast<double>(kIperfBytesMax)));
}

bool flag(const JsonValue* value) {
    if (!value) {
        return false;
    }
    if (value->type == JsonValue::Type::Bool) {
        return value->boolean;
    }
    return value->type == JsonValue::Type::Number && value->number != 0;
}

// The peer's address, only when it is a plain IPv4 address (an IPv4 address
// in IPv6 form loses its "::ffff:").
std::string peer_address(const JsonValue* value) {
    if (!value || value->type != JsonValue::Type::String) {
        return {};
    }
    std::string_view text = value->text;
    constexpr std::string_view kMapped = "::ffff:";
    if (text.substr(0, kMapped.size()) == kMapped) {
        text.remove_prefix(kMapped.size());
    }
    return is_ipv4_text(text) ? std::string(text) : std::string();
}

// Lower case, for the error checks.
std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool has(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

constexpr std::size_t kIperfErrorMax = 200;
// A sample shorter than this (the last piece of a phase) is left out.
constexpr double kSampleSecondsMin = 0.2;

} // namespace

bool read_iperf_event(std::string_view line, IperfEvent& out) {
    out = IperfEvent{};
    if (line.size() > kIperfLineMax) {
        return false;
    }
    JsonValue root;
    if (!JsonReader(line).read(root) || root.type != JsonValue::Type::Object) {
        return false;
    }
    const JsonValue* event = root.member("event");
    const JsonValue* data = root.member("data");
    if (!event || event->type != JsonValue::Type::String || !data) {
        return false;
    }
    const std::string& name = event->text;
    if (name == "start") {
        out.kind = IperfEventKind::Start;
        const JsonValue* connected = object_member(data, "connected");
        if (connected && connected->type == JsonValue::Type::Array && !connected->items.empty()) {
            out.peer = peer_address(connected->items.front().member("remote_host"));
        }
        const JsonValue* test = object_member(data, "test_start");
        out.reverse = flag(object_member(test, "reverse"));
        out.bidir = flag(object_member(test, "bidir"));
    } else if (name == "interval") {
        out.kind = IperfEventKind::Interval;
        const JsonValue* sum = object_member(data, "sum");
        out.seconds = plain_number(object_member(sum, "seconds"), 1e6);
        out.bytes = byte_count(object_member(sum, "bytes"));
        out.bps = plain_number(object_member(sum, "bits_per_second"), kIperfBpsMax);
        out.back_bps = plain_number(object_member(object_member(data, "sum_bidir_reverse"), "bits_per_second"),
                                    kIperfBpsMax);
    } else if (name == "end") {
        out.kind = IperfEventKind::End;
        const JsonValue* sent = object_member(data, "sum_sent");
        const JsonValue* received = object_member(data, "sum_received");
        out.has_sent = sent && sent->type == JsonValue::Type::Object;
        out.has_received = received && received->type == JsonValue::Type::Object;
        out.sent_bytes = byte_count(object_member(sent, "bytes"));
        out.sent_bps = plain_number(object_member(sent, "bits_per_second"), kIperfBpsMax);
        out.received_bytes = byte_count(object_member(received, "bytes"));
        out.received_bps = plain_number(object_member(received, "bits_per_second"), kIperfBpsMax);
        out.back_sent_bps =
            plain_number(object_member(object_member(data, "sum_sent_bidir_reverse"), "bits_per_second"),
                         kIperfBpsMax);
    } else if (name == "error") {
        out.kind = IperfEventKind::Error;
        if (data->type == JsonValue::Type::String) {
            out.error = data->text.substr(0, kIperfErrorMax);
        }
    }
    return true;
}

void IperfLines::add(std::string_view bytes, std::vector<IperfEvent>& events) {
    while (!bytes.empty()) {
        const std::size_t end = bytes.find('\n');
        const std::string_view piece = bytes.substr(0, end);
        if (!skipping_) {
            if (partial_.size() + piece.size() > kIperfLineMax) {
                partial_.clear();
                skipping_ = true;
            } else {
                partial_.append(piece);
            }
        }
        if (end == std::string_view::npos) {
            return;  // the rest of the line comes later
        }
        if (!skipping_) {
            IperfEvent event;
            if (read_iperf_event(partial_, event) && event.kind != IperfEventKind::None) {
                events.push_back(std::move(event));
            }
        }
        partial_.clear();
        skipping_ = false;
        bytes.remove_prefix(end + 1);
    }
}

void IperfLines::clear() {
    partial_.clear();
    skipping_ = false;
}

std::string lan_error_words(std::string_view error, LanMode mode) {
    const std::string text = lower(error);
    if (has(text, "already in use")) {
        return "Port " + std::to_string(kLanPort) + " is busy";
    }
    if (has(text, "cannot assign requested address")) {
        return mode == LanMode::Server ? "That address is gone" : "No answer from there";
    }
    if (has(text, "permission denied") || has(text, "not permitted")) {
        return "Not allowed";
    }
    if (has(text, "connection refused")) {
        return "No iperf3 server there";
    }
    if (has(text, "server is busy")) {
        return "Their server is busy";
    }
    if (has(text, "timed out") || has(text, "unreachable") || has(text, "no route")) {
        return "No answer from there";
    }
    if (has(text, "unable to connect")) {
        return "Could not connect";
    }
    if (has(text, "closed") || has(text, "reset by peer") || has(text, "interrupt")) {
        return "The test was cut off";
    }
    return "The test failed";
}

bool lan_server_cannot_listen(std::string_view error) {
    const std::string text = lower(error);
    return has(text, "unable to start listener") || has(text, "already in use") ||
           has(text, "cannot assign requested address") || has(text, "permission denied");
}

int32_t lan_kbps(double bps) {
    if (!std::isfinite(bps) || bps <= 0) {
        return 0;
    }
    const double kbps = std::round(bps / 1000.0);
    return kbps >= static_cast<double>(std::numeric_limits<int32_t>::max())
               ? std::numeric_limits<int32_t>::max()
               : static_cast<int32_t>(kbps);
}

void lan_client_event(LanRun& run, const IperfEvent& event) {
    if (run.state != ToolState::Running ||
        (run.phase != LanPhase::Download && run.phase != LanPhase::Upload)) {
        return;
    }
    const bool down = run.phase == LanPhase::Download;
    uint64_t& bytes = down ? run.down_bytes : run.up_bytes;
    int32_t& kbps = down ? run.down_kbps : run.up_kbps;
    std::vector<int32_t>& samples = down ? run.down_samples : run.up_samples;
    switch (event.kind) {
        case IperfEventKind::Interval: {
            bytes = std::min<uint64_t>(bytes + event.bytes, kIperfBytesMax);
            if (event.seconds >= kSampleSecondsMin) {
                samples.push_back(lan_kbps(event.bps));
                if (samples.size() > kLanSamples) {
                    samples.erase(samples.begin());
                }
            }
            // The speed so far: the average of the samples.
            if (!samples.empty()) {
                int64_t total = 0;
                for (const int32_t sample : samples) {
                    total += sample;
                }
                kbps = static_cast<int32_t>(total / static_cast<int64_t>(samples.size()));
            }
            break;
        }
        case IperfEventKind::End:
            // What reached the receiver; the sender's own count as a fallback.
            if (event.has_received && event.received_bytes > 0) {
                bytes = event.received_bytes;
                kbps = lan_kbps(event.received_bps);
            } else if (event.has_sent && event.sent_bytes > 0) {
                bytes = event.sent_bytes;
                kbps = lan_kbps(event.sent_bps);
            } else if (kbps < 0) {
                kbps = 0;
            }
            break;
        case IperfEventKind::Error:
            run.state = ToolState::Failed;
            run.reason = lan_error_words(event.error, LanMode::Client);
            break;
        case IperfEventKind::Start:
        case IperfEventKind::None:
            break;
    }
}

void lan_server_event(LanRun& run, const IperfEvent& event) {
    switch (event.kind) {
        case IperfEventKind::Start: {
            LanServed test;
            test.peer = event.peer;
            test.both = event.bidir;
            test.to_peer = event.reverse && !event.bidir;
            run.served.insert(run.served.begin(), std::move(test));
            if (run.served.size() > kLanServedMax) {
                run.served.resize(kLanServedMax);
            }
            run.phase = LanPhase::Testing;
            run.phase_ms = 0;
            break;
        }
        case IperfEventKind::Interval:
            if (!run.served.empty() && !run.served.front().done) {
                LanServed& test = run.served.front();
                test.kbps = lan_kbps(event.bps);
                if (test.both) {
                    test.back_kbps = lan_kbps(event.back_bps);
                }
                test.bytes = std::min<uint64_t>(test.bytes + event.bytes, kIperfBytesMax);
            }
            break;
        case IperfEventKind::End:
            if (!run.served.empty() && !run.served.front().done) {
                LanServed& test = run.served.front();
                // The server knows its own side: what it received, or what
                // it sent when the app asked it to send.
                const bool sent_side = test.to_peer;
                const uint64_t bytes = sent_side ? event.sent_bytes : event.received_bytes;
                const double bps = sent_side ? event.sent_bps : event.received_bps;
                if (bytes > 0) {
                    test.bytes = bytes;
                    test.kbps = lan_kbps(bps);
                }
                if (test.both && event.back_sent_bps > 0) {
                    test.back_kbps = lan_kbps(event.back_sent_bps);
                }
            }
            lan_server_exit(run);
            break;
        case IperfEventKind::Error:
        case IperfEventKind::None:
            break;
    }
}

void lan_server_exit(LanRun& run) {
    if (!run.served.empty() && !run.served.front().done) {
        if (run.served.front().kbps < 0) {
            run.served.erase(run.served.begin());
        } else {
            run.served.front().done = true;
            ++run.tests;
        }
    }
    run.phase = LanPhase::Waiting;
    run.phase_ms = 0;
}

} // namespace model
