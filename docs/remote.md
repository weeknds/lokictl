<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Remote view

Remote view shows the Cardputer's screen in a web browser on another
computer on the same network, and lets you press its keys from there. It is
**off at every start** and is never saved: you switch it on yourself, each
time, on the Settings page.

## Using it

1. Open **Settings** (key 6 on the home page), move down to **Remote view**
   and press **Enter** or key **6** (`On`).
2. A panel shows the address to open, for example
   `Open http://10.0.0.12:8780`, a six-digit code such as `482 913`, and the
   time left to use it (2 minutes).
3. Open that address in a browser on the other computer and type the code.
4. Once paired the panel goes away, the row reads `viewed` and the browser
   shows the screen (see "The page" below). While the browser stays paired,
   a `REMOTE` tag in the top-left corner of every page tells anyone holding
   the Cardputer that its screen is being viewed.
5. To stop, press Enter or key 6 (`Off`) on the row again. Quitting the app
   stops it too.

The row reads `off`, `on` (waiting for the code) or `viewed` (a browser is
paired), and `on + hotspot` or `viewed + hotspot` while devices on the
Cardputer's own hotspot may use it too (see below).

## Devices on the Cardputer's hotspot

Normally only computers on the network the Cardputer uses for the internet
can open Remote view; devices that joined the Cardputer's own hotspot (in
the Router or Share mode) are kept out. To let them in for this session:

1. Switch Remote view on as above.
2. On the Remote view row, press key **7** (`+AP`). The row reads
   `on + hotspot`.
3. While LokiCTL's hotspot runs, the panel shows a second address, for
   example `or http://10.42.0.1:8780`. Open that one on the device that
   joined the hotspot and type the code.

Key **7** (`-AP`) stops letting hotspot devices in. The choice is only
possible while Remote view is on, it is cleared whenever Remote view goes
off, and it is never saved. It changes no network setting: it only decides
whether Remote view also listens on the hotspot's address.

When the hotspot starts after you pressed 7, the second address appears
within a few seconds; when it stops, that address goes away and Remote view
goes on for the other network. If the browser paired through the hotspot
address and that address goes away (the hotspot stopped, or you pressed
`-AP`), Remote view switches itself off.

## The page

The page shows the Cardputer's screen, scaled to fit the window, with a key
pad under it laid out like the device: keys **4** to **8** right under the
screen, then **ESC**, the arrows, **Enter** and **Help**. Clicking a key
presses it on the Cardputer.

While the screen shows, typing on the computer's keyboard types on the
Cardputer: letters, digits and other printable characters, **Enter**,
**Esc**, **Backspace**, the arrow keys, and **F1** for help. A key held down
counts once, and shortcuts with Ctrl or Cmd stay with the browser. Each key
goes through the same steps as a key pressed on the Cardputer: when its
screen has dimmed, the first key only wakes it; keys 4 to 8 press the soft
keys and F, X, Z and C move like arrows, except while a page is waiting for
typing, which keeps only the characters it accepts.

A key from the browser is pressed and let go at once, so it can never be a
long press: holding ESC (which quits LokiCTL or goes back to the launcher)
and fn+J (screenshot) only work on the Cardputer itself.

The page asks for a new picture about four times a second for 2 seconds
after a key, then once a second, and once every 2 seconds when there was no
key and no change on the screen for 30 seconds. A changing screen alone
(the live home page) does not make it ask more often. While its browser tab
is hidden it asks nothing at all, and asks at once when the tab shows
again.

When the Cardputer stops answering (it went out of range, or Remote view
was switched off) or gives no answer within 5 seconds, the last picture
stays on the page, dimmed, under the message
`No answer from the Cardputer. Trying again...`, and the page tries again
every 3 seconds. When Remote view ended and was switched on again,
the page asks for the new code.

### From the command line

`curl` can do the same (replace the address and code). A key needs the
`Origin` header the page's browser sends:

```sh
curl -c jar.txt -d code=482913 http://10.0.0.12:8780/pair
curl -b jar.txt -o screen.png http://10.0.0.12:8780/screen.png
curl -b jar.txt -H 'Origin: http://10.0.0.12:8780' -d k=c54 http://10.0.0.12:8780/key
```

