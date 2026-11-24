/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote/remote_server.h"

#include "network.h"
#include "remote/remote_addresses.h"
#include "remote/remote_http.h"
#include "remote/remote_keys.h"
#include "remote/remote_page.h"
#include "remote/remote_web.h"
#include "report.h"
#include "report_files.h"
#include "wireguard.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string_view>
#include <system_error>
#include <utility>

namespace platform::remote {
namespace {

constexpr int64_t kRequestTimeoutMs = 2000;
// A report is sent within this long (a few tens of kilobytes; at most
// kReportFileMax).
constexpr int64_t kFileSendTimeoutMs = 10000;
constexpr int64_t kAddressCheckMs = 2000;
constexpr int kIdlePollMs = 1000;
// A browser opens several connections at once for the dashboard's files.
constexpr int kListenBacklog = 16;
// Linux takes a connection only once data arrived, or after this long.
constexpr int kDeferAcceptSeconds = 2;
// A file of the dashboard is sent in pieces of this size, so a font never
// sits in memory whole; files above kWebFileMax are not sent.
constexpr std::size_t kFileChunk = 16 * 1024;
constexpr off_t kWebFileMax = 2 * 1024 * 1024;
constexpr uint32_t kLoopback = 0x7F000001U; // 127.0.0.1
constexpr const char* kCookieName = "lokictl_session";
// The tools the browser may start by name (POST /api/v1/tool), and the
// longest target it may send for them (model::kToolTargetMax).
constexpr std::string_view kBrowserTools[] = {"ping", "dns", "trace", "ports", "speed"};
constexpr std::size_t kMaxTargetBytes = 63;

bool browser_tool(std::string_view name) {
    for (const std::string_view tool : kBrowserTools) {
        if (tool == name) {
            return true;
        }
    }
    return false;
}

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

// Opens a listening socket on `address` (host byte order) and `port`.
// Returns the socket, or -1 with `error` set to the errno of the step that
// failed.
int open_listener(uint32_t address, uint16_t port, int& error) {
#if defined(__linux__)
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
#else
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (fd < 0) {
        error = errno;
        return -1;
    }
    set_cloexec_nonblocking(fd);
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#if defined(TCP_DEFER_ACCEPT)
    // A spare connection a browser opens ahead of time sends nothing; it
    // would hold the one-at-a-time server for the whole 2 s timeout.
    int defer = kDeferAcceptSeconds;
    ::setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer, sizeof(defer));
#endif

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(port);
    bind_address.sin_addr.s_addr = htonl(address);
    // With SO_REUSEADDR a clash with another listener can also show up at
    // listen().
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0 ||
        ::listen(fd, kListenBacklog) != 0) {
        error = errno;
        ::close(fd);
        return -1;
    }
    return fd;
}

void close_side(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
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

void send_body(int fd, int status, std::string_view content_type, std::string_view body, int wake_fd,
               int64_t deadline_ms, std::string_view extra_headers = {}) {
    std::string answer = response_head(status, content_type, body.size(), extra_headers);
    answer.append(body);
    send_all(fd, answer, wake_fd, deadline_ms);
}

void send_text(int fd, int status, std::string_view text, int wake_fd, int64_t deadline_ms,
               std::string_view extra_headers = {}) {
    send_body(fd, status, "text/plain; charset=utf-8", text, wake_fd, deadline_ms, extra_headers);
}

// Sends one file of the web dashboard from `folder`, read and sent a piece
// at a time; 404 when it is missing, not a plain file or too large.
void send_web_file(int fd, const std::string& folder, const WebFile& file, int wake_fd, int64_t deadline_ms) {
    int source = -1;
    if (!folder.empty()) {
        const std::string path = folder + "/" + std::string(file.name);
#if defined(O_CLOEXEC)
        source = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
#else
        source = ::open(path.c_str(), O_RDONLY);
#endif
    }
    struct stat info {};
    if (source < 0 || ::fstat(source, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > kWebFileMax) {
        if (source >= 0) {
            ::close(source);
        }
        send_text(fd, 404, "not found\n", wake_fd, deadline_ms);
        return;
    }
    std::size_t left = static_cast<std::size_t>(info.st_size);
    bool sending = send_all(fd, response_head(200, file.content_type, left, {}, file.cache_control), wake_fd, deadline_ms);
    char chunk[kFileChunk];
    while (sending && left > 0) {
        const ssize_t got = ::read(source, chunk, left < sizeof(chunk) ? left : sizeof(chunk));
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            break; // the file got shorter: the browser sees a cut-off answer
        }
        left -= static_cast<std::size_t>(got);
        sending = send_all(fd, std::string_view(chunk, static_cast<std::size_t>(got)), wake_fd, deadline_ms);
    }
    ::close(source);
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

void Server::set_hotspot_interface(const std::string& interface_name) {
    std::lock_guard<std::mutex> lock(hotspot_mutex_);
    hotspot_interface_ = interface_name;
}

void Server::set_status_json(std::string json) {
    std::lock_guard<std::mutex> lock(json_mutex_);
    status_json_ = std::move(json);
}

void Server::set_hotspot_json(std::string json) {
    std::lock_guard<std::mutex> lock(json_mutex_);
    hotspot_json_ = std::move(json);
}

void Server::finish(StopReason reason) {
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.phase = ServerPhase::Stopped;
        status_.reason = reason;
        status_.code.clear();
        status_.hotspot_url.clear();
    }
    finished_.store(true);
}

void Server::publish_hotspot_url(const std::string& url) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.hotspot_url = url;
}

