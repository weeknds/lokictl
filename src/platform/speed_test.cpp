/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "speed_test.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if !USE_DESKTOP
#include "sent_counter.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <type_traits>

#if APP_USE_CURL
#include <curl/curl.h>
#include <dlfcn.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#endif

namespace platform {
namespace {

// ---------------------------------------------------------------------------
// The pretend runner's made-up test.

// The made-up speeds of each half second, in kbit/s: a quick start, then
// about 89 Mbit/s down and 18 Mbit/s up, never quite even.
constexpr int32_t kPretendDownKbps[model::kSpeedSamples] = {
    41200, 78400, 88100, 91300, 86900, 89500, 92200, 90100, 84700, 88800,
    91600, 89900, 87300, 90400, 92800, 88200, 86100, 89700, 91100, 90300,
};
constexpr int32_t kPretendUpKbps[model::kSpeedSamples] = {
    9800,  16900, 18400, 17700, 18900, 18100, 17300, 18800, 19200, 18500,
    17900, 18600, 19100, 18300, 17600, 18900, 18700, 18200, 19000, 18400,
};
// The first request (name, connection, TLS) and the upload's new
// connection take this long; one 25 MB download and one 10 MB upload take
// about this long at those speeds.
constexpr uint32_t kPretendConnectMs = 250;
constexpr uint32_t kPretendUploadConnectMs = 300;
constexpr uint32_t kPretendDownloadRequestMs = 2300;
constexpr uint32_t kPretendUploadRequestMs = 4400;

// One phase `phase_ms` into it: the samples so far, the bytes they add up
// to and the speed over them.
void pretend_phase(const int32_t* table,
                   uint32_t phase_ms,
                   int32_t& shown_ms,
                   std::vector<int32_t>& samples,
                   uint64_t& bytes,
                   int32_t& kbps) {
    const std::size_t count =
        std::min<std::size_t>(phase_ms / static_cast<uint32_t>(model::kSpeedSampleMs), model::kSpeedSamples);
    samples.assign(table, table + count);
    bytes = 0;
    for (std::size_t index = 0; index < count; ++index) {
        // kbit/s over half a second: 1000 / 8 / 2 bytes per kbit/s.
        bytes += static_cast<uint64_t>(table[index]) * 125u / 2u;
    }
    shown_ms = static_cast<int32_t>(count) * model::kSpeedSampleMs;
    kbps = count > 0 ? static_cast<int32_t>(bytes * 8u / static_cast<uint64_t>(shown_ms)) : model::kSpeedRateUnknown;
}

} // namespace

model::SpeedRun pretend_speed_test(uint32_t elapsed_ms, const std::string& server, uint32_t& sent, bool& done) {
    constexpr auto kPhaseMs = static_cast<uint32_t>(model::kSpeedPhaseMs);
    model::SpeedRun run;
    run.state = model::ToolState::Running;
    run.server = server;
    run.phase = model::SpeedPhase::Connect;
    sent = 1;  // the first request
    if (elapsed_ms < kPretendConnectMs) {
        return run;
    }

    const uint32_t down_ms = std::min(elapsed_ms - kPretendConnectMs, kPhaseMs);
    run.phase = model::SpeedPhase::Download;
    sent += 1 + down_ms / kPretendDownloadRequestMs;
    pretend_phase(kPretendDownKbps, down_ms, run.phase_ms, run.down_samples, run.down_bytes, run.down_kbps);
    const uint32_t upload_at = kPretendConnectMs + kPhaseMs;
    if (elapsed_ms < upload_at) {
        return run;
    }

    run.phase = model::SpeedPhase::Upload;
    run.phase_ms = 0;
    sent += 1;
    const uint32_t up_start = upload_at + kPretendUploadConnectMs;
    if (elapsed_ms < up_start) {
        return run;
    }
    const uint32_t up_ms = std::min(elapsed_ms - up_start, kPhaseMs);
    sent += up_ms / kPretendUploadRequestMs;
    pretend_phase(kPretendUpKbps, up_ms, run.phase_ms, run.up_samples, run.up_bytes, run.up_kbps);
    if (elapsed_ms >= up_start + kPhaseMs) {
        run.state = model::ToolState::Done;
        done = true;
    }
    return run;
}

#if !USE_DESKTOP
namespace {

void fail_run(model::SpeedRun& run, const std::string& reason) {
    run.state = model::ToolState::Failed;
    run.error = model::ToolError::Failed;
    run.reason = reason;
}

} // namespace

#if APP_USE_CURL
namespace {

using Clock = std::chrono::steady_clock;

// The longest single wait, so a stop is seen quickly.
constexpr int kWaitSliceMs = 100;
// One download and one upload request. The server takes downloads below
// 100,000,000 bytes.
constexpr curl_off_t kDownloadBytes = 25000000;
constexpr curl_off_t kUploadBytes = 10000000;
constexpr long kConnectTimeoutMs = 10000;
// The first request, which only connects, must be over in this long.
constexpr long kFirstRequestTimeoutMs = 15000;
// A request that moves no data for this long fails.
constexpr long kStallSeconds = 10;
constexpr long kBufferBytes = 64 * 1024;
// TLS 1.3 suites, ChaCha20 first: the Cardputer's processor has no AES
// instructions, so it is the cheapest of them there.
constexpr const char* kTls13Suites =
    "TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";

// The few libcurl functions the test uses, found with dlsym().
struct CurlApi {
    decltype(&curl_global_init) global_init{nullptr};
    decltype(&curl_easy_init) easy_init{nullptr};
    decltype(&curl_easy_setopt) easy_setopt{nullptr};
    decltype(&curl_easy_getinfo) easy_getinfo{nullptr};
    decltype(&curl_easy_strerror) easy_strerror{nullptr};
    decltype(&curl_easy_cleanup) easy_cleanup{nullptr};
    decltype(&curl_multi_init) multi_init{nullptr};
    decltype(&curl_multi_add_handle) multi_add_handle{nullptr};
    decltype(&curl_multi_remove_handle) multi_remove_handle{nullptr};
    decltype(&curl_multi_perform) multi_perform{nullptr};
    decltype(&curl_multi_poll) multi_poll{nullptr};
    decltype(&curl_multi_info_read) multi_info_read{nullptr};
    decltype(&curl_multi_cleanup) multi_cleanup{nullptr};
    decltype(&curl_slist_append) slist_append{nullptr};
    decltype(&curl_slist_free_all) slist_free_all{nullptr};
};

// Loads libcurl once, the first time a test runs, and keeps it: OpenSSL,
// which it brings along, does not unload cleanly. Null with `reason` when
// it cannot be used.
const CurlApi* load_curl(std::string& reason) {
    static std::once_flag once;
    static CurlApi api;
    static bool usable = false;
    static std::string failure;
    std::call_once(once, [] {
        void* library = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) {
            failure = "libcurl not found";
            return;
        }
        bool found = true;
        const auto find = [library, &found](auto& slot, const char* name) {
            void* symbol = dlsym(library, name);
            static_assert(sizeof(slot) == sizeof(symbol), "a function pointer must fit a pointer");
            std::memcpy(&slot, &symbol, sizeof(symbol));
            found = found && symbol != nullptr;
        };
        find(api.global_init, "curl_global_init");
        find(api.easy_init, "curl_easy_init");
        find(api.easy_setopt, "curl_easy_setopt");
        find(api.easy_getinfo, "curl_easy_getinfo");
        find(api.easy_strerror, "curl_easy_strerror");
        find(api.easy_cleanup, "curl_easy_cleanup");
        find(api.multi_init, "curl_multi_init");
        find(api.multi_add_handle, "curl_multi_add_handle");
        find(api.multi_remove_handle, "curl_multi_remove_handle");
        find(api.multi_perform, "curl_multi_perform");
        find(api.multi_poll, "curl_multi_poll");
        find(api.multi_info_read, "curl_multi_info_read");
        find(api.multi_cleanup, "curl_multi_cleanup");
        find(api.slist_append, "curl_slist_append");
        find(api.slist_free_all, "curl_slist_free_all");
        if (!found) {
            failure = "libcurl is too old";
            return;
        }
        if (api.global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            failure = "libcurl could not start";
            return;
        }
        usable = true;
    });
    if (!usable) {
        reason = failure;
        return nullptr;
    }
    return &api;
}

// The bytes of a connection the other end acknowledged so far; 0 when the
// system cannot tell.
uint64_t acknowledged_bytes(int fd) {
    tcp_info info{};
    socklen_t length = sizeof(info);
    if (::getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &length) != 0 ||
        length < offsetof(tcp_info, tcpi_bytes_acked) + sizeof(info.tcpi_bytes_acked)) {
        return 0;
    }
    return info.tcpi_bytes_acked;
}

