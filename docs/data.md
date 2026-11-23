<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# The data layer

The data layer collects the values the app shows and hands them to the
screen. A small background reader asks a *source* for values, and the home
page shows each new value while it is open: a value in place, and a new
situation (another kind of link, an access point switched on or off, other
clients) by building its content again, once.

On the device every value is real: the **clock**, the **battery**, the
**network link** and its **traffic**. Made-up example situations (Wi-Fi,
cable, no network) let every page be looked at and tested in the states the
device can really be in, on any computer. This page explains where the
values come from, how they reach a page, how to add a new value, and how to
test all of it by script.

## The sources

Where the values come from is called the source. There are seven:

| Source | What it gives |
|---|---|
| `sample` | the router sample: fixed made-up values that never change. A router with its own access point and three clients; the clock says `07:46` and the battery `91%` |
| `demo` | the router sample, moving on a fixed script (see [The demo script](#the-demo-script)) |
| `live` | real readings: the device's clock, battery, network link and traffic (see [What is real today](#what-is-real-today)) |
| `wifi` | fixed made-up values: joined to a Wi-Fi network, no access point, no clients |
| `cable` | fixed made-up values: on a network cable, no access point, no clients |
| `offline` | fixed made-up values: no network at all; every network value is not known |
| `tour` | walks through the made-up situations, one stop per reading (see [The tour](#the-tour)) |

`wifi`, `cable`, `offline` and `tour` show the same clock (`07:46`) and
battery (`91%`) as the sample, and all of them keep the `sample data` mark.
Their values are in one place, `src/model/home_examples.cpp`, and are all
made up. `demo` is still the same script on the router sample, and keeps
the mark too. `live` shows the device as it is: in its real mode (Client,
or Router or Share while LokiCTL's hotspot runs), with its real link and traffic,
and without the mark.

Which one is used:

- **A snapshot** (`--snapshot`) always uses `sample`, on the Mac and on the
  device. So two pictures of the home page are still the same file, byte
  for byte.
- **A normal start and `--print-data`** use `live` on the device and `demo`
  in the Mac simulator, which has no battery to read.
- `--data <name>` picks the source in every mode, for example
  `--data live` in the simulator, `--data demo` on the device, or
  `--data wifi` anywhere to see the page joined to a Wi-Fi network.

The default is chosen in one place, `default_data_source()` in
`src/app/app.cpp`. The example sources are never a default; they are only
used when `--data` names them.

## What is real today

With `live`:

- the clock is the device's local time as `HH:MM`;
- the battery charge and its state come from the system's battery files
  (see [The battery](#the-battery));
- the network link: Wi-Fi, cable or none, the interface, its IPv4 address,
  the router's address, the Wi-Fi signal and the cable's speed (see
  [The network link](#the-network-link));
- the traffic: the download and upload rates and their graphs (see
  [The traffic](#the-traffic));
- on the device, from NetworkManager: the Wi-Fi network's name, security and
  speed, whether the internet answers, and why there is no link (see
  [Names and state from NetworkManager](#names-and-state-from-networkmanager)).

The device is shown in the Client mode, or in Router or Share while
LokiCTL's hotspot runs (see [Switching modes](#switching-modes)), with the devices
on the hotspot (see [Devices on the hotspot](#devices-on-the-hotspot)).
What NetworkManager does not tell (in
the simulator, or when it is not running) shows `wi-fi network` and `--`,
never a made-up name.

Nothing is made up any more, so `live` sets `HomeData::sample` to false
and the page shows no `sample data` mark. (Only if the system could not
tell the time would the clock fall back to the sample's `07:46`, and the
mark come back with it.) Every other source keeps the mark.

The title bar of the other pages (Settings and the rest) takes its clock,
battery and link icon from the same data layer, so it always reads the same
as the home page. In a snapshot it therefore shows the sample's values
(`07:46`, the cable icon, `91%`), and two snapshots of a page are the same
file.

## The tour

The tour (`--data tour`) is the test for "the page follows a new
situation". It walks through 25 fixed stops and then starts again: reading
`n` (counting the first reading as 0) shows stop `n` modulo 25, so
`--data-steps 25` and `--data-steps 50` show stop 0 again. In a window it
takes one stop every 2 s, so a person can watch the page change.

In the table, A is the router sample, W the `wifi` set, C the `cable` set
and O the `offline` set:

| Stops | Shows |
|---|---|
| 0 to 12 | A, W, A, C, A, O, W, C, W, O, C, O, A: every set once after every other set |
| 13 | W |
| 14 | W with a weak signal: −83 dBm, 6 Mbit/s |
| 15 | W with the widest texts: the name `Harbour-Cafe-Upstairs-Guest-5GHz` (32 characters, the longest a Wi-Fi name can be), `WPA3`, −100 dBm, 1000 Mbit/s, address `100.100.100.200`, router `100.100.100.100` |
| 16 | W with values that are not known: no name, no security, no signal, no speed, no router's address; the interface `wlan0` and the address `10.20.30.44` stay |
| 17, 18 | W, C |
| 19 | C, fast: 1000 Mbit/s, address `100.100.100.200`, router `100.100.100.100` |
| 20 | C with values that are not known: no speed, no address, no router's address; the interface `eth0` stays |
| 21 | A |
| 22 | A with another uplink: address `100.100.100.200`, 1000 Mbit/s |
| 23 | A with one client left (`laptop`) |
| 24 | A with no clients left |

Stops 13 to 17, 18 to 20 and 21 to 22 change only values that the page
changes in place, so between them nothing on the page is built again. Every
other step brings a new situation, and the page's content is built again
(see [How a page updates in place](#how-a-page-updates-in-place)). All the
names and addresses are made up.

## The reader

Reading a value can take time: a file under `/sys` may be answered by a
chip on a slow bus. Drawing must never wait for that, so the reading
happens beside the screen, on a background thread called the reader
(`platform::DataReader`, in `src/platform/data_reader.h`).

What the reader does, over and over:

1. It sleeps until the source says its values can have changed
   (`next_read_delay()`).
2. It asks the source for the values as they are now (`read()`). The
   source fills in one whole `model::HomeData` block.
3. If the block differs from the one before, it puts a copy into the
   hand-over slot. An equal block changes nothing.

Rules the reader keeps:

- It never calls LVGL, never touches the view model and never writes to
  the log. LVGL and the log are not made for two threads, so the reader
  only touches its source and the hand-over slot.
- The hand-over slot is one block, guarded by a mutex. The reader writes
  it; the screen's side takes it.
- A reading that fails with an exception is dropped, and the values stay as
  they were. The reader never ends because of a bad file.
- SIGTERM and SIGINT are blocked on the reader thread, so a stop request is
  always handled by the main thread.

The screen's side is `app::DataController` (`src/app/data_controller.h`).
It owns the reader and an LVGL timer that looks four times a second (every
250 ms) whether a new block is in the slot. When nothing new is there it
does nothing else, so nothing is redrawn. It is also the only place that
writes the data layer's log lines.

**The first reading** is taken at startup on the screen's own thread,
before the first page is built. So the page never flashes the sample
values first. With `live` this is a few small file reads; the log says how
long it took. After that, in a normal run, every reading happens on the
reader thread.

### How often each source reads

- `sample`, `wifi`, `cable` and `offline` never wake up again: their
  values never change.
- `demo` takes one step a second.
- `tour` takes one stop every 2 s.
- `live` reads the clock just after each new minute starts (20 ms after
  it), the network every 5 s and the battery every 10 s. The reader sleeps
  until whichever comes first. That is at most about nineteen wake-ups a
  minute, each a few small file reads, so it costs next to nothing. While
  the home page is on screen it also reads the traffic once a second (see
  [The traffic](#the-traffic)). The network, battery and traffic files are
  read only when their time has come; in between their last reading is
  used again. A block that equals the last
  one is not handed over, so the page only changes when a shown value
  does (the Wi-Fi signal moves by a dBm now and then; that changes one
  text). While the connection to NetworkManager starts (about a tenth of a
  second on the device) it reads every 100 ms; the app's first reading
  waits up to 250 ms for that start, so the first page already shows the
  names (see
  [Names and state from NetworkManager](#names-and-state-from-networkmanager)).

The reader sleeps on a clock that never jumps, and works out the next
minute from the wall clock each time it wakes up. If the device's time is
set while the app runs, the clock on the page can lag behind for at most
10 s, until the next wake-up.

## How values reach a page

```text
source            fills in one model::HomeData block
  -> platform::DataReader         (reader thread) keeps the newest block
  -> app::DataController          (screen's thread) takes it, 4 times a second
  -> viewmodel::BaseViewModel     set_home_data() stores it, then
                                  home_data_subject() counts one change
  -> screen::HomeScreen           its observer is told and compares the
                                  new block with the one it shows
  -> screen::HomeLayout::update() only values changed: the layout retexts
                                  its labels
     or the content built again   a new situation: a new layout builds
                                  the page afresh
```

- `BaseViewModel::set_home_data()` is only ever called on the screen's
  thread, by `app::DataController`. A block equal to the one it holds
  changes nothing and tells nobody.
- If a new block has fewer clients than the selection's place, the
  selection is pulled back to the last client shown.
- `home_data_subject()` is a counter: it goes up by one for every new
  block. `home()` says what the values are.
- `screen::HomeScreen` adds one observer on `home_data_subject()`, bound
  to the page's content area like the selection observer, so it goes away
  with the page. LVGL calls it once when it is added; that first call
  changes nothing, because `build()` has just shown the same values.
- A page that is built while the app runs, after a theme switch or when
  coming back from Settings, takes `home()` as it is then. So values that
  arrived while another page was open are there at once.

## How a page updates in place

Each home layout has two functions for values: `build(context)` makes the
page, and `update(context)` shows new values on it. `screen::HomeScreen`
keeps a copy of the block the layout shows and decides which of the two a
new block needs, with `home_values_only()` in
`src/view/screens/home_layout.h`:

- **In place**, when the new block differs only in the clock, the battery,
  the uplink's interface, address and link speed, the router's address,
  the Wi-Fi network's name, security and signal, the router view's
  uptime, the two rates and their histories, or the clients' signal, rate
  and time while the same devices are listed in the same order (same
  hardware address, name, address and `new` mark; `same_clients()`), or in
  values the home page does not show (the Status page's details and the
  switch between modes): `HomeScreen` calls `update()`.
- **Built again**, for any other difference (the kind of link, the access
  point, the mode, another list of clients, the sample mark, or a field added
  later): `HomeScreen` throws the layout away, empties the page's
  content area, and a new layout builds it afresh, once (on the device a
  new situation costs one frame of roughly 20 to 40 ms, depending on the
  theme). The soft-key bar, the page and its observers stay. [home.md](home.md#values-in-place-a-new-situation-built-again)
  explains why that is safe.

`update()`:

- only changes the text of labels `build()` made, and may move them. It
  never creates or deletes an object and never builds the page again
  itself;
- sets a text through `set_text_if_changed(label, shown, text)`
  (`src/view/screens/home_layout.h`), which sets it only when it differs
  from `shown`, the text the layout gave that label last time. So nothing
  is redrawn for a value that stayed the same. It compares with the
  layout's own copy, not with the label, because LVGL rewrites a label's
  text when it ends it in dots;
- uses the same text function as `build()`, so a page updated in place and
  a page built fresh with the same values look exactly the same.

A value that changes in place moves nothing else, with the two exceptions
below. The battery text is right-aligned and grows to the left. In
Constellation and Terminal the clock stays where it is. In Instrument the
clock and the battery are one right-aligned label that ends 8 px from the
right edge, so a longer battery text pushes the clock to the left. `--%`
is as wide as `91%`. Names and addresses sit in boxes of fixed width. A
few things change with a value: Instrument's captions next to its numbers
(`DBM` or `MBIT`, `MB/S`) follow their number when its width changes, for
example from `−57` to `−100` or from `9.9` to `12.0`; in Constellation's
router view the dim star right of the uplink's text is hidden while a wide
text, such as `eth0 · 1000M`, would come too close to it; and Terminal's
bars change height (only the bars whose height changed are touched, see
`view::widgets::update_bar_graph()`).

## The battery

The battery is read by `platform::read_battery()` in
`src/platform/battery.h`. It takes the folder to read from, so a test
folder works on any computer.

- **Which battery:** the sub-folders of `/sys/class/power_supply` are taken
  in order of their names, and the first whose `type` file says `Battery`
  is used. The battery's name is not written into the code.
- **`capacity`** is the charge in per cent. Only a whole number counts. A
  number above 100 counts as 100. A negative number, a missing or empty
  file, or text that is not a number means unknown. Unknown is never shown
  as 0.
- **`status`** gives the state, read on its own: `Charging` is charging,
  `Discharging` is discharging, `Full` and `Not charging` are idle (on the
  charger, not charging); anything else, or no file, is unknown.

On the page, `screen::home::battery_text()` in
`src/view/screens/home_format.h` writes it for all three themes:

| Charge and state | Text |
|---|---|
| 91, discharging or idle | `91%` |
| 91, charging | `91%+` |
| unknown, any state | `--%` |

At 15 % or less while discharging (`model::battery_low()`), the battery text
takes the theme's warning colour: on all three home layouts, in the title
bar of the other pages and in the Status page's battery heading. Only the
charge in per cent counts, never the charge in mAh.

## Battery care

`app::PowerController` (`src/app/power_controller.h`) saves power while
nobody looks at the screen, and warns before the battery runs out.

- **Dim and dark:** 60 s after the last key the screen dims, and after
  5 min it goes dark (the backlight is off). On the charger (charging or
  idle) it only dims. Any key while the screen is dim or dark only brings it
  back and does nothing else.
- **The backlight:** `/sys/class/backlight/backlight/brightness` (0 to 100,
  the `video` group may write it) is the only file on the device that
  LokiCTL writes besides its settings. Its value is read when LokiCTL first
  lowers it, so a backlight that is already off is left alone. Dim is a
  fifth of that value, at least 10. Waking and quitting (hold ESC, SIGTERM,
  SIGINT) put the value back, but only if the file still holds what LokiCTL
  wrote; when something else changed it meanwhile, such as the launcher's
  own sleep, it is left as it is. Snapshots and `--print-data` never open
  the file. The simulator darkens its window instead.
- **Slower reading:** only while the screen is dark. The traffic and the
  Status page's details stop (`DataController::set_screen_dark()`), and they
  start again at once when the screen wakes. Clock, battery and link keep
  their pace. While the screen is dim everything keeps going, so a dim screen
  on the charger still works as a monitor.
- **Warnings:** once at 15 % ("Battery low") and once at 5 % ("Battery very
  low") while discharging. Each message wakes the screen and logs
  `battery: low warning at <n>%`. Being on the charger arms both again.
- **The setting:** the Settings page's "Dim when idle" row (Enter or key 6)
  switches dimming on or off. It is saved as `dim_when_idle=yes|no` in
  `[application]`; the default is on.

Log lines: `screen: dim`, `screen: dark`, `screen: awake (<why>)` and
`backlight: <old> -> <new>`.

Test hooks:

- `LOKICTL_IDLE_SECONDS=<dim>,<dark>` (whole seconds, dim at least 1, dark
  later) shortens the times in a normal start.
- `LOKICTL_BACKLIGHT_FILE=<file>` uses a plain file instead of the
  backlight, so the writes can be followed on any computer.
- Snapshots count the times in LVGL time, so `--settle-ms` crosses them. A
  stand-in backlight starts at 100 and prints each write as
  `backlight: <n>` on standard error:

```sh
B=./build/darwin-arm64/Debug/lokictl
$B --snapshot build/shots/woken.png --settle-ms 60000 --keys 6   # dims; 6 only wakes, Home stays
$B --snapshot build/shots/settings.png --keys 6                  # 6 opens Settings
```

## The network link

The link is read by `platform::read_network()` in
`src/platform/network.h`, from files the kernel offers to every user. It
only reads: it never brings an interface up or down, never starts a scan
and never changes a setting. It takes the folder to read from, like the
battery, so a test folder works on any computer.

- **Which link (the uplink):** the default route with the lowest metric in
  `/proc/net/route` whose interface has a cable or a network
  (`/sys/class/net/<name>/carrier` reads `1`). It is Wi-Fi when
  `/sys/class/net/<name>/wireless` exists and a cable when `type` is `1`;
  any other interface is skipped. An interface that does not carry the
  default route, such as a second Wi-Fi stick, is never the uplink. With
  no such route the page shows its "no network" look.
- **The router's address** is the gateway of that route.
- **The address** is the interface's first IPv4 address, from the system's
  list of addresses (`getifaddrs()`).
- **The signal** is the level in `/proc/net/wireless`, in dBm. Anything
  outside −255 to −1 is not known.
- **The cable's speed** is `/sys/class/net/<name>/speed` in Mbit/s. Without
  a cable that file gives an error or −1, which means not known.

Whatever is not known shows `--`. The title bar shows the Wi-Fi icon by
signal (−60 dBm and above full, −75 and above medium, lower low, not known
without bars) or the cable icon; only the uplink's icon is shown.

## Names and state from NetworkManager

The kernel picks the uplink; NetworkManager, the system's network service,
adds what only it knows (`src/platform/network_manager.h`). On the device
the app talks to it through libnm; the Mac simulator has no libnm and gets
a stand-in that is never available, so there these values stay unknown
(the made-up sources and test folders still show them).

- **For a Wi-Fi uplink:** the network's name (SSID), its security and the
  link speed, taken from the NetworkManager device whose interface is the
  uplink's. The speed is the device's current bit rate, as
  `nmcli -g CAPABILITIES.SPEED device show wlan0` shows it (not the
  access point's top rate). The security is the strongest the access point
  offers: `WPA3`, `WPA2/3` (both), `WPA2`, `WPA`, `WPA3-EAP`, `WPA2-EAP`,
  `WPA-EAP`, `OWE`, `WEP` or `open`.
- **With a link:** whether the internet answers, from NetworkManager's own
  check. The page shows `no internet` (limited or none) or `login page`
  (portal) in the warning colour: in the middle of Instrument's status
  strip, in the corner of Terminal's link pane (where `sample data` stands
  on made-up values, which win) and in place of Constellation's `internet`
  caption.
- **Without a link:** why. `network off` when networking is switched off,
  else `connecting` while a Wi-Fi or cable device is being connected, else
  `wi-fi off` when the radio is off, else `no network`. A cable that is not
  plugged in is not named: the cable port is empty most of the time.
- Devices NetworkManager does not manage (the USB Wi-Fi stick) and devices
  that are neither Wi-Fi nor cable are skipped.

Reading only reads: every libnm function a reading calls is a getter,
apart from making and releasing the client. The only calls that change
anything are the switch between modes (see
[Switching modes](#switching-modes)), kept in one marked block of
`src/platform/network_manager_client.cpp`; the only other request is a
Wi-Fi scan the user asks for (see [The Wi-Fi list](#the-wi-fi-list)), in
a block of its own, which changes no setting. It has no thread of its own. The client
starts without waiting (and without asking polkit which actions the app may
take) on its own GLib main context; NetworkManager's change messages queue
up there, and the live source applies them, without waiting, at each of its
readings, so nothing asks NetworkManager again. While the client starts,
the live source reads every 100 ms; the app's first reading waits up to
250 ms for it and `--print-data` up to 2 s.
When NetworkManager is not running, or there is no system bus, the kernel's
values are shown and these stay unknown. On quit the client is released and
its context run until libnm has let go (at most half a second).

The internet and offline states change seldom, so a change of either
builds the page again instead of updating it in place.

## Switching modes

The Modes page (key 4 on the home page) switches between Client, Router,
Share and VPN. The page only asks: `BaseViewModel::request_mode()` counts a
request, `app::DataController` hands it with the hotspot's password to the
reader (`DataReader::request_mode()`), and the reader hands it to the
source on its own thread and takes a reading at once. The switch's state
comes back in `HomeData::mode_switch` (idle, switching or failed, the mode
asked for, and why it failed), and the mode itself in `HomeData::mode`. The
UI thread never waits.

**The mode comes from NetworkManager, not from memory.** An active profile
named `LokiCTL hotspot` that carries LokiCTL's own user-data mark means
Router: `mode`, `ap_active`, `ap_name`, `ap_security`, `ap_channel` and
`uptime_seconds` are filled in from it (the channel from the frequency of
the access point NetworkManager shows on `wlan0`; 0 when it shows none),
and `mode_detail` reads `eth0 → wi-fi ap · nat · dhcp`. The same profile
active on `lokiap0` means Share, with the same values. So a restarted
LokiCTL shows Router or Share and can undo it, and a restart of the device
always lands in Client.

Who really switches: only the `live` source on the device's real root, in a
normal start (`make_data_source(..., may_change_network = true)`). Every
other case pretends (`platform::PretendModeSwitch`): the made-up sources,
test folders, the Mac, snapshots and `--print-data`. Snapshots never
change anything.

- **Router** (`NetworkManagerClient::start_router()`): only when the
  kernel's uplink is a cable (a link and the default route), checked again
  at the request; otherwise it fails at once with `Plug in a cable first`.
  LokiCTL adds the profile `LokiCTL hotspot` in memory only (never written
  to disk, autoconnect off, not tied to the app, so it survives a crash):
  an access point on `wlan0`, 2.4 GHz, WPA2-PSK (CCMP), IPv4 shared, IPv6
  off. NetworkManager's shared mode hands out addresses (its own
  `dnsmasq`) and shares the link (`nft`). The name is the view model's
  (see below): `LokiCTL-` and the last four hex digits of `wlan0`'s
  hardware address until the user renames it. The UUID of the
  Wi-Fi profile `wlan0` was on, and the time the profile was made, go into
  its user data (`lokictl.restore`, `lokictl.started`). A leftover profile
  of LokiCTL's that is not active is deleted first. The switch is done when
  the hotspot is active; if it fails, or is not done after 20 s, it is
  undone (as Client below) and the page shows `Failed: <reason>`, or `Not
  allowed: permission missing` when polkit refused (the rule is missing).
- **Share** (`NetworkManagerClient::start_share()`): only when `wlan0` is
  joined to a Wi-Fi network as a client (`wifi_channel()` gives its
  channel); otherwise it fails at once with `Join a Wi-Fi network first`.
  The live source first has the helper add a second, virtual interface of
  the built-in Wi-Fi, `lokiap0` (`add_share_interface()`, `lokictl-helper
  ap-add`, which runs `iw dev wlan0 interface add lokiap0 type __ap`), on
  the reader thread. Once NetworkManager shows `lokiap0` as a Wi-Fi device
  that is ready (about a second; read() checks), the same in-memory
  profile as Router's is added there, on `wlan0`'s channel (the chip sends
  on one channel only) and without `lokictl.restore`: `wlan0` stays on its
  network and its profile is never touched. The same 20 s limit and undo
  apply. Devices on the hotspot get the internet through the default
  route: the cable when one is plugged in, otherwise `wlan0`. The chip
  restarts its radio whenever the hotspot starts, stops or restarts, so
  `wlan0` drops for a few seconds (usually 2 to 3; up to about 15 after a
  restart, when its first rejoin times out) and rejoins by itself with the
  same address; NetworkManager keeps it `connected`. Router while
  Share runs, and Share while Router runs, fail at once with `Back to
  Client first`.
- **Client** (`start_client()`): deletes every profile of LokiCTL's
  hotspot, then activates the Wi-Fi profile recorded in it on `wlan0`
  (from Share there is none, so nothing more is sent). Afterwards the live
  source removes `lokiap0` through the helper (`ap-del`) whenever it exists
  without an active hotspot and no switch runs: also after a Share switch
  that failed, and a `lokiap0` a killed run left (up to three tries, 5 s
  apart; never in the first reading, which runs on the UI thread).
  Without one, NetworkManager's autoconnect picks. It is done when
  NetworkManager has answered both; the Wi-Fi join itself then follows as
  usual. In plain Client nothing is sent.
- **VPN** (`NetworkManagerClient::start_vpn()`, see [The VPN](#the-vpn)):
  reads the chosen WireGuard file and adds the profile `LokiCTL VPN` in
  memory only, on the interface `lokiwg0`, then activates it. A bad file
  fails at once with its reason (`Failed: bad PrivateKey`). The same 20 s
  limit applies, with fixed reasons (`tunnel refused`, `tunnel stopped`,
  `no tunnel after 20 s`): NetworkManager's own text is not shown for the
  tunnel. A failed switch deletes only the profile it was adding (a VPN
  switch the tunnel's, a hotspot switch the hotspot's), so the other half
  of a gateway stays. Client deletes every profile of LokiCTL's; VPN off
  (`ModeRequest::vpn_off`, the VPN row's `Off`) deletes only `LokiCTL VPN`
  (`stop_vpn()`) while a hotspot runs, and is Client otherwise.
- **The VPN gateway** (F4.7): VPN while Router or Share runs, and Router
  or Share while VPN runs, are allowed (Router and Share together still
  fail with `Back to Client first`). Before the second half starts, the
  live source arms the **kill switch** on the reader thread
  (`DeviceBlocker::arm_kill_switch()`, `lokictl-helper kill-on`); if that
  fails the switch is refused with `Failed: kill switch: <reason>`. The
  kill switch lives in LokiCTL's table `inet lokictl`, in two chains of
  its own that block and unblock never touch: `kill_forward` drops
  whatever comes in on `wlan0` or `lokiap0` and would leave by any
  interface but `lokiwg0` (by name, so it holds while the tunnel is gone),
  and `kill_output` drops DNS (port 53) that the hotspot's `dnsmasq` (user
  `nobody`) sends by anything but `lokiwg0` or `lo`. It follows the user's
  intent, not the tunnel: it stays when the tunnel drops or NetworkManager
  takes it down, through a hotspot restart and through Block and Allow,
  and comes off (`kill-off`, or the whole table once no hotspot runs) only
  with VPN off or Client. `HomeData::vpn.kill_switch` shows it; with it on
  and no tunnel the pages say `VPN down: hotspot devices blocked`. The
  first time a start sees a hotspot (and only once NetworkManager has
  answered), the table is read (`lokictl-helper list`) instead of
  removed: its blocked devices count as applied, and a kill chain there
  adopts the gateway. In a test folder the stand-in file's `vpn on <file>
  <server>` and `kill-switch on` lines show the tunnel and the kill
  switch.
- **Every quit returns to Client** unless the Settings row "Keep mode
  after quitting" is on (`keep_mode=yes`; `DataReader::set_keep_modes()`):
  then the reader leaves the hotspot, `lokiap0`, the tunnel and the whole
  firewall table as they are, and the next start takes them over as
  above. Otherwise: hold-ESC, SIGTERM and SIGINT.
  Before the reader stops it calls `DataSource::leave_modes()`, which waits
  for an add still on its way, sends the Client requests and waits at most
  3 s (`DataReader::kLeaveModesLimit`) for NetworkManager to answer, well
  under the 5 s `scripts/device.sh stop` gives before SIGKILL. The Wi-Fi
  join finishes after the app has gone. In Share, at least 1 s of the 3 s
  is kept to remove `lokiap0` last, and it gets all the time left (right
  after the hotspot stops, the driver takes over a second). A quit in
  plain Client sends
  nothing.
- **A new name or password while Router or Share runs**
  (`restart_router()`, `ModeRequest::restart`): the profile is built
  afresh with the same UUID, the same `lokictl.restore` and a new
  `lokictl.started` (Share: on `wlan0`'s channel again), updated in memory
  only (`Update2` with the in-memory flag; it stays under `/run`) and
  activated again on its interface (`wlan0`, or `lokiap0` in Share). So `wlan0`
  goes straight from the old hotspot to the new one and never falls back
  to the home Wi-Fi in between; devices on it must join again. A failure,
  or 20 s without the hotspot, deletes the hotspot's profile (a tunnel
  stays). Nothing is sent while the hotspot is not active.
- While a switch is busy the live source reads every 100 ms, so the
  answers are handled soon.

**The pretend switch** counts readings, not time, so a snapshot always
gives the same picture: a request is taken at the next reading (a mode that
is already on does nothing; Router without a cable uplink fails at once,
Share without a Wi-Fi uplink too, and Router or Share while the other runs),
`Switching` shows for that reading, and the next one ends it. In a window
the source asks to be read again a second later. Once it has finished, the
pretend mode replaces the source's own: Router and Share show the sample's
made-up hotspot (`LokiCTL-7F`), Client shows no access point and no
clients, VPN shows the tunnel on with the file's server (in a test
folder) or the made-up `vpn.example.net`, and a reply 4 s ago. For
example:

```sh
B=./build/darwin-arm64/Debug/lokictl
$B --snapshot build/shots/modes-switching.png --page modes --data cable --keys down,6
$B --snapshot build/shots/modes-router.png --page modes --data cable --keys down,6,data
$B --snapshot build/shots/modes-no-cable.png --page modes --data wifi --keys down,6
$B --snapshot build/shots/modes-share.png --page modes --data wifi --keys down,down,6,data
$B --snapshot build/shots/modes-no-wifi.png --page modes --data cable --keys down,down,6
```

### The VPN

`HomeData::vpn` (`model::VpnStatus`) holds the WireGuard files LokiCTL can
use and the tunnel's state: `off`, `connecting` (NetworkManager activates
it) or `on`, with the file it came from and the server's host. Only those
two names: keys and the rest of a file never leave
`src/platform/wireguard.*`.

- **The folder** is `~/.config/lokictl/wireguard/` (in `$XDG_CONFIG_HOME`
  when that is set; `<folder>/wireguard` in a test folder). A normal start
  on the device creates it with mode 0700 and takes every right of group
  and others from it and from its `.conf` files (0700 and 0600), at every
  network reading. Snapshots, `--print-data` and the Mac never create it.
  Regular files ending in `.conf` count (no links), at most 16, sorted by
  name; the name without `.conf` is what pages show. They are listed with
  the network (every 5 s).
- **The reader** (`parse_wireguard_config()`) takes `[Interface]`
  PrivateKey, Address, DNS, ListenPort, MTU and FwMark, and `[Peer]`
  PublicKey, PresharedKey, AllowedIPs, Endpoint and PersistentKeepalive,
  without regard to case; `#` starts a comment. Keys must be base64 of 32
  bytes, addresses are checked with `inet_pton()`, files over 16 KiB are
  refused. `PreUp`, `PostUp`, `PreDown`, `PostDown`, `Table`, `SaveConfig`
  and other keys are skipped: nothing in a file is ever run. A bad file
  gives a fixed reason that quotes nothing of it (`bad PrivateKey`, `no
  [Peer]`, `bad line 7`). The file is read only at the request and its
  keys are wiped once NetworkManager has them.
- **The profile** is WireGuard on `lokiwg0` with the file's keys, port,
  mark, MTU and peers; manual addresses (a family without one is
  disabled); the file's DNS servers, used alone (priority -50) when a peer
  routes everything (`0.0.0.0/0` or `::/0`); peer routes and the default
  route are left to NetworkManager. The time it was made, the file's name
  and the server's host go into its user data (`lokictl.started`,
  `lokictl.file`, `lokictl.server`), so a restarted LokiCTL shows the
  tunnel; its keys are never read back.
- **Replies instead of a handshake time.** WireGuard counts as up without
  any answer, and its handshake time needs CAP_NET_ADMIN (libnm does not
  give it). While the tunnel is on, the live source reads `lokiwg0`'s
  received bytes at every reading, and every second until the first
  answer; `reply_seconds` is the time since they last grew, or
  `kVpnReplyNone` before anything came back. A new tunnel sends nothing
  (not even its handshake) until something goes through it, so `none`
  right after it comes up is true. In a test folder each reading takes the
  next line of `sys/class/net/lokiwg0/statistics/rx_bytes` and counts as
  one second, so `--data-steps` shows the age growing:

  ```sh
  printf 'running yes\nvpn on home 192.0.2.1\n' > build/vpn/networkmanager
  mkdir -p build/vpn/sys/class/net/lokiwg0/statistics
  printf '0\n0\n1200\n1200\n' > build/vpn/sys/class/net/lokiwg0/statistics/rx_bytes
  printf '0\n0\n100\n100\n' > build/vpn/sys/class/net/lokiwg0/statistics/tx_bytes
  LOKICTL_DATA_ROOT=build/vpn $B --print-data --data live --data-steps 3   # vpn_reply_s=1
  ```

For example (with the made-up files `home` and `travel`):

```sh
$B --snapshot build/shots/modes-vpn.png --page modes --data wifi --keys down,down,down
$B --snapshot build/shots/modes-vpn-on.png --page modes --data wifi --keys down,down,down,6,data
```

**The hotspot's password** never goes into `HomeData`, the log or
`--print-data`. On the device it is the `hotspot_password=` key of the
settings file: made at the first switch to Router (12 characters from a
set without look-alikes such as `0`/`o` and `1`/`l`), saved with the file
made readable by its owner only (mode 0600) before the line goes in, and
kept. The shipped `/etc/lokictl.conf` has no such line. The simulator and
snapshots show a fixed made-up password and never write one.

**The hotspot's name** lives in the view model next to the password: the
`hotspot_name=` key of the settings file when it holds a valid name (1 to
32 printable characters, no space at either end), else the default name.
The simulator and snapshots use the sample's made-up `LokiCTL-7F`.

**Rename and new password** (the Hotspot page): the page asks the view
model (`request_hotspot_rename()`, `request_new_hotspot_password()`), and
`app::DataController` makes the password (`random_hotspot_password()`; in
snapshots a fixed made-up one, `w4nbe8rhq2js`), sets the new values in the
view model, has them saved (only where the reader changes the real
network) and, while Router or Share runs, sends a restart request. A request while
a switch runs is ignored. Log lines: `data: hotspot renamed`, `data: new
hotspot password`, then the usual switch lines; never the values. The
pretend switch takes a restart like any switch (`Switching` for one
reading) and shows the new name afterwards. For example:

```sh
$B --snapshot build/shots/hotspot.png --page hotspot --data sample
$B --snapshot build/shots/hotspot-off.png --page hotspot --data cable
$B --snapshot build/shots/hotspot-renamed.png --page hotspot --data sample --keys 5,backspace,x,enter,data
$B --snapshot build/shots/hotspot-new-password.png --page hotspot --data sample --keys 6,6,data
```

The rights come from the package's polkit rule (see
[permissions.md](permissions.md#the-polkit-rule)): apps
started by the launcher are not an active login session, so without it
NetworkManager refuses with "not allowed".

## The traffic

The traffic is read by `platform::read_traffic_counters()` in
`src/platform/network.h`, and turned into rates by
`platform::LiveDataSource`. NetworkManager's own traffic statistics cannot
be switched on by an app, so the rates come from the kernel's byte
counters, which every user may read:
`/sys/class/net/<uplink>/statistics/rx_bytes` (received, the download) and
`tx_bytes` (sent, the upload).

- **Only while it is seen.** `app::DataController` follows the current page
  and tells the reader whether the home page, the only page with the
  traffic, is on top (`DataReader::set_traffic_shown()`). The reader wakes
  up at once and hands that on to the source. While another page is open
  nothing reads the counters, which saves battery.
- **Once a second.** The rate is the bytes since the reading before, times
  8, over the time between the two readings (measured, not assumed), in
  kbit/s. Each new pair of rates goes into the two histories, the last 24
  readings, which the Terminal theme draws as its two bar graphs.
- **Starting afresh.** The rates are not known (`--`), and both graphs are
  empty, after the first reading, without a link, while the home page is
  not on screen, when a counter cannot be read or went backwards (the
  interface came up again), and when the uplink changes. Each of these
  starts the traffic afresh, so the page never shows a huge made-up jump.
  So a second after the home page opens, the first rates appear, and the
  graphs fill from the right.
- **The graph's scale.** `model::RateHistory` (`src/model/rate_history.h`)
  keeps the rates and gives each bar as a share of the largest rate in its
  history, so each graph fills its own height. A quiet link would make a
  few stray bytes the peak, so the scale is never lower than 100 kbit/s
  (`kRateGraphFloorKbps`): a link that reads `0.0` never fills its graph.
- **Text.** Rates are written in Mbit/s with one decimal (`3.8`), and from
  100 Mbit/s on in whole Mbit/s (`120`), so a number below 1000 Mbit/s is
  never wider than `99.9`. When Constellation's line `↓3.8 ↑0.9 Mb/s`
  does not fit its panel (two rates of 10 Mbit/s or more), it writes
  `↓12.3M ↑10.2M` instead.

## Devices on the hotspot

While LokiCTL's hotspot is active, `live` fills `HomeData::clients` with
the devices on it (`platform::HotspotClients`, in
`src/platform/hotspot_clients.h`). They are read with the network (every
5 s), and at once when the hotspot comes up; when it stops they are
forgotten. Each device has:

- **its hardware address, time joined and traffic**, from the Wi-Fi
  driver's list of devices on the access point (a station dump of the
  kernel's nl80211 interface over plain generic netlink, which every user
  may read; only Linux headers, no extra library). A device that is listed
  but not let in yet is left out. The traffic is the bytes it received and
  sent since the reading before, per second; `--` at its first reading or
  when its counter went back;
- **its address**, from the neighbour table `/proc/net/arp` (complete
  entries on the hotspot's interface, by hardware address);
- **its name**, the one it sent when it got its address. NetworkManager's
  own DNS server on the hotspot (dnsmasq) knows it, and the lease file is
  readable by root only, so LokiCTL asks that server for the name of each
  address (a reverse lookup) at the hotspot's own address. All questions
  go out at once and it waits at most 250 ms per reading in all; nothing
  is asked while a switch between modes runs. An address is asked once,
  and again after a minute while the server knew no name. Only the first
  label is kept, at most 32 characters, and every byte that is not
  printable ASCII becomes `?`. A device without a name shows `unnamed`;
- **its signal**, when the driver tells it. The built-in Wi-Fi (brcmfmac)
  does not in access point mode, so on the device it is not known (`--` on
  the home page, left out on the Devices page).

A device keeps its place from one reading to the next, and newcomers go
after it, the longest joined first (then by hardware address), so the order
never changes under the highlight; at most 16 are listed. A station dump
that fails or comes too late keeps the last list. A device is `new` for its first five
minutes. While the same devices are listed, their signal, traffic and time
change in place on the home page (see
[How a page updates in place](#how-a-page-updates-in-place)). The made-up
sources keep their own clients; the sample's three have made-up,
locally administered hardware addresses (`02:5e:11:7a:c3:0n`).

### Blocking a device

Key 8 on the Devices page asks the view model to block or unblock the
highlighted device (`request_block()`). `app::DataController` keeps the
list of blocked hardware addresses (at most 16, the oldest dropped first),
saves it on the device as `blocked_devices=` (addresses separated by commas)
and hands the whole list to the reader (`DataReader::set_blocked()`), which
wakes and takes a reading at once. Each source marks the listed clients
(`HomeClient::blocked`; the Devices page then says `blocked`).

Only the live source on the device, when it changes the network, blocks for
real (`platform::DeviceBlocker`, in `src/platform/firewall.h`). It runs the
helper `lokictl-helper` (see
[permissions.md](permissions.md#lokictl-helper-cap_net_admin)) on the reader thread and makes
LokiCTL's own nftables table, `inet lokictl`, match the list: when the
hotspot comes up (first clearing a table a killed run may have left), when
the list changes (only the differences), and it removes the table when the
hotspot stops, once after the start, and on the way out before the switch
back to Client. It never runs the helper in the first reading (the UI
thread's) or while a switch runs; each run is killed after 3 s. A device
counts as blocked only once the helper said so. A failed change keeps the
old state, raises `HomeData::block_failures` and sets
`HomeData::block_message` (`Block failed: helper not installed`, `… no
permission`, `… firewall refused`, …), which the app logs and shows as a
message. Everywhere else (the made-up sources, test folders, the Mac,
snapshots) every listed device counts as blocked at once and nothing runs,
so `--keys 8` on the Devices page pictures a blocked device.

## The tools

The Ping, DNS, Trace, Ports and Speed pages, and listening and probing on
the Discover page (see [Listening](#listening)), run their tools away from
the UI thread.
`app::ToolController` hands the pages' requests to a
`platform::ToolRunner` and, from an LVGL timer every 250 ms, gives the
latest runs to the view model (`tools()`, `tools_subject()`) with the Sent
count. One job runs at a time; leaving the page does not stop it.

- **The live runner** (device builds, with `--data live`, the default
  there) has one thread of its own, started with the first job, with
  SIGTERM and SIGINT blocked. It never touches the view model, LVGL or the
  log, and every wait is at most 100 ms, so Stop and quitting are seen at
  once.
  - **Ping** uses an ICMP datagram socket (`SOCK_DGRAM`, `IPPROTO_ICMP`):
    no helper and no privileges, allowed by
    `net.ipv4.ping_group_range`, which LokiCTL never touches. When the
    system refuses the socket, the page says `Ping not allowed for this
    user`. One echo a second, ten in all; an echo with no reply in 2 s is
    lost. The kernel sets the identifier and hands back replies without the
    IP header, so replies are matched by sequence number. A name is looked
    up first (one A question).
  - **DNS** writes its own messages (`platform/dns_message.*`, also for
    later tools): one A and one AAAA question over UDP to the first IPv4
    `nameserver` of `/etc/resolv.conf` (`No DNS server` without one). A
    question with no answer in 2 s is sent once more, to the next IPv4
    nameserver when there is one, as the system's resolver does when the
    first one is down. Answers must match the question's ID, name and type;
    the parser checks every length against the end of the message and
    follows name compression only backwards. NXDOMAIN shows `No such
    name`, silence `No answer`.
  - **Trace** sends one echo per hop on the same kind of ICMP datagram
    socket, each with a time to live one higher than the last (1 to 20).
    With `IP_RECVERR` set, the system puts a hop's `Time exceeded` on the
    socket's error queue (`MSG_ERRQUEUE`), with the hop's address
    (`SO_EE_OFFENDER`) and the start of the echo, so its sequence number;
    the target answers with an echo reply. Each message is checked (ICMP
    origin, type, lengths, sequence) and copied out before use; the whole
    queue is read at every wake. The next echo goes out as soon as a hop
    answers, or after 1 s. Probing stops at the target's reply, at
    `Destination unreachable` or after 20 hops. Each hop that answers gets
    a PTR question (`*.in-addr.arpa`) at once, not waiting for the ones
    before; the same 2 s and next-nameserver rule as DNS applies, answers
    must match ID, name and type, and a name with anything but printable
    ASCII is dropped. The run ends when every name is in or given up. A
    name target is looked up first (one A question).
- **The pretend runner** (the Mac, snapshots, and the made-up data sources)
  has no thread and never opens a socket. Each run follows the LVGL time
  since it started, with fixed made-up results in the documentation ranges
  (192.0.2.0/24, 198.51.100.0/24, 2001:db8::/32): echo 6 takes 104 ms and
  echo 8 is lost, names in `.invalid` do not exist. So `--keys 6` on `ping`
  shows a run under way and `--keys 6 --settle-ms 11000` its end, and the
  same command always gives the same picture. A made-up trace to a home
  network address (such as the sample router) is reached in one hop; any
  other goes through six made-up hops (203.0.113.0/24 too, `.example`
  names, hop 4 silent, hop 6 over 100 ms) to the target as hop 7, each
  probe 100 ms after the one before settled. So `--keys right,6` on
  `trace` shows a run under way and `--keys right,6 --settle-ms 2500` its
  end.

**Sent** (`platform::sent_packets()`) counts every packet LokiCTL sent in
this run of the app: each echo (a trace's probes too) and each DNS
question (a trace's PTR questions too), retries included.
The pretend runner counts the packets it pretends to send. Later tools add
to the same count with `platform::count_sent()`. Listening adds nothing; a
port scan adds one per connection attempt and a speed test one per request
(see below).

Log lines: `tools: <ping|dns|trace|listen|probe|ports|speed> started`, `tools:
<ping|dns|trace|listen|probe|ports|speed> stopped by the user`, `tools: ping done, 9/10
replies`, `tools: ping failed: <why>`, `tools: dns done, <n> addresses`,
`tools: dns failed: <why>`, `tools: trace done, reached in <n> hops` (or
`unreachable at hop <n>`, `not reached in <n> hops`), `tools: trace failed:
<why>`, `tools: listen done, devices heard: <n>, frames: <f>`, `tools:
listen ended, devices heard: <n>`, `tools: listen failed: <why>`, `tools: ports
done, asked: <n>`, `tools: ports ended, asked: <n> of <t>`, `tools: ports
failed: <why>`, `tools: speed done, down <d> kbit/s (<n> bytes), up <u> kbit/s
(<m> bytes)`, `tools: speed failed: <why>` (never a target, a server, an
address, a hardware address, a name or a port result), and `tools: runner stopped` when a run was cut short by
quitting.

### Listening

The Discover page's step 1 (`model::ToolKind::Listen`, its run in
`tools().listen`, `model::ListenRun`) lists the devices heard on the
uplink interface (`HomeData::uplink_name`) for 5 minutes, and sends
nothing.

- **The live runner** runs the capture helper,
  `/usr/libexec/lokictl/lokictl-capture discover <interface> 300`
  (`platform::CaptureProcess` in `platform/listen.*`), with `posix_spawn`,
  an empty environment, no blocked signals and its standard output on a
  pipe (closed on exec, read without blocking). The helper alone holds
  `CAP_NET_RAW`; it hears only ARP, DHCP, mDNS (UDP to 5353) and SSDP
  (UDP to 1900), joins no multicast group, drops every capability and can
  then only receive (see
  [permissions.md](permissions.md#lokictl-capture-cap_net_raw)). The runner reads its PCAP stream
  without any rights (`platform::CaptureReader`: a wrong file header or a
  record longer than 2048 bytes ends the run with `bad capture data`) and
  hands each frame to `platform::HeardDevices`. Stop and quitting send the
  helper SIGTERM and SIGKILL after 100 ms. The helper's exit status 2 shows
  `Interface not usable`, 3 `Capture not allowed`, 4 `Capture could not
  start`; a helper that is missing shows `Capture helper not installed`.
- **The device list** (`platform::HeardDevices`) keeps one entry per
  hardware address (group addresses and zeros ignored), at most 64, sorted
  by IPv4 address with those without one last. From ARP it takes the
  sender's hardware and IPv4 address (a probe's 0.0.0.0 gives none). From
  DHCP it takes the client's hardware address from `chaddr` (never the
  frame's sender, which may be the server), its address (`ciaddr`, the
  requested address of a request, or the address an acknowledgement gives)
  and its host name (option 12: the first label, at most 32 printable ASCII
  characters, anything else `?`), plus the server itself. From mDNS and
  SSDP it takes the frame's sender and the packet's source address. An
  mDNS response (sent from port 5353, read with
  `dns::parse_all_records()`, every section, the cache-flush bit masked)
  also gives a host name, from an A record whose address is the packet's
  source (so a proxy answering for others is not named after them), and a
  kind from the service types it names (`_ipp._tcp` printer,
  `_googlecast._tcp` Google Cast, `_airplay._tcp` AirPlay, `_hap._tcp`
  HomeKit, `_smb._tcp` file share, `_ssh._tcp` SSH and more; the first in
  the table wins). An SSDP `NOTIFY` gives a kind from its `NT` device type
  (router, access point, printer, media player, media server, TV for
  DIAL), else the product its `SERVER` header names after `UPnP/1.x`.
  Questions (mDNS queries, SSDP `M-SEARCH`) only say the sender is there.
  An SSDP announcement or reply also marks the device as speaking UPnP
  (`upnp`) and keeps its `SERVER` line (`ssdp_server`, at most 60
  printable characters), which the health check uses.
  A DHCP name wins over an mDNS one; a kind is only replaced by a surer
  one (mDNS, then SSDP's device type, then its server product). Every
  length is checked against the captured bytes; a frame cut short or an
  option or record running past the end is read only as far as it is
  whole. The Cardputer's own ports (`/sys/class/net/*/address`, without
  `lo`) are marked with their interface. On a network whose switch or
  port filter keeps SSDP's multicast group away, no SSDP is heard (the
  helper does not join the group).
- **Saving** (`ToolRunner::save_capture()`, key 7): the reader keeps the
  stream as read, the file header and then whole records, at most 4 MiB
  (`model::kCaptureSaveMax`), until the next listen starts. A save during
  a run creates the file (`platform::CaptureFile`: `capture_folder()`,
  made 0700; a new file named after the local time, 0600, `O_EXCL` and
  `O_NOFOLLOW`), writes what was kept and appends the rest on each read
  until the run ends; a save after a run writes what it kept. Both happen
  on the runner's thread. The run's `save`, `save_name`, `save_bytes`,
  `save_full` and `save_reason` say how it goes; the log gets byte counts
  only, never the path.
- **The pretend runner** reads a made-up capture
  (`platform::pretend_capture()`) through the same reader and list, each
  frame at its own time: this device's cable and Wi-Fi ports, the router
  (asking again every 10 s), a TV, a printer and a phone that take their
  addresses over DHCP, a speaker that probes first, one frame cut short
  and one DHCP option that runs past the end, mDNS answers from the
  printer and the TV shaped like real ones (the cache-flush bit,
  compressed names, the address in the additional section, the TV's cut
  short in its last record), a phone's mDNS question and SSDP
  announcements from the router and the speaker, all with made-up locally
  administered hardware addresses on the sample's 10.0.0.x network. Its
  save writes no file and shows the made-up name `20260614-074600.pcap`.

### Probing

The Discover page's step 3 (`model::ToolKind::Probe`, its run in
`tools().probe`, `model::ProbeRun`) asks every address of the uplink's own
network, and sends. It starts only after the page's confirmation (key 8
twice), for that run only.

- **The range** (`platform::read_probe_range()` in `platform/probe.*`,
  from `getifaddrs()` on the runner's thread): the uplink interface's
  network, at most the /24 around its address, and only a private one
  (10/8, 172.16/12, 192.168/16); never the network or broadcast address or
  any of the Cardputer's own addresses. Anything else fails with a short
  reason (`not a private network`) and sends nothing.
- **The sweep:** one ICMP echo per address, 50 ms apart (about 13 s for a
  /24), on the unprivileged ICMP socket Ping uses. Each echo makes the
  system ask for the address by ARP; `/proc/net/arp`, read every second and
  once more 3 s after the last echo, gives the complete entries inside the
  range (`platform::parse_neighbours()`), which go into a
  `platform::HeardDevices` list like heard frames. `replied` counts the
  addresses that answered their echo.
- **Kinds and names:** then, from one UDP socket on an ephemeral port
  (never 5353, which avahi holds), with `IP_MULTICAST_IF` set to the
  uplink and loopback off: one mDNS question for
  `_services._dns-sd._udp.local`, one SSDP `M-SEARCH` (`ssdp:all`, MX 2,
  multicast time to live 1) and one mDNS reverse (PTR) question per device
  found without a name, 50 ms apart. A question from a port other than
  5353 is answered by unicast to that port; answers count for 3 s after
  the last question, only from addresses in the range whose device is in
  the neighbour table. Service types give kinds, a reverse record of the
  sender's own address gives a name, and an SSDP reply's `ST` header a kind
  as `NT` does.
- **Sent** counts every echo and question handed to the system (the
  system's own ARP questions are not counted). The log gets counts only.
- **The list:** the page shows the latest listen run and the latest probe
  run together (`model::discover_devices()`): one entry per hardware
  address; a device in both keeps the listen run's name and kind when it
  has them, the probe's address and the smaller age. `ToolsData::latest`
  says which of the two (Listen or Probe) started last, for the status
  line and the step caption.
- **The pretend runner** (`platform::pretend_probe()`) times the same
  steps on the sample's 10.0.0.0/24 network (the Cardputer at .12 and
  .13, so 252 addresses): the devices of the pretend capture plus a file
  server and a camera that never speak up while listening, through a
  made-up neighbour table and made-up mDNS and SSDP replies read by the
  same code.

### Port scan

The Ports page (`model::ToolKind::Ports`, its run in `tools().ports`,
`model::PortRun`) checks one device's TCP ports, and sends. It starts only
after the page's confirmation (key 6 twice), for that target only.

- **The target** is an IPv4 address, never a name, on a private network
  (10/8, 172.16/12, 192.168/16) or a link-local one (169.254/16):
  `model::port_scan_address()`, checked by the page, by
  `app::ToolController` and on the runner's thread, which also needs it
  inside the subnet of one of the Cardputer's own interfaces
  (`platform::read_own_networks()` and `platform::port_scan_allowed()` in
  `platform/port_scan.*`, from `getifaddrs()`), and not that subnet's
  network or broadcast address. Anything else fails with a short reason and
  sends nothing.
- **The scan:** one unprivileged non-blocking TCP `connect()` per port of
  `model::common_ports()` (50 well-known ports, ascending), one new attempt
  every 100 ms, at most 8 sockets at once, each attempt given 1 s. A
  refusal means closed; nothing in 1 s means no answer. When the system
  refuses an attempt itself (no route, the host known to be unreachable,
  not allowed) the run ends with the reason. Every socket is closed when
  the run ends, is stopped or fails.
- **Open ports say what they are** (`src/model/service_banner.*`,
  `model::PortRun::banners`). A connection that is made means open. A TLS
  port (443, 465, 853, 993, 995, 8443, 8883) is closed at once and keeps
  `TLS`, shown as `encrypted (TLS)` (there is no TLS library). A plain web port (80, 631, 1900, 3000,
  5000, 7000, 7547, 8000, 8008, 8080, 8123, 8888, 10000, 32400, 49152)
  gets one `HEAD / HTTP/1.0` request with a Host line, and the values of
  the answer's `Server:` and `X-Powered-By:` lines are kept, joined by
  `; ` (`nginx/1.18.0; PHP/7.4.33`). Port 53 gets one DNS question over TCP
  for the server's version (TXT `version.bind`, class CHAOS, no recursion,
  `platform::dns::build_version_query()`); the first text of the answer is
  kept, with `BIND ` before a bare version number. Every other port is
  only listened to, so nothing is ever written to a printer's raw port
  (9100, 515). What comes in is read for at most 1.5 s and 512 bytes, and
  reading ends early once a whole greeting, the end of the headers or the
  whole DNS answer is in. The run keeps printable ASCII only, at most 120
  characters (`model::kBannerKeptMax`): the first piece of text without its
  protocol's prefix (`SSH-2.0-`, `220 `, `+OK`, `* OK`), with Telnet's
  option bytes skipped. Pages show what fits.
- **The check** (`model::check_services()`, `src/model/vuln_list.*`): when
  a scan ends (not when it is stopped), the runner reads the software and
  version each port named (`model::read_service()`,
  `src/model/service_version.*`) and compares them with the built-in list
  `data/vulndb.tsv`, read line by line from the app's data folder and
  closed again, on the runner's own thread (in the pretend runner once, as
  the scan ends). `PortRun::list_date`, `verdicts` (per port: holes,
  exploited, outdated, vendor) and `findings` (at most 8 holes per port,
  worst first, then its outdated line) hold the result; the list's date is
  also `ToolsData::list_date` from the start, for the Status page. See
  [checks.md](checks.md).
- **Sent** counts one per attempt the system takes (its handshake packets
  and retries are not counted, like ARP for a probe), one per request to a
  web port and one per version question. The log gets attempt counts
  only: never the target, a port, what a port said, how many were open or
  what the check found.
- **The pretend runner** (`platform::pretend_port_scan()`) times the same
  attempts on the examples' networks (10.0.0.0/24, 10.42.0.0/24,
  10.20.30.0/24). The router (.1) has DNS, HTTP, HTTPS and UPnP open and
  two silent ports, the Cardputer itself SSH, and any other device one of
  three made-up sets; other addresses fail as not on the Cardputer's
  networks. So `--keys 6,6 --settle-ms 1500` on `ports` shows a scan under
  way and `--keys 6,6 --settle-ms 6000` its end. The made-up greetings
  give the check something to find: the router's dnsmasq 2.85 and
  MiniUPnPd 2.3.7 have holes, the Cardputer's own Debian OpenSSH reads
  `Check`, a printer's GoAhead (any address ending in a multiple of 3)
  `Attacked`, a media player's nginx 1.28 (addresses ending 3n+1) `Old`,
  and a storage box (3n+2, like the sample's hotspot devices) holes in
  Dropbear 2019.78 and an attacked PHP 7.4.33 behind nginx 1.18.0.

### Speed test

The Speed page (`model::ToolKind::Speed`, its run in `tools().speed`,
`model::SpeedRun`) measures the speed to the internet: a download, then an
upload, each for 10 s or 50 MB (`model::kSpeedPhaseBytesMax`), whichever
comes first, over HTTPS (`platform/speed_test.*`). So one test moves at
most about 100 MB, and the page says so before it starts.

- **The server** is `https://speed.cloudflare.com` by default: Cloudflare's
  speed endpoints, answered by its nearest site anywhere. Downloads are
  `GET <server>/__down?bytes=25000000` (the server takes up to 99,999,999
  bytes; never more than the phase may still move), uploads
  `POST <server>/__up` with up to 10 MB of zeros. Another server
  with the same two endpoints can be set in the settings file:

  ```ini
  [application]
  speed_test_server=https://speed.example.net
  ```

  The app reads the key and never writes it. It must be `https://` or
  `http://` and a host (port and path allowed), at most 200 printable
  characters, with no space, `@`, `?` or `#` (`model::valid_speed_server()`);
  anything else is ignored with a warning and the default is used. The
  page shows only the host, and the log never shows the address.
- **libcurl** does the transfers. Device builds compile against its
  headers only and load `libcurl.so.4` with `dlopen()` for each test, on
  the runner's thread, and let it go afterwards (`curl_global_cleanup()`,
  `dlclose()`, then `malloc_trim(0)`). Measured on Debian 13 arm64 after
  one HTTPS request, that gives back about 4.5 MB of the 13 MB that
  loading and the request added; libssl and libcrypto stay mapped, because
  OpenSSL pins itself and could never be started again after
  `OPENSSL_cleanup()`. libcurl stays loaded for the rest of the session
  once a test ended during its first request: `CURLOPT_QUICK_EXIT` may
  leave the name lookup's thread running inside libcurl then. The run's
  DNS cache never expires, so no later connection starts a lookup.
  Linking libcurl at build time would load 18 more libraries (OpenSSL,
  Kerberos, LDAP and more) at every start: a small test program grew from
  1.0 to 7.3 MB RSS. So the package
  names `libcurl4t64` and `ca-certificates` in `Depends:` itself.
  Certificates are always checked, no redirect is followed, HTTP/1.1 only.
- **The run:** one small request first (`bytes=0`: the name, the
  connection and TLS), then downloads one after another until 10 s passed
  since the first body byte or the phase moved 50 MB, then uploads the
  same way. A phase that a server overfills ends at once too. A phase cut
  short by the cap ends with one shorter bar (at least 125 ms) and its
  speed over its real time, so its graph has fewer than 20 bars. One curl multi
  handle drives each request with waits of at most 100 ms, so Stop and
  quitting are seen at once. A 10 s connect timeout; 10 s without data, or
  an answer other than 200, ends the run with the reason (`Server name not
  found`, `Server said 403`).
- **Counting, honestly:** downloaded bytes are the body bytes received;
  uploaded bytes are the bytes the server acknowledged (the kernel's
  `tcpi_bytes_acked` from `TCP_INFO`), because bytes handed to the system
  wait in its send buffer first and would read high (and never more than
  the body bytes handed over, since TLS adds a little). A speed is bytes × 8
  ÷ the phase's time; each half second gives one bar of the graph.
  **Sent** counts one per request (like a port scan's one per attempt):
  the TCP segments, TLS and the system's name lookup are not counted. The
  page shows the bytes each way instead. A full test moves 10 s at the
  line's full speed each way, at most 50 MB each: 50 MB down and 25 MB up
  at 90 and 20 Mbit/s.
- **The pretend runner** (`platform::pretend_speed_test()`) gives a fixed
  made-up run of about 15 s and opens no socket: the download (about 89
  Mbit/s) reaches the cap after about 4.8 s and shows 50.0 MB, 10 bars and
  82.9 Mbit/s; the upload (about 18 Mbit/s) runs its 10 s. So `--keys 6
  --settle-ms 3000` on `speed` shows the download, `--settle-ms 9000` the
  upload and `--settle-ms 16000` the end.

### Health check

The health check (`model::HealthRun`, in `tools().health`; rules in
`model/health_check.*`, what each looks at in [checks.md](checks.md#the-health-check))
is one more tool job, so it takes the runner like the others.

- **The request** carries the Cardputer's own links as the check starts
  (`model::HealthLocal`, filled by `app::ToolController` from the data
  layer): the uplink's address and router, the joined Wi-Fi and its
  security (the uplink's, else the scan list's joined entry), the scan list
  itself and whether the uplink is a cable. The Health page has the data
  layer read NetworkManager's scan list like the Wi-Fi page does (no scan
  is asked for).
- **The live runner** reads the cable's `speed` and `duplex`, then runs the
  probe's sweep and questions (`probe_network()`), picks the devices
  (`model::health_hosts()`: router, this device, then the sweep's and the
  Discover list's devices on that network, by address, at most 32) and
  scans each one's ports (`scan_ports()`), one after the other. Each
  finished scan is checked against the list of known holes (plus the SSDP
  SERVER line's version, `platform::add_ssdp_version()`) and turned into
  findings at once; only the findings and counts are kept. Neither the
  probe's nor the port scan's own latest run changes. Stop ends it within
  about 0.1 s. The log gets counts only.
- **The pretend runner** (`platform::pretend_health()`) works the whole
  made-up check out as the job starts (the made-up probe and each device's
  made-up scan, the cable at half duplex) and shows it on a short clock:
  4 s of sweep, then 0.6 s per device. `--keys 6,6,data,data --settle-ms
  9000` on `health` shows the end; `--keys 6,6 --settle-ms 2000` the sweep.

## Watch

Watch (`model::WatchData`, in `BaseViewModel::watch()`) warns about
suspicious things on the uplink's network while LokiCTL is open. It only
listens and never blocks anything. It does not use the tool runner, so it
runs next to any tool: `app::WatchController` owns a
`platform::WatchRunner` (`platform/watch.*`), takes its run four times a
second, keeps the known devices and hands each new alert to the key
controller's message.

- **The live runner** (device builds) has its own thread. It reads the
  router's hardware address from `/proc/net/arp` and the IPv6 routers in
  use from `/proc/net/ipv6_route`, then runs
  `/usr/libexec/lokictl/lokictl-capture watch <interface> 1800` (the same
  `platform::CaptureProcess`, its `watch` filter: ARP, DHCP, TCP SYN
  without ACK sent to this machine, IPv6 router announcements) and reads
  the stream into `platform::WatchDetector`. When the helper ends
  normally (30 minutes, or 16 MiB) it runs again after a second, and the
  detector keeps what it knew; any other end fails the run.
- **The pretend runner** (the Mac and snapshots) feeds made-up frames
  (`platform::pretend_watch_frames()`: one of each alert within the first
  17 s) through the same detector, timed from the start.
- **The detector** (`platform/watch_detector.cpp`) holds every threshold
  in `model/watch_data.h`: a scan is 10 different ports within 10 s from
  one source; a flood is more than 50 ARP frames in one second; two
  hardware addresses on one IPv4 address count within 2 minutes; a scan or
  flood may alert again after 60 quiet seconds; everything else alerts
  once per subject per run. The Cardputer's own hardware addresses never
  count. Every table has a fixed size. On a first run (no known devices)
  the first 60 s learn the devices heard instead of alerting; they are
  saved only once the whole minute has passed, so a run stopped sooner
  keeps nothing and the next one learns again.
- **Known devices** are saved on the device as `known_devices=` (at most
  64, the oldest dropped first); the simulator starts with made-up ones
  and never writes them.
- Log lines: `watch: started`, `watch: stopped by the user`, `watch:
  ended, alerts: <n>, frames: <f>`, `watch: failed: <why>`, `watch: alert:
  <kind>`, `watch: learned devices: <n>`, `watch: device marked known`
  (never an address, a hardware address or a name).
- `tests/watch_alerts.cpp` (`lokictl_watch_tests`) replays made-up frames
  for each alert kind, and ordinary traffic that must raise none.
- Alerts from elsewhere (the trap's connections, `WatchController::
  add_alert()`) join the log in the order they came, show with Watch on or
  off, and stay when a new Watch run starts.

## The trap

The trap (`model::TrapRun`, in `BaseViewModel::trap()`) is a honeypot:
fake services on fixed ports (`model::trap_services()`: 21, 23, 80, 445,
3389 and 8080). `app::TrapController` owns a `platform::TrapRunner`
(`platform/trap.*`), takes its run four times a second and hands each new
connection to the key controller's message and to Watch's log. It is off
at every start and stops when LokiCTL quits, keep mode or not.

- **The live runner** (device builds) has its own thread. It leaves alone
  every port something on the Cardputer already listens on
  (`/proc/net/tcp` and `tcp6`), listens on the others (IPv4, as the user;
  the low ports on 30021, 30023, 30080 and 30445) and runs the firewall
  helper's `trap-on` (`platform::trap_on()`), which sends the low ports on
  to those; a low port it did not send on is closed again (`in use`, or
  `no helper`). Each connection, within 20 a minute in all and 5 per
  source (`model::TrapLimiter`; more are closed at once and only counted),
  is logged at once, gets a harmless greeting (FTP `220`, a Telnet
  `login:` prompt, a plain web page after the request; nothing for file
  sharing and remote desktop; one packet in Sent each), is read for at
  most 5 s and 512 bytes and is closed. At most 8 are open at once. Every
  10 s it checks that nothing else began to listen on a low port it sends
  on, and runs `trap-on` again without it if something did. A stop closes
  everything and runs `trap-off`.
- **The firewall table.** The helper runs one at a time
  (`platform::run_helper()` holds a lock), and while the trap is set a
  removal of the whole table (`DeviceBlocker`'s `clear`, when the hotspot
  stops) sets the redirect up again at once. A trap chain a killed run
  left behind goes at the next start (`clear`, or `trap-off` when the
  table is adopted).
- **The pretend runner** (the Mac and snapshots) adds four made-up
  connections in the first 6 s and shows 8080 as in use.
- **What is kept:** the newest 100 connections, in memory only: time,
  address and port, the fake port, and the first bytes as printable text
  (`model::trap_shown_text()`: line ends become spaces, other bytes dots,
  60 characters at most).
- Log lines (from `app::TrapController`): `trap: set by the user`, `trap:
  taken away by the user`, `trap: on, ports open: <n>`, `trap: connection
  to port <p>`, `trap: off, connections: <n>, closed at once: <m>`, `trap:
  failed: <why>`, `trap: firewall helper: <why>` (never an address or what
  was sent).
- `tests/trap_checks.cpp` (`lokictl_trap_tests`) checks the limits, the
  shown text, the reading of the listening ports and the port map.

## The Wi-Fi list

The Wi-Fi page shows `model::WifiScan`, which the block carries as
`HomeData::wifi_scan`; the home page ignores it.

- **Only while it is seen.** `app::DataController` tells the reader whether
  the Wi-Fi page is on top (`DataReader::set_wifi_shown()`). With `live`
  the list is read at every reading while it is, and is empty otherwise.
  The made-up sources always fill it (`platform::PretendWifiScan`, with the
  invented networks of `model::example_wifi_networks()`).
- **Where it comes from.** The reader's one libnm client
  (`NetworkManagerClient::read_wifi_scan()`), for `wlan0` only (the USB
  stick is not managed by NetworkManager): each access point's name (empty
  for a hidden network), security (as the home page writes it), strength in
  per cent (libnm gives no dBm) and channel; joined = the device's active
  access point. Strongest first, at most 32 kept, plus the number found.
  The last-scan time is the device's own, shown as a clock time, so nothing
  ticks between scans. No list without NetworkManager, without a managed
  `wlan0`, with the radio off, or while LokiCTL's hotspot runs.
- **Scans.** Only the page's key 6 asks for one
  (`BaseViewModel::request_wifi_scan()`, then `DataReader::
  request_wifi_scan()` and `DataSource::request_wifi_scan()`), never
  opening the page. `NetworkManagerClient::request_wifi_scan()` sends it
  from its own marked block: a scan changes no setting, has its own
  in-flight flag (it is not `busy()`, so it never holds up a switch, the
  firewall helper or client names), is ignored while a switch runs and is
  cancelled on quit. It ends when the device's last-scan time changes, or
  after 15 s; the reader reads every 250 ms meanwhile. A switch to Router
  or Share drops a scan still waiting, without an error. A permission
  refusal shows `Scan not allowed: permission missing`, any other refusal
  `Scan refused: <reason>` with the list kept; opening the page again
  starts without that text. NetworkManager sends the scan, so
  `Sent` does not count it. The permission (`wifi.scan`) comes from M5's
  own rule for the `netdev` group.
- **Log lines:** `data: wi-fi scan asked`, then `data: wi-fi scan done
  (<n> networks)` or `data: wi-fi scan: <why not>`; never a network name.
  `--print-data` has no Wi-Fi lines.

## The Status page's details

The Status page (home page, key 8) shows the extra values of
`model::StatusDetails`, which the block carries as `HomeData::details`. The
home page shows none of them, so a change there never builds it again.

- **Only while it is seen.** Like the traffic, `app::DataController` tells
  the reader whether the Status page is on top
  (`DataReader::set_details_shown()`). With `live`, the details are read at
  once when the page opens (they appear a moment later and fill in place),
  then the ports and the DNS servers every 5 s with the network, and the
  battery details and the device values every 10 s with the battery. While
  another page is on top every detail is unknown and nothing reads them.
  The made-up sources always fill them, from their own home values
  (`model::example_status_details()`), with made-up hardware addresses
  (`02:...`) and IPv6 addresses from the range kept for examples
  (`2001:db8::/32`).
- **Ports** (`platform::read_ports()` in `src/platform/network.h`): every
  interface in `/sys/class/net` with `type` 1 and a `device` entry (real
  hardware: on the device `eth0`, `wlan0` and a USB Wi-Fi stick), by name,
  at most four. State from `flags` (not up: `off`) and `carrier` (`no
  cable` or `not joined`, else `connected`); the IPv4 address with its
  prefix (`getifaddrs()`); from `/proc/net/if_inet6` the first IPv6 address
  that is not link-local and the link-local one; the router of the port's
  own default route; the hardware address (`address`); signal and cable
  speed as for the uplink; NetworkManager's Wi-Fi name, security and speed.
- **DNS** (`platform::read_dns_servers()`): the `nameserver` lines of
  `/etc/resolv.conf`, at most three.
- **Battery** (`platform::read_battery_details()`): `voltage_now` (µV),
  `current_now` (µA, negative while discharging), `temp` (tenths of a
  degree), `health`, `cycle_count`, `charge_now` and `charge_full` (µAh),
  and `time_to_empty_now` or `time_to_full_now` (seconds; the battery
  answers them with a read error while they do not apply).
- **Device** (`platform::read_device_details()`): the thermal zone whose
  `type` is `cpu-thermal` (thousandths of a degree), `MemAvailable` and
  `MemTotal` of `/proc/meminfo`, and `/proc/uptime`.

Every reader only reads. In a test folder the same files are read under
the folder, and the `addresses` file may give a prefix:
`<interface> <IPv4>/<prefix>`.

## USB Wi-Fi adapters

LokiCTL needs no USB Wi-Fi adapter (a Wi-Fi stick): every feature runs on
the built-in Wi-Fi, `wlan0`. The Wi-Fi scan, the hotspot and the modes
always use `wlan0`, and LokiCTL never changes a stick. It only notices when
one is plugged in or removed, and says so. (If NetworkManager joins a
managed stick to a network and routes the traffic through it, the home
page shows that link like any other uplink.)

- **Reading** (`platform::read_usb_wifi()` in `src/platform/usb_wifi.h`):
  every interface in `/sys/class/net` with a `wireless` folder whose
  `device/uevent` says `DEVTYPE=usb_interface`. The built-in Wi-Fi sits on
  SDIO and a USB network cable port has no `wireless` folder, so neither
  counts. The driver comes from `DRIVER=`, the USB id from `PRODUCT=`
  (`cf3/9271/108` reads `0cf3:9271`), the name from the USB device's
  `manufacturer` and `product` files (printable ASCII only, at most 48
  bytes). Interfaces on the same stick (a monitor interface beside
  `wlan1`) are one adapter, named after the first by name. At most four.
- **Managed:** whether NetworkManager manages it (`yes`, `no`, or `--`
  without the service), from the same connection as the names. It reads
  `no` where NetworkManager is set to leave the stick alone (an
  `unmanaged-devices` rule); otherwise NetworkManager usually takes it as
  a second Wi-Fi device and it reads `yes`.
- **When:** with the network, every 5 s, whatever page is on top (in a test
  folder at every reading), into `HomeData::usb_wifi`. It only reads; it
  never changes the adapter or any network setting.
- **Message:** when a stick appears or goes after the first reading,
  `app::DataController` logs it and the app shows `USB Wi-Fi adapter
  connected: wlan1` or `USB Wi-Fi adapter removed: wlan1` for 3 s, on any
  page. A stick that is there at the start is only logged, and a new
  managed state or a renamed interface on the same stick shows nothing.
- **Status page:** after the ports, a `usb wi-fi` part: per adapter its
  interface, name, driver, usb id and managed state; with none, `adapter
  none (optional)` and `built-in Wi-Fi is used`. The stick's own port gets
  ` · usb` after its name.
- **Test folder:** a stick is a folder like the device's, for example
  `sys/class/net/wlan1` as a link to
  `../../devices/.../1-1.2/1-1.2:1.0/net/wlan1`, with a `wireless` folder,
  `device` as a link to `../../../1-1.2:1.0`, that folder's `uevent`, and
  `manufacturer` and `product` one folder above it. The made-up sources
  have no adapter.

## The demo script

The demo shows that the page follows its data. It is fixed, so pictures of
it can be compared. One step is one reading, and step 0 is exactly the
sample.

- The clock is the sample's `07:46` plus one minute per step. After 23:59
  comes 00:00.
- The battery starts at the sample's 91 % and loses one per cent per step,
  discharging, down to 5 %. Then it charges, one more per step, up to
  100 %. It stays idle at 100 % for three steps, then discharges again down
  to 5 %, and so on in the same cycle.
- The traffic moves: at step `n` the sample's two histories are turned
  left by `n` places (the oldest value comes back as the newest), so the
  graph scrolls round and round, and each rate matches its newest bar
  (the sample's rate times that bar over the sample's own newest bar).
  Step 1 reads `0.5` down and `0.4` up. The clients keep their rates.
- Every other value stays the sample's.

Fixed points (`--print-data --data demo --data-steps <n>` prints them):

| Step | Clock | Battery on the page | State |
|---|---|---|---|
| 0 | 07:46 | `91%` | discharging |
| 1 | 07:47 | `90%` | discharging |
| 82 | 09:08 | `9%` | discharging |
| 86 | 09:12 | `5%` | discharging |
| 87 | 09:13 | `6%+` | charging |
| 181 | 10:47 | `100%+` | charging |
| 183 | 10:49 | `100%` | idle |
| 185 | 10:51 | `99%` | discharging |
| 973 | 23:59 | `83%` | discharging |
| 974 | 00:00 | `82%` | discharging |

In a window (a normal start) the demo takes one step a second, so the clock
runs one minute per second and the battery counts down.

## Adding a new value

Say a later version shows the Wi-Fi network's channel. The steps:

1. **Model:** add a field to `model::HomeData` in `src/model/home_data.h`,
   on one line, with its unit in the comment and its "not known" value as
   the default, and add it to `operator==` in `src/model/home_data.cpp`.
   That operator decides whether a block is new, so a field missing there
   never reaches the page.
2. **Built again by itself:** from then on, a change in the new field
   builds the page's content again, because `home_values_only()` does not
   know it. That is always correct, only slower. If the value changes
   while the situation stays the same, add it to `home_values_only()` in
   `src/view/screens/home_layout.h` and to every layout's `update()` (step
   7), so it changes in place.
3. **Sample and example sets:** give it a made-up value in
   `model::sample_home_data()` and, where it belongs, in the sets and tour
   stops of `src/model/home_examples.cpp`. These two files are the only
   places where made-up values are written; nothing under `src/view` or
   `src/app` writes one.
4. **Live reader:** write a small reader in `src/platform`, like
   `battery.*`. It takes the folder to read from, never throws, and never
   uses LVGL, the view model or the log. Call it from
   `platform::LiveDataSource::read()`. If the value changes on its own
   schedule, take that into account in `next_read_delay()`.
5. **Demo:** if the value should move in the demo, add it to the demo
   script in `src/platform/data_source.cpp`, starting from the sample's
   value.
6. **Text:** add a helper to `src/view/screens/home_format.h`, so the
   value reads the same in every theme, with `--` when it is not known.
7. **Layouts:** in each layout, `build()` makes the label and keeps a
   pointer to it and the text it shows; if the value is on the in-place
   list, `update()` sets the new text with `set_text_if_changed()`. Use
   one text function for both.
8. **Readout:** add a `name=value` line to `--print-data` in
   `src/app/data_readout.cpp`, after the lines that are there, with
   `unknown` for a value that is not known.
9. **Test folder:** make a small folder that holds the files the reader
   reads, and check the reading with `LOKICTL_DATA_ROOT` and
   `--print-data`.

If the value needs a log line, write it in `app::DataController`, never in
the reader.

## Test hooks

None of these changes a setting.

### `--data <sample|demo|live|wifi|cable|offline|tour>`

Picks the source in every mode. A wrong name stops the app with exit code
2 and a message that names all seven. Names are lowercase and exact:
`Wifi` and `wi-fi` are refused.

```sh
B=./build/darwin-arm64/Debug/lokictl
$B --data sample                 # a window whose values never change
$B --data tour                   # a window that changes situation every 2 s
$B --snapshot build/shots/home-live.png --data live
$B --snapshot build/shots/home-wifi.png --data wifi
```

### `--data-steps <n>`

Only with `--snapshot` or `--print-data`: takes `n` further readings (0 to
100000, default 0) right after the first one. These runs have no
background thread. Each step asks the source, hands the values over the
same way and, in a snapshot, updates the page or builds it again, one after
the other, so nothing has to be waited for and the same command always
gives the same picture. With `demo` each step is one step of the script;
with `tour` each step is one stop; with `live` a step reads the clock
again, and the battery only once 10 s have passed (in a test folder each
step also reads the traffic; see [`LOKICTL_DATA_ROOT`](#lokictl_data_root)). With `sample`, `wifi`,
`cable` and `offline` the steps change nothing. A tour step usually builds
the page again, so many thousands of tour steps take minutes in a
snapshot, while demo steps stay fast.

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
$B --snapshot build/shots/home-181.png --page home --data demo --data-steps 181
$B --snapshot build/shots/home-widest.png --page home --data tour --data-steps 15
```

The first picture shows `10:47` and `100%+`, the second the tour's widest
texts.

### The name `data` in `--keys`

Among the names of `--keys`, `data` is not a key: it takes one further
reading at that point. It has no `:long`. For example, two readings arrive
while Settings is open, and the home page shows them when it comes back:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
$B --snapshot build/shots/a.png --page home --data demo --keys 6,data,data,esc
$B --snapshot build/shots/b.png --page home --data demo --data-steps 2
cmp build/shots/a.png build/shots/b.png && echo same
```

With the tour, a new situation that arrives while Settings is open is
there when the home page comes back; this picture is the `wifi` picture:

```sh
$B --snapshot build/shots/c.png --page home --data tour --keys 6,data,esc
$B --snapshot build/shots/d.png --page home --data wifi
cmp build/shots/c.png build/shots/d.png && echo same
```

It works in snapshot runs only. `scripts/device.sh key` does not know it.

### The order inside a snapshot

1. The page is built, with the first reading.
2. `--data-steps` readings.
3. `--keys`, where each `data` takes one more reading.
4. `--switch-to`.
5. The picture.

A page updated in place must look exactly like one built fresh with the
same values. `--switch-to` builds the page again at the end, so this checks
it:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
$B --snapshot build/shots/in-place.png --page home --theme terminal --data demo --data-steps 82
$B --snapshot build/shots/fresh.png --page home --theme terminal --data demo --data-steps 82 --switch-to instrument,terminal
cmp build/shots/in-place.png build/shots/fresh.png && echo same
```

The same check with `--data tour` and any step from 1 to 26 covers both
the values changed in place and the content built again.

### `--print-data`

No screen and no keys. It takes the first reading (and the `--data-steps`
further ones) and prints one `name=value` line per value on standard
output, then exits with code 0. It uses the same default source as a
normal start (`live` on the device, `demo` in the simulator), never reads
or writes the settings file and never touches the screen. It takes only
`--data` and `--data-steps`; any other option with it stops the app with
exit code 2.

```sh
$ ./build/darwin-arm64/Debug/lokictl --print-data --data wifi
source=wifi
sample=yes
clock=07:46
battery_percent=91
battery_state=discharging
mode=client
link=wifi
interface=wlan0
address=10.20.30.44
router=10.20.30.1
speed_mbps=72
network=Harbour-Cafe
security=WPA2
signal_dbm=-57
access_point=no
ap_channel=unknown
clients=0
down_kbps=2400
up_kbps=300
down_history=12,18,30,44,62,80,100,86,70,52,40,30,24,20,28,40,56,72,60,46,34,26,30,38
up_history=8,10,8,14,22,12,8,10,30,55,100,60,28,14,8,8,12,20,12,8,10,16,10,8
internet=unknown
state=unknown
ports=2
port=eth0 kind=cable state=no-link uplink=no ipv4=unknown ipv6=unknown ipv6_link=unknown router=unknown speed_mbps=unknown signal_dbm=unknown hardware=02:4c:4b:00:00:01 security=unknown network=unknown
port=wlan0 kind=wifi state=connected uplink=yes ipv4=10.20.30.44/24 ipv6=2001:db8:85a3:42:1000:8a2e:370:7334 ipv6_link=fe80::4c:4bff:fe00:2 router=10.20.30.1 speed_mbps=72 signal_dbm=-57 hardware=02:4c:4b:00:00:02 security=WPA2 network=Harbour-Cafe
dns=10.20.30.1
battery_voltage_mv=4137
battery_current_ma=-480
battery_temp_tenths=314
battery_health=Good
battery_cycles=7
battery_charge_mah=1592
battery_full_mah=1750
battery_empty_min=199
battery_full_min=unknown
cpu_temp_tenths=523
memory_free_mb=212
memory_total_mb=446
uptime_s=191820
usb_wifi=0
vpn=off
vpn_file=unknown
vpn_server=unknown
vpn_reply_s=unknown
vpn_kill_switch=off
vpn_files=home,travel
```

The lines always come in this order:

| Line | What it says |
|---|---|
| `source=` | the source's name |
| `sample=` | `yes` while the values are made up |
| `clock=` | the clock as `HH:MM` |
| `battery_percent=` | a number from 0 to 100, or `unknown` |
| `battery_state=` | `charging`, `discharging`, `idle` or `unknown` |
| `mode=` | `client`, `router`, `share` or `vpn` |
| `link=` | the kind of link: `none`, `wifi` or `cable` |
| `interface=` | the uplink's interface, for example `wlan0` |
| `address=` | the device's address on the uplink |
| `router=` | the router's address |
| `speed_mbps=` | the link speed in Mbit/s |
| `network=` | the Wi-Fi network's name |
| `security=` | the Wi-Fi network's security, for example `WPA2` |
| `signal_dbm=` | the Wi-Fi signal in dBm, with a plain hyphen: `-57` |
| `access_point=` | `yes` while the device runs its own access point, else `no` |
| `ap_channel=` | the access point's Wi-Fi channel, or `unknown` (always without an access point) |
| `clients=` | the number of connected clients |
| `down_kbps=`, `up_kbps=` | the current rates in kbit/s |
| `down_history=`, `up_history=` | the two graphs: 24 shares of 0 to 100 (100 is the history's peak), oldest first, separated by commas |
| `internet=` | NetworkManager's internet check: `full`, `limited`, `portal`, `none` or `unknown` (always `unknown` without a link) |
| `state=` | why there is no link: `connecting`, `wifi-off`, `network-off` or `unknown` (always `unknown` with a link) |
| `ports=` | how many ports the Status page lists |
| `port=` | one line per port: its interface, then `kind=`, `state=` (`off`, `no-link`, `connected`, `access-point` or `unknown`), `uplink=`, `ipv4=`, `ipv6=`, `ipv6_link=`, `router=`, `speed_mbps=`, `signal_dbm=`, `hardware=`, `security=` and, last because it may hold spaces, `network=` |
| `dns=` | the DNS servers, separated by commas |
| `battery_voltage_mv=` to `battery_full_min=` | the battery's details: voltage (mV), current (mA, negative while discharging), temperature (tenths of a degree), health, cycles, charge and full charge (mAh), minutes until empty and until full |
| `cpu_temp_tenths=`, `memory_free_mb=`, `memory_total_mb=`, `uptime_s=` | the processor's temperature (tenths of a degree), free and total memory (MiB) and the seconds since the device started |
| `usb_wifi=` | how many USB Wi-Fi adapters are plugged in (see [USB Wi-Fi adapters](#usb-wi-fi-adapters)) |
| `usb_wifi_adapter=` | one line per adapter (none without one): its interface, then `driver=`, `usb_id=`, `managed=` (`yes`, `no` or `unknown`) and, last because it may hold spaces, `name=` |
| `vpn=` | the tunnel: `off`, `connecting` or `on` (see [The VPN](#the-vpn)) |
| `vpn_file=`, `vpn_server=`, `vpn_reply_s=` | the tunnel's file (without `.conf`), the server's host and the seconds since data last came back; `unknown` without a tunnel |
| `vpn_kill_switch=` | `on` while the VPN gateway's kill switch is on (also with the tunnel down), else `off`. A separate `--print-data` run on the device does not read the firewall, so it says `off` there; `nft list table inet lokictl` shows the kill chains |
| `vpn_files=` | the WireGuard files to use, separated by commas; empty when there are none |
| `client=` | one line per connected client, in the page's order (none without clients): its hardware address, then `address=`, `signal_dbm=`, `rate_kbps=` (both ways together), `joined_s=` (seconds on the access point), `new=` (`yes` for the first five minutes) and, last because it may hold spaces, `name=` |

A value that is not known prints `unknown`. `network=`, `security=` and
`signal_dbm=` belong to a Wi-Fi network only, so they say `unknown` on a
cable and in the router sample. `--data offline` prints `mode=client` and
`link=none`, with `unknown` for every value from `interface=` to
`signal_dbm=` and for both rates; the page says `OFFLINE` for it. Lines for
later values are added at the end.

The detail lines are read as if the Status page were open. With `live` on
the device and a link, `--print-data` waits one second after
the first reading and takes one more, so the rates it prints are real (a
rate needs two readings of the counters). In a test folder it does not
wait.

### `LOKICTL_DATA_ROOT`

With `LOKICTL_DATA_ROOT=<folder>`, the `live` source reads
`<folder>/sys/...`, `<folder>/proc/...` and `<folder>/etc/resolv.conf` in
place of `/sys/...`, `/proc/...` and `/etc/resolv.conf`. That makes every
battery state and every kind of link testable on the Mac. It only matters
for `live`.

The system keeps its list of addresses in no file, so in a test folder the
system is not asked at all: `<folder>/addresses` stands in for it, one line
per address, `<interface> <IPv4 address>` or, for the Status page's prefix,
`<interface> <IPv4 address>/<prefix>`.

WireGuard files come from `<folder>/wireguard/` (see [The VPN](#the-vpn));
a VPN request there reads the file for real and then pretends.

NetworkManager is never asked in a test folder either: `<folder>/networkmanager`
stands in for it (see `read_network_manager_file()` in
`src/platform/network_manager.h`). Without the file NetworkManager counts as
not running, which leaves every value it adds unknown. For example:

```text
running yes
internet portal
link wlan0 WPA2 72 Harbour-Cafe
```

gives `network=Harbour-Cafe`, `security=WPA2`, `speed_mbps=72` and
`internet=portal` on a Wi-Fi uplink `wlan0`. Other lines: `networking on|off`,
`wifi on|off`, `connecting yes|no` and `managed <interface>` (a Wi-Fi
device NetworkManager manages; see [USB Wi-Fi adapters](#usb-wi-fi-adapters));
in a `link` line `-` means not known.
`ap wlan0 WPA2 LokiCTL-1A2B` says LokiCTL's hotspot runs on `wlan0`
(Router, with that name and security; `ap lokiap0 …` means Share).
`vpn on <file> <server>` (or `vpn connecting …`) says LokiCTL's tunnel
runs, and `kill-switch on` that the VPN gateway's kill switch is on (it
stands in for the firewall table), so `ap …` with `kill-switch on` and no
`vpn` line shows the gateway with its tunnel down. In a test folder a switch on the
Modes page only pretends (see [Switching modes](#switching-modes)), and
`fail permission` or `fail <reason>` makes it fail as not allowed or with
that reason.

While an `ap` line says the hotspot runs, three more files stand in for
the devices on it (see [Devices on the hotspot](#devices-on-the-hotspot)):
`<folder>/stations` for the Wi-Fi driver's list, one line per device,
`<interface> <hardware address> <signal|-> <seconds joined|-> <bytes|->`
(the seconds and the bytes may be lists, `1000,6000`: reading n takes the
n-th value, the last one past the end, so a rate or a moving time can be
tested); `<folder>/proc/net/arp`
for the neighbour table, as the kernel writes it; and `<folder>/hostnames`
for the names, `<IPv4 address> <name>` per line. Nothing is asked of a DNS
server then, and each reading counts as 5 s after the one before. For
example, with the lines `running yes` and `ap wlan0 WPA2 LokiCTL-TEST` in
`networkmanager`:

```text
stations          wlan0 02:5e:11:7a:c3:04 - 17 1000,6000
proc/net/arp      IP address  HW type  Flags  HW address         Mask  Device
                  10.42.0.183 0x1      0x2    02:5e:11:7a:c3:04  *     wlan0
hostnames         10.42.0.183 phone
```

`--print-data --data live --data-steps 1` prints `clients=1` and, at the
end, `client=02:5e:11:7a:c3:04 address=10.42.0.183 signal_dbm=unknown
rate_kbps=8 joined_s=17 new=yes name=phone`.

A small test folder with a charger and a battery (all made up):

```text
build/test-power/
  sys/class/power_supply/
    ac0/
      type        Mains
      online      1
    battery0/
      type        Battery
      capacity    72
      status      Discharging
```

Each name on the right is the content of a one-line file. Make it and read
it, from the repository root:

```sh
D=build/test-power/sys/class/power_supply
mkdir -p $D/ac0 $D/battery0
echo Mains > $D/ac0/type; echo 1 > $D/ac0/online
echo Battery > $D/battery0/type; echo 72 > $D/battery0/capacity; echo Discharging > $D/battery0/status
LOKICTL_DATA_ROOT=build/test-power ./build/darwin-arm64/Debug/lokictl --print-data --data live
```

It prints `battery_percent=72` and `battery_state=discharging`, and the
Mac's own time as `clock=`. Write `Charging` into `status` and a snapshot
with `--data live` shows `72%+`; delete `capacity` and it shows `--%`:

```sh
echo Charging > $D/battery0/status
LOKICTL_DATA_ROOT=build/test-power ./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-charging.png --data live
```

A link is added the same way. A made-up Wi-Fi link (the numbers in
`proc/net/route` are hexadecimal, the address bytes in reverse order:
`011E140A` is 10.20.30.1):

```text
build/test-power/
  addresses               wlan0 10.20.30.44
  proc/net/route          Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT
                          wlan0 00000000 011E140A 0003 0 0 600 00000000 0 0 0
  proc/net/wireless       (two header lines, then)
                          wlan0: 0000 53. -57. -256 0 0 0 0 0 0
  sys/class/net/wlan0/
    carrier               1
    type                  1
    wireless/             (an empty folder)
```

`--print-data --data live` then prints `link=wifi`, `interface=wlan0`,
`address=10.20.30.44`, `router=10.20.30.1` and `signal_dbm=-57`.

The traffic comes from `sys/class/net/<interface>/statistics/rx_bytes` and
`tx_bytes`. In a test folder the traffic is timed by the readings, not by
the clock, so the same folder always gives the same rates and graphs:

- every reading (the first one and each `--data-steps` step) reads the
  counters and counts as exactly one second after the one before;
- a counter file may hold several numbers, one per line: reading 1 takes
  the first line, reading 2 the second, and so on, and once the lines run
  out the last one again (a rate of 0).

For example, in the Wi-Fi folder above, with
`sys/class/net/wlan0/statistics/rx_bytes` holding the lines `0`, `1000` and
`313500` and `tx_bytes` holding `0`, `500` and `13000`:

```sh
LOKICTL_DATA_ROOT=build/test-power ./build/darwin-arm64/Debug/lokictl --print-data --data live --data-steps 2
```

prints `down_kbps=2500` and `up_kbps=100` (312500 bytes in a second are
2500 kbit/s), and the histories end in `...,0,100` and `...,4,100`. With
`--data-steps 0` both rates are `unknown`: one reading is not enough.

### On the device

```sh
scripts/device.sh data                    # the device's own values, live
scripts/device.sh data --data sample      # the sample lines
scripts/device.sh data --data wifi        # the Wi-Fi example's lines
scripts/device.sh run --data demo         # the demo on the real screen
scripts/device.sh run --data tour         # every situation on the real screen
scripts/device.sh snapshot build/shots/device-181.png --data demo --data-steps 181
scripts/device.sh snapshot build/shots/device-offline.png --data offline
```

`scripts/device.sh data` runs `lokictl --print-data` on the device and
prints its lines. It works while the launcher is on screen and leaves the
screen alone. See [development.md](development.md#run-on-a-device) for
the other commands.

## Log lines

In a normal run the data layer writes these lines:

- `data: <source> (first reading in <n> ms)` once at startup, before the
  first page, for example `data: live (first reading in 3 ms)`;
- `data: battery 91% discharging` for the first reading, and then only
  when the charge or the state changes. `data: battery --% discharging`
  when only the charge is unknown, and `data: battery unknown` when neither
  is known. There is no line per reading and none per minute;
- `data: link <none|wifi|cable>, access point <on|off>, <n> clients` for
  the first reading, for example `data: link cable, access point on, 3
  clients`, and then only when the kind of link, the access point or the
  number of clients changes. So the tour logs a new line at most stops, but
  none at stops 14 to 17, 19, 20 and 22, which change only values;
- `data: mode <client|router>` for the first reading and whenever the mode
  changes; `data: switching to Router`, `data: switch to Router done` and
  `data: switch to Router failed: <no cable|no Wi-Fi network|another mode
  runs|not allowed|reason>` (or `Share`) as a
  switch moves on. No network name and never the password;
- `data: back to Client before quitting` when the app stops while Router or
  Share is on or a switch runs (only where switches are real);
- `data: block a device (<n> blocked)` and `data: unblock a device (<n>
  blocked)` for each change of the list (never an address), and the
  failure's message, for example `data: Block failed: no permission`, as a
  warning;
- `data: usb wi-fi: none` or `data: usb wi-fi adapter wlan1 (ath9k_htc,
  0cf3:9271)` for the first reading, then `data: usb wi-fi adapter
  connected: wlan1 (ath9k_htc, 0cf3:9271)` and `data: usb wi-fi adapter
  removed: wlan1` as sticks come and go;
- `data: reader stopped` once on the way out.

A snapshot logs only warnings, so these lines do not appear there, and
`--print-data` writes nothing but its `name=value` lines.

## Stopping

On quit (holding ESC, closing the simulator window) and on SIGTERM or
SIGINT, the main loop ends. Right after it, before anything else is torn
down, `app::ToolController::stop()` stops a tool's run and ends its
thread, then `app::DataController::stop()` deletes its timer, wakes the reader and
waits for it to end, and the log says `data: reader stopped`. Before the
reader ends it switches back to Client if a mode is on (at most 3 s; see
[Switching modes](#switching-modes)). The reader
wakes at once, also from a long sleep, so the app never hangs on the way
out. Then the fonts are released and the log ends with `LokiCTL stopped`.
