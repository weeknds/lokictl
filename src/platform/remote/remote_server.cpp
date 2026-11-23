/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote/remote_server.h"

#include "network.h"
#include "remote/remote_http.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string_view>
#include <system_error>
#include <utility>

namespace platform::remote {
namespace {

constexpr int64_t kRequestTimeoutMs = 2000;
constexpr int64_t kAddressCheckMs = 2000;
constexpr int kIdlePollMs = 1000;
constexpr int kListenBacklog = 4;
constexpr uint32_t kLoopback = 0x7F000001U; // 127.0.0.1
constexpr const char* kCookieName = "lokictl_session";

constexpr std::string_view kIndexText =
    "LokiCTL remote view\n"
    "\n"
    "POST /pair with code=NNNNNN (the code on the Cardputer's screen),\n"
    "then GET /screen.png with the cookie it sets.\n";

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// Every descriptor of the server is closed on exec, so the helpers the app
// starts never inherit a socket. Linux sets the flags atomically where it
// can (below); this covers the rest.
void set_cloexec_nonblocking(int fd) {
    const int fd_flags = ::fcntl(fd, F_GETFD);
    if (fd_flags >= 0) {
        ::fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
    }
    const int status_flags = ::fcntl(fd, F_GETFL);
    if (status_flags >= 0) {
        ::fcntl(fd, F_SETFL, status_flags | O_NONBLOCK);
    }
}

// A broken connection must never raise SIGPIPE in the app: Linux sends
// with MSG_NOSIGNAL, the Mac sets SO_NOSIGPIPE on the socket.
void no_sigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
}

// 10/8, 172.16/12 and 192.168/16.
bool private_address(uint32_t address) {
    return (address & 0xFF000000U) == 0x0A000000U || (address & 0xFFF00000U) == 0xAC100000U ||
           (address & 0xFFFF0000U) == 0xC0A80000U;
}

std::string address_text(uint32_t address) {
    in_addr value{};
    value.s_addr = htonl(address);
    char text[INET_ADDRSTRLEN] = {};
    if (!::inet_ntop(AF_INET, &value, text, sizeof(text))) {
        return {};
    }
    return text;
}

enum class Wait { Ready, Timeout, Stop };

// Waits until `fd` can be read (or written, with POLLOUT) or the wake pipe
// says stop, at most until `deadline_ms`.
Wait wait_for(int fd, short events, int wake_fd, int64_t deadline_ms) {
    for (;;) {
        const int64_t left = deadline_ms - Server::now_ms();
        if (left <= 0) {
            return Wait::Timeout;
        }
        pollfd fds[2] = {{fd, events, 0}, {wake_fd, POLLIN, 0}};
        const int ready = ::poll(fds, 2, static_cast<int>(left));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Wait::Stop;
        }
        if (ready == 0) {
            return Wait::Timeout;
        }
        if (fds[1].revents != 0) {
            return Wait::Stop;
        }
        if (fds[0].revents != 0) {
            return Wait::Ready;
        }
    }
}

