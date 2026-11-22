<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Themes

LokiCTL has three looks, called themes: **Instrument**, **Constellation** and
**Terminal**. This page explains what a theme is made of, how a screen gets
its look, how the soft keys at the bottom of the screen take theirs, how the
app picks a theme and switches it while running, and how to add a new one.

## What a theme is

A theme is one entry in a table that is compiled into the app, in
`src/view/theme.cpp`. There is no theme file to read at run time. Each entry
holds:

- a name for the command line (`instrument`) and one to show (`Instrument`);
- 15 colours, each with a job (ground, text, accent and so on);
- six font roles, each a bundled font file at a fixed size;
- spacing steps, the corner radius and the border width;
- four flags for things that change the layout, not only the colours.

The types are declared in `src/view/theme.h`. `view::ThemeEngine`
(`src/view/theme_engine.h`) owns the active theme: it loads the theme's fonts
and hands out a `view::Theme` with the colours ready for LVGL and the fonts
loaded.

## The three themes

- **Instrument:** warm paper ground, thin ink lines, square corners, big
  Space Grotesk numbers, an orange filled selection with an ink frame, and
  captions in spaced capital letters.
- **Constellation:** near-black blue ground, thin dark-blue lines, slightly
  rounded corners, a lime accent, the selected item outlined in lime with
  lime text, and violet addresses. Everything is set in Geist Mono.
- **Terminal:** black ground, grey boxed panes with the title set into the
  top border line, an amber accent, cyan addresses, and a magenta selected
  row with black bold text. Everything is set in JetBrains Mono.

### Colours

Values marked with \* are our own additions in the same spirit; all other
values come from the design mockups.

| Colour | Used for | Instrument | Constellation | Terminal |
|---|---|---|---|---|
| `background` | screen ground | `#ECEAE4` | `#06080F` | `#000000` |
| `surface` | raised areas: cards, pop-ups, messages | `#F6F4EE`\* | `#0C1120`\* | `#0A0A0A` |
| `text` | main text | `#141414` | `#E8ECF5` | `#D0D0D0` |
| `muted` | secondary text, things that are switched off | `#57544E` | `#7D89A6` | `#8A8A8A` |
| `accent` | the main highlight | `#FF4F1A` | `#C8FF4D` | `#FFB000` |
| `accent2` | links and addresses | `#C2380C` | `#8C7CFF` | `#5FD7FF` |
| `border` | lines, pane and card borders | `#141414` | `#1A2238` | `#3A3A3A` |
| `success` | something worked | `#1F7A3D`\* | `#5BE49B`\* | `#5FD75F`\* |
| `warning` | needs attention | `#A35F00`\* | `#FFC857`\* | `#FFD75F`\* |
| `error` | something failed | `#C1121F`\* | `#FF6B7A`\* | `#FF5F5F`\* |
| `border_strong` | borders that must stand out, such as pop-ups | `#141414` | `#3A4766` | `#FFB000` |
| `border_subtle` | faint grid lines and dividers | `#CFCBC2`\* | `#151D33` | `#1C1C1C` |
| `on_accent` | text on an accent fill | `#141414` | `#06080F` | `#000000` |
| `selection` | the selected item | `#FF4F1A` | `#C8FF4D` | `#FF5FAF` |
| `on_selection` | text on a filled selection | `#141414` | `#06080F` | `#000000` |

The first ten are the colours every screen works with. The last five exist
because the three designs treat borders and selections differently.

### Font roles

A screen never asks for a font file. It asks for a role, and the theme says
which file and size that role uses. All files are in `assets/fonts/` (see
[fonts.md](fonts.md)); sizes are in pixels.

| Role | Used for | Instrument | Constellation | Terminal |
|---|---|---|---|---|
| `Display` | big numbers and single words | `SpaceGrotesk-Bold` 36 | `GeistMono-Bold` 30 | `JetBrainsMono-Bold` 28 |
| `Title` | headings and important values | `SpaceGrotesk-Bold` 18 | `GeistMono-Bold` 16 | `JetBrainsMono-Bold` 14 |
| `Body` | normal interface text | `Inter-Regular` 14 | `GeistMono-Regular` 13 | `JetBrainsMono-Regular` 13 |
| `Strong` | emphasised text, same size as Body | `Inter-SemiBold` 14 | `GeistMono-Bold` 13 | `JetBrainsMono-Bold` 13 |
| `Mono` | data: addresses, counters, tables | `IBMPlexMono-Regular` 13 | `GeistMono-Regular` 13 | `JetBrainsMono-Regular` 13 |
| `Small` | captions, hints, secondary details | `IBMPlexMono-Regular` 12 | `GeistMono-Regular` 12 | `JetBrainsMono-Regular` 12 |