void Server::publish(const Pairing& pairing) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.phase = pairing.paired() ? ServerPhase::Paired : ServerPhase::Listening;
    status_.wrong_codes = pairing.wrong_codes();
    if (pairing.paired()) {
        status_.code.clear();
    }
}

StopReason Server::find_address(InterfaceAddress& address) const {
    if (options_.loopback_only) {
        address.interface_name = "lo";
        address.address = kLoopback;
        address.netmask = 0xFFFFFFFFU;
        return StopReason::None;
    }

    const NetworkReading reading = read_network("/", {});
    if (reading.interface_name.empty() || reading.address.empty()) {
        return StopReason::NoNetwork;
    }
    for (const InterfaceAddress& entry : system_addresses()) {
        if (entry.interface_name != reading.interface_name || address_text(entry.address) != reading.address) {
            continue;
        }
        if (!private_address(entry.address)) {
            return StopReason::NotPrivate;
        }
        address = entry;
        return StopReason::None;
    }
    return StopReason::NoNetwork;
}

void Server::run() {
    Side sides[kSideCount];
    const StopReason found = find_address(sides[kUplinkSide].address);
    if (found != StopReason::None) {
        finish(found);
        return;
    }

    int error = 0;
    sides[kUplinkSide].fd = open_listener(sides[kUplinkSide].address.address, options_.port, error);
    if (sides[kUplinkSide].fd < 0) {
        finish(error == EADDRINUSE      ? StopReason::PortBusy
               : error == EADDRNOTAVAIL ? StopReason::NoNetwork
                                        : StopReason::StartFailed);
        return;
    }
    sides[kUplinkSide].host = address_text(sides[kUplinkSide].address.address) + ":" + std::to_string(options_.port);

    const StopReason reason = serve(sides);
    for (Side& side : sides) {
        close_side(side.fd);
    }
    finish(reason);
}

bool Server::update_hotspot_side(Side (&sides)[kSideCount], const std::string& wanted, const Pairing& pairing) {
    InterfaceAddress pick;
    const bool found = !wanted.empty() && pick_hotspot_address(system_addresses(), wanted, sides[kUplinkSide].address, pick);

    Side& hotspot = sides[kHotspotSide];
    if (hotspot.fd >= 0) {
        if (found && pick.interface_name == hotspot.address.interface_name && pick.address == hotspot.address.address &&
            pick.netmask == hotspot.address.netmask) {
            return true; // open where it should be
        }
        close_side(hotspot.fd);
        hotspot.host.clear();
        publish_hotspot_url({});
        if (paired_side_ == kHotspotSide && pairing.paired()) {
            return false;
        }
    }
    if (!found) {
        return true;
    }
    int error = 0;
    hotspot.fd = open_listener(pick.address, options_.port, error);
    if (hotspot.fd < 0) {
        return true; // tried again at the next check
    }
    hotspot.address = pick;
    hotspot.host = address_text(pick.address) + ":" + std::to_string(options_.port);
    publish_hotspot_url("http://" + hotspot.host);
    return true;
}

