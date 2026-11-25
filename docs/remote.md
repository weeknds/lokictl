<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Remote view

Remote view opens LokiCTL's web dashboard in a browser on a phone or a
computer on the same network, and shows the Cardputer's screen there with
its keys. It is **off at every start** and is never saved: you switch it on
yourself, each time, on the Settings page.

## Using it

1. Open **Settings** (key 6 on the home page), move down to **Remote view**
   and press **Enter** or key **6** (`On`).
2. A panel shows the address to open, for example
   `Open http://10.0.0.12:8780`, a six-digit code such as `482 913`, and the
   time left to use it (2 minutes).
3. Open that address in a browser on the phone or computer and type the
   code.
4. Once paired the panel goes away, the row reads `viewed` and the browser
   shows the dashboard (see "The dashboard" below); its **Cardputer screen**
   page shows the screen (see "The screen page"). While the browser stays
   paired, a `REMOTE` tag in the top-left corner of every page tells anyone
   holding the Cardputer that its screen is being viewed.
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

## A laptop on the cable (Laptop link)

While the Laptop link mode runs, Remote view also listens on the cable
port's fixed address, without key 7: the cable goes straight to your own
laptop. Switch Remote view on, and the panel shows the second address,
`or http://10.42.0.1:8780`. Open it on the laptop and type the code. When
Laptop link stops (Client), that address goes away, and a browser that
paired through it is switched off as above.

## The dashboard

The address opens the dashboard. It works in Safari and Chrome on phones
and computers, with nothing to install, and loads nothing from the
internet.

- **Connect.** A drawing of the Cardputer, six boxes for the code (typing
  fills the drawing too, so you can hold it next to the real screen),
  **Continue**, and **Where is the code?**. A wrong code says so; 5 wrong
  codes switch Remote view off, as below.
- **On a phone** (narrower than 900 px): a large title with a search
  button beside it, the page, and a tab bar at the bottom: Home, Devices,
  Security, Tools and Modes.
- **On a computer** (900 px and wider): a sidebar with every page (Overview,
  Devices, Security, Watch and trap, Map, then Modes and VPN, Tools,
  Reports and Cardputer screen) and the Cardputer's battery and address at
  the bottom, and a top bar that shows where you are, with the search
  field.
- **Find a device or run a command.** **⌘K** (**Ctrl+K** on Windows and
  Linux), the top bar's search field or the phone's search button opens
  one search field over the page. It finds pages, tools, devices (by
  name, address or service) and actions, and takes the Cardputer's own
  `:` commands: `ping 9.9.9.9`, `dns example.com`, `trace router`,
  `ports nas` (`router`, `this` or a device's name stand for its
  address), `mode share`, `vpn on`, `health`, `leak`, `watch`. Up and
  Down pick, Enter opens, Esc (or Cancel) closes and puts you back where
  you were. It never does more than a press of the page's own button: it
  opens the page, and Ping, a name lookup and Trace run as if typed there
  and Run pressed, while a health check, a port scan, a mode switch and
  the VPN first ask the same question as their buttons. `theme`, `scan`,
  `probe` and the Wi-Fi, Status, Settings, Sign-in and Talk pages stay on
  the Cardputer.
- **Home** (Overview on a computer) answers "is my network OK?" first: a
  verdict (`1 thing to fix`, then the one thing as a link, then
  `2 more worth a look · 9 look fine`), a picture of the devices around
  the Cardputer (an inner ring for devices reached by the cable, an outer
  one for Wi-Fi; red to fix, and it pulses unless the system asks for
  less motion; yellow worth a look; white fine), what needs attention,
  the traffic of the last minute, the mode and VPN, and Watch's newest
  alerts and the trap. A computer also lists the devices. Before any
  health check the verdict says `Not checked yet`: a device nobody has
  checked never counts as fine. A check counts only on the network it
  checked: once the Cardputer is on another one (another Wi-Fi, cable
  port, router or address range), the verdict says `Not checked yet`
  again and the old check's verdict, devices and findings are not shown.
  Devices that Discover heard or a Ports scan checked before the change
  stay in the list until that tool runs again or LokiCTL is restarted.
