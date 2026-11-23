/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "remote/remote_addresses.h"
#include "remote/remote_frames.h"
#include "remote/remote_keys.h"
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
    HotspotClosed,  // the hotspot side closed while the browser paired through it
};

// What the UI thread may know about the server. The code is only for the
// Cardputer's own screen: it never goes to the log.
struct ServerStatus {
    ServerPhase phase{ServerPhase::Starting};
    StopReason reason{StopReason::None};
    std::string url;               // "http://<address>:8780" once listening
    std::string hotspot_url;       // the same for the hotspot side while it is open, else empty
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
    // Where POST /key puts the keys of the paired browser. Owned by the
    // caller and kept until the server's thread has ended; without it every
    // key gets 400.
    KeyQueue* keys{nullptr};
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
// - GET / gives the web page (page_html()) with its Content-Security-Policy.
//   POST /pair with code=NNNNNN pairs one browser (see Pairing) and sets
//   the session cookie (HttpOnly, SameSite=Strict, Path=/).
//   GET /screen.png?since=N needs the cookie (else 401) and answers 204
//   while frame N is current, else the screen as a PNG with X-Frame: M.
//   POST /key with k=<name> (key_from_name()) needs an Origin header of
//   exactly "http://<address>:<port>" (else 403) and the cookie (else 401);
//   an unknown name gets 400, a full queue or too many keys a second 429
//   (KeyQueue), an accepted key 204. Other paths get 404, a wrong method
//   405.
// - It ends by itself (see StopReason) when the pairing ends, or when the
//   address it listens on goes away or changes (checked every 2 s).
// - The hotspot side (set_hotspot_interface()): while an interface is named,
//   a second listener on that interface's address (pick_hotspot_address()),
//   same port, accepts peers from that interface's subnet only. Each side
//   checks the peer, Host and Origin against its own address; the pairing
//   belongs to the side it was made on, and a cookie sent to the other side
//   gets 401. The side opens and closes with the 2 s check, and within
//   about a second of a change of the name, without stopping the server;
//   when it closes while the browser paired through it, the server ends
//   (HotspotClosed). A port that can't be opened there only leaves that
//   side closed. The desktop simulator (loopback_only) never opens it.
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

    // The interface of LokiCTL's own hotspot while hotspot devices may use
    // Remote view; empty (the start) for none. Safe from any thread; the
    // server's thread acts on it within about a second.
    void set_hotspot_interface(const std::string& interface_name);

    // The steady clock in milliseconds, as the status uses it.
    static int64_t now_ms();

private:
    // One listening address: the uplink's (side 0) or the hotspot's
    // (side 1).
    struct Side {
        int fd{-1};
        InterfaceAddress address;
        std::string host; // "<address>:<port>": what Host must say
    };
    static constexpr int kUplinkSide = 0;
    static constexpr int kHotspotSide = 1;
    static constexpr int kSideCount = 2;

    void run();
    StopReason serve(Side (&sides)[kSideCount]);
    StopReason find_address(InterfaceAddress& address) const;
    // Opens, moves or closes the hotspot side for the interface `wanted`
    // (empty: none). Returns false when it closed while the browser paired
    // through it.
    bool update_hotspot_side(Side (&sides)[kSideCount], const std::string& wanted, const Pairing& pairing);
    void handle_connection(int fd, int side, const Side& own, Pairing& pairing, FrameCache& frames);
    void publish(const Pairing& pairing);
    void publish_hotspot_url(const std::string& url);
    void finish(StopReason reason);

    ServerOptions options_;
    std::unique_ptr<FrameSource> frames_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{false};
    int wake_read_{-1};
    int wake_write_{-1};
    // The side the browser paired through; -1 while none. Server thread only.
    int paired_side_{-1};

    mutable std::mutex hotspot_mutex_;
    std::string hotspot_interface_;

    mutable std::mutex status_mutex_;
    ServerStatus status_;
};

} // namespace platform::remote
