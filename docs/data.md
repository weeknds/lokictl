<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# The data layer

The data layer collects the values the app shows and hands them to the
screen. A small background reader asks a *source* for values, and the home
page shows each new value while it is open, without being built again.

So far two values are real: the **clock** and the **battery**. Everything
about the network is still made up. This page explains where the values
come from, how they reach a page, how to add a new value, and how to test
all of it by script.

## Three sources

Where the values come from is called the source. There are three:

| Source | What it gives |
|---|---|
| `sample` | fixed made-up values that never change: the clock says `07:46` and the battery `91%` |
| `demo` | the same made-up values, moving on a fixed script (see [The demo script](#the-demo-script)) |
| `live` | real readings: the device's clock and battery; every other value is still the sample's |

Which one is used:

- **A snapshot** (`--snapshot`) always uses `sample`, on the Mac and on the
  device. So two pictures of the home page are still the same file, byte
  for byte.
- **A normal start and `--print-data`** use `live` on the device and `demo`
  in the Mac simulator, which has no battery to read.
- `--data <name>` picks the source in every mode, for example
  `--data live` in the simulator or `--data demo` on the device.

The default is chosen in one place, `default_data_source()` in
`src/app/app.cpp`.

## What is real today

With `live`:

- the clock is the device's local time as `HH:MM`;
- the battery charge and its state come from the system's battery files
  (see [The battery](#the-battery)).

The mode, the uplink, the access point, the addresses, the traffic and the
clients are still the sample's made-up values in every source. That is why
the page keeps its `sample data` mark: `HomeData::sample` stays true in all
three sources. A real clock next to the mark is fine; a made-up client
without it would not be. The mark goes away once the last made-up value on
the page is real.

The pages with a title bar, such as Settings, still read the clock and the
battery for their title bar on their own, as before. Only the home page
takes its values from the data layer for now.

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

- `sample` never wakes up again: its values never change.
- `demo` takes one step a second.
- `live` reads the clock just after each new minute starts (20 ms after
  it) and the battery every 10 s. The reader sleeps until whichever comes
  first. That is about seven wake-ups a minute, each a few small file
  reads, so it costs next to nothing. The battery files are read only when
  10 s have passed since the last battery reading.

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
  -> screen::HomeScreen           its observer is told
  -> screen::HomeLayout::update() the layout retexts its labels
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
page, and `update(context)` shows new values on it. `update()`:

- only changes the text of labels `build()` made. It never creates or
  deletes an object and never builds the page again (a build takes 26 to
  29 ms on the device);
- sets a text through `set_text_if_changed(label, shown, text)`
  (`src/view/screens/home_layout.h`), which sets it only when it differs
  from `shown`, the text the layout gave that label last time. So nothing
  is redrawn for a value that stayed the same. It compares with the
  layout's own copy, not with the label, because LVGL rewrites a label's
  text when it ends it in dots;
- uses the same text function as `build()`, so a page updated in place and
  a page built fresh with the same values look exactly the same.

Nothing moves when a value changes. The battery text is right-aligned and
grows to the left. In Constellation and Terminal the clock stays where it
is. In Instrument the clock and the battery are one right-aligned label
that ends 8 px from the right edge, so a longer battery text pushes the
clock to the left. `--%` is as wide as `91%`.

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

There is no colour change yet.

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

Say a later version shows the Wi-Fi signal for real. The steps:

1. **Model:** add a field to `model::HomeData` in `src/model/home_data.h`,
   with its unit in the comment, and add it to `operator==` in
   `src/model/home_data.cpp`. That operator decides whether a block is new,
   so a field missing there never reaches the page.
2. **Sample:** give it a made-up value in `model::sample_home_data()`. That
   function is the only place where made-up values are written; nothing
   under `src/view` or `src/app` writes one.
3. **Live reader:** write a small reader in `src/platform`, like
   `battery.*`. It takes the folder to read from, never throws, and never
   uses LVGL, the view model or the log. Call it from
   `platform::LiveDataSource::read()`. If the value changes on its own
   schedule, take that into account in `next_read_delay()`.
4. **Demo:** if the value should move in the demo, add it to the demo
   script in `src/platform/data_source.cpp`, starting from the sample's
   value.
5. **Text:** add a helper to `src/view/screens/home_format.h`, so the
   value reads the same in every theme.
6. **Layouts:** in each layout, `build()` makes the label and keeps a
   pointer to it and the text it shows; `update()` sets the new text with
   `set_text_if_changed()`. Use one text function for both.
7. **Readout:** add a `name=value` line to `--print-data` in
   `src/app/data_readout.cpp`, after the five lines that are there.
8. **Test folder:** make a small folder that holds the files the reader
   reads, and check the reading with `LOKICTL_DATA_ROOT` and
   `--print-data`.

If the value needs a log line, write it in `app::DataController`, never in
the reader.

## Test hooks

None of these changes a setting.

### `--data <sample|demo|live>`

Picks the source in every mode. A wrong name stops the app with exit code
2 and a message that names the three.

```sh
B=./build/darwin-arm64/Debug/lokictl
$B --data sample                 # a window whose values never change
$B --snapshot build/shots/home-live.png --data live
```

### `--data-steps <n>`

Only with `--snapshot` or `--print-data`: takes `n` further readings (0 to
100000, default 0) right after the first one. These runs have no
background thread. Each step asks the source, hands the values over the
same way and, in a snapshot, updates the page, one after the other, so
nothing has to be waited for and the same command always gives the same
picture. With `demo` each step is one step of the script; with `live` a
step reads the clock again, and the battery only once 10 s have passed.

```sh
B=./build/darwin-arm64/Debug/lokictl
mkdir -p build/shots
$B --snapshot build/shots/home-181.png --page home --data demo --data-steps 181
```

That picture shows `10:47` and `100%+`.

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

### `--print-data`

No screen and no keys. It takes the first reading (and the `--data-steps`
further ones) and prints one `name=value` line per value on standard
output, then exits with code 0. It uses the same default source as a
normal start (`live` on the device, `demo` in the simulator), never reads
or writes the settings file and never touches the screen. It takes only
`--data` and `--data-steps`; any other option with it stops the app with
exit code 2.

```sh
$ ./build/darwin-arm64/Debug/lokictl --print-data --data sample
source=sample
sample=yes
clock=07:46
battery_percent=91
battery_state=discharging
```

The first five lines always come in this order. `battery_percent` is a
number from 0 to 100 or `unknown`, and `battery_state` is `charging`,
`discharging`, `idle` or `unknown`. Lines for more values may follow
later.

### `LOKICTL_DATA_ROOT`

With `LOKICTL_DATA_ROOT=<folder>`, the `live` source reads
`<folder>/sys/class/power_supply/...` in place of
`/sys/class/power_supply/...`. That makes every battery state testable on
the Mac. It only matters for `live`.

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

### On the device

```sh
scripts/device.sh data                    # the device's own values, live
scripts/device.sh data --data sample      # the sample lines
scripts/device.sh run --data demo         # the demo on the real screen
scripts/device.sh snapshot build/shots/device-181.png --data demo --data-steps 181
```

`scripts/device.sh data` runs `lokictl --print-data` on the device and
prints its lines. It works while the launcher is on screen and leaves the
screen alone. See the README for the other commands.

## Log lines

In a normal run the data layer writes these lines:

- `data: <source> (first reading in <n> ms)` once at startup, before the
  first page, for example `data: live (first reading in 3 ms)`;
- `data: battery 91% discharging` for the first reading, and then only
  when the charge or the state changes. `data: battery --% discharging`
  when only the charge is unknown, and `data: battery unknown` when neither
  is known. There is no line per reading and none per minute;
- `data: reader stopped` once on the way out.

A snapshot logs only warnings, so these lines do not appear there, and
`--print-data` writes nothing but its `name=value` lines.

## Stopping

On quit (holding ESC, closing the simulator window) and on SIGTERM or
SIGINT, the main loop ends. Right after it, before anything else is torn
down, `app::DataController::stop()` deletes its timer, wakes the reader and
waits for it to end, and the log says `data: reader stopped`. The reader
wakes at once, also from a long sleep, so the app never hangs on the way
out. Then the fonts are released and the log ends with `LokiCTL stopped`.
