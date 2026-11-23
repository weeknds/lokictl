/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "remote/remote_frames.h"
#include "remote/remote_pairing.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace platform::remote {

// The port Remote view listens on.
constexpr uint16_t kPort = 8780;

enum class ServerPhase {
    Starting,  // finding the address and opening the port
    Listening, // waiting for the code
    Paired,    // a browser is paired
    Stopped,   // the port is closed; StopReason says why
};

enum class StopReason {
    None,
    Asked,          // switched off on the Cardputer, or the app quits
    NoNetwork,      // no network address to listen on
    NotPrivate,     // the network's address is not a private one
    PortBusy,       // another program holds the port
    StartFailed,    // the port could not be opened, or no random bytes
    CodeUnused,     // the code was not used in time
    WrongCodes,     // too many wrong codes
    ViewerIdle,     // the paired browser sent nothing for too long
    AddressChanged, // the address went away or changed
};

// What the UI thread may know about the server. The code is only for the
// Cardputer's own screen: it never goes to the log.
struct ServerStatus {
    ServerPhase phase{ServerPhase::Starting};
    StopReason reason{StopReason::None};
    std::string url;               // "http://<address>:8780" once listening
    std::string code;              // empty once paired
    int64_t code_deadline_ms{0};   // on the steady clock (Server::now_ms())
    int wrong_codes{0};
};

struct ServerOptions {
    // The desktop simulator: listen on 127.0.0.1 and accept peers from
    // 127.0.0.1 only. Otherwise the server listens on the private IPv4
    // address of the interface that carries the default route, and accepts
    // peers from that interface's subnet only.
    bool loopback_only{false};
    uint16_t port{kPort};
};

// The Remote view server: a small HTTP/1.1 server on its own thread, never
// the UI thread. One connection at a time, one request per connection
// (Connection: close), a head of at most 2 KB, a body of at most 64 bytes
// and 2 s to send them.
//
// - It binds the one address above, never 0.0.0.0, and closes a connection
//   from any other peer at once, before reading anything.
// - A Host header other than "<address>:<port>" gets 421 (this stops DNS
//   rebinding).
// - GET / gives a short plain text. POST /pair with code=NNNNNN pairs one
//   browser (see Pairing) and sets the session cookie (HttpOnly,
//   SameSite=Strict, Path=/). GET /screen.png?since=N needs the cookie
//   (else 401) and answers 204 while frame N is current, else the screen
//   as a PNG with X-Frame: M. Other paths get 404, a wrong method 405.
// - It ends by itself (see StopReason) when the pairing ends, or when the
//   address it listens on goes away or changes (checked every 2 s).
//
// request_stop() never blocks: the thread sees it within a few
// milliseconds (it waits on a pipe next to its sockets) and closes the
// port. The UI thread then calls join(), which returns at once once
// finished() is true.
class Server {
public:
    Server(ServerOptions options, std::unique_ptr<FrameSource> frames);
    // Stops the thread and waits for it.
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Starts the thread. Call once.
    void start();
    // Asks the thread to stop; returns at once.
    void request_stop();
    // True once the thread has closed the port and ended.
    bool finished() const { return finished_.load(); }
    // Waits for the thread to end.
    void join();

    ServerStatus status() const;

    // The steady clock in milliseconds, as the status uses it.
    static int64_t now_ms();

private:
    struct Address {
        uint32_t address{0}; // host byte order
        uint32_t netmask{0}; // host byte order
        std::string text;
        std::string interface_name;
    };

    void run();
    StopReason serve(const Address& address, int listen_fd);
    StopReason find_address(Address& address) const;
    void handle_connection(int fd, Pairing& pairing, FrameCache& frames);
    void publish(const Pairing& pairing);
    void finish(StopReason reason);

    ServerOptions options_;
    std::unique_ptr<FrameSource> frames_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{false};
    int wake_read_{-1};
    int wake_write_{-1};
    std::string expected_host_;

    mutable std::mutex status_mutex_;
    ServerStatus status_;
};

} // namespace platform::remote
