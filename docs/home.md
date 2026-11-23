<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# The home page

The home page, `home`, is the first page of the app and the one it starts
on. It has no title bar: the whole area above the soft-key bar, 320 x 140
px, belongs to the page, and the page shows the clock and the battery
itself. Every other page keeps its title bar.

The page looks different in each theme. Keys, soft keys and help are the
same in all three; only the picture changes. This page explains what each
theme shows, where the values come from, when the page changes a value in
place and when it is built again, how the page is laid out on such a small
screen, and how to add a layout for a new theme.

## What each theme shows

Every layout has two views:

- **The router view**, while the device runs its own access point
  (`HomeData::ap_active`). It shows the active network mode, the uplink,
  the access point, the traffic and the connected clients, with one client
  marked as selected.
- **The link view**, without an access point. That is the device as it
  usually is: joined to a Wi-Fi network, on a network cable, or with no
  network at all. Without an access point there are no connected clients,
  so there is no client list; that space shows the link's own details
  instead: the network, the device's address and the router's address.

Inside the link view, the kind of link decides the words and which values
are there:

- **Wi-Fi:** the network's name and security, the signal in dBm and the
  link speed. When the name is not known the page says `wi-fi network`.
- **Cable:** `wired network` and the link speed. A cable has no name,
  security or signal.
- **No network:** the page says `OFFLINE` where the mode word stands and
  `no network` where the network is named, in the muted colour. It is a
  calm state, not an alarm. The mode is still Client, so Instrument's
  `CLIENT` chip stays filled.

The words are the same in every theme: `wi-fi` and `cable` for the kind of
link, `wi-fi network`, `wired network` and `no network` for the network.

