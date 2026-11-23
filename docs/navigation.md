<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Navigation and keys

This page explains how LokiCTL moves from page to page and what each key
does: the page path, the ESC rule, the order in which a key is handled, the
arrow keys, typing mode and the help panel. It also shows how a page takes
part, how to add a page, and how to test keys by script with `--keys`.

## The page path

Pages open on top of each other, like on a phone, and ESC goes back one
page. The app remembers the path of open pages, for example:

```text
home -> settings
```

A snapshot run that starts with `--page demo-counter` (see
[Testing keys](#testing-keys-with---keys)) shows another path, with the two
demo pages:

```text
demo-counter -> demo-keys
```

- Only the page on top exists as a screen. The pages below it are just names
  in a list. Going back builds the page below again, from scratch. This
  keeps memory low, and it fits the theme switch, which also builds the
  current page again.
- The path holds at most 8 pages (`model::kMaxPageDepth`). Opening a ninth
  page does nothing.
- Opening a page that is already in the path does not add it a second time.
  The app goes back to it instead, and the pages above it are closed.
- The first page is never closed.

The path is kept in `model::BaseModel`. Pages and the rest of the app use it
through `viewmodel::BaseViewModel`:

| Function | What it does |
|---|---|
| `open_page(page)` | Opens `page` on top. If `page` is already further down the path, goes back to it. Returns false when nothing changed: the page is already on top, or the path is full. |
| `go_back()` | Closes the top page. Returns false on the first page. |
| `page_depth()` | How many pages are in the path: 1 on the first page. |
| `current_page()` | The page on top. |
| `show_page(page)` | Makes `page` the only page in the path. Used at startup and by snapshots. |

None of these builds a screen right away. They change the path and publish
the new top page in `current_page_subject()`. `app::ScreenManager` watches
that subject and builds the page a moment later, after the key or click
that asked for it has been handled. That is why a soft key can open a page
from inside its own action.

Every page change writes one line to the log:

```text
page: home (start, depth 1)
page: settings (opened, depth 2)
page: home (back, depth 1)
```

`start` is the first page after the app starts, `opened` means the path got
longer and `back` means it got shorter. A fourth word, `shown`, means a
different page at the same depth; only `show_page()` can cause that. A theme
switch builds the same page again and writes no `page:` line.

### Keep a page's state in the view model

A page's screen is thrown away when the user leaves the page and when the
theme changes. So everything a page shows must live in the view model, not
in the screen. The selected client of the home page lives there, and so
do the counter value and the selection, the picked row, the last key and
the typing switch of the key test page. When
the user comes back to the page, or the theme changes, the new screen reads
the same values and looks exactly as before.

The Settings page keeps its highlight in the view model too, so a theme
switch, which builds the page again, leaves the highlight where it was. But
the page is meant to start fresh each time the user opens it: the home
page's `SETUP` key calls `open_settings()`, which puts the highlight back on
the active theme and then opens the page. A page that should start fresh
does it this way, in the view model function that opens it, and not by
keeping the value in the screen.

A screen holds only its LVGL objects. It ties them to the view model's
subjects with observers that are bound to those objects
(`lv_subject_add_observer_obj()`, `lv_label_bind_text()`), so the observers
go away together with the screen.

## The ESC rule

A short ESC means "back". The app checks these cases in this order:

1. **The help panel is open:** ESC closes it and does nothing else.
2. **The page is in typing mode:** a short ESC ends typing. The page does
   this itself, in its `on_back()` (see below).
3. **A deeper page,** one with a page below it in the path: a short ESC goes
   back one page. Short means let go within 700 ms. The page changes when
   ESC is let go, not when it goes down.
4. **The first page,** with nothing below it: while ESC is held, the message
   `Hold ESC to exit` shows. Holding it for 700 ms quits the app. Letting go
   sooner only hides the message.

A long ESC has no meaning of its own inside the app. On a deeper page, or
while typing, an ESC held for 700 ms or longer does nothing, also not when
it is let go. Holding ESC belongs to the device's launcher, which closes the
app after 3 seconds. Quitting from the first page ends the same way, back on
the launcher's HOME screen, only faster.

## The order in which a key is handled

One class decides what every key does: `app::KeyController`, in
`src/app/key_controller.cpp`. The simulator, the device and snapshot runs
with `--keys` all use it, so a key does the same thing everywhere.
`src/platform/linux_input.cpp` only reads the keyboard and times long
presses; it does not decide what a key does.

When a key goes down, the controller goes through these steps in order. The
first step that takes the key ends it:

1. **fn+J** takes a screenshot.
2. **fn+H** opens or closes the help panel of the page that is shown.
3. **While the help panel is open,** ESC closes it and every other key is
   swallowed.
4. **ESC** follows the ESC rule above.
5. **Keys 4 to 8** press the page's soft keys (not while typing). An empty
   or switched-off key does nothing, and the page never gets the digit.
6. **F, X, Z and C** become up, down, left and right (not while typing).
7. **The page** gets the key, through its `on_key()`.

fn+H and fn+J work on every page, also while typing. Never give them
another job. Holding TAB is reserved as well; the app does not use TAB yet.

**A held key acts once.** The keyboard layer reports a key once when it
goes down, and once more if it is still held after 700 ms. Only a held ESC
acts on that second report. For every other key it is ignored, so holding
a soft key or an arrow does its job one time.
There is no key repeat yet: to move three rows down, press down three
times.

## Arrows

When the page is not in typing mode, four letters work as arrow keys:

| Key | Works as |
|---|---|
| `F` | up |
| `X` | down |
| `Z` | left |
| `C` | right |

They are the keys that give the arrows together with fn on the
CardputerZero's keyboard. Capital letters work too. The real arrow keys
(fn+F, fn+X, fn+Z, fn+C) always work, also in typing mode. `Enter`
confirms: on the key test page it picks the selected row.

The letters are turned into arrows by `platform::arrow_for_letter()` before
the page gets them. So a page never has to treat `f` as "up": it gets
`LV_KEY_UP`.

## Typing mode

A page says "the user is typing now" by returning true from
`text_entry()`. While it does:

- letters stay letters: `f` arrives as `f`, not as up;
- keys 4 to 8 arrive as the digits `4` to `8` and do not press the soft
  keys;
- the real arrow keys and `Enter` still work;
- a short ESC goes to the page's `on_back()`, which ends typing. Even on the
  first page ESC then shows no exit message, and holding it does not quit.

The controller asks the page at every key whether it is typing, so the
answer always comes from the page itself. A page in typing mode should draw
its soft keys switched off, because they cannot be pressed then; the key
test page does this.

On the device, letters arrive in lowercase for now: shift and sym are not
handled yet. The key test page shows how the switch works; the Hotspot
page uses typing mode to rename the hotspot.

## How a page takes part

A page is a class derived from `screen::BaseScreen`. It can override these
functions. Each one has a default that does nothing:

| Function | When it is called | What it returns |
|---|---|---|
| `soft_keys()` | once, while the page is built | the five soft keys 4 to 8 (see [Soft keys](themes.md#soft-keys)) |
| `on_key(key)` | a key for the page, after steps 1 to 6 | true when the page used the key |
| `on_back()` | a short ESC, on every page except the first one, and on the first one while typing | true when the page used the ESC itself, for example to end typing; false lets the app go back one page |
| `text_entry()` | at every key | true while the user is typing on this page |
| `help()` | when fn+H opens the help panel | a badge and at most 5 rows |

The key that `on_key()` gets is already translated: `LV_KEY_UP`,
`LV_KEY_DOWN`, `LV_KEY_LEFT`, `LV_KEY_RIGHT`, `LV_KEY_ENTER`,
`LV_KEY_BACKSPACE`, or a printable character such as `' '`, `'q'` or, in
typing mode, `'f'`.

The counter page (`src/view/screens/butter_screen.cpp`) uses three of the
functions. Keys 5 and 7 count down and up through its soft keys, and left
and right do the same through `on_key()`. So Z and C count as well, because
they arrive as left and right:

```cpp
view::widgets::SoftKeySet ButterScreen::soft_keys() {
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Less", [&view_model] { view_model.decrement_counter(); }},
        {},
        {"More", [&view_model] { view_model.increment_counter(); }},
        {"Keys", [&view_model] { view_model.open_page(model::AppPage::KeyTest); }},
    }};
}

// Left counts down and right counts up, like keys 5 and 7.
bool ButterScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_LEFT:
            view_model().decrement_counter();
            return true;
        case LV_KEY_RIGHT:
            view_model().increment_counter();
            return true;
        default:
            return false;
    }
}

view::widgets::HelpContent ButterScreen::help() const {
    return {"COUNTER",
            {
                {"ESC / 4", "Back"},
                {"Z / 5", "Decrease"},
                {"C / 7", "Increase"},
                {"8", "Key test"},
            }};
}
```

The key test page (`src/view/screens/key_test_screen.cpp`) adds typing
mode. The switch lives in the view model, and the page only reads and
changes it:

```cpp
// A short ESC ends typing; otherwise the app goes back one page.
bool KeyTestScreen::on_back() {
    if (!view_model().key_test_typing()) {
        return false;
    }
    view_model().key_test_set_typing(false);
    return true;
}

bool KeyTestScreen::text_entry() const {
    return view_model().key_test_typing();
}
```

The rules:

- **Only ask the view model.** These functions run inside an LVGL input
  callback, and soft-key actions inside an LVGL click event. They may change
  values in the view model or ask it for a page change, but they must never
  build or delete a screen themselves. The view model's page changes and
  theme switches are carried out later, after LVGL has finished the
  callback.
- **Read state from the view model,** not from members of the screen. The
  key test page asks `key_test_typing()` in `text_entry()` and in
  `soft_keys()`, because a theme switch can build the page again while the
  user is typing.
- **No focus groups and no button widgets.** Pages never use LVGL's focus
  groups (`lv_group_...`), and they do not create `lv_button`,
  `lv_textarea`, `lv_list` or similar widgets. Such widgets join the
  simulator's focus group by themselves and would get keys and a focus
  outline there, but not on the device, whose keyboard has no group. Build
  pages from plain objects and labels, and take keys only through
  `on_key()`.

## Help

fn+H opens the help panel. It shows the rows of the page that is open,
under the page's short name (the badge), for example `COUNTER`. ESC or
fn+H closes it, and the page underneath stays as it was. While the panel is
open, every other key is swallowed.

A page fills the panel with its `help()`. It returns a
`view::widgets::HelpContent`: a badge (empty for none) and a list of rows,
each with the keys and what they do. The panel shows at most 5 rows
(`view::widgets::kHelpRowMax`), because five rows is what fits the 170 px
screen in all three themes. A sixth row is left out, and a warning goes to
the log. Keep the key text short, such as `ESC / 4` or `Z / 5`: the key
column is as wide as the widest key text. The line at the bottom of the
panel is the same on every page.

The panel floats above the page, on LVGL's top layer, so snapshot pictures
never show it. The developer page `dev-help` draws the home page's help on
a page of its own, so its look can still be checked.

## The home page

`home` is the first page and the one the app starts on. It has no title
bar, and its picture depends on the theme; [home.md](home.md) describes
it. Its keys are the same in every theme:

| Key | What it does |
|---|---|
| `z`/`f` or left/up | selects the previous client; it stops at the first one |
| `c`/`x` or right/down | selects the next client; it stops at the last one shown |
| `6` SETUP | opens the Settings page |
| `8` INFO | opens the Status page |
| `4` MODES | opens the Modes page |
| `5` SCAN | opens the Discover page |
| `7` TOOLS | opens the Tools page |
| `Enter` | opens the Devices page, with the highlight on the selected client |
| ESC | the ESC rule for the first page: a short press shows `Hold ESC to exit`, holding it for 700 ms quits |

The selection lives in the view model, so it is kept when Settings is
opened and closed and when the theme changes.

## The Modes page

`modes` switches the device between its modes. It has a title bar, four
rows and a status line, and it is drawn the same way in every theme:

- one row per mode, Client, Router, Share and VPN, each with a short hint
  and a tick on the mode that is on (it comes from NetworkManager: an
  active `LokiCTL hotspot` profile means Router, or Share when it runs on
  `lokiap0`). VPN comes later and is drawn muted;
- the status line: `Switching…` while a switch runs; after one
  failed, `Plug in a cable first`, `Join a Wi-Fi network first` or `Back to
  Client first` (warning colour), `Not allowed: permission missing` or
  `Failed: <reason>` (error colour); while Router or Share runs, the
  hotspot's name and, at the right, its password; in Client the current
  link, muted.

| Key | What it does |
|---|---|
| `f`/`x` or up/down | moves the highlight; it stops at the first and the last row |
| `Enter` or `6` USE | switches to the highlighted mode; USE is dimmed on the mode that is on, on VPN, and while a switch runs |
| `7` QR | opens the Hotspot page, in any mode |
| `4` BACK or a short ESC | goes back |

The page only asks (`BaseViewModel::request_mode()`); the data layer does
the switch (see [data.md](data.md#switching-modes)), so the page never
waits, and it follows the values as they arrive. The highlight lives in the
view model and starts on the mode that is on each time the page is opened
from the home page. The help panel shows the badge `MODES` and four rows.

## The Hotspot page

`hotspot` shows how to join the hotspot and changes its name and password.
It has no title bar, so the code can be as big as the height allows:

- at the left, while Router or Share runs, a QR code (LVGL's own widget) that a
  phone camera scans to join: `WIFI:T:WPA;S:<name>;P:<password>;;`, with
  `\`, `;`, `,`, `:` and `"` escaped by a backslash. It is black on white
  with a white quiet zone in every theme, because phone cameras often fail
  on light-on-dark codes. While Router is off, and while a switch runs, a
  box says when the code shows instead;
- at the right, the heading with `on` or `off`, the name (the running
  hotspot's while Router runs, otherwise the one Router will use), the
  password, the channel and security, and a line with the state:
  `Applying…`, an error as on the Modes page, the question of key 6, or how
  long the hotspot has run.

| Key | What it does |
|---|---|
| `5` NAME | typing mode with the current name: characters are added (at most 32), `Backspace` takes one away, `Enter` saves a valid name (1 to 32 printable characters, no space at either end) or, on an empty name, puts the default name back; a short ESC cancels |
| `6` PASS | the first press asks (`Press 6 again for a new password`), the second makes a new random password; any other key cancels |
| `4` BACK or a short ESC | goes back (a short ESC first cancels typing or the question) |

NAME and PASS are dimmed while a switch runs, and every key is dimmed while
typing. The page only asks (`BaseViewModel::request_hotspot_rename()` and
`request_new_hotspot_password()`); `app::DataController` saves the change
and, while Router runs, starts the hotspot again (see
[data.md](data.md#switching-modes)). Typing and the name typed so far live
in the view model. The help panel shows the badge `HOTSPOT` and four rows.

## The Devices page

`devices` lists every device on the hotspot, two at a time under the title
bar. Each card has three lines: the name (or `unnamed`) with `new` for the
first five minutes and the signal at the right (left out when not known);
the address with the traffic; the hardware address with
`joined N min ago`. [data.md](data.md#devices-on-the-hotspot) says where
the values come from. Without the hotspot a box says `Hotspot is off`; with
the hotspot and nobody on it, `No devices yet` and the hotspot's name. A
thin mark at the right edge shows where the cards lie when there are more
than two.

| Key | What it does |
|---|---|
| `f`/`x`, `z`/`c`, the arrows, `5` UP, `6` DOWN | move the highlight; it stops at the first and the last device |
| `7` QR | opens the Hotspot page with the join code |
| `8` BLOCK / ALLOW | blocks the highlighted device, or lets a blocked one through again; off without devices |
| `4` BACK or a short ESC | goes back |

A blocked device says `blocked` at the right of its name (in the warning
colour unless highlighted), in place of `new` and the signal; key 8 then
reads ALLOW. See [data.md](data.md#blocking-a-device) for what blocking does.

The highlight lives in the view model (`devices_selection_*`); the home
page's Enter puts it on the selected client. The highlighted card is the top
one unless the list ends. Only the two cards exist as LVGL objects: new
values and the highlight retext and restyle them, nothing is built again.
The help panel shows the badge `DEVICES` and four rows.

## The Tools page

`tools` (home key 7) lists the tools in two columns: Ping, DNS and Trace on
the left, Wi-Fi, Ports and Speed on the right. All but Speed open a page
so far; Speed is muted with `coming`. Next to Ping, DNS, Trace and Ports a
tag tells how the latest run stands (`running`, the average time, `3
found`, `7 hops`, `not reached`, `3 open`).
The status line says what the highlighted tool does, with `SENT N` at its
right: every packet LokiCTL has sent in this run of the app (see
[data.md](data.md#the-tools)).

| Key | What it does |
|---|---|
| `f`/`x` or the arrows up and down | move through Ping, DNS, Trace, Wi-Fi and Ports |
| `6` OPEN or Enter | opens the highlighted tool |
| `4` BACK or a short ESC | goes back |

The highlight lives in the view model (`tools_selection_*`).

## The Ping, DNS and Trace pages

`ping`, `dns` and `trace` share one screen class (`screen::ToolScreen`).
Under the title bar: the target row (`TARGET`, `ROUTER` or `NAME`, the
target and `‹ 2/3 ›`), the results, and the status line with `SENT N`.
Ping shows its last five echoes as a table (`#6` over `12.4`, `lost`, `…`
while one waits) and two summary lines (sent, received and lost; min, avg
and max in ms). DNS shows up to six addresses, IPv4 first and several to a
line when they fit, and the server that answered with the time taken.
Trace shows one row per hop: its number, the address that answered, its
reverse name and the time (`12.4ms`); a hop that never answers shows `*`
and `no answer`, one still waiting `…`. As many rows as fit are shown
(four in every theme), with a thin scroll mark at the right when there are
more. The status line says `Hop 4…`, `Looking up names…`, then `Reached in
10 hops`, `Unreachable at hop 6` or `Not reached in 20 hops`.

| Key | What it does |
|---|---|
| `z`/`c` or the arrows left and right | step through the targets; off while this tool runs |
| `5` EDIT | types a target; off while this tool runs |
| `6` RUN / STOP or Enter | starts a run on the target, or stops it |
| `f`/`x` or the arrows up and down | Trace only: scroll the hops; while it runs and was not scrolled up, the list follows the newest hop |
| `4` BACK or a short ESC | goes back; a run goes on and the page shows it again later |

Targets: for Ping and Trace the router (when the network has one),
`9.9.9.9` and `1.1.1.1`; for DNS `example.com` and `wikipedia.org`; then
the typed target. Only one tool runs at a time: while one runs, the others
have RUN switched off and say which one is running (`Ping is running`).

Typing works like the Hotspot page's rename: a panel takes the page's
place, letters, digits and `.` are added (at most 63), Backspace takes one
away, Enter keeps a valid target and selects it (Enter on an empty text
drops the typed one), and a short ESC cancels. A valid target has no dot at
either end and no two in a row; digits and dots only must be an IPv4
address, which DNS refuses (it needs a name). The keyboard sends `.` as a
key of its own (`KEY_DOT`); `--keys` and the device kit call it `dot`.

The target choice, the typed target, the draft, the runs and the Trace
page's scroll (`trace_scroll_*`) live in the view model, so they survive
leaving the page and a theme switch. The help panel shows the badge
`PING`, `DNS` or `TRACE` and five rows.

## The Ports page

`ports` (the Tools page's Ports row) uses the same screen class and the
same target row, typing and status line as the Ping page. It checks one
device for open TCP ports from a fixed list of 50 common ones (see
[data.md](data.md#port-scan)) and shows the open ones with their services,
several to a line (`22 ssh   80 http   443 https`, `+2` beyond three
lines), then `3 open · 45 closed · 2 no answer`. The status line says
`Scanning 20 of 50`, `Scanned 50, 3 open`, `Scan stopped at 20 of 50` or
why it failed (`Not on the Cardputer's networks`). A finished scan shows
only while its target is the selected one; another target shows the page
as before any scan, so one device's ports never stand under another's
address.

Targets, stepped with `z`/`c`: the router (`ROUTER`), the Cardputer's own
uplink address (`THIS DEVICE`), the devices of the Discover list and the
hotspot's connected devices (their name, or `DEVICE`), then the typed one.
Only private IPv4 addresses are taken (10/8, 172.16/12, 192.168/16, and
link-local 169.254/16): the typing panel takes digits and `.` only, and
refuses anything else; names are never looked up.

| Key | What it does |
|---|---|
| `6` SCAN or Enter | first shows what will be asked (`Asks 50 common ports, 10 a second`) and, in the warning colour, `Only devices you own or may test. Press 6 again to scan.`; nothing is sent (`Nothing sent yet`). A second `6` or Enter scans that target; any other key, a short ESC, leaving the page, another tool starting, the target changing or 10 s cancels. While a scan runs it reads STOP |
| `z`/`c` or the arrows left and right | step through the targets |
| `5` EDIT | types an address |
| `4` BACK or a short ESC | goes back; a scan goes on |

The confirmation lives in the page only, so nothing remembers it. The help
panel shows the badge `PORTS` and five rows, one of them `6 twice`.

## The Wi-Fi page

`wifi` (the Tools page's Wi-Fi row) lists the networks the built-in Wi-Fi
(`wlan0`) hears, strongest first (see [data.md](data.md#the-wi-fi-list)).
Under the title bar: `WLAN0` with how many networks were found, then one
row per network: four signal bars (drawn as shapes, lit at nmcli's steps:
above 80, 55, 30 and 5 per cent), the name (`hidden` in the muted colour
for a hidden network), `ch N`, the security and the signal in per cent.
The network the Wi-Fi is joined to is in the accent colour. Four rows fit
in every theme, with the thin scroll mark of the Trace page when there are
more. The status line says `Last scan 14:32`, `Scanning…` or what went
wrong (`Scan not allowed: permission missing`, `No scan result in 15 s`,
`Scan refused: …`), with `SENT N` at its right; a scan is sent by the
network service, so it is not counted. In Router and Share there is no
list: a note and `Not while the hotspot runs`; likewise `Wi-Fi is off` and
`No network service`.

| Key | What it does |
|---|---|
| `6` SCAN or Enter | asks for a fresh scan; off while one runs or while there is no list |
| `f`/`x` or the arrows up and down | scroll the networks |
| `4` BACK or a short ESC | goes back |

Opening the page asks for nothing: it shows the list the network service
already has. The first row shown lives in the view model
(`wifi_scroll_*`, back to the top each time the page opens). The help panel
shows the badge `WI-FI` and five rows. Snapshots: the default `sample`
source runs Router, so `--page wifi` shows the refusal; `--data wifi`
shows the made-up list, `--keys 6` a scan under way and `--keys 6,data`
its end.

## The Discover page

`discover` (the home page's key 5) is step 1 of discovery, Listen: it
lists the devices heard on the network the Cardputer is on (the uplink
interface) from their ARP, DHCP, mDNS and SSDP traffic, and sends nothing
(see [data.md](data.md#listening)). Under the title bar: the interface with how
many devices were heard and `STEP 1 OF 3: LISTEN` at the right, then two
lines per device: its name with its IPv4 address (`no address` while none
was heard), then its hardware address, its kind when known (`printer`,
`Google Cast`, `media player`, from mDNS or SSDP) and how long ago it was
last heard (`now`, `12 s ago`, `3 min ago`). The name is the host name it
gave over DHCP or else mDNS, `router` for the uplink's router when it gave none, `unnamed` in the
muted colour, or `this device, eth0` in the accent colour for the
Cardputer's own ports (both of them can be heard when they are on the same
network). Two devices fit in every theme, with the thin scroll mark of the
Trace page when there are more. In place of an empty list a note says why:
no network, not listened yet, listening, nothing heard, or a helper
problem. The status line says `Listening, 4:12 left`, `Listened 5:00, 40
frames`, `Stopped after 1:23`, `Join a network first` or what went wrong
(`Capture not allowed`, `Capture helper not installed`), with `SENT N` at
its right, which does not move while listening. Saving takes the status
line over: `Saving, 4:12 left, 14 KB` while the run lasts, then `Saved
20261123-075130.pcap` (`, full` when the run heard more than 4 MB) or
`Save failed: disk full` (see the README's "Saved captures").

| Key | What it does |
|---|---|
| `6` START / STOP | listens for 5 minutes on the uplink interface, or stops; off without a network and while another tool runs |
| `7` SAVE | saves the latest run as a PCAP file: on while one runs and after one that heard something, until it is saved; off while another tool runs |
| `8` PROBE / STOP | first shows step 2, `STEP 2 OF 3: YOUR ADDRESS`: the Cardputer's IPv4 address and router, what a probe asks (`Asks 10.0.0.1-254, 20 a second`) and, in the warning colour, `Only networks you own or may test. Press 8 again to probe.`; nothing is sent (`Nothing sent yet`). A second `8` starts step 3, Probe (see [data.md](data.md#probing)); any other key, a short ESC, leaving the page or 10 s cancels. While a probe runs it reads STOP. Off without a network and while another tool runs; on a network that is not private, step 2 says so and the second `8` does nothing |
| `f`/`x` or the arrows up and down | scroll the devices |
| `4` BACK or a short ESC | goes back; a run goes on |

After a probe the caption reads `STEP 3 OF 3: PROBE`, the list shows the
devices of the latest listen run and the latest probe run together (one
entry per hardware address), and the status line says `Probing 120 of
253`, `Asking for names`, `Probed 253, 9 replied`, `Probe stopped at 120
of 253` or what went wrong; the Sent count grows with every echo and
question.

Listening and probing are tools like Ping (`model::ToolKind::Listen` and
`Probe`), so only one of them runs at a time: the tool pages say
`Discover is listening` or `Discover is probing`, and the Discover page
says which tool runs. The first device shown lives in the
view model (`discover_scroll_*`, back to the top each time the page opens
or a run starts). The help panel shows the badge `DISCOVER` and five rows.
Snapshots: `--page discover` shows the page before a run, `--keys 6
--settle-ms 1600` a run under way with the first made-up devices and
`--settle-ms 12000` all of them; `--keys 6,down,down,down --settle-ms 3000`
the printer and the TV with their kinds; `--keys 6,7 --settle-ms 4000`
saving and `--keys 6,7,6 --settle-ms 2000` saved (the pretend runner
writes no file); `--data offline` shows `Join a network first`; `--keys 8`
step 2, `--keys 8,8 --settle-ms 5000` a probe under way, and `--keys
6,6,8,8,data,data --settle-ms 9000` a finished probe merged with a short
listen run (add `down` keys to see the quiet file server and camera).

## The Status page

`status` lists every detail the home page has no room for: each network
port (Wi-Fi and cable) with its state, network name, security, signal,
speed, IPv4 and IPv6 addresses, router and hardware address, then the DNS
servers, the battery's details and the device (processor temperature,
free memory, time since start). [data.md](data.md#the-status-pages-details)
says where the values come from. It has a title bar and shows six rows at a
time; a thin mark at the right edge shows where they lie in the whole list.

| Key | What it does |
|---|---|
| `f`/`x` or up/down | scrolls one row; it stops at the first and the last row |
| `z`/`c` or left/right, `5` UP, `6` DOWN | jumps to the previous or next heading (a port, `dns`, `battery`, `device`) |
| `4` BACK or a short ESC | goes back to the home page |

The first row shown lives in the view model, so it is kept when the theme
changes; the home page's key 8 always opens the page at the top. Only the
six rows on screen exist as LVGL objects: scrolling and new values retext
them, nothing is built again.

## The demo pages

Two demo pages are kept for navigation and key tests. Nothing in the app
opens them, so, like the developer pages, they are drawn by name only:
start a snapshot run with `--page demo-counter` (see
[Testing keys](#testing-keys-with---keys)). The counter page's key 8 then
opens the key test page.

| Page | Keys |
|---|---|
| `demo-counter` | as the first page of a snapshot run, `4` BACK has nothing to go back to, and ESC follows the ESC rule for the first page; `5` LESS (or `z` or left) counts down, `7` MORE (or `c` or right) counts up, `8` KEYS opens the key test page |
| `demo-keys` | `4` BACK, `5` TYPE turns typing mode on, `f`/`x` or up/down move the selection and stop at the first and last row, `Enter` picks the selected row; a box shows the last key, the picked row and whether typing is on |

## The Settings page

`settings` is the Settings page, which key 6 (`SETUP`) on the home page
opens. It holds the theme picker (one row per theme, with the theme's
name, four small squares in its colours and a tick on the theme that is
active) and, below it, the row "Dim when idle" with `on` or `off` (see
[Battery care](data.md#battery-care)).

| Key | What it does |
|---|---|
| `f`/`x` or up/down | move the highlight; it stops at the first and the last row |
| `Enter` or `6` USE | switches to the highlighted theme and saves it; `USE` is switched off while the highlight is on the active theme, and picking that theme does nothing |
| `Enter` or `6` OFF / ON | on the "Dim when idle" row: switches dimming off or on and saves it |
| `4` BACK or a short ESC | goes back to the home page |

Left and right and the keys 5, 7 and 8 do nothing here. The highlight starts
on the active theme each time the page is opened, and a theme switch leaves
it where it was (see
[Keep a page's state in the view model](#keep-a-pages-state-in-the-view-model)).

The pick travels the same way as every other action: the page only asks the
view model, with `request_theme(row)`, and `app::ThemeController` switches
a moment later, after the key has been handled. The switch builds the
Settings page again in the new theme, so the page never deletes itself.
[Switching while the app runs](themes.md#switching-while-the-app-runs) in
themes.md describes each step.

## Adding a page

1. Add a value to `enum class AppPage` in `src/model/base_model.h`, before
   the developer pages.
2. Add a line such as `{"demo-keys", model::AppPage::KeyTest}` to the list
   in `src/app/page_registry.cpp`. The name is what `--page`,
   `--list-pages` and the `page:` log line use. Names that start with
   `dev-` are developer pages, which nothing in the app opens. The two
   `demo-` pages are not opened by anything in the app either; they are
   kept for tests.
3. Write the screen class in `src/view/screens/`, a `.h` and a `.cpp` file.
   The build finds new files by itself. Derive it from `screen::BaseScreen`,
   call `init()` in the constructor, build the page in `build_content()`,
   and override `soft_keys()`, `on_key()`, `on_back()`, `text_entry()` and
   `help()` where the page needs them. A static `handles(model::AppPage)`,
   as in `KeyTestScreen`, keeps the next step short.
4. In `src/app/screen_manager.h` and `src/app/screen_namager.cpp`, add a
   `show_..._screen()` function like `show_key_test_screen()`, and call it
   from `ScreenManager::show_page()`.
5. Put everything the page shows in `viewmodel::BaseViewModel`, as
   subjects, so it is kept after going back and after a theme switch.
6. Give the page its help rows, at most 5, in `help()`.
7. Open it from another page with `open_page()`, for example from a soft
   key: `view_model.open_page(model::AppPage::KeyTest)`. A page that starts
   fresh each time gets its own function that resets its values first, like
   `open_settings()`.
8. Add the page to the page list in `README.md`. Then take snapshots of it
   in all three themes, with `--keys` and with `--switch-to`, and look at
   each picture.

## Testing keys with `--keys`

`--keys` presses keys in a snapshot run. That way key behaviour can be
checked by a script, on the Mac and on the device, without anyone at the
keyboard. From the repository root, after building:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
$B --snapshot build/shots/keys-x.png --page demo-counter --keys 8,x
$B --snapshot build/shots/keys-down.png --page demo-counter --keys 8,down
cmp build/shots/keys-x.png build/shots/keys-down.png && echo same
```

Both pictures show the key test page with the second row selected, so `cmp`
prints `same`. In a snapshot every page, and the title bar of the demo
pages, shows the fixed sample values unless `--data` says otherwise, so the
pictures never differ because the clock moved on.

How it works:

- The keys are pressed one after another, after the page has been drawn and
  before any `--switch-to`. Each one goes through the same path as a real
  key press (`platform::inject_key()` and `app::KeyController`).
- Names: `0` to `9`, `a` to `z`, `esc`, `enter`, `space`, `backspace`, `up`,
  `down`, `left`, `right` and `help` (fn+H). Capitals are accepted, but a
  letter always arrives in lowercase, as the device's keyboard sends it.
- The name `data` is not a key: it takes one further reading of the
  values at that point, the way the data layer hands over new values in a
  normal run. For example `--page home --data demo --keys 6,data,data,esc`
  lets two readings arrive while Settings is open, and the home page shows
  them when it comes back. It has no `:long`, and like the keys it takes
  its turn after any `--data-steps`. [data.md](data.md#test-hooks)
  explains the data layer's test options.
- A key is held for 60 ms of LVGL time. Add `:long` to hold it for 1 second
  instead, for example `esc:long`. That is longer than the 700 ms long
  press.
- After every key the app runs for `--settle-ms` (300 ms by default), so a
  page change has happened before the next key. The program does not
  really wait that long.
- The page given with `--page` is the first page of the path, with nothing
  below it. To test going back, start one page lower: at `home` for the
  Settings page (`--page home --keys 6,esc`) and at `demo-counter` for the
  key test page (`--page demo-counter --keys 8,esc`). With
  `--page demo-keys --keys esc` the app stays on the key test page,
  because there it is the first page.
- A theme picked on the Settings page, for example with
  `--page home --keys 6,x,enter`, really switches the theme. As in every
  snapshot, nothing is saved.
- If the keys made the app ask to quit, for example with a held ESC on the
  first page, the line `quit requested` goes to standard error. The picture
  is still taken and the exit code stays 0.
- The picture shows the page only. The help panel and the messages, such as
  `Hold ESC to exit`, float above the page and are not in it. So
  `--page home --keys c,help` gives the same picture as
  `--page home --keys c`.
- `--keys` only works together with `--snapshot`. Without it, or with an
  unknown name, an empty name or an unknown suffix, the app stops with exit
  code 2 and a message.

More examples:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
# The key test page in typing mode, after typing f, in the Terminal theme.
$B --snapshot build/shots/keys-typing.png --page demo-counter --keys 8,5,f --theme terminal
# A held ESC on the first page asks to quit: standard error says "quit requested".
$B --snapshot build/shots/keys-quit.png --keys esc:long
# Pick the second row, then switch to Instrument: the page keeps its state.
$B --snapshot build/shots/keys-switched.png --page demo-counter --keys 8,x,enter --switch-to instrument
# Select the second client on the home page, then switch to Terminal: the
# selection stays.
$B --snapshot build/shots/home-switched.png --page home --keys c --switch-to terminal
# From the default theme, Constellation: open Settings, move the highlight to
# Terminal and pick it. The Settings page is now drawn in Terminal.
$B --snapshot build/shots/keys-settings.png --page home --keys 6,x,enter
```

### On the device

`scripts/device.sh snapshot` passes `--keys` on to LokiCTL on the device.
The keys are pressed inside that snapshot run, not on the device's
keyboard, so LokiCTL does not need to be running and the screen does not
change:

```sh
scripts/device.sh snapshot build/shots/device-keys.png --page demo-counter --keys 8,x
```

To press keys on the real keyboard while LokiCTL runs on the screen, use
`scripts/device.sh key` (see the README). Its names differ a little: it
also knows `tab`, `screenshot` and the `fn+` combinations, it holds a key
with `--long` instead of `:long`, and it does not know `data`, which is not
a key.

```sh
scripts/device.sh run
scripts/device.sh key c
scripts/device.sh key 6
scripts/device.sh key x
scripts/device.sh screen build/shots/device-settings-live.png
scripts/device.sh logs -n 20
scripts/device.sh stop
```

`c` selects the second client on the home page, and `6` opens Settings.
The log then shows `page: home (start, depth 1)` and
`page: settings (opened, depth 2)`, and the saved picture shows the
Settings page with the highlight moved one row down (it stops on the
last row). `x` only moves the highlight, so no theme is picked. An `esc`
before `stop` would bring back the home page with the second client still
selected.