// Sends all of `data` before `deadline_ms`. Returns false when the peer
// went away, the time ran out or the server stops.
bool send_all(int fd, std::string_view data, int wake_fd, int64_t deadline_ms) {
    while (!data.empty()) {
        const ssize_t sent = ::send(fd, data.data(), data.size(), kSendFlags);
        if (sent > 0) {
            data.remove_prefix(static_cast<std::size_t>(sent));
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_for(fd, POLLOUT, wake_fd, deadline_ms) != Wait::Ready) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

void send_text(int fd, int status, std::string_view text, int wake_fd, int64_t deadline_ms,
               std::string_view extra_headers = {}) {
    std::string answer = response_head(status, "text/plain; charset=utf-8", text.size(), extra_headers);
    answer.append(text);
    send_all(fd, answer, wake_fd, deadline_ms);
}

} // namespace

Server::Server(ServerOptions options, std::unique_ptr<FrameSource> frames)
    : options_(options), frames_(std::move(frames)) {
    int fds[2] = {-1, -1};
#if defined(__linux__)
    const int made = ::pipe2(fds, O_CLOEXEC | O_NONBLOCK);
#else
    const int made = ::pipe(fds);
#endif
    if (made == 0) {
        set_cloexec_nonblocking(fds[0]);
        set_cloexec_nonblocking(fds[1]);
        wake_read_ = fds[0];
        wake_write_ = fds[1];
    }
}

Server::~Server() {
    request_stop();
    join();
    if (wake_read_ >= 0) {
        ::close(wake_read_);
    }
    if (wake_write_ >= 0) {
        ::close(wake_write_);
    }
}

int64_t Server::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Server::start() {
    if (thread_.joinable() || finished_.load()) {
        return;
    }
    if (wake_read_ < 0 || !frames_) {
        finish(StopReason::StartFailed);
        return;
    }
    try {
        thread_ = std::thread([this] { run(); });
    } catch (const std::system_error&) {
        finish(StopReason::StartFailed);
    }
}

void Server::request_stop() {
    if (stop_.exchange(true)) {
        return;
    }
    if (wake_write_ >= 0) {
        const char byte = 1;
        if (::write(wake_write_, &byte, 1) < 0) {
            // The pipe is full or closed: the thread wakes up anyway.
        }
    }
    if (frames_) {
        frames_->cancel();
    }
}

void Server::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

ServerStatus Server::status() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

void Server::finish(StopReason reason) {
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.phase = ServerPhase::Stopped;
        status_.reason = reason;
        status_.code.clear();
    }
    finished_.store(true);
}

void Server::publish(const Pairing& pairing) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.phase = pairing.paired() ? ServerPhase::Paired : ServerPhase::Listening;
    status_.wrong_codes = pairing.wrong_codes();
    if (pairing.paired()) {
        status_.code.clear();
    }
}

StopReason Server::find_address(Address& address) const {
    if (options_.loopback_only) {
        address.address = kLoopback;
        address.netmask = 0xFFFFFFFFU;
        address.text = "127.0.0.1";
        address.interface_name = "lo";
        return StopReason::None;
    }

    const NetworkReading reading = read_network("/", {});
    if (reading.interface_name.empty() || reading.address.empty()) {
        return StopReason::NoNetwork;
    }

    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return StopReason::NoNetwork;
    }
    StopReason result = StopReason::NoNetwork;
    for (const ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || !entry->ifa_netmask || entry->ifa_addr->sa_family != AF_INET ||
            !entry->ifa_name || reading.interface_name != entry->ifa_name) {
            continue;
        }
        sockaddr_in own{};
        sockaddr_in mask{};
        std::memcpy(&own, entry->ifa_addr, sizeof(own));
        std::memcpy(&mask, entry->ifa_netmask, sizeof(mask));
        const uint32_t value = ntohl(own.sin_addr.s_addr);
        if (address_text(value) != reading.address) {
            continue;
        }
        if (!private_address(value)) {
            result = StopReason::NotPrivate;
            break;
        }
        address.address = value;
        address.netmask = ntohl(mask.sin_addr.s_addr);
        address.text = reading.address;
        address.interface_name = reading.interface_name;
        result = StopReason::None;
        break;
    }
    ::freeifaddrs(list);
    return result;
}

