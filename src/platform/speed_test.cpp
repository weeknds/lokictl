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
#include <type_traits>

#if APP_USE_CURL
#include <curl/curl.h>
#include <dlfcn.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
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
// connection take this long.
constexpr uint32_t kPretendConnectMs = 250;
constexpr uint32_t kPretendUploadConnectMs = 300;
// One download and one upload request ask for this much, like the live
// test's.
constexpr uint64_t kPretendDownloadRequestBytes = 25000000;
constexpr uint64_t kPretendUploadRequestBytes = 10000000;
constexpr auto kPretendSampleMs = static_cast<uint32_t>(model::kSpeedSampleMs);
// A last, shorter sample of a phase cut short by the cap is shown when it
// is at least this long, as in the live test.
constexpr uint32_t kPretendLastSampleMs = kPretendSampleMs / 4;

// The bytes of one made-up sample of `kbps` lasting `ms`: kbit/s times
// milliseconds is bits.
uint64_t pretend_bytes(int32_t kbps, uint32_t ms) {
    return static_cast<uint64_t>(kbps) * ms / 8u;
}

// How long a made-up phase lasts: model::kSpeedPhaseMs, or less when its
// bytes reach model::kSpeedPhaseBytesMax first.
uint32_t pretend_phase_length(const int32_t* table) {
    uint64_t bytes = 0;
    for (std::size_t index = 0; index < model::kSpeedSamples; ++index) {
        const uint64_t step = pretend_bytes(table[index], kPretendSampleMs);
        if (bytes + step >= model::kSpeedPhaseBytesMax) {
            return static_cast<uint32_t>(index) * kPretendSampleMs +
                   static_cast<uint32_t>((model::kSpeedPhaseBytesMax - bytes) * kPretendSampleMs / step);
        }
        bytes += step;
    }
    return static_cast<uint32_t>(model::kSpeedPhaseMs);
}

// One phase of `length` ms, `phase_ms` into it: the samples so far, the
// bytes they add up to and the speed over them. At its end a phase cut
// short by the cap gets its last, shorter sample and exactly the cap.
// Returns the requests made so far, each asking for `request_bytes`.
uint32_t pretend_phase(const int32_t* table,
                       uint32_t length,
                       uint32_t phase_ms,
                       uint64_t request_bytes,
                       int32_t& shown_ms,
                       std::vector<int32_t>& samples,
                       uint64_t& bytes,
                       int32_t& kbps) {
    const bool over = phase_ms >= length;
    const uint32_t time = std::min(phase_ms, length);
    const std::size_t count = std::min<std::size_t>(time / kPretendSampleMs, model::kSpeedSamples);
    samples.assign(table, table + count);
    bytes = 0;
    for (std::size_t index = 0; index < count; ++index) {
        bytes += pretend_bytes(table[index], kPretendSampleMs);
    }
    shown_ms = static_cast<int32_t>(count * kPretendSampleMs);
    if (over && length < static_cast<uint32_t>(model::kSpeedPhaseMs)) {
        // Cut short by the cap.
        if (length % kPretendSampleMs >= kPretendLastSampleMs) {
            samples.push_back(table[count]);
        }
        bytes = model::kSpeedPhaseBytesMax;
        shown_ms = static_cast<int32_t>(length);
    }
    kbps = shown_ms > 0 ? static_cast<int32_t>(bytes * 8u / static_cast<uint64_t>(shown_ms)) : model::kSpeedRateUnknown;
    // One request at the start, one more each time a request's bytes are
    // through, none past the cap.
    const uint64_t most = (model::kSpeedPhaseBytesMax + request_bytes - 1) / request_bytes;
    return static_cast<uint32_t>(std::min<uint64_t>(bytes / request_bytes + 1, most));
}

} // namespace