std::string failure_reason(const CurlApi& api, CURLcode code, long status) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
            return "server name not found";
        case CURLE_COULDNT_CONNECT:
            return "could not connect to the server";
        case CURLE_OPERATION_TIMEDOUT:
            return "no answer from the server";
        case CURLE_PEER_FAILED_VERIFICATION:
            return "server certificate not trusted";
        case CURLE_SSL_CACERT_BADFILE:
            return "no trusted certificates on this device";
        case CURLE_SSL_CONNECT_ERROR:
            return "secure connection failed";
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
            return "connection lost";
        case CURLE_HTTP_RETURNED_ERROR:
            return "server said " + std::to_string(status);
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_URL_MALFORMAT:
            return "server address not usable";
        default:
            break;
    }
    const char* text = api.easy_strerror(code);
    return text != nullptr ? std::string(text) : std::string("transfer failed");
}

class SpeedTest {
public:
    SpeedTest(const CurlApi& api,
              const std::string& server,
              const std::function<bool()>& stopping,
              const std::function<void(const model::SpeedRun&)>& publish)
        : api_(api), stopping_(stopping), publish_(publish) {
        run_.state = model::ToolState::Running;
        run_.server = server;
        base_url_ = server;
        while (!base_url_.empty() && base_url_.back() == '/') {
            base_url_.pop_back();
        }
    }

