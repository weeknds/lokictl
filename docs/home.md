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
theme shows, where the values come from, how the page is laid out on such
a small screen, and how to add a layout for a new theme.

## What each theme shows

All three layouts show the same things: the active network mode, the
uplink, the access point, the traffic and the connected clients, with one
client marked as selected.

### Instrument: the gauge

Drawn by `src/view/screens/home_instrument.cpp`.

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

### Constellation: the map

Drawn by `src/view/screens/home_constellation.cpp`.

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

### Terminal: the panes

Drawn by `src/view/screens/home_terminal.cpp`. Like every Terminal pane,
each title sits in the top border line.

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

To see all three, take a snapshot of each (see
[Looking at the page](#looking-at-the-page)).

## Sample data

Most values on the page are still made up: the mode, the uplink, the
access point, the addresses, the traffic and the clients. Only the clock
and the battery are real, and only when the app reads live values, which
it does on the device (the data layer decides this; see
[data.md](data.md)). The page says that its values are made up with a
small `sample data` mark in every theme. Instrument and Constellation write
it in capitals, like all their captions; Terminal writes it in lowercase.
The mark matters on a network tool: nobody should take three made-up
clients for a real reading. So it stays on, also next to a real clock,
until the network values are real too.

A snapshot uses the fixed sample values unless `--data` says otherwise, so
the same state always gives the same picture. Two snapshots of the home
page are the same file, byte for byte, which makes the page easy to check
by script. Pages with a title bar show the real time there, so in a
snapshot Settings shows a different clock from the home page.

## Where the values come from

- `model::HomeData` in `src/model/home_data.h` holds everything the page
  shows. Numbers are stored as numbers, in the unit written next to each
  field: the clock in minutes since midnight, the uptime in seconds, rates
  in kbit/s, the signal in dBm, the battery in per cent (-1 when it is not
  known). The battery's state is a `model::BatteryState`: unknown,
  discharging, charging or idle. Lists, such as the clients and the two
  traffic histories, are plain C++ containers. Each history value is a
  share (0 to 100) of that history's own recent peak, not of a fixed rate,
  so the quieter upload graph fills its height as well as the download
  graph does; the current rates give the real numbers.
- `model::sample_home_data()` in `src/model/home_data.cpp` fills it with the
  made-up values and sets `HomeData::sample` to true. It is the only place
  in the app that writes them; no value is written in `src/view` or
  `src/app`.
- A data source fills in one whole `HomeData` block: `sample` (the sample
  values), `demo` (the sample values, moving on a fixed script) or `live`
  (the sample values with the real clock and battery put in). A background
  reader asks the source and `app::DataController` hands every new block to
  `viewmodel::BaseViewModel::set_home_data()`. [data.md](data.md) explains
  the whole path.
- `model::BaseModel` owns one `HomeData`, and
  `viewmodel::BaseViewModel::home()` hands it to the page.
- A layout turns the numbers into text with the helpers in
  `src/view/screens/home_format.h`: `clock_text()` gives `07:46`,
  `battery_text()` gives `91%`, `91%+` while the battery charges and `--%`
  when the charge is not known (it replaced the earlier `percent_text()`),
  `uptime_text()` gives `00:12:31`, `rate_text()` gives `3.8` for 3800
  kbit/s, `signal_text()` gives `−61 dBm` with a real minus sign, and
  `count_text()` gives `1/3`. All layouts use the same helpers, so a value
  reads the same in every theme, and a value out of range is clamped
  instead of printed as garbage.

The page is built from the values once. When a new block arrives while it
is open, the layout's `update()` changes the clock and the battery text in
place; the page is not built again (see
[How a page updates in place](data.md#how-a-page-updates-in-place)). When
the page is built again, after a theme switch or when coming back from
Settings, it shows the current values at once.

More real values will replace the sample ones in the same place: the live
source will fill more of the same `HomeData`, and once the last made-up
value is gone it sets `sample` to false, which removes the mark. The
layouts take every value from `HomeData` already, so none of them has to
change for that, apart from showing any new value in `update()`.

## The selected client

One client is selected, and the arrow keys move the selection. It lives in
the view model, not in the page:

| Function | What it does |
|---|---|
| `home_selection_subject()` | the index of the selected client in `home().clients`, starting at 0 |
| `home_selection()` | the same index as a number |
| `home_select(delta)` | moves the selection by `delta`; it stops at the first and the last client shown and does not wrap around |

The page shows at most three clients for now
(`viewmodel::kHomeClientRows`), so the selection never goes past the
third. The client count on the page still shows how many there are.

Because the selection lives in the view model, it is kept when Settings is
opened and closed, and across a theme switch, which builds the page again
in the new theme's layout. When new values bring fewer clients than the
selection's place, `set_home_data()` pulls the selection back to the last
client shown.

`screen::HomeScreen` adds one observer to `home_selection_subject()`,
bound to the page's content area, so it goes away together with the page.
LVGL calls it once when it is added and again whenever the selection
changes, and it asks the layout to mark the new client. The layout then
only restyles, moves or retexts objects it already built; it never creates
or deletes anything there.

## Keys

The keys are the same in every theme:

| Key | What it does |
|---|---|
| `z`, `f`, left or up | select the previous client |
| `c`, `x`, right or down | select the next client |
| `6` SETUP | opens the Settings page |
| `4` MODES, `5` SCAN, `7` TOOLS | shown switched off; their pages do not exist yet |
| `8`, Enter | nothing |
| ESC | shows `Hold ESC to exit` while it is held; letting go sooner only hides it |
| ESC held for 700 ms | quits the app |

The help panel (fn+H) shows the badge `HOME` and four rows: `HOLD ESC`
Exit, `F Z / X C` Previous / next client, `6` Settings and `4 / 5 / 7` Not
available yet. [navigation.md](navigation.md) explains how keys travel
through the app.

The soft keys are set in `HomeScreen::soft_keys()`:

```cpp
view::widgets::SoftKeySet HomeScreen::soft_keys() {
    // Called from init(), before build_content(): it uses nothing but the
    // view model, which outlives every screen. Modes, Scan and Tools open
    // pages that do not exist yet, so they are shown switched off.
    auto& view_model = this->view_model();
    return {{
        {"Modes", {}, false, false},
        {"Scan", {}, false, false},
        {"Setup", [&view_model] { view_model.open_settings(); }, true},
        {"Tools", {}, false, false},
        {},
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
  the keys say `MODES`, `SCAN`, `SETUP` and `TOOLS`.
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
2. Add a factory such as `make_<theme>_home_layout()` to `home_layout.h`,
   and define it at the end of the new file.
3. Write `update(context)`: for every value the data layer can change
   (so far the clock and the battery), set the label's text with
   `set_text_if_changed()` from `home_layout.h`. Make the text with the
   same function `build()` uses, and keep a pointer to each label and the
   text it shows. `update()` may only change the text and style of objects
   `build()` made; it never creates or deletes anything. `HomeScreen`
   calls it once right after `build()`, when nothing has changed yet, and again
   whenever new values arrive (see [data.md](data.md)).
4. Add a case for the theme's `view::ThemeId` to the switch in
   `HomeScreen::build_content()`, and a line to the class comment in
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
  LVGL owns every object, and they go when the page goes.
- `select()` gets an index from the view model; keep it within the clients
  shown, as the three layouts do.
- A value that can change, such as the battery, must not move anything
  else: give it a box of fixed width or align it so that it grows into
  free space. Look at its widest text (`23:59` and `100%+`) and at `--%`.
- Show the sample mark while `context.data.sample` is true.

Then take pictures of the page in the new theme with each client selected
and with moving values (see [Looking at the page](#looking-at-the-page)),
look at them at normal size and enlarged, and add the theme to this page.

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

To see the page follow its data, take the demo values a number of steps
on (see [The demo script](data.md#the-demo-script)). A page updated in
place must be the same file as one built fresh with the same values, which
`--switch-to` checks by building the page again at the end:

```sh
$B --snapshot build/shots/home-181.png --page home --theme instrument --data demo --data-steps 181
$B --snapshot build/shots/home-181-fresh.png --page home --theme instrument --data demo --data-steps 181 --switch-to terminal,instrument
cmp build/shots/home-181.png build/shots/home-181-fresh.png && echo same
```

Step 181 shows `10:47` and `100%+`, the widest battery text; step 82
shows `9%`. `--data live` shows the computer's own clock.

`--page home` can be left out, because `home` is the default page. On a
device, `scripts/device.sh snapshot build/shots/device-home.png --page home
--theme terminal` draws the same page with the device's own font
rendering (see the README).
