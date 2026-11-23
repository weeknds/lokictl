/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "signin_fetch.h"

#include "signin_check.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

namespace platform {
namespace {

// The made-up check: the answer after this long.
constexpr uint32_t kPretendAnswerMs = 600;
// Documentation address of the check page.
constexpr const char* kPretendCheckAddress = "192.0.2.80";

using Clock = std::chrono::steady_clock;

constexpr auto kConnectLimit = std::chrono::milliseconds(8000);
constexpr auto kTotalLimit = std::chrono::milliseconds(10000);
// The longest single wait, so a stop is seen quickly.
constexpr int kWaitSliceMs = 100;

int slice_until(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return static_cast<int>(std::clamp<int64_t>(left, 0, kWaitSliceMs));
}

std::string reason_of(int number) {
    switch (number) {
        case ECONNREFUSED:
            return "connection refused";
        case ENETUNREACH:
        case EHOSTUNREACH:
            return "can't reach the web";
        case ECONNRESET:
            return "connection reset";
        case ETIMEDOUT:
            return "no answer";
        default:
            break;
    }
    const char* text = std::strerror(number);
    return text != nullptr ? std::string(text) : "error " + std::to_string(number);
}

// The socket, closed on the way out whatever happens.
struct Socket {
    int fd{-1};
    ~Socket() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

// Whether `head` holds the blank line that ends an answer's head.
bool head_complete(const std::string& head) {
    return head.find("\r\n\r\n") != std::string::npos || head.find("\n\n") != std::string::npos;
}

} // namespace

model::SigninRun pretend_signin_check(uint32_t elapsed_ms, const model::SigninLocal& local, uint32_t& sent,
                                      bool& done) {
    model::SigninRun run;
    run.state = model::ToolState::Running;
    if (!local.link) {
        run.state = model::ToolState::Failed;
        run.error = model::ToolError::Failed;
        run.reason = "no network";
        done = true;
        return run;
    }
    run.address = kPretendCheckAddress;
    sent = 2;  // the DNS question and the request
    if (elapsed_ms < kPretendAnswerMs) {
        return run;
    }
    model::SigninReply reply;
    reply.http = true;
    if (local.internet == model::Internet::Portal) {
        reply.status = 302;
        reply.location = kPretendSigninTarget;
    } else {
        reply.status = 204;
    }
    model::judge_signin(run, reply);
    run.state = model::ToolState::Done;
    done = true;
    return run;
}

std::string fetch_signin_head(const sockaddr_in& server, const std::string& host, const std::string& path,
                              const std::function<bool()>& stopping, std::string& head) {
    head.clear();
    const Clock::time_point started = Clock::now();
    Socket socket;
    socket.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket.fd < 0) {
        return reason_of(errno);
    }
    ::fcntl(socket.fd, F_SETFD, FD_CLOEXEC);
    const int flags = ::fcntl(socket.fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket.fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return reason_of(errno);
    }
#ifdef SO_NOSIGPIPE
    const int one = 1;
    ::setsockopt(socket.fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    // Connect, waiting in short slices.
    if (::connect(socket.fd, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) != 0) {
        if (errno != EINPROGRESS) {
            return reason_of(errno);
        }
        const Clock::time_point deadline = started + kConnectLimit;
        while (true) {
            if (stopping()) {
                return "stopped";
            }
            if (Clock::now() >= deadline) {
                return "no answer";
            }
            pollfd entry{socket.fd, POLLOUT, 0};
            if (::poll(&entry, 1, slice_until(deadline)) > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                if (::getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
                    error = errno;
                }
                if (error != 0) {
                    return reason_of(error);
                }
                break;
            }
        }
    }

    const Clock::time_point deadline = started + kTotalLimit;
    // The request: no keep-alive, nothing that asks for a body format.
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                                "\r\nUser-Agent: lokictl\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    std::size_t written = 0;
    while (written < request.size()) {
        if (stopping()) {
            return "stopped";
        }
        if (Clock::now() >= deadline) {
            return "no answer";
        }
        const ssize_t size = ::send(socket.fd, request.data() + written, request.size() - written, MSG_NOSIGNAL);
        if (size > 0) {
            written += static_cast<std::size_t>(size);
            continue;
        }
        if (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return reason_of(errno);
        }
        pollfd entry{socket.fd, POLLOUT, 0};
        ::poll(&entry, 1, slice_until(deadline));
    }

    // Read the head only.
    char buffer[1024];
    while (head.size() < model::kSigninHeadMax && !head_complete(head)) {
        if (stopping()) {
            return "stopped";
        }
        if (Clock::now() >= deadline) {
            break;
        }
        pollfd entry{socket.fd, POLLIN, 0};
        if (::poll(&entry, 1, slice_until(deadline)) <= 0) {
            continue;
        }
        const std::size_t room = std::min(sizeof(buffer), model::kSigninHeadMax - head.size());
        const ssize_t size = ::recv(socket.fd, buffer, room, 0);
        if (size > 0) {
            head.append(buffer, static_cast<std::size_t>(size));
            continue;
        }
        if (size == 0) {
            break;  // closed by the other end
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            if (head.empty()) {
                return reason_of(errno);
            }
            break;
        }
    }
    if (head.empty()) {
        return "no answer";
    }
    return {};
}

} // namespace platform