model::SpeedRun pretend_speed_test(uint32_t elapsed_ms, const std::string& server, uint32_t& sent, bool& done) {
    static const uint32_t down_length = pretend_phase_length(kPretendDownKbps);
    static const uint32_t up_length = pretend_phase_length(kPretendUpKbps);
    model::SpeedRun run;
    run.state = model::ToolState::Running;
    run.server = server;
    run.phase = model::SpeedPhase::Connect;
    sent = 1;  // the first request
    if (elapsed_ms < kPretendConnectMs) {
        return run;
    }

    run.phase = model::SpeedPhase::Download;
    sent += pretend_phase(kPretendDownKbps, down_length, elapsed_ms - kPretendConnectMs,
                          kPretendDownloadRequestBytes, run.phase_ms, run.down_samples, run.down_bytes,
                          run.down_kbps);
    const uint32_t upload_at = kPretendConnectMs + down_length;
    if (elapsed_ms < upload_at) {
        return run;
    }

    run.phase = model::SpeedPhase::Upload;
    run.phase_ms = 0;
    const uint32_t up_start = upload_at + kPretendUploadConnectMs;
    if (elapsed_ms < up_start) {
        sent += 1;  // the first upload request, connecting
        return run;
    }
    sent += pretend_phase(kPretendUpKbps, up_length, elapsed_ms - up_start, kPretendUploadRequestBytes,
                          run.phase_ms, run.up_samples, run.up_bytes, run.up_kbps);
    if (elapsed_ms >= up_start + up_length) {
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
// One download and one upload request ask for at most this much (less
// when the phase's cap, model::kSpeedPhaseBytesMax, is nearer). The server
// takes downloads below 100,000,000 bytes.
constexpr curl_off_t kDownloadRequestBytes = 25000000;
constexpr curl_off_t kUploadRequestBytes = 10000000;
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
    decltype(&curl_global_cleanup) global_cleanup{nullptr};
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

// libcurl while it is loaded. Only the tool runner's thread uses it (one
// tool at a time), so it needs no lock.
struct CurlLibrary {
    void* handle{nullptr};
    CurlApi api;
    // Set once a run ended while its first request's name lookup may still
    // have been running: with CURLOPT_QUICK_EXIT the lookup's thread is
    // left to finish inside libcurl, so libcurl must stay loaded from then
    // on.
    bool kept{false};
};

CurlLibrary& curl_library() {
    static CurlLibrary library;
    return library;
}

// Loads libcurl unless it is loaded. Null with `reason` when it cannot be
// used.
const CurlApi* load_curl(std::string& reason) {
    CurlLibrary& library = curl_library();
    if (library.handle != nullptr) {
        return &library.api;
    }
    void* handle = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        reason = "libcurl not found";
        return nullptr;
    }
    CurlApi api;
    bool found = true;
    const auto find = [handle, &found](auto& slot, const char* name) {
        void* symbol = dlsym(handle, name);
        static_assert(sizeof(slot) == sizeof(symbol), "a function pointer must fit a pointer");
        std::memcpy(&slot, &symbol, sizeof(symbol));
        found = found && symbol != nullptr;
    };
    find(api.global_init, "curl_global_init");
    find(api.global_cleanup, "curl_global_cleanup");
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
        dlclose(handle);
        reason = "libcurl is too old";
        return nullptr;
    }
    if (api.global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        dlclose(handle);
        reason = "libcurl could not start";
        return nullptr;
    }
    library.handle = handle;
    library.api = api;
    return &library.api;
}