StopReason Server::serve(Side (&sides)[kSideCount]) {
    Pairing pairing(now_ms());
    if (pairing.ended() != Pairing::End::None) {
        return StopReason::StartFailed;
    }
    paired_side_ = -1;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.phase = ServerPhase::Listening;
        status_.url = "http://" + sides[kUplinkSide].host;
        status_.hotspot_url.clear();
        status_.code = pairing.code();
        status_.code_deadline_ms = pairing.code_deadline_ms();
        status_.wrong_codes = 0;
    }

    FrameCache frames(*frames_);
    int64_t next_address_check = now_ms() + kAddressCheckMs;
    int64_t next_hotspot_check = 0;
    std::string hotspot_seen;
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
            InterfaceAddress current;
            const InterfaceAddress& own = sides[kUplinkSide].address;
            if (find_address(current) != StopReason::None || current.address != own.address ||
                current.netmask != own.netmask || current.interface_name != own.interface_name) {
                return StopReason::AddressChanged;
            }
            next_address_check = now + kAddressCheckMs;
        }
        if (!options_.loopback_only) {
            std::string wanted;
            {
                std::lock_guard<std::mutex> lock(hotspot_mutex_);
                wanted = hotspot_interface_;
            }
            // Every 2 s while a hotspot interface is named or the side is
            // open, and at once when the name changed.
            const bool check_due = (!wanted.empty() || sides[kHotspotSide].fd >= 0) && now >= next_hotspot_check;
            if (wanted != hotspot_seen || check_due) {
                hotspot_seen = wanted;
                next_hotspot_check = now + kAddressCheckMs;
                if (!update_hotspot_side(sides, wanted, pairing)) {
                    return StopReason::HotspotClosed;
                }
            }
        }

        pollfd fds[kSideCount + 1] = {};
        int side_of[kSideCount + 1] = {};
        nfds_t count = 0;
        for (int side = 0; side < kSideCount; ++side) {
            if (sides[side].fd >= 0) {
                fds[count] = {sides[side].fd, POLLIN, 0};
                side_of[count] = side;
                ++count;
            }
        }
        const nfds_t wake_index = count;
        fds[count++] = {wake_read_, POLLIN, 0};
        const int ready = ::poll(fds, count, kIdlePollMs);
        if (ready < 0 && errno != EINTR) {
            return StopReason::StartFailed;
        }
        if (ready <= 0 || fds[wake_index].revents != 0) {
            continue;
        }

        for (nfds_t index = 0; index < wake_index; ++index) {
            if (fds[index].revents == 0) {
                continue;
            }
            const int side = side_of[index];
            const Side& own = sides[side];
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
#if defined(__linux__)
            const int fd = ::accept4(own.fd, reinterpret_cast<sockaddr*>(&peer), &peer_size, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
            const int fd = ::accept(own.fd, reinterpret_cast<sockaddr*>(&peer), &peer_size);
#endif
            if (fd < 0) {
                continue;
            }
            set_cloexec_nonblocking(fd);
            no_sigpipe(fd);

            // Only peers on the same network as this side's address: Linux
            // also takes packets for an address that arrive on another
            // interface (the hotspot side for the uplink's address, and the
            // other way round), so the bind alone does not keep them out.
            const uint32_t peer_address = ntohl(peer.sin_addr.s_addr);
            const bool allowed = peer.sin_family == AF_INET &&
                                 (options_.loopback_only ? peer_address == kLoopback
                                                         : in_subnet(peer_address, own.address));
            if (allowed) {
                handle_connection(fd, side, own, pairing, frames);
                publish(pairing);
            }
            ::close(fd);
        }
    }
}

bool Server::accept_change(int fd, const HttpRequest& request, int side, const Side& own, Pairing& pairing,
                           int64_t now, int64_t deadline) {
    if (request.method != "POST") {
        send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: POST\r\n");
        return false;
    }
    // Only the page itself may change something: the browser names the
    // page's origin. Checked before the cookie, so a refused request is no
    // sign of life.
    if (request.origin != "http://" + own.host) {
        send_text(fd, 403, "wrong origin\n", wake_read_, deadline);
        return false;
    }
    // The session first, so a page whose session ended learns to pair
    // again; then the request token, which only the page can know.
    const Pairing::Change change =
        side != paired_side_ ? Pairing::Change::NoSession
                             : pairing.check_change(cookie_value(request.cookie, kCookieName), request.token, now);
    if (change == Pairing::Change::NoSession) {
        send_text(fd, 401, "pair first\n", wake_read_, deadline);
        return false;
    }
    if (change == Pairing::Change::WrongToken) {
        send_text(fd, 403, "wrong token\n", wake_read_, deadline);
        return false;
    }
    return true;
}