void Server::run() {
    Address address;
    const StopReason found = find_address(address);
    if (found != StopReason::None) {
        finish(found);
        return;
    }

#if defined(__linux__)
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
#else
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (listen_fd < 0) {
        finish(StopReason::StartFailed);
        return;
    }
    set_cloexec_nonblocking(listen_fd);
    int on = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(options_.port);
    bind_address.sin_addr.s_addr = htonl(address.address);
    if (::bind(listen_fd, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        const int error = errno;
        ::close(listen_fd);
        finish(error == EADDRINUSE      ? StopReason::PortBusy
               : error == EADDRNOTAVAIL ? StopReason::NoNetwork
                                        : StopReason::StartFailed);
        return;
    }
    // With SO_REUSEADDR a clash with another listener can also show up here.
    if (::listen(listen_fd, kListenBacklog) != 0) {
        const int error = errno;
        ::close(listen_fd);
        finish(error == EADDRINUSE ? StopReason::PortBusy : StopReason::StartFailed);
        return;
    }

    const StopReason reason = serve(address, listen_fd);
    ::close(listen_fd);
    finish(reason);
}

StopReason Server::serve(const Address& address, int listen_fd) {
    Pairing pairing(now_ms());
    if (pairing.ended() != Pairing::End::None) {
        return StopReason::StartFailed;
    }
    expected_host_ = address.text + ":" + std::to_string(options_.port);
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.phase = ServerPhase::Listening;
        status_.url = "http://" + expected_host_;
        status_.code = pairing.code();
        status_.code_deadline_ms = pairing.code_deadline_ms();
        status_.wrong_codes = 0;
    }

    FrameCache frames(*frames_);
    int64_t next_address_check = now_ms() + kAddressCheckMs;
    for (;;) {
        if (stop_.load()) {
            return StopReason::Asked;
        }
        const int64_t now = now_ms();
        switch (pairing.check_time(now)) {
            case Pairing::End::None:
                break;
            case Pairing::End::CodeUnused:
                return StopReason::CodeUnused;
            case Pairing::End::WrongCodes:
                return StopReason::WrongCodes;
            case Pairing::End::ViewerIdle:
                return StopReason::ViewerIdle;
            case Pairing::End::NoRandom:
                return StopReason::StartFailed;
        }
        if (!options_.loopback_only && now >= next_address_check) {
            Address current;
            if (find_address(current) != StopReason::None || current.address != address.address ||
                current.netmask != address.netmask || current.interface_name != address.interface_name) {
                return StopReason::AddressChanged;
            }
            next_address_check = now + kAddressCheckMs;
        }

        pollfd fds[2] = {{listen_fd, POLLIN, 0}, {wake_read_, POLLIN, 0}};
        const int ready = ::poll(fds, 2, kIdlePollMs);
        if (ready < 0 && errno != EINTR) {
            return StopReason::StartFailed;
        }
        if (ready <= 0 || fds[1].revents != 0) {
            continue;
        }

        sockaddr_in peer{};
        socklen_t peer_size = sizeof(peer);
#if defined(__linux__)
        const int fd = ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_size, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
        const int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_size);
#endif
        if (fd < 0) {
            continue;
        }
        set_cloexec_nonblocking(fd);
        no_sigpipe(fd);

        // Only peers on the same network as the address: Linux also takes
        // packets for this address that arrive on another interface (the
        // hotspot side), so the bind alone does not keep them out.
        const uint32_t peer_address = ntohl(peer.sin_addr.s_addr);
        const bool allowed = peer.sin_family == AF_INET &&
                             (options_.loopback_only
                                  ? peer_address == kLoopback
                                  : (peer_address & address.netmask) == (address.address & address.netmask));
        if (allowed) {
            handle_connection(fd, pairing, frames);
            publish(pairing);
        }
        ::close(fd);
    }
}

