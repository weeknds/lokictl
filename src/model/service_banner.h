/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// What an open port says about itself: after a connection is made, the
// port scan reads the service's greeting (SSH, FTP, SMTP, POP3, IMAP and
// Telnet speak first) or, on a web port, asks for the page's headers once
// and takes the Server and X-Powered-By lines; a name server (port 53) is
// asked once for its version (the tool runner reads that answer with
// platform::dns::version_answer()). The functions here only look at bytes
// that already came in; the tool runner does the reading.
namespace model {

// An open port's greeting is read for at most this long, and at most this
// many bytes of it.
constexpr int32_t kBannerReadMs = 1500;
constexpr std::size_t kBannerBytesMax = 512;
// What a port said is kept to at most this many characters (a page shows
// what fits).
constexpr std::size_t kBannerKeptMax = 120;

// How the scan asks an open port what it is.
enum class BannerProbe : int32_t {
    Listen = 0,  // reads what the service says first; nothing is written
    Http,        // writes one HEAD request, then reads the Server and X-Powered-By lines
    Tls,         // nothing is read or written; shown as "TLS"
    Dns,         // writes one version question over TCP (platform::dns::build_version_query())
};

// How `port` is asked. Only plain web ports get the request, and only port
// 53 the version question; a printer's raw port (9100) or anything not
// known is only listened to.
BannerProbe banner_probe(uint16_t port);

// The one request a web port gets: "HEAD / HTTP/1.0" with a Host line
// naming `host` (and `port` when it is not 80).
std::string http_head_request(const std::string& host, uint16_t port);

// What to keep of the `size` bytes at `data` that a port sent: printable
// ASCII only, at most kBannerKeptMax characters, empty when nothing
// useful came. Listen: the first piece of text, without the protocol's
// own prefix ("SSH-2.0-", "220 ", "+OK", "* OK"); Telnet's option bytes
// are skipped. Http: the Server line's value, then "; " and the
// X-Powered-By line's value when there is one ("nginx/1.18.0;
// PHP/7.4.3"); an answer that is not HTTP is read as with Listen. Tls:
// "TLS". Dns: empty (the runner reads the answer). Never reads past `size`.
std::string banner_text(BannerProbe probe, const uint8_t* data, std::size_t size);

// What a name server said for its version (platform::dns::version_answer())
// as kept: BIND answers with its version number alone, so a text that
// starts with a digit gets "BIND " before it ("BIND 9.18.24-1-Debian");
// printable ASCII, at most kBannerKeptMax characters.
std::string dns_version_text(const std::string& answer);

// Whether what came in already holds a whole answer, so reading may end
// before kBannerReadMs: Listen, a piece of text with something after it;
// Http, the end of the headers; Dns, the whole answer (its two length
// bytes and that many more).
bool banner_complete(BannerProbe probe, const uint8_t* data, std::size_t size);

} // namespace model