No role is smaller than 12 px, the smallest size that stays readable on the
1.9-inch screen. The constant `view::kMinTextSizePx` holds that limit, and
the build fails if a table entry goes below it.

### Spacing, corners and flags

| Setting | Instrument | Constellation | Terminal |
|---|---|---|---|
| `space_xs` / `space_sm` / `space_md` / `space_lg` | 2 / 4 / 8 / 12 | 2 / 4 / 8 / 12 | 2 / 4 / 6 / 10 |
| `radius` (corner radius) | 0 | 3 | 0 |
| `border_width` | 1 | 1 | 1 |
| `caption_letter_space` | 1 | 0 | 0 |
| `dark` | no | yes | yes |
| `pane_title_in_border` | no | no | yes |
| `uppercase_captions` | yes | yes | no |
| `selection_filled` | yes | no | yes |

All sizes are in pixels. What the flags mean:

- `dark`: the theme has a dark ground. The desktop simulator then draws the
  dark device frame around the screen, and LVGL's own default styles use
  their dark variant.
- `pane_title_in_border`: a pane's title sits on its top border line, like a
  terminal window. Otherwise the title is a caption inside the box.
- `uppercase_captions`: captions are written in capital letters.
- `selection_filled`: the selected item is filled with the selection colour.
  Otherwise it gets an outline and text in the selection colour.

## How a screen gets its look

Screens and widgets get the `view::ThemeEngine` in their constructor. While
they build their LVGL objects, they read the active theme once and set the
styles directly:

```cpp
const view::Theme& theme = theme_engine.theme();

auto* heading = lv_label_create(root);
lv_label_set_text(heading, "Network");
view::style::text(heading, theme, view::FontRole::Title);

auto* address = lv_label_create(root);
lv_label_set_text(address, "10.0.0.1");
view::style::text(address, theme, view::FontRole::Mono, theme.colors.accent2);

auto* label = lv_label_create(root);
view::style::caption(label, theme, "gateway");
```

The rules:

- **Use the helpers in `src/view/style.h`** where one fits: `screen`, `bar`,
  `popup`, `text`, `caption`, `card`, `list_row`, `pane` and `soft_key`.
  They already follow the flags, so a list row is filled or outlined and a
  pane's title sits in the border or inside the box without the screen
  having to check.
- **Never hard-code a colour or a text font.** No colour values and no
  `lv_color_hex()` outside `src/view/theme.cpp`, and no text font files
  loaded by a screen. Take colours from `theme.colors` and fonts from
  `theme.font(role)`.
- **Text is never smaller than 12 px.** Use the roles; they are all 12 px or
  more. If something does not fit, shorten the text or change the layout.
- **Screens do not watch for theme changes.** When the theme changes, the
  screen is built again from scratch (see below).
- **Icon fonts are not theme fonts.** The Phosphor icons of the title bar
  and the key-cap fonts of the home page are loaded where they are used,
  with `AssetManager::load_font()`. They take their colour from the theme
  like any other text.

## Soft keys

The five keys `4` to `8` sit right under the screen. On every page with a
title bar, the bar along the bottom edge of the screen says what each of
them does on that page. The widget is `view::widgets::SoftKeyBar`, in
`src/view/widgets/soft_key_bar.h`.

- The bar is 30 px tall (y 140 to 169) and as wide as the screen, with a
  1 px line in the border colour along its top edge.
- It has five slots of 56 px, one above each key. Their centres are
  x = 49, 105, 161, 217 and 273, exactly above the keys.
- A used slot shows the key's number in a small box (13 x 14 px), a 4 px
  gap and then a short word. The three together are centred in the slot,
  on y = 153, and the number and the word stand on the same baseline.
- A short line, 2 px wide and 5 px tall, runs from each used slot down to
  its key.
- The text is the theme's `Small` font (12 px), with no extra letter
  spacing.

**A word has at most 5 characters.** Text is never smaller than 12 px, and
at that size one character is 7 px wide in all three themes. Box, gap and
five characters take 52 of the 56 px. A longer word is cut to its first 5
characters, and the app writes a warning to the log. So pick short words:
`SCAN`, not `SCANNER`.