To see every view in every theme, take snapshots with the example sets
(see [Sample data](#sample-data) and
[Looking at the page](#looking-at-the-page)).

### Instrument: the gauge

Drawn by `src/view/screens/home_instrument.cpp`.

The router view:

- A status strip along the top with a thin line under it: the app name on
  the left, the sample mark in the middle, clock and battery on the right.
- The caption `MODE` and under it the mode in big type (`ROUTER`), with a
  line of detail on what the mode does.
- Right of an upright line, three large numbers with a caption each, on
  one baseline: the number of clients, the download rate and the upload
  rate.
- One chip per mode (`CLIENT`, `ROUTER`, `SHARE`, `VPN`); the active one is
  filled with the accent colour.
- The access point with its security, and the time the mode has been up.
- The selected client: its position in a filled tag (`1/3`), its name, its
  address and its signal.

The link view keeps the status strip, the caption `MODE`, the upright
line, the places of the three numbers and the four chips:

- The big word is `CLIENT`, or `OFFLINE` in the muted colour when there is
  no network.
- The line under it names the network: `Harbour-Cafe · WPA2` (the name,
  then the security when it is known), `wi-fi network`, `wired network` or
  `no network`. A long name ends in dots.
- The first number is the signal on Wi-Fi, `−57` with the caption `DBM`,
  and the link speed on a cable and without a network, `100` with the
  caption `MBIT`. The second and third stay download and upload. A number
  that is not known reads `--`.
- Where the access point line is in the router view: `LINK wlan0 · wi-fi`
  or `LINK eth0 · cable` (`LINK --` without a network), and on Wi-Fi the
  link speed in words at the right end, `72 Mbit/s`. On a cable the first
  number already shows the speed, so the right end stays empty.
- Where the client line is: `IP` and the device's address, then `ROUTER`
  and the router's address, both addresses in the address colour.

### Constellation: the map

Drawn by `src/view/screens/home_constellation.cpp`.

The router view:

- The map, on the left: the internet as a small square at the top, joined
  by a lime line to this device, a lime ring with a dot. The uplink's name
  and speed sit beside the line and the access point beside the device.
  Two faint rings surround the device. The clients sit below it, each a
  light dot with its name under it and a thin violet line to the device. A
  few dim dots are stars, for decoration.
- The selected client gets lime brackets around its dot, and its name is
  set in the bold font.
- The panel, on the right behind a thin upright line: clock and battery,
  the mode in lime, the traffic, the number of clients, the uptime, and
  under a rule the selected client's position, name, signal and address.
- The sample mark sits in the top left corner of the map.

The link view:

- The map is a chain down the middle: the internet square with its
  caption, a thin muted line down to the router (a small ring with the
  caption `ROUTER`), then the uplink, a lime line down to this device (the
  lime ring with its dot and the caption `THIS DEVICE`). The link's
  interface and kind, `wlan0 · wi-fi` or `eth0 · cable`, sit left of the
  uplink line. Two faint rings surround the device; the outer one is
  smaller than in the router view, so it stays clear of the text below. A
  few stars as before.
- Under the map, centred: the network's name in the bold font (the Wi-Fi
  name, `wi-fi network` when it is not known, `wired network` on a cable;
  a long name ends in dots), and under it in the muted colour the security
  and the link speed, `WPA2 · 72 Mbit/s` or `100 Mbit/s`. A part that is
  not known is left out.
- With no network, only the internet square and this device are left: no
  router and no lines, the device's ring and dot in the muted colour, and
  `NO NETWORK` in large type under it.
- The panel shows the clock and battery, the caption `MODE` and the mode
  word (`CLIENT` in lime, `OFFLINE` in the muted colour), the traffic
  (`↓2.4 ↑0.3 Mb/s`, `↓-- ↑-- Mb/s` when not known), and on Wi-Fi the
  signal (`signal −57 dBm`), on a cable and without a network the link
  speed (`link 100M`). Under the rule: the caption `IP` with the device's
  address and the caption `ROUTER` with the router's address, both in
  violet.
- No client dots, no brackets, no client count, no uptime and no selected
  client. The sample mark stays in the top left corner.

### Terminal: the panes

Drawn by `src/view/screens/home_terminal.cpp`. Like every Terminal pane,
each title sits in the top border line. Both views keep the three panes at
the same places and sizes.

The router view:

- `net`, top left: a download row and an upload row, each a bar graph of
  the recent rates with the current rate at the right end (cyan for down,
  magenta for up), then the uplink with its address and link speed. The
  clock sits in the pane's border.
- `mode`, top right: the active mode in amber with a dot after it, the
  access point and the uptime. The battery sits in the border.
- `clients`, along the bottom: a header row (`address`, `name`, `signal`,
  `rate`) and one row per client, with `new` on a client that has just
  joined. The selected row is filled magenta with black text. The sample
  mark sits in the border.

The link view:

- `net`: the same rows. A rate that is not known reads `--`, and a graph
  without traffic shows only its 1 px floor. With no network the third row
  is the muted text `no network` in place of the uplink.
- `mode`: `client` in amber with the dot (with no network `offline` in the
  muted colour, without the dot), then `ap off`, then `on wi-fi`,
  `on cable` or `on --`.
- The bottom pane is titled `link` (with no network: `no network`) and
  keeps the `sample data` mark. It lists labels and values in two columns:
  - Wi-Fi: `network` with the whole name (all 32 characters fit),
    `signal` and `auth`, `address` and `speed`, `router`;
  - cable: `network wired`, `address` and `speed`, `router`;
  - no network: `network`, `address` and `router`, each `--`.

## Sample data

When the app reads live values, which it does on the device (the data
layer decides this; see [data.md](data.md)), every value is real: the
clock, the battery, the network link (Wi-Fi, cable or no network, the
interface, the address, the router's address, the signal and the cable's
speed) and the traffic, so the page shows no mark. Every other source is
made up, and the page says so with a small `sample data` mark in every
theme. Instrument and Constellation write it in capitals, like all their
captions; Terminal writes it in lowercase. The mark matters on a network
tool: nobody should take a made-up rate for a real reading.

Next to the router sample there are made-up example sets for the other
situations, so every layout can be looked at in each of them before real
network values arrive. Each is a data source of its own (`--data <name>`):

| Source | What the page shows |
|---|---|
| `--data sample` | the router sample: a router with its own access point and three clients (the router view) |
| `--data wifi` | joined to the Wi-Fi network `Harbour-Cafe`, `WPA2`, −57 dBm, 72 Mbit/s, address `10.20.30.44`, router `10.20.30.1` |
| `--data cable` | on a cable: `eth0`, 100 Mbit/s, address `10.0.0.12`, router `10.0.0.1` |
| `--data offline` | no network at all: `OFFLINE`, every network value not known |
| `--data tour` | walks through all of these and more, one stop at a time (see [The tour](data.md#the-tour)) |

All of them are marked `sample data`, show the clock `07:46` and the
battery `91%`, and never change, except the tour. All the names and
addresses are made up. `--data live`, which the device uses by default,
shows the device's real link and traffic in the link view, with the Wi-Fi
network's name and state from NetworkManager on the device (in the
simulator the name reads `wi-fi network`), without the mark. A note
(`no internet`, `login page`) and the words without a link (`connecting`,
`wi-fi off`, `network off`) are described in [docs/data.md](data.md).

A snapshot uses the fixed sample values unless `--data` says otherwise, so
the same state always gives the same picture. Two snapshots of the home
page are the same file, byte for byte, which makes the page easy to check
by script. The title bar of the other pages takes its values from the same
data, so in a snapshot Settings shows the same clock and battery as the
home page.

## Where the values come from

- `model::HomeData` in `src/model/home_data.h` holds everything the page
  shows. Numbers are stored as numbers, in the unit written next to each
  field: the clock in minutes since midnight, the uptime in seconds, rates
  in kbit/s, the signal in dBm, the link speed in Mbit/s, the battery in
  per cent. The battery's state is a `model::BatteryState`: unknown,
  discharging, charging or idle. Lists, such as the clients and the two
  traffic histories, are plain C++ containers. Each history value is a
  share (0 to 100) of that history's own recent peak, not of a fixed rate,
  so the quieter upload graph fills its height as well as the download
  graph does; the current rates give the real numbers. The live source
  never scales to less than 100 kbit/s, so a quiet link does not fill its
  graph (see [The traffic](data.md#the-traffic)).
- The fields for the link and the access point:

  | Field | What it holds |
  |---|---|
  | `uplink_kind` | the kind of link, a `model::LinkKind`: `None`, `Wifi` or `Cable` |
  | `uplink_name`, `uplink_address` | the uplink's interface (`wlan0`) and the device's address on it |
  | `gateway_address` | the address of the router the uplink leads to |
  | `uplink_speed_mbps` | the link speed |
  | `wifi_name`, `wifi_security`, `wifi_signal_dbm` | the Wi-Fi network's name, its security (`WPA2`) and the received signal; empty on a cable and without a network |
  | `internet` | whether the internet answers (`model::Internet`: full, limited, portal, none, unknown); only with a link; a problem shows as a note |
  | `offline_state` | why there is no link (`model::OfflineState`: connecting, Wi-Fi off, network off, unknown); only without a link; it picks the words in place of `no network` |
  | `ap_active` | true while the device runs its own access point; it decides between the router view and the link view |
  | `ap_name`, `ap_security`, `uptime_seconds`, `clients` | the access point, the time the mode has been up and the connected clients; they only mean something while `ap_active` is true |

- A value that is not known has a value of its own, never 0 and never an
  old number: an empty text for names and addresses, `kSignalUnknown` (0)
  for the signal (a received signal is always below 0), `kSpeedUnknown` (0)
  for the link speed, `kRateUnknown` (-1, or any negative value) for a
  rate, and `kBatteryPercentUnknown` (-1) for the battery. The page shows
  such a value as `--`, or leaves it out where a line is made of several
  parts, so a line never starts or ends with a stray `·`.
- `model::sample_home_data()` in `src/model/home_data.cpp` fills the block
  with the router sample and sets `HomeData::sample` to true. The other
  made-up situations are in `src/model/home_examples.h` and `.cpp`:
  `wifi_home_data()`, `cable_home_data()`, `offline_home_data()` and
  `home_tour_stop()`. These two files are the only places in the app that
  write made-up values; nothing in `src/view` or `src/app` writes or asks
  for one.
- A data source fills in one whole `HomeData` block: `sample`, `demo` (the
  sample, moving on a fixed script), `live` (the real clock, battery, link
  and traffic, in Client, Router or Share), `wifi`, `cable`,
  `offline` or `tour`. A background
  reader asks the source and `app::DataController` hands every new block to
  `viewmodel::BaseViewModel::set_home_data()`. [data.md](data.md) explains
  the whole path.
- `model::BaseModel` owns one `HomeData`, and
  `viewmodel::BaseViewModel::home()` hands it to the page.
- A layout turns the numbers into text with the helpers in
  `src/view/screens/home_format.h`. All layouts use the same helpers, so a
  value reads the same in every theme, and a value out of range is clamped
  instead of printed as garbage:

  | Helper | Gives |
  |---|---|
  | `clock_text()` | `07:46` |
  | `battery_text()` | `91%`, `91%+` while the battery charges, `--%` when the charge is not known |
  | `uptime_text()` | `00:12:31` |
  | `rate_text()`, `rate_short_text()` | `3.8` and `3.8M` for 3800 kbit/s; whole Mbit/s from 100 Mbit/s on (`120`, `120M`); `--` when not known |
  | `signal_text()`, `signal_number_text()` | `−61 dBm` and `−61`, with a real minus sign; `--` when not known |
  | `speed_text()`, `speed_long_text()`, `speed_number_text()` | `100M`, `100 Mbit/s` and `100`; `--` when not known |
  | `speed_part_text()` | `100 Mbit/s`, or an empty text when not known, for a line made of parts |
  | `text_or_unknown()` | a name or an address as it is, `--` when it is empty |
  | `network_text()` | the Wi-Fi name, `wi-fi network`, `wired network` or `no network` |
  | `network_name_text()` | the Wi-Fi name (`--` when not known), `wired`, or `--` without a network |
  | `link_kind_text()`, `link_text()` | `wi-fi` or `cable`; `wlan0 · wi-fi`, with `--` when nothing is known |
  | `is_offline()`, `mode_word()` | true with no link and no access point; the mode's name, or `Offline` then |
  | `join_parts()` | the parts that are not empty, joined by ` · ` |
  | `count_text()` | `1/3` |

Real values replaced the made-up ones in the same place: the live source
fills the same `HomeData`, and sets `sample` to false, which removes the
mark. The layouts take every value from `HomeData`, so none of them had to
change for that.

## Values in place, a new situation built again

The page is built from the values once. When a new block arrives while it
is open, `screen::HomeScreen` decides, in one place, whether the layout
can show it in place or whether the page's content is built again.

**Changed in place**, by the layout's `update()`, in both views: the
clock, the battery (charge and state), the uplink's interface, the
device's address, the router's address, the link speed, the Wi-Fi
network's name, security and signal, the router view's uptime, the two
rates with their graphs, and the clients' signal and traffic while the same
devices are listed in the same order (`same_clients()`: same hardware
address, name, address and `new` mark), shown in Instrument's and
Constellation's selected-client line and Terminal's rows (and the Status
page's details and the switch between modes, which the home page does not
show).
These are values that change while the situation stays the same, and each
sits in a box of its own, so a new text moves nothing else. The
exceptions: Instrument's captions follow their numbers, Terminal's bars
change height, and in Constellation's router view the star right of the
uplink's text is hidden while a wide text (such as `eth0 · 1000M`) would
come too close to it.

**Built again** for any other change: the kind of link, the access point
switched on or off, the mode, another list of clients (a device joins or
leaves, gets its address or name, or stops being new), the access point's
name, the sample mark, and any field added to `HomeData`
later. Such a change can need other objects, so the
content is built again, once.

How it works:

- `home_values_only(shown, next)` in `src/view/screens/home_layout.h`
  makes the decision. It copies `shown`, the block the layout shows, takes
  over exactly the in-place values from `next` and compares the copy with
  `next`. When they are equal, only in-place values changed. A field it
  does not know is never taken over, so a new field means "built again"
  by itself, and no value can ever go stale on the page.
- `HomeScreen` keeps a copy of the block the layout shows. When only
  in-place values changed, it calls `update()`. Otherwise it throws the
  layout away (its pointers go with it), empties the content area with
  `lv_obj_clean()`, makes a new layout for the theme, lets it `build()`
  the page with the new values, and marks the current selection with
  `select()`.
- Only the content area is emptied. The soft-key bar belongs to the page's
  root, the help panel and messages live on LVGL's top layer, and both of
  the page's observers are bound to the content area itself, which stays.
  A layout adds no observers of its own. The screen manager is not asked:
  it must not be called from an observer.
- On the device a new situation costs one frame of roughly 20 to 40 ms,
  depending on the theme. It happens once per new situation; a value that
  changes inside a situation costs one label's text and nothing more.

When the page is built again for another reason, after a theme switch or
when coming back from Settings, it shows the current values at once.

## The selected client

In the router view one client is selected, and the arrow keys move the
selection. It lives in the view model, not in the page:

| Function | What it does |
|---|---|
| `home_selection_subject()` | the index of the selected client in `home().clients`, starting at 0 |
| `home_selection()` | the same index as a number |
| `home_select(delta)` | moves the selection by `delta`; it stops at the first and the last client shown and does not wrap around |

The page shows at most three clients for now
(`viewmodel::kHomeClientRows`), so the selection never goes past the
third. The client count on the page still shows how many there are.

Without clients, in the link view or for a router that has none, the arrow
keys do nothing, and a layout's `select()` does nothing in the link view.

Because the selection lives in the view model, it is kept when Settings is
opened and closed, and across a theme switch, which builds the page again
in the new theme's layout. A value that changes in place keeps the
selection as it is. When new values bring fewer clients than the
selection's place, `set_home_data()` pulls the selection back to the last
client shown; a changed list of clients builds the page again, which then
marks the pulled-back client.

`screen::HomeScreen` adds one observer to `home_selection_subject()`,
bound to the page's content area, so it goes away together with the page.
LVGL calls it once when it is added and again whenever the selection
changes, and it asks the layout to mark the new client. The layout then
only restyles, moves or retexts objects it already built; it never creates
or deletes anything there. `set_home_data()` sets the selection before it
counts the new block, so the selection observer does nothing while the
shown block and the new one differ in more than in-place values: the data
observer, which always follows, builds the page with the new selection.

## Keys

The keys are the same in every theme and in both views:

| Key | What it does |
|---|---|
| `z`, `f`, left or up | select the previous client (nothing without clients) |
| `c`, `x`, right or down | select the next client (nothing without clients) |
| `6` SETUP | opens the Settings page |
| `8` INFO | opens the Status page (see [navigation.md](navigation.md#the-status-page)) |
| `4` MODES | opens the Modes page (see [navigation.md](navigation.md#the-modes-page)) |
| `7` TOOLS | opens the Tools page (see [navigation.md](navigation.md#the-tools-page)) |
| `5` SCAN | opens the Discover page (see [navigation.md](navigation.md#the-discover-page)) |
| Enter | opens the Devices page on the selected client (see [navigation.md](navigation.md#the-devices-page)) |
| ESC | shows `Hold ESC to exit` while it is held; letting go sooner only hides it |
| ESC held for 700 ms | quits the app |

The help panel (fn+H) shows the badge `HOME` and five rows: `HOLD ESC`
Exit, `F Z / X C` Previous / next client, `ENTER` Devices, `4 / 5` Modes /
Discover and `6 / 8` Settings / Status (key 7 shows TOOLS on the bar
itself). It is the
same in both views. [navigation.md](navigation.md)
explains how keys travel through the app.

The soft keys are set in `HomeScreen::soft_keys()`:

```cpp
view::widgets::SoftKeySet HomeScreen::soft_keys() {
    // Called from init(), before build_content(): it uses nothing but the
    // view model, which outlives every screen.
    auto& view_model = this->view_model();
    return {{
        {"Modes", [&view_model] { view_model.open_modes(); }},
        {"Scan", [&view_model] { view_model.open_discover(); }},
        {"Setup", [&view_model] { view_model.open_settings(); }, true},
        {"Tools", [&view_model] { view_model.open_tools(); }},
        {"Info", [&view_model] { view_model.open_status(); }},
    }};
}
```

## Fitting the page into 140 px

The design mockups for the home page use text of 6.5 to 7 px. The app
never draws text below 12 px, so the layouts are reflowed, not copied. The
target is about 10 rows of 45 characters above the soft-key bar.

### Measured font sizes

These numbers were measured with FreeType, the same way the app loads its
fonts:

| Role | Instrument | Constellation | Terminal |
|---|---|---|---|
| `Small` (12 px) | 7 px per character, 16 px line, baseline 12 px below the label's top | the same | the same |
| `Mono` (13 px) | 8 px per character, 17 px line | the same | the same |
| `Body`, `Strong` | Inter 14, proportional, 17 px line | Geist Mono 13: 8 px per character, 17 px line | JetBrains Mono 13: 8 px per character, 17 px line |
| `Title` | Space Grotesk 18, 23 px line, baseline 17 px below the top | Geist Mono Bold 16: 10 px per character, 21 px line | JetBrains Mono Bold 14: 8 px per character, 18 px line |
| `Display` | Space Grotesk 36, 46 px line, baseline 35 px below the top | Geist Mono Bold 30: 18 px per character, 39 px line | JetBrains Mono Bold 28: 17 px per character, 37 px line |

So 45 characters of `Small` fit across the screen, and 40 of the 13 px
mono fonts.

### A fixed 14 px row step

Stacked by their natural height, 16 px labels give only eight rows in 140
px, so flex columns that let LVGL stack labels do not fit. The ink of a
12 px line only covers about rows 3 to 14 of its 16 px label, though. So
the layouts place every label at a fixed position with
`lv_obj_set_pos()`, on a 14 px row step: neighbouring label boxes overlap,
but their ink does not. That gives ten rows.

- A row that gets a fill, such as Terminal's selected table row, steps
  15 px instead, so descenders keep an empty row above the next fill.
- A label's parent must be tall enough for the whole label: LVGL clips a
  child to its parent, and a cut-off descender (`p`, `y`, `g`) is easy to
  miss at normal size. Look at enlarged pictures.
- Text in a bigger or bolder font that shares a line with `Small` text is
  moved so that both stand on the same baseline, using the font's line
  height and baseline instead of a guessed offset.
- Values that can grow, such as names and addresses, sit in boxes of fixed
  width. Longer text ends in dots instead of running into its neighbour.
  The boxes for the device's and the router's address hold 15 characters,
  so the widest address, `100.100.100.200`, fits whole.
- Rows 138 and 139, right above the soft-key bar's line, stay empty.
  Constellation's upright panel line is the only thing that runs down to
  the bar.

### Reflow choices

- **Bars instead of braille.** The Terminal mockup draws its traffic graphs
  with braille dots, but no bundled font has braille (see
  [fonts.md](fonts.md)). The graphs are bars drawn as shapes.
- **Solid slanted lines.** LVGL's software renderer only dashes horizontal
  and vertical lines, so the map's links to the clients, which slant, are
  solid thin lines, and the rings are solid thin circles.
- **Short words.** A soft-key word has at most 5 characters at 12 px, so
  the keys say `MODES`, `SCAN`, `SETUP` and `TOOLS`. For the same reason
  Instrument says `OFFLINE` in big type: `NO NETWORK` does not fit there.
- **Less on screen.** Terminal shows three clients under a header row
  instead of a longer host list, and it leaves out a hint line for the
  command bar, which does not exist yet.

## Shared parts

The home page added four parts that other pages can use too:

- **`screen::BaseScreen::Layout::NoTitleBar`** (`base_screen.h`): a page with
  the soft-key bar but no title bar. Its content area is 320 x 140 px at
  the top of the screen.
- **`view::widgets::build_bar_graph()`** (`src/view/widgets/bar_graph.h`):
  a small bar graph made of plain objects, one filled bar per value,
  standing on the bottom edge. Values are shares of the full height, from
  0 to 100. A bar is never less than 1 px tall, so the graph always shows
  its floor. The caller passes the colour and places the graph.
  `view::widgets::update_bar_graph()` shows new values on it in place: it
  changes the height of the bars that changed and never adds or deletes
  one.
- **`view::style::PaneDensity::Compact`**, the last parameter of
  `view::style::pane()`: less padding for pages whose rows sit on a fixed
  grid. In Terminal, whose pane titles sit in the border, the content
  starts 2 px into the title line, so a pane's rows start 14 px below its
  top; keep the first two rows of the content free of ink, because the
  title's ground fill covers them. In the other themes the padding is
  `space_xs` all round. `PaneDensity::Normal`, the default, is unchanged.
- **`view::style::pane_note()`**: a second short label on a pane's top line,
  at its right end, such as the clock in Terminal's `net` pane. In Terminal
  it sits in the border line like the title; in the other themes it sits
  in the box's top right corner. It returns the label, so its text can be
  changed later.

[themes.md](themes.md#how-a-screen-gets-its-look) lists these next to the
other style helpers.

## Adding a layout for a new theme

A new theme works on the home page without any work: a theme without a
layout of its own gets the Instrument layout, in its own colours and
fonts. To give it its own:

1. Write a class derived from `screen::HomeLayout` (`home_layout.h`) in a
   new file, `src/view/screens/home_<theme>.cpp`, inside an anonymous
   namespace, like the three that exist. `build()` draws the page into
   `context.content`; `select()` marks the selected client; `update()`
   shows new values on the page that is already built.
2. Draw both views in `build()`: the router view while
   `context.data.ap_active` is true, the link view otherwise, with its
   Wi-Fi, cable and no-network forms. Let `build()` choose only by
   `ap_active` and `uplink_kind`, never by an in-place value: a label that
   can be empty, such as a speed that is not known, is still made, with an
   empty text. Otherwise a page updated in place would not look like one
   built fresh.
3. Write `select()` for the router view. In the link view it does nothing.
4. Write `update(context)`: for every in-place value the layout draws (see
   [Values in place, a new situation built again](#values-in-place-a-new-situation-built-again)),
   in both views, set the label's text with `set_text_if_changed()` from
   `home_layout.h`. Make the text with the same function `build()` uses,
   and keep a pointer to each label and the text it shows. `update()` may
   only change the text and style of objects `build()` made, and move them
   (Instrument's caption follows its first number when the number's width
   changes); it never creates or deletes anything. `HomeScreen` calls it
   once right after `build()`, when nothing has changed yet, and again
   whenever only in-place values changed (see [data.md](data.md)).
5. Add a factory such as `make_<theme>_home_layout()` to `home_layout.h`,
   and define it at the end of the new file.
6. Add a case for the theme's `view::ThemeId` to the switch in
   `HomeScreen::build_layout()`, and a line to the class comment in
   `home_screen.h`.

The rules for a layout:

- Take every value from `context.data` and turn it into text with the
  helpers in `home_format.h`. Never write a value into the layout.
- Take colours only from `context.theme.colors` and fonts only from
  `context.theme.font(role)`. No colour values, no font files, no timers,
  no animations, no focus groups and no buttons.
- Build from plain objects, labels and lines, at fixed positions. Copy the
  `plain_object()` and `plain_label()` helpers of the other layouts; a bare
  `lv_obj_create()` brings padding, a border and scrollbars.
- `lv_line_set_points()` keeps a pointer to the points instead of copying
  them, so the points must be a `static const` array at namespace scope,
  never a local array.
- Keep pointers only to the objects `select()` and `update()` change.
  LVGL owns every object, and they go when the page goes or when its
  content is built again. A new layout object is made for every build, so
  no pointer outlives its objects.
- Never delete an object yourself and never empty the content area:
  building the page again is `HomeScreen`'s job.
- `select()` gets an index from the view model; keep it within the clients
  shown, as the three layouts do.
- A value that can change in place must not move anything else: give it a
  box of fixed width or align it so that it grows into free space. Look at
  its widest text and at its text when it is not known:
  - the clock and the battery: `23:59`, `100%+` and `--%`;
  - the network values: tour stop 15 has the widest texts (a 32-character
    Wi-Fi name, `100.100.100.200`, −100 dBm, 1000 Mbit/s), stops 19 and 22
    a fast cable and another uplink, and stops 16 and 20 values that are
    not known, as does `--data offline`.
  A long name ends in dots inside its box; an address, a speed or a signal
  must always be whole.
- What is not known shows `--` or is left out. Never `0`, `0M`, `0.0`,
  `--M` or a line that starts or ends with ` · `; the helpers take care of
  that when a line is made with `join_parts()`.
- Show the sample mark while `context.data.sample` is true.

Then take pictures of the page in the new theme in every example set and
at the tour's stops, with each client selected, look at them at normal
size and enlarged, check that a page walked to in place equals one built
fresh (see [Looking at the page](#looking-at-the-page)), and add the theme
to this page.

## Looking at the page

From the repository root, after building the simulator:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
for t in instrument constellation terminal; do
  $B --snapshot "build/shots/home-$t.png" --page home --theme "$t"
  $B --snapshot "build/shots/home-$t-next.png" --page home --theme "$t" --keys c
done
```

The second picture of each theme shows the second client selected. Two
pictures of the same state are the same file:

```sh
$B --snapshot build/shots/home-a.png --page home --keys c
$B --snapshot build/shots/home-b.png --page home --keys right
cmp build/shots/home-a.png build/shots/home-b.png && echo same
```

The link view, in each situation:

```sh
for t in instrument constellation terminal; do
  $B --snapshot "build/shots/wifi-$t.png" --page home --theme "$t" --data wifi
  $B --snapshot "build/shots/cable-$t.png" --page home --theme "$t" --data cable
  $B --snapshot "build/shots/offline-$t.png" --page home --theme "$t" --data offline
  $B --snapshot "build/shots/widest-$t.png" --page home --theme "$t" --data tour --data-steps 15
done
```

`--data tour --data-steps 15` shows the widest texts; steps 16 and 20 show
values that are not known (see [The tour](data.md#the-tour) for every
stop).

To see the page follow its data, take a source a number of steps on. A
page updated in place must be the same file as one built fresh with the
same values, which `--switch-to` checks by building the page again at the
end. With the tour this checks both ways of changing the page: step 15
reaches the widest texts in place from step 14, and step 18 is a cable
after a Wi-Fi network, built again:

```sh
for n in 15 18; do
  $B --snapshot "build/shots/tour-$n.png" --page home --theme instrument --data tour --data-steps $n
  $B --snapshot "build/shots/tour-$n-fresh.png" --page home --theme instrument --data tour --data-steps $n --switch-to terminal,instrument
  cmp "build/shots/tour-$n.png" "build/shots/tour-$n-fresh.png" && echo same
done
```

The same works with the demo, which moves the clock and the battery (see
[The demo script](data.md#the-demo-script)):

```sh
$B --snapshot build/shots/home-181.png --page home --theme instrument --data demo --data-steps 181
$B --snapshot build/shots/home-181-fresh.png --page home --theme instrument --data demo --data-steps 181 --switch-to terminal,instrument
cmp build/shots/home-181.png build/shots/home-181-fresh.png && echo same
```

Step 181 shows `10:47` and `100%+`, the widest battery text; step 82
shows `9%`. `--data live` shows the computer's own clock.

In a window, `./build/darwin-arm64/Debug/lokictl --data tour` takes one
stop every 2 s, so you can watch the page go from one situation to the
next.

`--page home` can be left out, because `home` is the default page. On a
device, `scripts/device.sh snapshot build/shots/device-home.png --page home
--theme terminal --data wifi` draws the same page with the device's own
font rendering (see [development.md](development.md#run-on-a-device)).
