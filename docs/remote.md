<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Remote view

Remote view shows the Cardputer's screen in a web browser on another
computer on the same network. It is **off at every start** and is never
saved: you switch it on yourself, each time, on the Settings page.

## Using it

1. Open **Settings** (key 6 on the home page), move down to **Remote view**
   and press **Enter** or key **6** (`On`).
2. A panel shows the address to open, for example
   `Open http://10.0.0.12:8780`, a six-digit code such as `482 913`, and the
   time left to use it (2 minutes).
3. Send the code from the other computer. The page for a browser comes in a
   later version; for now `curl` does it (replace the address and code):

   ```sh
   curl -c jar.txt -d code=482913 http://10.0.0.12:8780/pair
   curl -b jar.txt -o screen.png http://10.0.0.12:8780/screen.png
   ```

4. Once paired the panel goes away and the row reads `viewed`.
5. To stop, press Enter or key 6 (`Off`) on the row again. Quitting the app
   stops it too.

The row reads `off`, `on` (waiting for the code) or `viewed` (a browser is
paired).

## When it switches itself off

The port closes, the session is forgotten and the row reads `off` when:

- the code was not used within **2 minutes**;
- **5 wrong codes** were sent;
- the paired browser sent nothing for **10 minutes**;
- the network address went away or changed (a cable pulled, another
  network joined);
- the app quits.

A new code always needs a key press on the Cardputer itself.

If Remote view cannot start, the panel says why for 4 seconds:
`Join a network first`, `Only on a private network`, `Port 8780 is busy` or
`Remote view could not start`.

## Security model

Remote view is meant for your own home or office network. It is built so
that nobody else can see the screen, and so that switching it on changes
nothing else on the device.

- **Off by default.** Nothing listens until you switch it on, and it never
  stays on across a restart.
- **Your local network only.** The server listens on port 8780 of one
  address only: the private IPv4 address (10.x, 172.16-31.x or 192.168.x) of
  the network the Cardputer uses for the internet. It never listens on all
  addresses. A connection from any computer outside that network's address
  range is closed at once, before anything is read; this also keeps out the
  devices on the Cardputer's own hotspot. Remote view does not start on a
  public address.
- **A code shown only on the device.** Pairing needs the six-digit code from
  the Cardputer's screen. It comes from the kernel's random source, works
  once and lasts 2 minutes; 5 wrong codes switch Remote view off.
- **One browser at a time.** The paired browser gets a random 128-bit
  session token in a cookie (`HttpOnly`, `SameSite=Strict`, `Path=/`). It is
  kept in memory only and is compared in constant time. While a browser is
  paired, other pairing attempts are refused (409) and do not count as
  wrong codes, so another computer can't push the viewer out.
- **No DNS rebinding.** Every request must name the server by its address
  and port in the `Host` header; anything else gets 421. A web page on the
  internet can't reach the server through a name that points at it.
- **Small, strict requests.** One request per connection, a request head of
  at most 2 KB, a body of at most 64 bytes and 2 seconds to send them. The
  server runs on its own thread, so the device's screen never waits for it.
- **Nothing private in the log.** The log holds states and counts only;
  never the code, the session token or an address.

### What it does not protect against

Remote view uses **plain HTTP**, not HTTPS. Someone who can read the traffic
on your network (an open Wi-Fi network, or a device on it that captures
traffic) could see the pictures and copy the session cookie while it is
valid. Use it on networks you trust, and switch it off when you are done.

## Endpoints

| Request | Answer |
|---|---|
| `GET /` | a short plain text |
| `POST /pair` with the form body `code=NNNNNN` | `200` and the session cookie; `403` for a wrong code; `409` while a browser is paired |
| `GET /screen.png?since=N` with the cookie | `204` while frame N is still the current one, otherwise `200` with the screen as a 320x170 PNG and `X-Frame: M`, the number of that frame; `401` without the cookie |
| anything else | `404` for another path, `405` for another method |

Every answer carries `Connection: close`, `Cache-Control: no-store`,
`X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer` and
`X-Frame-Options: DENY`.

Frame numbers grow only when the screen changed, so a viewer that sends the
last number it got receives a picture only when there is a new one. On the
device the picture is read from the screen itself (so help panels and
messages are in it), at most every 200 ms and only when a viewer asks.

## Trying it in the desktop simulator

Desktop builds only: start the simulator with `LOKICTL_REMOTE=on` and Remote
view switches on at once. The server listens on `127.0.0.1:8780`, accepts
connections from the same computer only, and the code is printed to
standard error:

```sh
LOKICTL_REMOTE=on build/darwin-arm64/Debug/lokictl
# remote view: code 482913 at http://127.0.0.1:8780
```

The simulator's pictures leave out the panels on top of the page (help,
messages); the device's pictures have them.