    // Frees the handles; libcurl closes the connections through
    // close_socket_cb() while this object still exists.
    ~SpeedTest() {
        if (added_) {
            api_.multi_remove_handle(multi_, easy_);
        }
        if (easy_ != nullptr) {
            api_.easy_cleanup(easy_);
        }
        if (multi_ != nullptr) {
            api_.multi_cleanup(multi_);
        }
        if (headers_ != nullptr) {
            api_.slist_free_all(headers_);
        }
        for (const int fd : sockets_) {
            ::close(fd);
        }
    }

    SpeedTest(const SpeedTest&) = delete;
    SpeedTest& operator=(const SpeedTest&) = delete;

    void run() {
        publish_(run_);
        multi_ = api_.multi_init();
        easy_ = api_.easy_init();
        // Without this header libcurl asks before an upload and may wait
        // a second for the answer.
        headers_ = api_.slist_append(nullptr, "Expect:");
        if (multi_ == nullptr || easy_ == nullptr || headers_ == nullptr) {
            fail("out of memory");
            return;
        }
        set_options();

        const End first = transfer(Request::First);
        if (first != End::Finished) {
            end_with(first);
            return;
        }
        run_.phase = model::SpeedPhase::Download;
        publish_(run_);
        if (!phase(Request::Download)) {
            return;
        }
        run_.phase = model::SpeedPhase::Upload;
        run_.phase_ms = 0;
        publish_(run_);
        if (!phase(Request::Upload)) {
            return;
        }
        run_.state = model::ToolState::Done;
        publish_(run_);
    }

private:
    enum class Request { First, Download, Upload };
    enum class End { Finished, PhaseOver, Stopped, Failed };

    template <typename Value>
    void option(CURLoption which, Value value) {
        api_.easy_setopt(easy_, which, value);
    }