void Server::save_vpn_file(int fd, const HttpRequest& request, std::string_view body, int64_t deadline) {
    const std::string name = form_value(request.query, "name");
    std::string server;
    std::string reason;
    const WireGuardSave saved = save_wireguard_file(options_.vpn_folder, name, body, server, reason);
    int status = 200;
    std::string error;
    switch (saved) {
        case WireGuardSave::Saved:
            break;
        case WireGuardSave::NoFolder:
            status = 409;
            error = "VPN files can only be added on the Cardputer itself, not to sample data.";
            break;
        case WireGuardSave::BadName:
            status = 400;
            error = "Give the file a name of up to 32 letters, digits, dots, dashes or underscores.";
            break;
        case WireGuardSave::TooLarge:
            status = 413;
            error = "This file is too large for a WireGuard file (16 KB at most).";
            break;
        case WireGuardSave::NotWireGuard:
            status = 400;
            error = "This isn't a WireGuard file LokiCTL can use (" + reason + ").";
            break;
        case WireGuardSave::NameTaken:
            status = 409;
            error = "There is already a VPN file called " + name + ". Rename yours and try again.";
            break;
        case WireGuardSave::Full:
            status = 409;
            error = "The Cardputer already holds 16 VPN files, the most it lists.";
            break;
        case WireGuardSave::Failed:
            status = 500;
            error = "The Cardputer couldn't save the file. Try again.";
            break;
    }
    // The name is a checked one (valid_upload_name()) and the host a checked
    // name or address; json_text() escapes them anyway.
    const std::string json = status == 200
                                 ? "{\"name\":" + model::json_text(name) + ",\"server\":" + model::json_text(server) + "}\n"
                                 : "{\"error\":" + model::json_text(error) + "}\n";
    send_body(fd, status, "application/json; charset=utf-8", json, wake_read_, deadline);
}