### How a key looks

| | Instrument | Constellation | Terminal |
|---|---|---|---|
| Word | CAPITALS | CAPITALS | lowercase |
| Number box | filled, square corners | 1 px outline, corners rounded by 2 px | filled, square corners |
| Normal key | ink box, paper number, ink word | grey-blue (`muted`) outline and number, light word | grey (`muted`) box, black number, light-grey word |
| Main key | orange box, ink number, ink word | lime outline, number and word | amber box, black number, amber word |
| Switched-off key | pale box (`border_subtle`), muted number and word | dark outline (`border`), muted number and word | near-black box (`border_subtle`), muted number and word |
| Line to the key | same colour as the box | same colour as the outline | same colour as the box |
| Empty key | nothing drawn, the key does nothing | the same | the same |

The table is not stored anywhere. `view::style::soft_key()` works it out
from colours and flags every theme already has, so a new theme needs no
code for its soft keys:

- The box is filled when the theme fills its selection
  (`selection_filled`). Otherwise it is an outline as thick as
  `border_width`.
- The corners use the theme's `radius`, but never more than 2 px.
- The word is written in capitals when the theme writes its captions in
  capitals (`uppercase_captions`), otherwise in lowercase. Only the letters
  A to Z change.
- Normal key: the box is `text` on a light theme and `muted` on a dark one
  (`dark`). The number is `background` on a filled box and the box colour on
  an outline. The word is `text`.
- Main key: the box is `accent`. The number is `on_accent` on a filled box
  and `accent` on an outline. The word is `accent` on a dark theme and
  `text` on a light one, because orange text on paper is hard to read.
- Switched-off key: the box is `border_subtle` when it is filled and
  `border` when it is an outline. The number and the word are `muted`.
- The line to the key always has the colour of the box.

### Giving a page its keys

A page says which keys it uses by overriding `soft_keys()` from
`screen::BaseScreen`. It returns five `view::widgets::SoftKey` values, for
the keys 4 to 8 in that order. Each one has a word, an action, whether it is
the page's main key (drawn in the accent colour) and whether it is switched
on. A key written as `{}` stays empty. The home page of the demo does this:

```cpp
view::widgets::SoftKeySet AppleScreen::soft_keys() {
    auto& view_model = this->view_model();
    return {{
        {"Exit", [&view_model] { view_model.request_quit(); }},
        {"Size", [&view_model] { view_model.toggle_bold_text(); }},
        {"Theme", [&view_model] { view_model.request_next_theme(); }, true},
        {"Info", [&view_model] { view_model.toggle_info(); }},
        {"Count", [&view_model] { view_model.toggle_page(); }},
    }};
}
```

Some things to know:

- Write the word in normal letters, such as `"Exit"`. The bar writes it in
  capitals or lowercase, as the theme asks.
- A switched-off key, such as `{"Pcap", {}, false, false}`, is drawn dimmed
  and does nothing until it is switched on.
- `soft_keys()` is called once, from `init()`, before `build_content()`. A
  page that needs its own members there sets them before it calls `init()`.
  A page without the override gets a bar with five empty keys, and a
  full-screen page has no bar at all.
- **An action may only ask the view model for something**, as in the
  example. It runs inside an LVGL click event, so it must never delete or
  build a screen itself. The view model's page changes and theme switches
  are already put off until LVGL has finished its callbacks, which is why
  key 6 can rebuild the very page whose key was pressed.
- To change a key later, for example to turn `SCAN` into `STOP` while a scan
  runs, call `soft_key_bar()->set_key(index, key)`, with an index from 0 to
  4 for the keys 4 to 8. Only that key is drawn again. It also works from
  inside a key's own action.
- The bar reads the theme once, when it is built, like every other part of
  a screen. A theme switch builds the page, and the bar with it, again.
- Keys 4 to 8 press the slots of the bar on the current page; an empty or
  switched-off key does nothing. On the counter page `z` and `left` also
  press key 5, and `c` and `right` key 7. On the home page the app's exit
  handling takes key 4 first (a short press shows a hint, holding it
  quits), so the `Exit` action only runs when its slot is clicked with the
  mouse in the simulator.