    void set_options() {
        option(CURLOPT_NOSIGNAL, 1L);
        // A stop during the name lookup must not wait for it: without this,
        // taking the request away waits until the system's lookup returns
        // (9.5 s with a nameserver that never answers). The lookup's thread
        // then ends by itself.
        option(CURLOPT_QUICK_EXIT, 1L);
        option(CURLOPT_PROTOCOLS_STR, "https,http");
        option(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
        option(CURLOPT_USERAGENT, "lokictl");
        option(CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
        option(CURLOPT_LOW_SPEED_LIMIT, 1L);
        option(CURLOPT_LOW_SPEED_TIME, kStallSeconds);
        option(CURLOPT_FAILONERROR, 1L);
        option(CURLOPT_BUFFERSIZE, kBufferBytes);
        option(CURLOPT_UPLOAD_BUFFERSIZE, kBufferBytes);
        // Refused by a libcurl built without TLS 1.3 suites; the defaults
        // then stay.
        option(CURLOPT_TLS13_CIPHERS, kTls13Suites);
        option(CURLOPT_HTTPHEADER, headers_);
        option(CURLOPT_WRITEFUNCTION, &SpeedTest::write_cb);
        option(CURLOPT_WRITEDATA, static_cast<void*>(this));
        option(CURLOPT_READFUNCTION, &SpeedTest::read_cb);
        option(CURLOPT_READDATA, static_cast<void*>(this));
        option(CURLOPT_OPENSOCKETFUNCTION, &SpeedTest::open_socket_cb);
        option(CURLOPT_OPENSOCKETDATA, static_cast<void*>(this));
        option(CURLOPT_CLOSESOCKETFUNCTION, &SpeedTest::close_socket_cb);
        option(CURLOPT_CLOSESOCKETDATA, static_cast<void*>(this));
    }

    // One download or upload phase: requests one after another until the
    // phase's time is over. False when the run ended (stopped or failed).
    bool phase(Request kind) {
        started_ = false;
        phase_over_ = false;
        last_sample_bytes_ = 0;
        last_sample_ms_ = 0;
        while (true) {
            const uint64_t body_before = body_bytes_;
            const End end = transfer(kind);
            if (end == End::PhaseOver) {
                return true;
            }
            if (end != End::Finished) {
                end_with(end);
                return false;
            }
            // A server that answers without moving data would be asked
            // again and again.
            if (kind == Request::Download && body_bytes_ == body_before) {
                fail("server sent no data");
                return false;
            }
            if (kind == Request::Upload && upload_left_ == kUploadBytes) {
                fail("server took no data");
                return false;
            }
        }
    }

    void end_with(End end) {
        if (end == End::Stopped) {
            run_.state = model::ToolState::Stopped;
            publish_(run_);
        }
        // End::Failed has been published by fail().
    }

    void fail(const std::string& reason) {
        fail_run(run_, reason);
        publish_(run_);
    }

    std::string url(Request kind) const {
        switch (kind) {
            case Request::First:
                return base_url_ + "/__down?bytes=0";
            case Request::Download:
                return base_url_ + "/__down?bytes=" + std::to_string(kDownloadBytes);
            case Request::Upload:
                break;
        }
        return base_url_ + "/__up";
    }

    // Makes one request and drives it until it ends, the phase's time is
    // over, or the run is stopped.
    End transfer(Request kind) {
        request_ = kind;
        upload_left_ = kUploadBytes;
        const std::string address = url(kind);
        option(CURLOPT_URL, address.c_str());
        if (kind == Request::Upload) {
            option(CURLOPT_POST, 1L);
            option(CURLOPT_POSTFIELDSIZE_LARGE, kUploadBytes);
        } else {
            option(CURLOPT_HTTPGET, 1L);
        }
        option(CURLOPT_TIMEOUT_MS, kind == Request::First ? kFirstRequestTimeoutMs : 0L);
        if (api_.multi_add_handle(multi_, easy_) != CURLM_OK) {
            fail("transfer failed");
            return End::Failed;
        }
        added_ = true;
        count_sent();
        const End end = drive();
        api_.multi_remove_handle(multi_, easy_);
        added_ = false;
        return end;
    }

    End drive() {
        while (true) {
            int running = 0;
            if (api_.multi_perform(multi_, &running) != CURLM_OK) {
                fail("transfer failed");
                return End::Failed;
            }
            bool done = false;
            CURLcode result = CURLE_OK;
            int waiting = 0;
            while (CURLMsg* message = api_.multi_info_read(multi_, &waiting)) {
                if (message->msg == CURLMSG_DONE && message->easy_handle == easy_) {
                    done = true;
                    result = message->data.result;
                }
            }
            const Clock::time_point now = Clock::now();
            take_samples(now);
            if (phase_over_) {
                return End::PhaseOver;
            }
            if (done) {
                long status = 0;
                api_.easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
                if (result != CURLE_OK) {
                    fail(failure_reason(api_, result, status));
                    return End::Failed;
                }
                if (status != 200) {
                    fail("server said " + std::to_string(status));
                    return End::Failed;
                }
                return End::Finished;
            }
            if (stopping_()) {
                return End::Stopped;
            }
            int wait = kWaitSliceMs;
            if (started_) {
                const int64_t elapsed = elapsed_ms(now);
                const int64_t next_sample =
                    (static_cast<int64_t>(run_samples().size()) + 1) * model::kSpeedSampleMs;
                wait = static_cast<int>(std::clamp<int64_t>(next_sample - elapsed, 0, kWaitSliceMs));
            }
            api_.multi_poll(multi_, nullptr, 0, wait, nullptr);
        }
    }

    int64_t elapsed_ms(Clock::time_point now) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_).count();
    }

