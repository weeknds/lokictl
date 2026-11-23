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
// and takes the Server line. The functions here only look at bytes that
// already came in; the tool runner does the reading.
namespace model {

// An open port's greeting is read for at most this long, and at most this
// many bytes of it.
constexpr int32_t kBannerReadMs = 1500;
constexpr std::size_t kBannerBytesMax = 256;
// A page shows at most this many characters of what a port said.
constexpr std::size_t kBannerShownMax = 40;

// How the scan asks an open port what it is.
enum class BannerProbe : int32_t {
    Listen = 0,  // reads what the service says first; nothing is written
    Http,        // writes one HEAD request, then reads the Server line
    Tls,         // nothing is read or written; shown as "TLS"
};

// How `port` is asked. Only plain web ports get the request; a printer's
// raw port (9100) or anything not known is only listened to.
BannerProbe banner_probe(uint16_t port);

// The one request a web port gets: "HEAD / HTTP/1.0" with a Host line
// naming `host` (and `port` when it is not 80).
std::string http_head_request(const std::string& host, uint16_t port);

// What to show of the `size` bytes at `data` that a port sent: printable
// ASCII only, at most kBannerShownMax characters, empty when nothing
// useful came. Listen: the first piece of text, without the protocol's
// own prefix ("SSH-2.0-", "220 ", "+OK", "* OK"); Telnet's option bytes
// are skipped. Http: the Server line's value; an answer that is not HTTP
// is read as with Listen. Tls: "TLS". Never reads past `size`.
std::string banner_text(BannerProbe probe, const uint8_t* data, std::size_t size);

// Whether what came in already holds a whole answer, so reading may end
// before kBannerReadMs: Listen, a piece of text with something after it;
// Http, a whole Server line or the end of the headers.
bool banner_complete(BannerProbe probe, const uint8_t* data, std::size_t size);

} // namespace model