## The REMOTE tag

While a browser is paired, the Cardputer shows a small `REMOTE` tag in the
top-left corner, at the same place on every page: over the app name in the
title bar, and over the app name, the `sample data` mark or the first
pane's title on the home page. It is drawn in the theme's accent colour
(and reads `remote` in the Terminal theme, which writes such words small),
stays still (so a still screen sends no new pictures) and is in the pictures
the browser gets, too. It goes away when Remote view is switched off or
switches itself off, and when the app quits. The help panel covers it while
help shows.

## When it switches itself off

The port closes, the session is forgotten and the row reads `off` when:

- the code was not used within **2 minutes**;
- **5 wrong codes** were sent;
- the paired browser sent nothing for **10 minutes** (a page in a hidden
  browser tab sends nothing, so leaving it hidden that long ends Remote
  view too);
- the network address went away or changed (a cable pulled, another
  network joined);
- the browser paired through the hotspot address and that address went
  away (the hotspot stopped, or `-AP` was pressed);
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
  address: the private IPv4 address (10.x, 172.16-31.x or 192.168.x) of
  the network the Cardputer uses for the internet. It never listens on all
  addresses. A connection from any computer outside that network's address
  range is closed at once, before anything is read; unless you let them in
  (next point), this also keeps out the devices on the Cardputer's own
  hotspot. Remote view does not start on a public address.
- **The hotspot only when you ask.** After key 7 (`+AP`), and only while
  LokiCTL's own hotspot runs (Router or Share, as NetworkManager reports
  it), the server also listens on the hotspot's address. That address must
  be private, on the hotspot's own interface (never the uplink's), and its
  range must not overlap the other network's. Each address keeps its own
  rules: it takes connections only from its own network's range and
  requests only with its own address in `Host` and `Origin`, and the
  session belongs to the address it was paired on (the same cookie sent to
  the other address gets 401). Both addresses share one code, one paired
  browser and the 5-wrong-codes limit.
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
- **Keys only from the page.** A key needs the session cookie and an
  `Origin` header naming the server itself (`http://<address>:8780`), so
  another web site open in the same browser can't send keys. At most 16 keys
  wait and at most 20 are taken a second; more get 429. Keys never go to the
  log.
- **A locked-down page.** The page loads nothing from anywhere else. Its
  `Content-Security-Policy` allows only its own script and style, pictures
  from the server's answers and requests to the server itself.
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
With `+AP`, everyone who knows the hotspot's password can reach the code
form too; the code from the Cardputer's screen still guards it.

## Endpoints

| Request | Answer |
|---|---|
| `GET /` | the web page (`text/html`, with its `Content-Security-Policy`) |
| `POST /pair` with the form body `code=NNNNNN` | `200` and the session cookie; `403` for a wrong code; `409` while a browser is paired |
| `GET /screen.png?since=N` with the cookie | `204` while frame N is still the current one, otherwise `200` with the screen as a 320x170 PNG and `X-Frame: M`, the number of that frame; `401` without the cookie |
| `POST /key` with the form body `k=<name>`, the cookie and `Origin: http://<address>:8780` | `204` when the key was taken; `400` for an unknown name; `401` without the cookie; `403` without that `Origin`; `429` when 16 keys wait or 20 came in the last second |
| anything else | `404` for another path, `405` for another method |

Key names: `c32` to `c126` for a printable ASCII character by its code
(`c97` is `a`, `c46` is `.`, `c54` is the key 6), and `esc`, `enter`,
`backspace`, `up`, `down`, `left`, `right` and `help`. Form bodies are not
decoded, which is why characters go by their code.

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

Open `http://127.0.0.1:8780` in a browser on the same computer and type the
code.

The simulator's pictures, like the device's, show what lies over the page:
help, messages, the panel and the `REMOTE` tag.

The simulator never opens a hotspot address: key 7 changes the row there,
but the server stays on `127.0.0.1` only.