- **Devices** lists every device the Cardputer knows, one line each, in
  three groups: `Needs attention`, `Looks fine` and `Not checked yet`
  (only when there are any). A search box finds a name, an address or a
  service, and the filters `All`, `Issues`, `New` and `Hotspot` show
  their counts. A computer shows the same as a table.
- **A device's page** leads with its worst problem in plain words and the
  first step of the fix (`Fix it step by step` shows all of them), then
  anything else found, its open services (port, the software it named,
  a tag such as `2 holes` or `Old`) and its known security holes: how
  serious (a word, the score such as `9.8 of 10` and a 10-step meter) and
  the official number. Holes are found by the version a service reports,
  checked against LokiCTL's built-in list. `Check again` leads to
  Security and asks there. A device on LokiCTL's own hotspot has
  **Block** (it stays connected but can't use the hotspot) or **Allow**,
  the same as `8` on the Cardputer's Devices page; the page shows the
  result, or why it failed.
- **Security** gives one verdict sentence (`1 thing to fix and 2 worth a
  look. The other 9 devices look fine.`), when the last check ran and how
  long it took, then what was found in three groups with their counts:
  `Fix soon`, `Worth a look` and `Good to know`. Picking one shows what it
  means, numbered steps to fix it, anything else found about the same
  device, and its known holes. On a phone a finding about a device opens
  that device's page. **Check again** (also in the top bar, and on a
  device's page) first asks the same question as the Cardputer: what the
  check will ask (`10.0.0.1 to 10.0.0.254`, then 50 common ports on up to
  32 devices) and `Only check networks you own or may test.` **Check my
  network** then starts it on the Cardputer, the same check as `6` twice
  on its Health page, with the same rules: a private home network only,
  and not while another tool runs (the page says why when it can't). The
  page shows where the check is (`Finding devices, 120 of 254`, then
  `Checking device 3 of 12`) and **Stop** ends it. The findings fill in
  as it goes. **Download report** saves a new report on the Cardputer and
  downloads its page; the saved reports are listed below it, each with
  its page and its devices table.
- **Reports** lists the same saved reports, with **Download a new report**.
- **Modes** (Modes and VPN on a computer) has three tiles, Client, Router
  and Share, with a check mark on the one that is on. A tap asks first
  (`Switch to Router?`, what will happen, **Switch** or **Cancel**), then
  the Cardputer switches the way its own Modes page does, with the same
  rules: what it refuses shows under the tiles in its own words (`Plug in
  a cable first`, `Back to Client first`). When the browser came through
  the hotspot, the question warns that the page will lose the Cardputer.
  The hotspot card shows the join code while the hotspot runs (drawn from
  the Cardputer's own QR code), the name, the password behind **Show**,
  and **Change**: a new name (empty puts the Cardputer's own name back) or
  **New password** (a random one, after a question), as on its Hotspot
  page. The VPN group has the VPN switch (on asks first, as a switch), the
  **File** to use (a list while no tunnel runs), the **Kill switch** (a
  state: it turns on by itself when the VPN and the hotspot run together),
  the **Leak test** with its four lines, and **Add a VPN file from this
  phone** (or computer): pick a WireGuard `.conf` file of at most 16 KB and
  the Cardputer checks it and saves it, or says why not (too large, not a
  WireGuard file with the reason, the name is taken, 16 files already).
  While the kill switch holds hotspot devices back with the VPN down, a
  card offers **Turn the VPN on again** and **Turn the VPN off**. Last,
  **Keep this mode running** is the Settings row of the same name.
- **Tools** lists Ping, Look up a name, Trace route, Open ports,
  Internet speed, LAN speed and the Wi-Fi survey, and Watch and trap. On
  a phone each tool opens on its own page; a computer shows the list
  beside the open tool. Each tool runs on the Cardputer the way its own
  page (or its `:` command) runs it, with the same rules: one tool at a
  time (the page says which one runs), a target it takes (typed, or one
  of the quick picks such as **Router**), and the Cardputer's own texts
  for the results. **Open ports** asks first, as the Cardputer does:
  `Only devices you own or may test.` **Internet speed** says how much
  data it uses. **LAN speed** tests against a device that runs `iperf3
  -s`; the Cardputer as the server stays on the Cardputer, because it
  listens only while its LAN page is shown. The **Wi-Fi survey** needs a
  free USB Wi-Fi stick and shows
  how busy each channel is (the best of 1, 6 and 11 in green, a kick-off
  attack in red), what it found and the networks heard.
- **Watch and trap** (from Tools or Home on a phone) turns Watch and the
  trap on and off, the same as `6` on their pages. Watch's alerts show
  with their time, who they are about and, on a tap, what they mean; the
  trap shows its fake ports and who touched them, with what they sent.
  **Tell me about new alerts** works for this browser only, while a
  dashboard page is open and in front (a page in the background asks
  the Cardputer for nothing). Browsers allow notifications only on secure
  pages, and the Cardputer's address is a plain `http://` one, so on most
  phones and computers new alerts show at the top of the page and as a
  count in the tab's title instead (the page says so).
- The Map page says what it will show; its contents come in a later
  update.
- **The findings are worked out once, on the Cardputer**, and every page
  shows the same numbers. One *thing* per device (or the Wi-Fi, the cable
  link, the network), at its worst finding: **Fix soon** (a risk of the
  health check, known holes, a fake router), **Worth a look** (a warning,
  someone scanning the Cardputer or trying the trap), **Good to know**
  (information, a new device). They come from the last health check, the
  Ports page's holes and Watch's alerts; the devices from the health
  check, Discover and the hotspot.

While the page is visible it asks the Cardputer for news every 2 seconds;
while it is hidden it asks nothing. When the Cardputer stops answering, a
line says `No answer from the Cardputer. Trying again...` and the page
tries every 3 seconds. When the session ended, the page asks for the new
code.

On Apple devices the dashboard uses the system font. Elsewhere the
Cardputer serves its own Inter font (Regular and SemiBold), which the
browser keeps for a week.

## The screen page

The **Cardputer screen** page of the dashboard (`#/screen`, in the
sidebar on a computer and on Home on a phone) shows the Cardputer's
screen, scaled to fit (exactly twice its size on a large window), drawn
like the device with its keys: **4** to **8** right under the screen,
then **ESC**, the arrows, **Enter** and **Help**. Clicking or tapping a
key presses it on the Cardputer.

While the screen itself has the focus (it does when the page opens on a
computer, and after a click on it; an orange ring shows it), typing on
the computer's keyboard types on the Cardputer: letters, digits and other
printable characters, **Enter**, **Esc**, **Backspace**, the arrow keys,
and **F1** for help. **Tab** leaves the screen, so the rest of the page
works with the keyboard as usual, and nothing typed in the search field
goes to the Cardputer. A key held down counts once, and shortcuts with
Ctrl or Cmd stay with the browser. Each key
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

### Reports

The **Reports** page lists the reports saved on the Cardputer (see the
README), the newest first, as download links. A report is handed out as a
download, never shown as part of a page.

### From the command line

`curl` can do the same (replace the address and code). A key needs the
`Origin` header the page's browser sends and the session's request token:

```sh
curl -c jar.txt -d code=482913 http://10.0.0.12:8780/pair
curl -b jar.txt http://10.0.0.12:8780/api/v1/session    # {"token":"<32 hex digits>"}
curl -b jar.txt http://10.0.0.12:8780/api/v1/status
curl -b jar.txt -o screen.png http://10.0.0.12:8780/screen.png
curl -b jar.txt -H 'Origin: http://10.0.0.12:8780' -H 'X-LokiCTL-Token: <token>' \
     -d k=c54 http://10.0.0.12:8780/key
curl -b jar.txt http://10.0.0.12:8780/reports
curl -b jar.txt -O -J 'http://10.0.0.12:8780/report?name=lokictl-20261123-075130.html'
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
- **Changes only from the page.** Every request that changes something is
  a `POST`. Apart from `POST /pair`, which makes the session and is guarded
  by the code, each one needs the session cookie, an `Origin` header naming the
  server itself (`http://<address>:8780`) and the session's request token
  in an `X-LokiCTL-Token` header. Pairing makes that token (another 128
  random bits, apart from the cookie's), and only the paired browser can
  read it (`GET /api/v1/session`), so another web site open in the same
  browser can't send keys or anything else. Today that is the keys (at
  most 16 wait and at most 20 are taken a second; more get 429), Block and
  Allow on the hotspot, starting or stopping a health check (a start
  carries the page's own-network confirmation, and the Cardputer starts
  one only where its Health page could) and saving a report (at most one
  every 10 seconds). Keys and addresses never go to the log.
- **Locked-down pages.** Every answer carries exactly one
  `Content-Security-Policy`. The dashboard's is `default-src 'self';
  object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors
  'none'`: scripts, styles, fonts and pictures only from the Cardputer,
  nothing inline and no frames, also on the Cardputer screen page, which
  draws the pictures on a canvas.
- **Only the dashboard's own files.** Its page, style, scripts, icon and
  the two Inter fonts come from a fixed list of addresses, each mapped to
  one installed file (`/usr/share/lokictl/web` and
  `/usr/share/lokictl/fonts`); no other file can be asked for. They are
  handed out before pairing (the connect screen needs them) and hold
  nothing private; everything else needs the session.
- **Reports by name only.** A report is read only by a name exactly like
  the ones LokiCTL makes (`lokictl-<date>-<time>.html` or
  `...-devices.csv`), only from the reports folder, never through a link
  and at most 4 MB; anything else gets 404. It needs the session cookie,
  is sent as a download (`Content-Disposition: attachment`) and with a
  `Content-Security-Policy` of `sandbox`, so even when opened it runs
  nothing and can't reach this page. Its own page forbids scripts too, and
  every text from the network in it is escaped.
- **Small, strict requests.** One request per connection, a request head of
  at most 2 KB, a body of at most 256 bytes (a WireGuard file: 16 KB) and 2
  seconds to send them. The
  server runs on its own thread, so the device's screen never waits for it.
  On the device a connection is only taken once its request starts to
  arrive, so a browser's spare connections never hold the others up.
- **Nothing private in the log.** The log holds states and counts only;
  never the code, the session token or an address.
- **VPN keys stay on the Cardputer.** A WireGuard file added from the
  browser is checked, saved readable by its owner only (mode 0600) and
  never sent back: answers and the status name only the file and its
  server. The buffer it arrived in is wiped. The hotspot's password and
  join code come only from `GET /api/v1/hotspot`, which the Modes page asks
  for while the hotspot runs or after **Show**, never in the status.

### What it does not protect against

Remote view uses **plain HTTP**, not HTTPS. Someone who can read the traffic
on your network (an open Wi-Fi network, or a device on it that captures
traffic) could see the pictures and copy the session cookie while it is
valid. Use it on networks you trust, and switch it off when you are done.
The same goes for a WireGuard file added from the Modes page (its keys
cross the network once, unencrypted) and the hotspot's password behind
**Show**: on a network you don't trust, copy the file over SSH instead.
With `+AP`, everyone who knows the hotspot's password can reach the code
form too; the code from the Cardputer's screen still guards it.

## Endpoints

| Request | Answer |
|---|---|
| `GET /`, `/app.css`, `/app.js`, `/pair.js`, `/shell.js`, `/ui.js`, `/home.js`, `/devices.js`, `/security.js`, `/modes.js`, `/tools.js`, `/watch.js`, `/palette.js`, `/screen.js`, `/preact.module.js`, `/icon.svg`, `/fonts/Inter-Regular.ttf`, `/fonts/Inter-SemiBold.ttf` | the dashboard's files, no session needed (`404` when the file is not installed) |
| `POST /pair` with the form body `code=NNNNNN` | `200` and the session cookie; `403` for a wrong code; `409` while a browser is paired |
| `GET /api/v1/session` with the cookie | `200` with `{"token":"<request token>"}`; `401` without the cookie |
| `GET /api/v1/status` with the cookie | `200` with what the home page knows, as JSON (below); `503` before the app handed any over; `401` without the cookie |
| `GET /screen.png?since=N` with the cookie | `204` while frame N is still the current one, otherwise `200` with the screen as a 320x170 PNG and `X-Frame: M`, the number of that frame; `401` without the cookie |
| `POST /key` with the form body `k=<name>`, the cookie, `Origin: http://<address>:8780` and `X-LokiCTL-Token` | `204` when the key was taken; `400` for an unknown name; `401` without the cookie; `403` without that `Origin` or with a wrong or missing token; `429` when 16 keys wait or 20 came in the last second |
| `POST /api/v1/block` with the form body `id=<IPv4>&on=1` (block) or `on=0` (allow), the cookie, `Origin` and `X-LokiCTL-Token` as for `/key` | `204` when the request was queued; `400` for an id that is not an IPv4 address or another `on`; `401` without the cookie; `403` without that `Origin` or with a wrong or missing token; `429` when 4 requests wait. The Cardputer applies it only to a device on its own hotspot right now; the next status shows the result |
| `POST /api/v1/check` with the form body `run=1&own=1` (start a health check; `own=1` is the page's "own network only" confirmation) or `run=0` (stop it), the cookie, `Origin` and `X-LokiCTL-Token` as for `/key` | `204` when the request was queued; `400` for any other body; `401` without the cookie; `403` without that `Origin` or with a wrong or missing token; `429` when 4 requests wait. The Cardputer starts a check only when its Health page could (a private IPv4 network, no tool running) and stops only a running health check; the next status shows the result |
| `POST /api/v1/report`, with the cookie, `Origin` and `X-LokiCTL-Token` as for `/key` | `204` when the request was queued (same refusals as `/api/v1/check`). The Cardputer saves a new report, at most one every 10 seconds; it then shows in `GET /reports` |
| `POST /api/v1/mode` with `mode=client`, `router`, `share` or `laptop`; `POST /api/v1/vpn` with `on=1`, `on=0` or `file=<name>`; `POST /api/v1/hotspot` with `name=<name>` (empty: the Cardputer's own name) or `password=new`; `POST /api/v1/keep` with `on=1` or `on=0`; `POST /api/v1/leak` with `run=1` or `run=0`; each with the cookie, `Origin` and `X-LokiCTL-Token` as for `/key`. Values are form-encoded as a browser sends them | `204` when the request was queued; `400` for any other body; `401`, `403` and `429` as for `/api/v1/check`. The Cardputer applies it the way its Modes, Hotspot and Leak pages do, with their rules (not while a switch runs, not the mode that is on, a valid name, a listed file, no other tool for the leak test); the next status shows the result |
| `POST /api/v1/tool` with `run=<tool>&target=<target>` (`ping`, `dns`, `trace` or `ports`; Ports also needs `own=1`, the page's "own network only" answer) or `run=speed` (no target), or `stop=<tool>`; `POST /api/v1/lan` with `test=<IPv4>` or `stop=1`; `POST /api/v1/survey` with `run=1` or `run=0`; `POST /api/v1/watch` and `POST /api/v1/trap` with `on=1` or `on=0`; each with the cookie, `Origin` and `X-LokiCTL-Token` as for `/key` | `204` when the request was queued; `400` for any other body (another tool, a target over 63 bytes); `401`, `403` and `429` as for `/api/v1/check`. The Cardputer applies it the way its Tools, LAN, Survey, Watch and Trap pages do, with their rules (`can` and `why` in the status); the next status shows the result |
| `GET /api/v1/hotspot` with the cookie | `200` with `{"name":"LokiCTL-7F3A","password":"...","qr":{"size":29,"rows":["fe0b3f8",...]}}`: the hotspot's name and password and its join code, one row of hex digits a line (the first module in the highest bit), `qr` `null` without a password; `503` before the app handed any over; `401` without the cookie |
| `POST /api/v1/vpn/file?name=<name>` with the WireGuard file as the body (at most 16 KB), the cookie, `Origin` and `X-LokiCTL-Token` as for `/key` | `200` with `{"name":"home","server":"vpn.example.com"}` once saved; `400` with `{"error":"<why>"}` for a bad name (1 to 32 letters, digits, `.`, `_` or `-`) or a file that is not a usable WireGuard file; `409` with the reason when the name is taken, 16 files are there or the Cardputer has no VPN folder (made-up data); `413` above 16 KB; `401` and `403` as for `/key` |
| `GET /reports` with the cookie | `200` with the saved reports as JSON, the newest first, at most 40: `{"reports":[{"name":"lokictl-20261123-075130.html","bytes":21000},...]}`; `401` without the cookie |
| `GET /report?name=<name>` with the cookie | `200` with that file as a download (`text/html` or `text/csv`); `404` for a name that is not a report's or a file that can't be read; `401` without the cookie |
| anything else | `404` for another path, `405` for another method |

Key names: `c32` to `c126` for a printable ASCII character by its code
(`c97` is `a`, `c46` is `.`, `c54` is the key 6), and `esc`, `enter`,
`backspace`, `up`, `down`, `left`, `right` and `help`. A key's body is not
decoded, which is why characters go by their code; the Modes page's names
and files are (`+` and `%XX`, no control characters).

Every answer carries `Connection: close`, `Cache-Control: no-store` (the
fonts: `private, max-age=604800`), `X-Content-Type-Options: nosniff`,
`Referrer-Policy: no-referrer`, `X-Frame-Options: DENY` and one
`Content-Security-Policy`.

The status (`GET /api/v1/status`) is version 1 and comes from what the
app already knows; asking for it makes the Cardputer ask the system for
nothing. Unknown numbers are `null`; texts from the network (the Wi-Fi
name, device names, addresses) are escaped. `history` holds the last
minute of traffic (down and up, kbit/s, one pair a second, oldest first)
since Remote view started; `things` (at most 32) and `devices` (at most
64) come worst first, and `total` counts every device:

```json
{"v":1,"sample":false,
 "cardputer":{"address":"10.0.0.12","battery":87,"charging":false},
 "network":{"name":"Home","link":"wifi","internet":"full"},
 "mode":{"id":"router","name":"Router","hotspot":true,"hotspot_devices":3,
         "hotspot_name":"LokiCTL-7F","vpn":"off","vpn_reply_s":null,
         "kill_switch":false,"keep":false,
         "switch":{"state":"failed","target":"share","error":"Back to Client first"},
         "why":{"client":"","router":"Already in Router mode","share":"","vpn":""},
         "next_hotspot_name":"LokiCTL-7F","vpn_files":["home","travel"],
         "vpn_file":"home","vpn_server":"","vpn_active":false,"gateway_down":false,
         "vpn_add":true,
         "leak":{"state":"idle","shown":false,"fails":0,"unknown":0,"lines":[],
                 "reason":"","can":true,"why":""}},
 "block":{"failures":0,"message":""},
 "traffic":{"down_kbps":1200,"up_kbps":80,"history":[[1100,70],[1200,80]]},
 "counts":{"devices":12,"security":1},
 "check":{"state":"idle","step":"sweep","asked":0,"total":0,"scanned":0,"hosts":0,
          "elapsed_s":0,"reason":"","can":true,"why":"","asks":"10.0.0.1-254",
          "max_devices":32,"max_ports":50},
 "overview":{"checked":true,"checking":false,"checked_at":"21:44","took_s":94,
             "fix":1,"look":2,"know":2,"total":12,"device_fix":1,
             "device_look":2,"fine":9,"unchecked":0,"device_new":2,"hotspot":3,
             "list_date":"2026-11-23",
             "things":[{"id":"10.0.0.43","tier":"fix","icon":"shield",
                        "title":"Smart TV has 2 security holes",
                        "hint":"update the device's software",
                        "action":"Update Smart TV","fix":"Fix Smart TV",
                        "device":"10.0.0.43","time":"21:44",
                        "about":"This version has known security holes ...",
                        "steps":["Open the device's settings ...","..."],
                        "where":"10.0.0.43 · port 22",
                        "also":["Smart TV runs old software"]}],
             "devices":[{"id":"10.0.0.43","name":"Smart TV","address":"10.0.0.43",
                         "ring":"cable","router":false,"new":false,"checked":true,
                         "tier":"fix","tag":"2 holes",
                         "services":"remote login, casting, web page","seen":"2 min",
                         "kind":"","hotspot":false,"blocked":false,
                         "ports":[{"port":22,"name":"Remote login","version":"Dropbear 2015.67",
                                   "tier":"fix","tag":"2 holes"}],"more_ports":0,
                         "holes":[{"port":22,"cve":"CVE-2016-7406","score":98,"used":false,
                                   "text":"A known hole in its remote login, Dropbear 2015.67."}],
                         "more_holes":0,
                         "issues":[{"tier":"fix","title":"2 security holes",
                                    "about":"This version has known security holes ...",
                                    "steps":["Open the device's settings ...","..."],"port":22}]}]},
 "watch":{"on":true,"trap":true,"trap_ports":6,
          "feed":[{"time":"21:42",
                   "text":"Someone scanned the Cardputer from 10.0.0.75"}]},
 "tools":{"running":"","router":"10.0.0.1","max":63,
          "data_note":"Uses up to 100 MB of data",
          "presets":{"ping":["9.9.9.9","1.1.1.1"],
                     "dns":["example.com","wikipedia.org"]},
          "ping":{"state":"done","can":true,"why":"","line":"Done","tone":"ok",
                  "target":"9.9.9.9","address":"9.9.9.9",
                  "counts":"10 sent · 10 received · 0% lost",
                  "times":"min 8.80 · avg 11.8 · max 13.1 ms",
                  "replies":[{"seq":6,"text":"12.4","tone":""}]},
          "dns":{"state":"idle",...,"server":"","time_ms":-1,"addresses":[]},
          "trace":{"state":"idle",...,"hops":[{"n":"1","address":"10.0.0.1",
                   "address_tone":"","name":"router","time":"1.2ms",
                   "time_tone":""}]},
          "ports":{"state":"idle",...,"counts":"","asked":0,"total":0,
                   "open":[{"port":22,"text":"22 ssh","plain":"remote login",
                            "said":"OpenSSH_9.6","tag":"Check",
                            "tag_tone":"warn"}]},
          "speed":{"state":"idle",...,"server":"","phase":"connect",
                   "down":"--","up":"--","down_bytes":"0 kB","up_bytes":"0 kB",
                   "down_samples":[],"up_samples":[]},
          "lan":{"state":"idle","mode":"client",...,"down":"--","up":"--",
                 "down_samples":[],"up_samples":[]},
          "survey":{"state":"idle",...,"stick":"wlan1","frames":0,
                    "networks_seen":0,"best":0,
                    "channels":[{"level":0,"load":"","networks":0,
                                 "kickoffs":0}],
                    "findings":[],"networks":[],"more_networks":0},
          "watch":{"state":"running",...,"raised":3,
                   "alerts":[{"time":"21:42","name":"Scan on you",
                              "text":"Someone scanned the Cardputer from ...",
                              "who":"10.0.0.75","fact":"Tried 20 ports",
                              "about":"...","serious":true}]},
          "trap":{"state":"idle",...,"helper":"","raised":0,
                  "ports":[{"port":21,"name":"File transfer (FTP)",
                            "state":"off"}],
                  "hits":[{"time":"13:05","who":"10.0.0.75","from_port":51000,
                           "port":23,"word":"login",
                           "name":"Remote login (Telnet)","sent":"root"}]}}}
```

`tier` is `fix`, `look`, `know` or `ok`; `device_fix + device_look + fine
+ unchecked = total`. `device_new` and `hotspot` count the new devices and
those on LokiCTL's own hotspot. Each device carries its page: at most 8
open services (`more_ports` more), 4 holes (`score` in tenths, `used` when
attackers are known to use it; a version a vendor built, such as Debian's,
gets no holes here, only a `May need updates` issue) and 4 issues, the
worst first. For a device both the health check and the Ports page
scanned, the one started last counts. Each thing has an `id` of its own
(a device's id, or `wifi`, `cable`, `network`, `own`), what its worst
finding means, 1 to 3 steps, who it is about (`where`) and up to 4 other
findings about the same subject (`also`). `check` is the health check as
it really runs, also while the findings are made up: `state` is `idle`,
`running`, `done`, `stopped` or `failed` (`reason` then says why), `step`
is `sweep` (finding devices, `asked` of `total` addresses) or `ports`
(`scanned` of `hosts` devices); `can` says whether one may start now and
`why` why not, in the Health page's words; `asks` is what the sweep asks.
`took_s` is how long the last finished check took, `null` before one.
`block` holds how many blocks or allows failed since the start and the
last failure's words. In `mode`, `switch` is the last switch asked for
(`state` `idle`, `switching` or `failed`, `error` the Modes page's words
for a failed one), `why` says why each mode can't be asked for now (the
`:mode` command's words), `next_hotspot_name` is the name Router and Share
start with, `vpn_file` the tunnel's file while `vpn_active` (else the one a
VPN switch would use) and `vpn_server` its server, `gateway_down` the kill
switch holding hotspot devices back without the VPN, `vpn_add` whether a
WireGuard file may be added, and `leak` the leak test (`lines` while it
runs or while a finished test still describes the VPN; `verdict` `pass`,
`fail`, `unknown`, `info` or `waiting`). Nothing secret is in the
status. While the Cardputer shows made-up data (the simulator) and no tool
has run, the findings are made up too and `sample` is `true`.

`tools` holds the Tools and Watch pages' parts. Each has its `state` (as
`check`), its page's status `line` and its colour (`tone`: `muted`, `ok`,
`warn`, `bad` or empty), `can` and `why` (whether it may start now, in its
page's words), and its results in the Cardputer's own texts: Ping's last 5
replies and counts, the name's addresses, the trace's hops, the open ports
with what they said and their tag, the rates in Mbit/s (`down`, `up`) and
the half-second samples in kbit/s. `running` names the tool that runs.
The survey has 13 channels (`level` 0 to 100 against the busiest,
`load`), its findings and up to 24 networks, the strongest first. Watch has
up to 30 alerts and `raised` (all raised in the run); the trap its 6 fake
ports and up to 30 visits. The states always come from the real runs;
while the findings are made up, the lists of Watch and the trap are too.
The status's `feed` keeps Watch's newest 5.

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
code. The dashboard then shows the simulator's sample data, marked
`Sample data`. `SDL_VIDEODRIVER=dummy` runs the simulator without a window.

The simulator's pictures, like the device's, show what lies over the page:
help, messages, the panel and the `REMOTE` tag.

The simulator lists and saves reports only when `LOKICTL_REPORT_DIR` names
a folder (an absolute path), so it never writes into your own Documents
folder:

```sh
LOKICTL_REMOTE=on LOKICTL_REPORT_DIR=$PWD/build/reports build/darwin-arm64/Debug/lokictl
```

The simulator never opens a hotspot address: key 7 changes the row there,
but the server stays on `127.0.0.1` only.

The simulator adds WireGuard files from the Modes page only to a test
folder, with the `live` source on `LOKICTL_DATA_ROOT`, so it never writes
into your own settings; with made-up data it says why it can't:

```sh
mkdir -p build/vpn-root
LOKICTL_REMOTE=on LOKICTL_DATA_ROOT=$PWD/build/vpn-root build/darwin-arm64/Debug/lokictl --data live
# files land in build/vpn-root/wireguard/
```