The developer page `dev-softkeys` shows all of this. Its own bar at the
bottom has a main key (`TOOLS`), a switched-off key (`PCAP`) and an empty
key. Above it, the bar at y 50 has five 5-letter words, the widest keys a
bar can show, and the bar at y 100 was built with other keys and then
changed key by key with `set_key()`. That one must come out exactly like
the bar at the bottom.

```sh
B=./build/darwin-arm64/Debug/lokictl
for t in instrument constellation terminal; do
  $B --snapshot "build/shots/softkeys-$t.png" --page dev-softkeys --theme "$t"
done
```

## Symbols and the fallback font

Of the five bundled families, only JetBrains Mono contains every symbol the
screens use (arrows, ●, ✓, ✕, › and the box-drawing lines; see
[fonts.md](fonts.md)). So every theme font that is not JetBrains Mono gets
JetBrains Mono Regular at the same size as its LVGL fallback font. When a
font lacks a character, LVGL draws it from the fallback instead of showing
an empty box. JetBrains Mono fonts get no fallback, because they need none.

Some things to know:

- **A font with a fallback is a separate copy.** The asset manager treats
  the same file at the same size with a different fallback as a different
  font. The theme's copy of Inter has the fallback; the copy that the
  `dev-fonts` pages load has none. So those pages still show which
  characters each font really contains.
- **Borrowed symbols can look slightly different.** A check mark inside
  Inter text comes from JetBrains Mono, so it can be a little wider or
  heavier than the letters around it.
- **There is no braille.** No bundled font has braille characters, the
  fallback included. Draw graphs with LVGL shapes instead.
- **Chinese, Japanese and Korean are not covered yet.** None of the theme
  fonts or the fallback has those characters, so such text (a Wi-Fi name,
  for example) would show empty boxes.
- **A font file that cannot be loaded** is replaced by its fallback, or by
  LVGL's built-in Montserrat font (never below 12 px) if that fails too. The
  app logs a warning, and snapshot mode stops with exit code 1, so a broken
  install is noticed.

## How the theme is chosen

At startup the app takes the first of these that applies:

1. **`--theme <name>`** on the command line: `instrument`, `constellation`
   or `terminal`. It works for snapshots and for a normal start. On a normal
   start it is used for that run only and is never written to the settings
   file. Any other name stops the app at once with a message that lists the
   three names.
2. **The saved choice**, the key `theme` in the `[application]` section of
   the settings file, for example `theme=instrument`. Key 6 writes it every
   time it switches the theme (see below). A name the app does not know
   gives a warning in the log. The app then carries on as if no theme had
   been saved (step 3 is skipped as well) and leaves the file exactly as it
   is.
3. **The old setting `dark_mode`**, which earlier versions wrote before
   themes were saved by name: `yes` means Terminal, `no` means Instrument.
   It only counts when the file has no `theme` line. A start that finds it
   replaces it once: the app writes the theme it stands for as a `theme`
   line and removes the `dark_mode` line. If both lines are there, `theme`
   wins and the `dark_mode` line is simply removed. This also happens on a
   start with `--theme`; what is written is the saved choice, never the
   `--theme` value. A `dark_mode` value the app cannot read is ignored with
   a warning in the log, and the file is left as it is.
4. **The default**, `view::kDefaultThemeId` in `src/view/theme.h`, which is
   Constellation. That one line is the only place to change the default; the
   help text, snapshots and a first start all follow it.

If the settings file cannot be written, the app logs a warning and carries
on in the theme it picked.

Snapshot mode never reads or writes the settings file, so without `--theme`
a snapshot always uses the default theme. The settings file is
`config/lokictl.conf` in the simulator. On the device it is
`/etc/lokictl.conf`, with your own changes saved to
`~/.config/lokictl/lokictl.conf`; once that file exists, the app reads it
instead. The shipped file has no `theme` line, so a fresh install starts in
the default theme.

The environment variable `LOKICTL_CONFIG_FILE` names a settings file that
the app reads and writes instead of the normal ones, on any platform. It is
meant for testing, above all in the simulator, where the normal settings
file is the repository's own `config/lokictl.conf`. If the named file does
not exist yet, the app logs that it could not load it, carries on as if no
theme had been saved and creates the file when key 6 first saves a theme.

The log says which theme was picked and why, for example
`theme: constellation (from the default)`. The other reasons are
`from the command line`, `from the saved setting` and
`from the old dark_mode setting`.

## Switching while the app runs