void Server::handle_connection(int fd, Pairing& pairing, FrameCache& frames) {
    const int64_t deadline = now_ms() + kRequestTimeoutMs;

    // Read the head, then the body, never more than both limits together.
    std::string data;
    data.reserve(kMaxHeadBytes + kMaxBodyBytes);
    HttpRequest request;
    std::size_t head_size = 0;
    bool have_head = false;
    for (;;) {
        if (!have_head) {
            switch (parse_request_head(data, request, head_size)) {
                case HeadResult::Incomplete:
                    break;
                case HeadResult::Complete:
                    have_head = true;
                    break;
                case HeadResult::Bad:
                    send_text(fd, 400, "bad request\n", wake_read_, deadline);
                    return;
                case HeadResult::TooLarge:
                    send_text(fd, 431, "request too large\n", wake_read_, deadline);
                    return;
                case HeadResult::BodyTooLarge:
                    send_text(fd, 413, "body too large\n", wake_read_, deadline);
                    return;
            }
        }
        if (have_head && data.size() >= head_size + request.content_length) {
            break;
        }

        char chunk[512];
        const std::size_t room = kMaxHeadBytes + kMaxBodyBytes - data.size();
        const ssize_t got = ::recv(fd, chunk, room < sizeof(chunk) ? room : sizeof(chunk), 0);
        if (got > 0) {
            data.append(chunk, static_cast<std::size_t>(got));
            continue;
        }
        if (got == 0) {
            return; // the peer closed before the request was complete
        }
        if (errno == EINTR) {
            continue;
        }
        if ((errno != EAGAIN && errno != EWOULDBLOCK) || wait_for(fd, POLLIN, wake_read_, deadline) != Wait::Ready) {
            return; // an error, the 2 s ran out, or the server stops
        }
    }
    const std::string_view body = std::string_view(data).substr(head_size, request.content_length);
    const int64_t now = now_ms();

    if (request.host != expected_host_) {
        send_text(fd, 421, "wrong host\n", wake_read_, deadline);
        return;
    }

    if (request.path == "/") {
        if (request.method != "GET") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: GET\r\n");
            return;
        }
        send_text(fd, 200, kIndexText, wake_read_, deadline);
        return;
    }

    if (request.path == "/pair") {
        if (request.method != "POST") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: POST\r\n");
            return;
        }
        bool found = false;
        const std::string code = form_value(body, "code", &found);
        std::string token;
        const Pairing::Attempt attempt = found ? pairing.try_code(code, now, token) : pairing.count_wrong(now);
        switch (attempt) {
            case Pairing::Attempt::Paired: {
                const std::string cookie = std::string("Set-Cookie: ") + kCookieName + "=" + token +
                                           "; HttpOnly; SameSite=Strict; Path=/\r\n";
                send_text(fd, 200, "paired\n", wake_read_, deadline, cookie);
                return;
            }
            case Pairing::Attempt::Busy:
                send_text(fd, 409, "a browser is already paired\n", wake_read_, deadline);
                return;
            case Pairing::Attempt::Wrong:
            case Pairing::Attempt::Ended:
                send_text(fd, 403, "wrong code\n", wake_read_, deadline);
                return;
        }
        return;
    }

    if (request.path == "/screen.png") {
        if (request.method != "GET") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: GET\r\n");
            return;
        }
        if (!pairing.check_token(cookie_value(request.cookie, kCookieName), now)) {
            send_text(fd, 401, "pair first\n", wake_read_, deadline);
            return;
        }
        uint64_t since = 0;
        bool found = false;
        const std::string since_text = form_value(request.query, "since", &found);
        if (found && !parse_decimal(since_text, since)) {
            since = 0;
        }
        if (!frames.refresh(now_ms())) {
            send_text(fd, 503, "no picture yet\n", wake_read_, deadline);
            return;
        }
        const std::string frame_header = "X-Frame: " + std::to_string(frames.frame()) + "\r\n";
        if (since == frames.frame()) {
            send_all(fd, response_head(204, {}, 0, frame_header), wake_read_, deadline);
            return;
        }
        const std::vector<uint8_t>* png = frames.png();
        if (!png) {
            send_text(fd, 503, "no picture yet\n", wake_read_, deadline);
            return;
        }
        std::string answer = response_head(200, "image/png", png->size(), frame_header);
        answer.append(reinterpret_cast<const char*>(png->data()), png->size());
        send_all(fd, answer, wake_read_, deadline);
        return;
    }

    send_text(fd, 404, "not found\n", wake_read_, deadline);
}

} // namespace platform::remote
