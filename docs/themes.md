<!--
SPDX-FileCopyrightText: 2026 Netdeck contributors
SPDX-License-Identifier: MIT
-->

# Themes

Netdeck has three looks, called themes: **Instrument**, **Constellation** and
**Terminal**. This page explains what a theme is made of, how a screen gets
its look, how the app picks a theme, and how to add a new one.

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
  `popup`, `text`, `caption`, `card`, `list_row` and `pane`. They already
  follow the flags, so a list row is filled or outlined and a pane's title
  sits in the border or inside the box without the screen having to check.
- **Never hard-code a colour or a text font.** No colour values and no
  `lv_color_hex()` outside `src/view/theme.cpp`, and no text font files
  loaded by a screen. Take colours from `theme.colors` and fonts from
  `theme.font(role)`.
- **Text is never smaller than 12 px.** Use the roles; they are all 12 px or
  more. If something does not fit, shorten the text or change the layout.
- **Screens do not watch for theme changes.** When the theme changes, the
  screen is built again from scratch (see below).
- **Icon fonts are not theme fonts.** The Phosphor icons and the key-cap
  fonts are loaded by the widgets that use them, with
  `AssetManager::load_font()`. They take their colour from the theme like
  any other text.

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
   start it is used for that run only and nothing is saved. Any other name
   stops the app at once with a message that lists the three names.
2. **The saved setting**, the key `dark_mode` in the `[application]` section
   of the settings file: `yes` means Terminal, `no` means Instrument. Key 6
   on the home page switches this setting between the two. The running
   theme stays as it is, and a message says which theme the next start will
   use ("Next start: Terminal theme"). Until the theme choice is saved by
   name, Constellation can only be picked with `--theme`.
3. **The default**, `view::kDefaultThemeId` in `src/view/theme.h`, which is
   Terminal. That one line is the only place to change the default; the
   help text, snapshots and a first start all follow it.

Snapshot mode never reads the settings file, so without `--theme` a
snapshot always uses the default theme. The settings file is
`config/netdeck.conf` in the simulator. On the device it is
`/etc/netdeck.conf`, with your own changes saved to
`~/.config/netdeck/netdeck.conf`. The shipped file has no `dark_mode` line,
so a fresh install starts in the default theme.

The log says which theme was picked and why, for example
`theme: terminal (from the default)`.

## How switching while the app runs will work

Switching themes without a restart is planned for a later version. The
engine is already built for it:

1. `ThemeEngine::set_theme(id)` loads the new theme's fonts, makes it the
   active theme and notifies `ThemeEngine::subject()`. The old fonts stay
   loaded for now, because the screen still uses them.
2. The current screen is built again from scratch with the new theme. A
   rebuild is needed anyway, because some flags change which objects exist
   (a pane title in the border is a different object from a title inside
   the box). The rebuild must not happen inside an observer callback; it is
   done afterwards, the same way the screen manager already switches pages.
3. `ThemeEngine::release_retired_fonts()` then frees the old theme's fonts.

The toast message and the help panel live on LVGL's top layer and outlive
every screen. They observe `ThemeEngine::subject()` and restyle themselves,
fonts included, when it changes.

Objects are created and destroyed in a fixed order, so no label ever points
at a font that has been freed: LVGL, then the asset manager, then the theme
engine, then the display and the screens; on exit the same in reverse.

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
4. Nothing else is needed for the command line: `--theme` and `--help` take
   the names from the table.
5. Snapshot every page in the new theme (see below), look at each picture,
   and add the theme to the tables on this page.

## Looking at the result

Snapshot mode draws a page into a 320x170 PNG without opening a window. These
pages exist for checking themes; they can only be drawn this way:

| Page | What it shows |
|---|---|
| `dev-theme` | all 15 colours with their names, every font role with the symbols ↑↓●✓✕›, a card, and a pane with a list in which one row is selected |
| `dev-help` | the keyboard help panel of the home page |
| `dev-toast` | three messages: success, warning and a normal one |

From the repository root, after building the simulator:

```sh
B=./build/darwin-arm64/Debug/netdeck
$B --snapshot build/shots/theme-terminal.png --page dev-theme --theme terminal
$B --snapshot build/shots/help-instrument.png --page dev-help --theme instrument
$B --snapshot build/shots/toast-constellation.png --page dev-toast --theme constellation
for t in instrument constellation terminal; do
  $B --snapshot "build/shots/home-$t.png" --page demo-home --theme "$t"
done
```

To start the simulator in one theme for a single run:

```sh
./build/darwin-arm64/Debug/netdeck --theme constellation
```

On a device, `scripts/device.sh snapshot <file.png> --page dev-theme --theme
constellation` draws the same page with the device's own font rendering, and
`scripts/device.sh run --theme constellation` shows the app on the real
screen (see the README).

A picture shows the exact pixels, but not how readable they are on the small
panel. Faint lines such as Constellation's dark-blue borders, and Instrument's
orange text on the paper ground, are worth a look on the device itself.