// After a run, when every handle is freed: unloads libcurl (unless it must
// stay, CurlLibrary::kept) and hands freed memory back to the system.
// Measured on Debian 13 arm64 after one HTTPS request: unloading gives back
// about 4 MB of the 13 MB that loading and the request added (25 of the 31
// libraries it loads go), the trim about 0.5 MB more. libssl, libcrypto
// and the few they need stay mapped whatever happens: OpenSSL pins itself,
// and OPENSSL_cleanup() would free its state for good (it cannot be
// started again), so that is never called. The next run loads libcurl
// again.
void release_curl() {
    CurlLibrary& library = curl_library();
    if (library.handle != nullptr && !library.kept) {
        library.api.global_cleanup();
        dlclose(library.handle);
        library.handle = nullptr;
        library.api = CurlApi{};
    }
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
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

    // Whether a name lookup may still be running inside libcurl: from the
    // start of the first request until it finished. After that the run's
    // DNS cache answers every later connection, so no lookup starts again.
    bool lookup_may_run() const {
        return lookup_may_run_;
    }

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

        lookup_may_run_ = true;
        const End first = transfer(Request::First, 0);
        if (first != End::Finished) {
            end_with(first);
            return;
        }
        lookup_may_run_ = false;
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
        // The first request's name lookup is the run's only one: a new
        // connection later takes the address from the cache, which lives
        // as long as the multi handle.
        option(CURLOPT_DNS_CACHE_TIMEOUT, -1L);
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
    // phase's time is over or it moved model::kSpeedPhaseBytesMax. Each
    // request asks for no more than the phase may still move, so the cap
    // holds on the wire too. False when the run ended (stopped or failed).
    bool phase(Request kind) {
        started_ = false;
        phase_over_ = false;
        phase_asked_ = 0;
        phase_handed_ = 0;
        last_sample_bytes_ = 0;
        last_sample_ms_ = 0;
        const curl_off_t most = kind == Request::Upload ? kUploadRequestBytes : kDownloadRequestBytes;
        while (true) {
            const uint64_t left = model::kSpeedPhaseBytesMax - std::min(phase_asked_, model::kSpeedPhaseBytesMax);
            if (left == 0) {
                finish_phase();
                return true;
            }
            const curl_off_t size = std::min(most, static_cast<curl_off_t>(left));
            const uint64_t body_before = body_bytes_;
            const End end = transfer(kind, size);
            if (end == End::PhaseOver) {
                finish_phase();
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
            if (kind == Request::Upload && upload_left_ == size) {
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

    std::string url(Request kind, curl_off_t size) const {
        switch (kind) {
            case Request::First:
            case Request::Download:
                return base_url_ + "/__down?bytes=" + std::to_string(size);
            case Request::Upload:
                break;
        }
        return base_url_ + "/__up";
    }

    // Makes one request for `size` bytes (the first request asks for none)
    // and drives it until it ends, the phase is over, or the run is
    // stopped.
    End transfer(Request kind, curl_off_t size) {
        request_ = kind;
        upload_left_ = kind == Request::Upload ? size : 0;
        const std::string address = url(kind, size);
        option(CURLOPT_URL, address.c_str());
        if (kind == Request::Upload) {
            option(CURLOPT_POST, 1L);
            option(CURLOPT_POSTFIELDSIZE_LARGE, size);
        } else {
            option(CURLOPT_HTTPGET, 1L);
        }
        option(CURLOPT_TIMEOUT_MS, kind == Request::First ? kFirstRequestTimeoutMs : 0L);
        if (api_.multi_add_handle(multi_, easy_) != CURLM_OK) {
            fail("transfer failed");
            return End::Failed;
        }
        added_ = true;
        phase_asked_ += static_cast<uint64_t>(size);
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

    // The bytes the current phase moved so far. An upload counts what the
    // server acknowledged, which also holds TLS and the request lines, so
    // never more than the body bytes handed over.
    uint64_t phase_bytes() const {
        const uint64_t now = request_ == Request::Upload ? acknowledged_total() : body_bytes_;
        const uint64_t moved = now > phase_base_ ? now - phase_base_ : 0;
        return request_ == Request::Upload ? std::min(moved, phase_handed_) : moved;
    }

    uint64_t acknowledged_total() const {
        uint64_t total = closed_acknowledged_;
        for (const int fd : sockets_) {
            total += acknowledged_bytes(fd);
        }
        return total;
    }

    // kbit/s of `bytes` over `ms`: bytes per millisecond times 8.
    static int32_t rate(uint64_t bytes, int64_t ms) {
        return static_cast<int32_t>(
            std::min<uint64_t>(bytes * 8u / static_cast<uint64_t>(std::max<int64_t>(ms, 1)), INT32_MAX));
    }

    // Puts the phase's bytes, its speed over `elapsed` and its time into
    // the run.
    void set_phase_figures(uint64_t bytes, int64_t elapsed) {
        if (request_ == Request::Upload) {
            run_.up_bytes = bytes;
            run_.up_kbps = rate(bytes, elapsed);
        } else {
            run_.down_bytes = bytes;
            run_.down_kbps = rate(bytes, elapsed);
        }
        run_.phase_ms = static_cast<int32_t>(std::min<int64_t>(elapsed, model::kSpeedPhaseMs));
    }

    // Adds the samples that are due (one per model::kSpeedSampleMs since
    // the phase's first byte; after a late wait the gap is shared out
    // evenly), updates the phase's bytes and speed, and notes when its
    // time is over or it moved model::kSpeedPhaseBytesMax (a server that
    // sends more than asked).
    void take_samples(Clock::time_point now) {
        if (!started_ || request_ == Request::First) {
            return;
        }
        const int64_t elapsed = elapsed_ms(now);
        const uint64_t bytes = phase_bytes();
        std::vector<int32_t>& samples = run_samples();
        const auto due = static_cast<std::size_t>(
            std::min<int64_t>(elapsed / model::kSpeedSampleMs, static_cast<int64_t>(model::kSpeedSamples)));
        if (due > samples.size()) {
            samples.resize(due, rate(bytes - std::min(bytes, last_sample_bytes_), elapsed - last_sample_ms_));
            last_sample_bytes_ = bytes;
            last_sample_ms_ = elapsed;
            set_phase_figures(bytes, elapsed);
            publish_(run_);
        }
        if (elapsed >= model::kSpeedPhaseMs || bytes >= model::kSpeedPhaseBytesMax) {
            phase_over_ = true;
        }
    }

    // The phase's last reading as it ends: the samples still due, then one
    // shorter sample for the time since the last one when that is at
    // least a quarter of a sample (a phase cut short by the cap), and the
    // bytes and speed over the phase's real time.
    void finish_phase() {
        if (!started_) {
            return;
        }
        const Clock::time_point now = Clock::now();
        take_samples(now);
        const int64_t elapsed = elapsed_ms(now);
        const uint64_t bytes = phase_bytes();
        std::vector<int32_t>& samples = run_samples();
        const int64_t span = elapsed - last_sample_ms_;
        if (samples.size() < model::kSpeedSamples && span >= model::kSpeedSampleMs / 4) {
            samples.push_back(rate(bytes - std::min(bytes, last_sample_bytes_), span));
        }
        set_phase_figures(bytes, elapsed);
        publish_(run_);
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
        test->phase_handed_ += bytes;
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
    curl_off_t upload_left_{0};  // body bytes of this upload request not handed over yet
    bool lookup_may_run_{false};
    // Body bytes received in all, and the acknowledged bytes of the
    // connections closed so far.
    uint64_t body_bytes_{0};
    uint64_t closed_acknowledged_{0};
    std::vector<int> sockets_;  // the connections libcurl has open
    // The current phase: the bytes its requests asked for and, uploading,
    // the body bytes handed over; whether its first byte came, when, the
    // byte count then, the last sample, and whether it is over.
    uint64_t phase_asked_{0};
    uint64_t phase_handed_{0};
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
    {
        SpeedTest test(*api, server, stopping, publish);
        test.run();
        if (test.lookup_may_run()) {
            curl_library().kept = true;
        }
    }
    release_curl();
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