    std::vector<int32_t>& run_samples() {
        return request_ == Request::Upload ? run_.up_samples : run_.down_samples;
    }

    // The bytes the current phase moved so far.
    uint64_t phase_bytes() const {
        const uint64_t now = request_ == Request::Upload ? acknowledged_total() : body_bytes_;
        return now > phase_base_ ? now - phase_base_ : 0;
    }

    uint64_t acknowledged_total() const {
        uint64_t total = closed_acknowledged_;
        for (const int fd : sockets_) {
            total += acknowledged_bytes(fd);
        }
        return total;
    }

    // Adds the samples that are due (one per model::kSpeedSampleMs since
    // the phase's first byte; after a late wait the gap is shared out
    // evenly), updates the phase's bytes and speed, and notes when its
    // time is over.
    void take_samples(Clock::time_point now) {
        if (!started_ || request_ == Request::First) {
            return;
        }
        const int64_t elapsed = elapsed_ms(now);
        std::vector<int32_t>& samples = run_samples();
        const auto due = static_cast<std::size_t>(
            std::min<int64_t>(elapsed / model::kSpeedSampleMs, static_cast<int64_t>(model::kSpeedSamples)));
        if (due > samples.size()) {
            const uint64_t bytes = phase_bytes();
            const int64_t span = std::max<int64_t>(elapsed - last_sample_ms_, 1);
            // Bytes per millisecond times 8 is kbit/s.
            const auto kbps = static_cast<int32_t>(std::min<uint64_t>(
                (bytes - std::min(bytes, last_sample_bytes_)) * 8u / static_cast<uint64_t>(span), INT32_MAX));
            samples.resize(due, kbps);
            last_sample_bytes_ = bytes;
            last_sample_ms_ = elapsed;
            const auto average = static_cast<int32_t>(
                std::min<uint64_t>(bytes * 8u / static_cast<uint64_t>(std::max<int64_t>(elapsed, 1)), INT32_MAX));
            if (request_ == Request::Upload) {
                run_.up_bytes = bytes;
                run_.up_kbps = average;
            } else {
                run_.down_bytes = bytes;
                run_.down_kbps = average;
            }
            run_.phase_ms = static_cast<int32_t>(std::min<int64_t>(elapsed, model::kSpeedPhaseMs));
            publish_(run_);
        }
        if (elapsed >= model::kSpeedPhaseMs) {
            phase_over_ = true;
        }
    }

