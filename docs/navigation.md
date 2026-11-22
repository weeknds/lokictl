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
demo-home -> demo-counter -> demo-keys
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
page: demo-home (start, depth 1)
page: demo-counter (opened, depth 2)
page: demo-keys (opened, depth 3)
page: demo-counter (back, depth 2)
page: demo-home (back, depth 1)
```

`start` is the first page after the app starts, `opened` means the path got
longer and `back` means it got shorter. A fourth word, `shown`, means a
different page at the same depth; only `show_page()` can cause that. A theme
switch builds the same page again and writes no `page:` line.

### Keep a page's state in the view model

A page's screen is thrown away when the user leaves the page and when the
theme changes. So everything a page shows must live in the view model, not
in the screen. The counter value lives there, and so do the selection, the
picked row, the last key and the typing switch of the key test page. When
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

On the demo home page, key 4 (`EXIT`) works like ESC on the first page: a
short press shows the message and holding it for 700 ms quits. This goes
away when the demo home page is replaced.

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
5. **Key 4 on the demo home page** is that page's hold-to-exit key.
6. **Keys 4 to 8** press the page's soft keys (not while typing). An empty
   or switched-off key does nothing, and the page never gets the digit.
7. **F, X, Z and C** become up, down, left and right (not while typing).
8. **The page** gets the key, through its `on_key()`.

fn+H and fn+J work on every page, also while typing. Never give them
another job. Holding TAB is reserved as well; the app does not use TAB yet.

**A held key acts once.** The keyboard layer reports a key once when it
goes down, and once more if it is still held after 700 ms. Only a held ESC,
or the home page's key 4, acts on that second report. For every other key
it is ignored, so holding a soft key or an arrow does its job one time.
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
handled yet. Only the key test page has typing mode, to show how the switch
works. A real text field comes later, with the command bar.

## How a page takes part

A page is a class derived from `screen::BaseScreen`. It can override these
functions. Each one has a default that does nothing:

| Function | When it is called | What it returns |
|---|---|---|
| `soft_keys()` | once, while the page is built | the five soft keys 4 to 8 (see [Soft keys](themes.md#soft-keys)) |
| `on_key(key)` | a key for the page, after steps 1 to 7 | true when the page used the key |
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

## The demo pages

| Page | Keys |
|---|---|
| `demo-home` | `4` EXIT (hold to quit, like ESC), `5` SIZE, `6` SETUP opens the Settings page, `7` INFO, `8` COUNT opens the counter page |
| `demo-counter` | `4` BACK or a short ESC goes back, `5` LESS (or `z` or left) counts down, `7` MORE (or `c` or right) counts up, `8` KEYS opens the key test page |
| `demo-keys` | `4` BACK, `5` TYPE turns typing mode on, `f`/`x` or up/down move the selection and stop at the first and last row, `Enter` picks the selected row; a box shows the last key, the picked row and whether typing is on |

## The Settings page

`settings` is the app's first real page. Key 6 (`SETUP`) on the home page
opens it. For now it holds only the theme picker: one row per theme, with
the theme's name, four small squares in its colours and a tick on the theme
that is active.

| Key | What it does |
|---|---|
| `f`/`x` or up/down | move the highlight; it stops at the first and the last row |
| `Enter` or `6` USE | switches to the highlighted theme and saves it; `USE` is switched off while the highlight is on the active theme, and picking that theme does nothing |
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
   `dev-` are developer pages, which nothing in the app opens.
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
$B --snapshot build/shots/keys-x.png --page demo-home --keys 8,8,x
$B --snapshot build/shots/keys-down.png --page demo-home --keys 8,8,down
cmp build/shots/keys-x.png build/shots/keys-down.png && echo same
```

Both pictures show the key test page with the second row selected, so `cmp`
prints `same`. The demo pages show a clock in the title bar: if the minute
changed between the two pictures, take both again.

How it works:

- The keys are pressed one after another, after the page has been drawn and
  before any `--switch-to`. Each one goes through the same path as a real
  key press (`platform::inject_key()` and `app::KeyController`).
- Names: `0` to `9`, `a` to `z`, `esc`, `enter`, `space`, `backspace`, `up`,
  `down`, `left`, `right` and `help` (fn+H). Capitals are accepted, but a
  letter always arrives in lowercase, as the device's keyboard sends it.
- A key is held for 60 ms of LVGL time. Add `:long` to hold it for 1 second
  instead, for example `esc:long`. That is longer than the 700 ms long
  press.
- After every key the app runs for `--settle-ms` (300 ms by default), so a
  page change has happened before the next key. The program does not
  really wait that long.
- The page given with `--page` is the first page of the path, with nothing
  below it. To test going back, start at `demo-home`: with
  `--page demo-counter --keys esc` the app stays on the counter page,
  because there it is the first page.
- A theme picked on the Settings page, for example with
  `--page demo-home --keys 6,x,enter`, really switches the theme. As in
  every snapshot, nothing is saved.
- If the keys made the app ask to quit, for example with a held ESC on the
  first page, the line `quit requested` goes to standard error. The picture
  is still taken and the exit code stays 0.
- The picture shows the page only. The help panel and the messages, such as
  `Hold ESC to exit`, float above the page and are not in it. So
  `--keys 8,help` gives the same picture as `--keys 8`.
- `--keys` only works together with `--snapshot`. Without it, or with an
  unknown name, an empty name or an unknown suffix, the app stops with exit
  code 2 and a message.

More examples:

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
# The key test page in typing mode, after typing f, in the Terminal theme.
$B --snapshot build/shots/keys-typing.png --page demo-home --keys 8,8,5,f --theme terminal
# A held ESC on the first page asks to quit: standard error says "quit requested".
$B --snapshot build/shots/keys-quit.png --keys esc:long
# Pick the second row, then switch to Instrument: the page keeps its state.
$B --snapshot build/shots/keys-switched.png --page demo-home --keys 8,8,x,enter --switch-to instrument
# From the default theme, Constellation: open Settings, move the highlight to
# Terminal and pick it. The Settings page is now drawn in Terminal.
$B --snapshot build/shots/keys-settings.png --page demo-home --keys 6,x,enter
```

### On the device

`scripts/device.sh snapshot` passes `--keys` on to LokiCTL on the device.
The keys are pressed inside that snapshot run, not on the device's
keyboard, so LokiCTL does not need to be running and the screen does not
change:

```sh
scripts/device.sh snapshot build/shots/device-keys.png --page demo-home --keys 8,8,x
```

To press keys on the real keyboard while LokiCTL runs on the screen, use
`scripts/device.sh key` (see the README). Its names differ a little: it
also knows `tab`, `screenshot` and the `fn+` combinations, and it holds a
key with `--long` instead of `:long`.

```sh
scripts/device.sh run
scripts/device.sh key 8
scripts/device.sh key 8
scripts/device.sh key x
scripts/device.sh screen build/shots/device-keys-live.png
scripts/device.sh logs -n 20
scripts/device.sh stop
```

The log then shows the `page:` lines of the path, and the saved picture
shows the key test page with the second row selected.