void Server::handle_connection(int fd, int side, const Side& own, Pairing& pairing, FrameCache& frames) {
    const int64_t deadline = now_ms() + kRequestTimeoutMs;

    // Read the head, then the body, never more than both limits together.
    // The buffer is taken once at its largest, so it never moves and leaves
    // no copy behind; a WireGuard file may pass through it, so whatever came
    // in is wiped whichever way the request ends.
    std::string data;
    data.reserve(kMaxHeadBytes + kMaxUploadBytes);
    struct Wipe {
        std::string& text;
        ~Wipe() {
            // Bytes past the size were read in too (see the loop below).
            text.resize(text.capacity());
            volatile char* bytes = &text[0];
            for (std::size_t index = 0; index < text.size(); ++index) {
                bytes[index] = 0;
            }
        }
    } wipe{data};
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

        // Straight into the buffer, at most up to the end of this request.
        const std::size_t most = have_head ? head_size + request.content_length : kMaxHeadBytes + kMaxBodyBytes;
        const std::size_t room = std::min<std::size_t>(most - data.size(), 4096);
        const std::size_t had = data.size();
        data.resize(had + room);
        const ssize_t got = ::recv(fd, &data[had], room, 0);
        data.resize(had + (got > 0 ? static_cast<std::size_t>(got) : 0));
        if (got > 0) {
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

    if (request.host != own.host) {
        send_text(fd, 421, "wrong host\n", wake_read_, deadline);
        return;
    }

    if (const WebFile* file = find_web_file(request.path)) {
        if (request.method != "GET") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: GET\r\n");
            return;
        }
        const std::string& folder = file->folder == WebFolder::Web ? options_.web_folder : options_.font_folder;
        send_web_file(fd, folder, *file, wake_read_, now_ms() + kFileSendTimeoutMs);
        return;
    }

    if (request.path == "/screen") {
        if (request.method != "GET") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: GET\r\n");
            return;
        }
        const std::string policy =
            std::string("Content-Security-Policy: ").append(page_security_policy()).append("\r\n");
        send_body(fd, 200, "text/html; charset=utf-8", page_html(), wake_read_, deadline, policy);
        return;
    }

    if (request.path == "/api/v1/session" || request.path == "/api/v1/status" ||
        (request.path == "/api/v1/hotspot" && request.method == "GET")) {
        if (request.method != "GET") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: GET\r\n");
            return;
        }
        if (side != paired_side_ || !pairing.check_token(cookie_value(request.cookie, kCookieName), now)) {
            send_text(fd, 401, "pair first\n", wake_read_, deadline);
            return;
        }
        std::string json;
        if (request.path == "/api/v1/session") {
            // The request token is 32 hex digits: nothing to escape.
            json = "{\"token\":\"" + pairing.request_token() + "\"}\n";
        } else {
            std::lock_guard<std::mutex> lock(json_mutex_);
            json = request.path == "/api/v1/status" ? status_json_ : hotspot_json_;
        }
        if (json.empty()) {
            send_text(fd, 503, "no data yet\n", wake_read_, deadline);
            return;
        }
        send_body(fd, 200, "application/json; charset=utf-8", json, wake_read_, deadline);
        return;
    }

    if (request.path == "/key") {
        if (!accept_change(fd, request, side, own, pairing, now, deadline)) {
            return;
        }
        uint32_t key = 0;
        if (!options_.keys || !key_from_name(form_value(body, "k"), key)) {
            send_text(fd, 400, "unknown key\n", wake_read_, deadline);
            return;
        }
        if (options_.keys->push(key, now) != KeyQueue::Push::Added) {
            send_text(fd, 429, "too many keys\n", wake_read_, deadline);
            return;
        }
        send_all(fd, response_head(204, {}, 0), wake_read_, deadline);
        return;
    }

    if (request.path == "/api/v1/block") {
        if (!accept_change(fd, request, side, own, pairing, now, deadline)) {
            return;
        }
        BlockRequest block;
        block.address = form_value(body, "id");
        const std::string on = form_value(body, "on");
        if (!options_.blocks || !dotted_ipv4(block.address) || (on != "0" && on != "1")) {
            send_text(fd, 400, "bad request\n", wake_read_, deadline);
            return;
        }
        block.block = on == "1";
        if (!options_.blocks->push(std::move(block))) {
            send_text(fd, 429, "too many requests\n", wake_read_, deadline);
            return;
        }
        send_all(fd, response_head(204, {}, 0), wake_read_, deadline);
        return;
    }

    if (request.path == kUploadPath) {
        if (accept_change(fd, request, side, own, pairing, now, deadline)) {
            save_vpn_file(fd, request, body, deadline);
        }
        return;
    }

    if (request.path == "/api/v1/check" || request.path == "/api/v1/report" || request.path == "/api/v1/mode" ||
        request.path == "/api/v1/vpn" || request.path == "/api/v1/hotspot" || request.path == "/api/v1/keep" ||
        request.path == "/api/v1/leak" || request.path == "/api/v1/tool" || request.path == "/api/v1/lan" ||
        request.path == "/api/v1/survey" || request.path == "/api/v1/watch" || request.path == "/api/v1/trap") {
        if (!accept_change(fd, request, side, own, pairing, now, deadline)) {
            return;
        }
        // What the body asks, or nothing (400).
        bool valid = true;
        Action action = Action::SaveReport;
        std::string text;
        std::string tool;
        bool found = false;
        if (request.path == "/api/v1/check") {
            const std::string run = form_value(body, "run");
            if (run == "1" && form_value(body, "own") == "1") {
                action = Action::StartCheck;
            } else if (run == "0") {
                action = Action::StopCheck;
            } else {
                valid = false;
            }
        } else if (request.path == "/api/v1/mode") {
            text = form_value(body, "mode");
            action = Action::Mode;
            valid = text == "client" || text == "router" || text == "share";
        } else if (request.path == "/api/v1/vpn") {
            const std::string on = form_value(body, "on", &found);
            if (found) {
                action = on == "1" ? Action::VpnOn : Action::VpnOff;
                valid = on == "1" || on == "0";
            } else {
                action = Action::VpnFile;
                valid = form_decode(form_value(body, "file", &found), text) && found && !text.empty();
            }
        } else if (request.path == "/api/v1/hotspot") {
            const std::string password = form_value(body, "password", &found);
            if (found) {
                action = Action::HotspotPassword;
                valid = password == "new";
            } else {
                action = Action::HotspotName;
                valid = form_decode(form_value(body, "name", &found), text) && found;
            }
        } else if (request.path == "/api/v1/keep") {
            const std::string on = form_value(body, "on");
            action = on == "1" ? Action::KeepOn : Action::KeepOff;
            valid = on == "1" || on == "0";
        } else if (request.path == "/api/v1/leak") {
            const std::string run = form_value(body, "run");
            action = run == "1" ? Action::StartLeak : Action::StopLeak;
            valid = run == "1" || run == "0";
        } else if (request.path == "/api/v1/tool") {
            // run=<tool>&target=<target> (Ports also own=1, the page's
            // "own network only" confirmation; Speed no target), or
            // stop=<tool>. Only the Tools page's own tools.
            const std::string stop = form_value(body, "stop", &found);
            if (found) {
                action = Action::ToolStop;
                tool = stop;
                valid = browser_tool(tool);
            } else {
                action = Action::ToolRun;
                tool = form_value(body, "run");
                bool has_target = false;
                const std::string target = form_value(body, "target", &has_target);
                valid = browser_tool(tool) && form_decode(target, text) && text.size() <= kMaxTargetBytes &&
                        (tool == "speed" ? !has_target : !text.empty()) &&
                        (tool != "ports" || form_value(body, "own") == "1");
            }
        } else if (request.path == "/api/v1/lan") {
            // test=<IPv4> or stop=1. The Cardputer as the server stays on
            // the Cardputer: it listens only while its LAN page is shown.
            text = form_value(body, "test", &found);
            if (found) {
                action = Action::LanTest;
                valid = dotted_ipv4(text);
            } else {
                action = Action::LanStop;
                valid = form_value(body, "stop") == "1";
            }
        } else if (request.path == "/api/v1/survey") {
            const std::string run = form_value(body, "run");
            action = run == "1" ? Action::SurveyStart : Action::SurveyStop;
            valid = run == "1" || run == "0";
        } else if (request.path == "/api/v1/watch" || request.path == "/api/v1/trap") {
            const bool watch = request.path == "/api/v1/watch";
            const std::string on = form_value(body, "on");
            action = on == "1" ? (watch ? Action::WatchStart : Action::TrapStart)
                               : (watch ? Action::WatchStop : Action::TrapStop);
            valid = on == "1" || on == "0";
        }
        if (!valid || !options_.actions) {
            send_text(fd, 400, "bad request\n", wake_read_, deadline);
            return;
        }
        if (!options_.actions->push(action, std::move(text), std::move(tool))) {
            send_text(fd, 429, "too many requests\n", wake_read_, deadline);
            return;
        }
        send_all(fd, response_head(204, {}, 0), wake_read_, deadline);
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
                paired_side_ = side;
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
        if (side != paired_side_ || !pairing.check_token(cookie_value(request.cookie, kCookieName), now)) {
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

    if (request.path == "/reports" || request.path == "/report") {
        if (request.method != "GET") {
            send_text(fd, 405, "method not allowed\n", wake_read_, deadline, "Allow: GET\r\n");
            return;
        }
        if (side != paired_side_ || !pairing.check_token(cookie_value(request.cookie, kCookieName), now)) {
            send_text(fd, 401, "pair first\n", wake_read_, deadline);
            return;
        }
        if (request.path == "/reports") {
            // Every name is a checked report name: letters, digits, '-' and '.' only.
            std::string list = "{\"reports\":[";
            bool first = true;
            for (const ReportFile& file : list_reports(options_.reports_folder)) {
                list += first ? "" : ",";
                list += "{\"name\":\"" + file.name + "\",\"bytes\":" + std::to_string(file.bytes) + "}";
                first = false;
            }
            list += "]}\n";
            send_body(fd, 200, "application/json", list, wake_read_, deadline);
            return;
        }
        const std::string name = form_value(request.query, "name");
        std::string content;
        if (!valid_report_name(name) || !read_report(options_.reports_folder, name, content)) {
            send_text(fd, 404, "no such report\n", wake_read_, deadline);
            return;
        }
        const bool page = name.size() > 5 && name.compare(name.size() - 5, 5, ".html") == 0;
        const std::string headers = "Content-Disposition: attachment; filename=\"" + name +
                                    "\"\r\n"
                                    "Content-Security-Policy: sandbox; default-src 'none'; style-src 'unsafe-inline'\r\n";
        send_body(fd, 200, page ? "text/html; charset=utf-8" : "text/csv; charset=utf-8", content, wake_read_,
                  now_ms() + kFileSendTimeoutMs, headers);
        return;
    }

    send_text(fd, 404, "not found\n", wake_read_, deadline);
}

} // namespace platform::remote