Key 6 on the home page switches to the next theme at once, in the order of
the theme table: Instrument, Constellation, Terminal, then Instrument again.
The page is built again in the new theme and keeps its state: the text size
picked with key 5, the version line shown with key 7 and the counter value
all stay. The message box and the help panel take the new look too, and in
the desktop simulator so does the device frame around the screen. A short
message says `Theme: <Name>`, or `Theme: <Name> (not saved)` when the
settings file could not be written. Holding the key switches once. While
the help panel is open, key 6 does nothing.

Every switch is saved as `theme=<name>` in the `[application]` section of
the settings file. All other lines, comments and sections of the file stay
as they are. After a start with `--theme`, key 6 goes on from the theme that
is running and saves the new choice, because the user picked it in the
running app.

What happens inside, step by step:

1. The action of key 6, the home page's `THEME` soft key, calls
   `BaseViewModel::request_next_theme()`. The view model knows nothing
   about themes; it only counts the requests in a subject,
   `next_theme_request_subject()`.
2. `app::ThemeController` (`src/app/theme_controller.h`) observes that
   subject. It does not switch right away: it notes the target theme and
   schedules one `lv_async_call()`. Several requests before that call runs
   fold into one switch, and a request for the active theme does nothing.
3. Called from `lv_timer_handler()`, the controller first runs
   `ThemeEngine::set_theme(id)`. It loads the new theme's fonts, makes it
   the active theme and notifies `ThemeEngine::subject()`. The old fonts
   stay loaded for now, because the old screen still uses them.
4. `ScreenManager::rebuild_current_page()` builds the current page again
   from scratch with the new theme, loads it and deletes the old screen. A
   rebuild is needed anyway, because some flags change which objects exist
   (a pane title in the border is a different object from a title inside
   the box). If a page change is still waiting, the new page is the one
   that gets built.
5. `ThemeEngine::release_retired_fonts()` frees the old theme's fonts, but
   only when the rebuild worked. If it failed, the old screen is still
   shown and still uses those fonts, so they stay loaded and the app logs
   an error.
6. The controller logs the switch and calls its listener. The listener, in
   `src/app/app.cpp`, sets the simulator's device frame, saves the choice
   and shows the message.

A switch adds two lines to the log:

```text
theme: terminal (switched from constellation in <n> ms, <n> fonts loaded)
saved theme=terminal to <path>
```

The time covers steps 3 to 5, which run on the UI thread, so it shows how
long the screen was busy. The font count is the number of fonts the asset
manager holds after the switch. It must be the same every time the same
theme comes round again; a number that keeps growing means fonts are not
freed.

The rules that keep this safe:

- **Screens do not watch for theme changes.** They read the theme once
  while they are built, and a switch builds them again. Nothing restyles a
  screen in place, so a page after a live switch is exactly the page a
  fresh start in that theme would draw.
- **Objects on LVGL's top layer outlive every screen.** The message box and
  the help panel live there. They observe `ThemeEngine::subject()` and
  restyle themselves, fonts included, during step 3, before the old fonts
  are freed. Anything new on the top layer that uses a theme font must do
  the same.
- **A switch never runs inside an LVGL event or observer callback.** Key 6
  arrives as a click on a soft-key button that the rebuild deletes, and the
  request passes through an observer. That is why the controller defers
  the switch, the same way the screen manager defers page changes.
- **Fonts are freed only after the old screen is gone**, so no label ever
  points at a font that has been freed.
- **Objects are created and destroyed in a fixed order:** LVGL, then the
  asset manager, then the theme engine, then the display and the screens,
  then the theme controller; on exit the same in reverse. When the
  controller is destroyed, it cancels a switch that is still waiting and
  stops observing the view model.

A future settings screen with a theme picker should not call
`ThemeEngine::set_theme()` itself. It should ask through the view model,
the way key 6 does, so that the controller stays the one place that
switches, rebuilds the page, frees fonts and saves the choice.

## Adding a theme

1. In `src/view/theme.h`, add a value to `enum class ThemeId` (at the end)
   and raise `kThemeCount`.
2. In `src/view/theme.cpp`, add an entry to `kThemes` at the same position.
   The entries are written in the order of the structs in `theme.h`; the
   comment above the table lists that order. The build checks that there is
   one entry per `ThemeId`, that they are in order, and that no font is
   smaller than 12 px.
3. Use font files listed in `src/view/bundled_fonts.h`. A new font family
   needs its files and license text in `assets/fonts/`, a constant in
   `bundled_fonts.h`, a line in `assets/fonts/LICENSE.txt` and a row in
   [fonts.md](fonts.md). Check its symbol coverage on the `dev-fonts` pages.
