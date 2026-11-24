/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "remote/remote_addresses.h"
#include "remote/remote_frames.h"
#include "remote/remote_http.h"
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
    // Where POST /api/v1/block puts the paired browser's Block and Allow
    // requests. Owned by the caller like `keys`; without it every request
    // gets 400.
    BlockQueue* blocks{nullptr};
    // Where POST /api/v1/check and POST /api/v1/report put the paired
    // browser's requests. Owned by the caller like `keys`; without it every
    // request gets 400.
    ActionQueue* actions{nullptr};
    // The folder of saved reports (report_files.h) that GET /reports lists
    // and GET /report hands out; empty: none (an empty list).
    std::string reports_folder;
    // The folders of the web dashboard's files and of the app's fonts (see
    // remote_web.h); a file that is not there gets 404.
    std::string web_folder;
    std::string font_folder;
    // The WireGuard folder (wireguard_folder()) that POST /api/v1/vpn/file
    // saves into; empty: none (every file gets 409).
    std::string vpn_folder;
};

// The Remote view server: a small HTTP/1.1 server on its own thread, never
// the UI thread. One connection at a time, one request per connection
// (Connection: close), a head of at most 2 KB, a body of at most 256 bytes
// (a WireGuard file to kUploadPath: 16 KiB) and 2 s to send them. On Linux a connection is only taken once its
// request has started to arrive (TCP_DEFER_ACCEPT), so a browser's spare
// connection never holds up the others.
//
// - It binds the one address above, never 0.0.0.0, and closes a connection
//   from any other peer at once, before reading anything.
// - A Host header other than "<address>:<port>" gets 421 (this stops DNS
//   rebinding).
// - Every answer carries exactly one Content-Security-Policy: the
//   dashboard's (default_security_policy()) unless the answer names its
//   own (the screen page, a report).
// - GET / and the other paths of the web dashboard (find_web_file()) give
//   its files without a session: they hold nothing private. Fonts may be
//   kept a week, everything else is no-store.
//   GET /screen gives the screen page (page_html()) with its own
//   Content-Security-Policy.
//   POST /pair with code=NNNNNN pairs one browser (see Pairing) and sets
//   the session cookie (HttpOnly, SameSite=Strict, Path=/).
//   GET /api/v1/session needs the cookie (else 401) and answers
//   {"token":"<request token>"}: the page sends that token in the
//   X-LokiCTL-Token header with every state-changing request.
//   GET /api/v1/status needs the cookie (else 401) and answers the JSON the
//   app handed over (set_status_json()), or 503 before the first.
//   GET /screen.png?since=N needs the cookie (else 401) and answers 204
//   while frame N is current, else the screen as a PNG with X-Frame: M.
//   POST /key with k=<name> (key_from_name()) needs an Origin header of
//   exactly "http://<address>:<port>" (else 403), the cookie (else 401)
//   and the request token in X-LokiCTL-Token (else 403); an unknown name
//   gets 400, a full queue or too many keys a second 429 (KeyQueue), an
//   accepted key 204.
//   POST /api/v1/block with id=<IPv4>&on=1 (block) or on=0 (allow) is
//   checked like POST /key (Origin 403, cookie 401, request token 403); an
//   id that is not a dotted IPv4 address or an `on` other than 0 or 1 gets
//   400, a full BlockQueue 429, a queued request 204. Whether it applies
//   (a device on LokiCTL's own hotspot) is for the UI thread to decide; the
//   page sees the result in the next status.
//   POST /api/v1/check with run=1&own=1 (start a health check; own=1 is
//   the page's "own network only" confirmation) or run=0 (stop it), and
//   POST /api/v1/report (save a new report; the body is not read) are
//   checked like POST /key too; any other check body gets 400, a full
//   ActionQueue 429, a queued request 204. Whether it applies is for the
//   UI thread to decide; the page sees the result in the next status or
//   the next GET /reports.
//   The Modes page's controls are checked like POST /key too, and each
//   queues one ActionRequest (204) for the UI thread to decide on:
//   POST /api/v1/mode with mode=client|router|share; POST /api/v1/vpn with
//   on=1 or on=0, or file=<name>; POST /api/v1/hotspot with name=<name>
//   (empty: the default name) or password=new; POST /api/v1/keep with on=1
//   or on=0; POST /api/v1/leak with run=1 or run=0. Values are decoded
//   (form_decode()); anything else gets 400, a full queue 429.
//   The Tools and Watch pages' controls are checked like POST /key too and
//   queue one ActionRequest each (204): POST /api/v1/tool with
//   run=<tool>&target=<target> (a tool of ping, dns, trace, ports and
//   speed; the target decoded, 1 to 63 bytes; ports also needs own=1, the
//   page's "own network only" confirmation; speed takes no target) or
//   stop=<tool>; POST /api/v1/lan with test=<IPv4> or stop=1;
//   POST /api/v1/survey with run=1 or run=0; POST /api/v1/watch and POST
//   /api/v1/trap with on=1 or on=0. Anything else gets 400 (also any other
//   tool name), a full queue 429.
//   GET /api/v1/hotspot needs the cookie (else 401) and answers the JSON
//   the app handed over (set_hotspot_json(): the hotspot's name, password
//   and QR code), or 503 before the first.
//   POST /api/v1/vpn/file?name=<name> carries a WireGuard file as its body
//   (at most kMaxUploadBytes, else 413), checked like POST /key. The server
//   thread itself saves it (save_wireguard_file()) into `vpn_folder` and
//   answers 200 {"name":"<name>","server":"<host>"}, or 400 (a bad name, not
//   a WireGuard file), 409 (the name is taken, the folder is full, no
//   folder) or 500 (not written) with {"error":"<plain reason>"}. Nothing
//   of the file is answered or logged, and the request's buffer is wiped
//   whichever way a request ends.
//   GET /reports needs the cookie (else 401) and lists the saved reports
//   as JSON, {"reports":[{"name":"lokictl-....html","bytes":N},...]}, the
//   newest first. GET /report?name=<name> needs the cookie too and hands
//   out that one file as a download (Content-Disposition: attachment),
//   under a Content-Security-Policy that runs nothing (sandbox); a name
//   that is not exactly a report's (valid_report_name()) or a file that
//   can't be read gets 404. Other paths get 404, a wrong method 405.
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

    // The JSON that GET /api/v1/status answers. Safe from any thread; the
    // app hands over a new one only when it changed.
    void set_status_json(std::string json);
    // The JSON that GET /api/v1/hotspot answers (it holds the hotspot's
    // password). Safe from any thread, like set_status_json().
    void set_hotspot_json(std::string json);

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
    // The checks of every state-changing request, in this order: POST only
    // (405), the page's origin (403), the session (401), the request token
    // (403). Sends the refusal itself; true when the request may go on.
    bool accept_change(int fd, const HttpRequest& request, int side, const Side& own, Pairing& pairing, int64_t now,
                       int64_t deadline);
    // POST /api/v1/vpn/file once accepted: saves the file and answers.
    void save_vpn_file(int fd, const HttpRequest& request, std::string_view body, int64_t deadline);
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

    mutable std::mutex json_mutex_;
    std::string status_json_;
    std::string hotspot_json_;
};

} // namespace platform::remote