    void start_phase() {
        started_ = true;
        phase_start_ = Clock::now();
    }

    static size_t write_cb(char* /*data*/, size_t size, size_t count, void* user) {
        auto* test = static_cast<SpeedTest*>(user);
        const size_t bytes = size * count;
        test->body_bytes_ += bytes;
        if (test->request_ == Request::Download && !test->started_ && bytes > 0) {
            // The phase starts with its first body byte, which counts.
            test->start_phase();
            test->phase_base_ = test->body_bytes_ - bytes;
        }
        return bytes;
    }

    static size_t read_cb(char* buffer, size_t size, size_t count, void* user) {
        auto* test = static_cast<SpeedTest*>(user);
        if (test->request_ == Request::Upload && !test->started_) {
            // Connected and asked for the body: the phase starts now.
            test->start_phase();
            test->phase_base_ = test->acknowledged_total();
        }
        const size_t bytes = static_cast<size_t>(std::min<curl_off_t>(
            static_cast<curl_off_t>(size * count), test->upload_left_));
        std::memset(buffer, 0, bytes);
        test->upload_left_ -= static_cast<curl_off_t>(bytes);
        return bytes;
    }

    static curl_socket_t open_socket_cb(void* user, curlsocktype /*purpose*/, curl_sockaddr* address) {
        auto* test = static_cast<SpeedTest*>(user);
        const int fd = ::socket(address->family, address->socktype | SOCK_CLOEXEC, address->protocol);
        if (fd < 0) {
            return CURL_SOCKET_BAD;
        }
        test->sockets_.push_back(fd);
        return fd;
    }

    static int close_socket_cb(void* user, curl_socket_t fd) {
        auto* test = static_cast<SpeedTest*>(user);
        const auto found = std::find(test->sockets_.begin(), test->sockets_.end(), fd);
        if (found != test->sockets_.end()) {
            // Its last count stays in the total.
            test->closed_acknowledged_ += acknowledged_bytes(fd);
            test->sockets_.erase(found);
        }
        return ::close(fd);
    }

    const CurlApi& api_;
    const std::function<bool()>& stopping_;
    const std::function<void(const model::SpeedRun&)>& publish_;
    model::SpeedRun run_;
    std::string base_url_;
    CURLM* multi_{nullptr};
    CURL* easy_{nullptr};
    curl_slist* headers_{nullptr};
    bool added_{false};
    Request request_{Request::First};
    curl_off_t upload_left_{0};
    // Body bytes received in all, and the acknowledged bytes of the
    // connections closed so far.
    uint64_t body_bytes_{0};
    uint64_t closed_acknowledged_{0};
    std::vector<int> sockets_;  // the connections libcurl has open
    // The current phase: whether its first byte came, when, the byte count
    // then, the last sample, and whether its time is over.
    bool started_{false};
    Clock::time_point phase_start_{};
    uint64_t phase_base_{0};
    uint64_t last_sample_bytes_{0};
    int64_t last_sample_ms_{0};
    bool phase_over_{false};
};

} // namespace

void run_speed_test(const std::string& server,
                    const std::function<bool()>& stopping,
                    const std::function<void(const model::SpeedRun&)>& publish) {
    model::SpeedRun run;
    run.state = model::ToolState::Running;
    run.server = server;
    if (!model::valid_speed_server(server)) {
        run.state = model::ToolState::Failed;
        run.error = model::ToolError::BadTarget;
        publish(run);
        return;
    }
    std::string reason;
    const CurlApi* api = load_curl(reason);
    if (api == nullptr) {
        fail_run(run, reason);
        publish(run);
        return;
    }
    SpeedTest test(*api, server, stopping, publish);
    test.run();
}
#else
void run_speed_test(const std::string& server,
                    const std::function<bool()>& /*stopping*/,
                    const std::function<void(const model::SpeedRun&)>& publish) {
    model::SpeedRun run;
    run.server = server;
    fail_run(run, "built without libcurl");
    publish(run);
}
#endif
#endif

} // namespace platform