4. Nothing else is needed for the command line or for key 6: `--theme`,
   `--switch-to` and `--help` take the names from the table, and key 6 goes
   through the table in order, so it picks up the new theme by itself.
5. Snapshot every page in the new theme (see below), look at each picture,
   and add the theme to the tables on this page.
6. Check live switching for every page: with `--switch-to`, from every other
   theme to the new one and from the new one to every other theme. Each
   picture must be identical to one drawn directly in the target theme.

## Looking at the result

Snapshot mode draws a page into a 320x170 PNG without opening a window. These
pages exist for checking themes; they can only be drawn this way:

| Page | What it shows |
|---|---|
| `dev-theme` | all 15 colours with their names, every font role with the symbols ↑↓●✓✕›, a card, and a pane with a list in which one row is selected |
| `dev-help` | the keyboard help panel of the home page |
| `dev-toast` | three messages: success, warning and a normal one |
| `dev-softkeys` | the soft-key bar: a page's own bar with a main key, a switched-off key and an empty one, a bar of five 5-letter words, and a bar changed with `set_key()` after it was built (see [Soft keys](#soft-keys)) |

From the repository root, after building the simulator:

```sh
B=./build/darwin-arm64/Debug/lokictl
$B --snapshot build/shots/theme-terminal.png --page dev-theme --theme terminal
$B --snapshot build/shots/help-instrument.png --page dev-help --theme instrument
$B --snapshot build/shots/toast-constellation.png --page dev-toast --theme constellation
for t in instrument constellation terminal; do
  $B --snapshot "build/shots/home-$t.png" --page demo-home --theme "$t"
done
```

To start the simulator in one theme for a single run, with a settings file
of its own so that key 6 leaves the repository's `config/lokictl.conf`
alone:

```sh
LOKICTL_CONFIG_FILE=build/simulator.conf ./build/darwin-arm64/Debug/lokictl --theme constellation
```

On a device, `scripts/device.sh snapshot <file.png> --page dev-theme --theme
constellation` draws the same page with the device's own font rendering, and
`scripts/device.sh run --theme constellation` shows the app on the real
screen (see the README).

A picture shows the exact pixels, but not how readable they are on the small
panel. Faint lines such as Constellation's dark-blue borders, and Instrument's
orange text on the paper ground, are worth a look on the device itself.

### Checking live switching

`--switch-to <theme>[,<theme>...]` makes a snapshot switch themes while the
page is shown, the same way key 6 does. The page is drawn in the starting
theme (`--theme`, or the default), the app then switches through the listed
themes in order, and the picture is taken after the last switch. After the
first draw and after every switch it prints `fonts loaded: <n> (<theme>)`
to standard error. Like every snapshot, it never reads or writes the
settings file.

The rule: **a picture taken after a live switch must be the same file, byte
for byte, as a picture drawn directly in the last theme.** Any difference
means that something on the page kept a piece of the old theme.

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
$B --snapshot build/shots/home-terminal.png --page demo-home --theme terminal
$B --snapshot build/shots/home-switched.png --page demo-home --theme instrument --switch-to terminal
cmp build/shots/home-terminal.png build/shots/home-switched.png && echo same
```

The demo pages show the clock in the title bar, so if two pictures were
taken on either side of a new minute, take both again.

To check that switching does not leak fonts, switch 30 times (10 full
rounds) and count the `fonts loaded` lines. Each theme must show a single
number, the same in every round:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
R=$(printf 'terminal,instrument,constellation,%.0s' 1 2 3 4 5 6 7 8 9 10)
$B --snapshot build/shots/cycle.png --page dev-theme --switch-to "${R%,}" 2> build/shots/cycle.txt
grep '^fonts loaded' build/shots/cycle.txt | sort | uniq -c
$B --snapshot build/shots/direct.png --page dev-theme --theme constellation
cmp build/shots/cycle.png build/shots/direct.png && echo same
```

`uniq -c` should print three lines: 11 for Constellation (the first draw
counts too, because the default theme is the starting theme) and 10 each
for the other two. The last two commands draw the last theme of the list
directly and compare the pictures.

On a device, `scripts/device.sh snapshot` takes `--switch-to` as well, and
the `fonts loaded` lines show up on your computer's standard error.
