<!--
SPDX-FileCopyrightText: 2026 Netdeck contributors
SPDX-License-Identifier: MIT
-->

# Fonts

Netdeck ships its own text fonts so the three themes look the same in the
desktop simulator and on the device. This page lists what is bundled, where it
comes from, and which special characters (arrows, dots, line pieces and
braille) each font can actually draw.

## Bundled fonts

All text fonts live in `assets/fonts/` and are installed to
`/usr/share/netdeck/fonts/` by the Debian package. They are the unmodified
static TTF files from each project's official release: nothing was subset,
renamed or re-saved. Only the two weights each theme needs are included.

| Family | Weights | Files | Size in bytes | Comes from | Release tag | Font version | License |
|---|---|---|---|---|---|---|---|
| Space Grotesk | Regular, Bold | `SpaceGrotesk-Regular.ttf`<br>`SpaceGrotesk-Bold.ttf` | 114,428<br>116,056 | https://github.com/floriankarsten/space-grotesk | `2.0.0` | 2.000 | SIL OFL 1.1, `SpaceGrotesk-OFL.txt` |
| IBM Plex Mono | Regular, SemiBold | `IBMPlexMono-Regular.ttf`<br>`IBMPlexMono-SemiBold.ttf` | 173,052<br>174,608 | https://github.com/IBM/plex | `@ibm/plex-mono@2.5.0` | 2.005 | SIL OFL 1.1, `IBMPlexMono-OFL.txt` |
| Geist Mono | Regular, Bold | `GeistMono-Regular.ttf`<br>`GeistMono-Bold.ttf` | 148,516<br>150,492 | https://github.com/vercel/geist-font | `v1.9.2` | 1.900 | SIL OFL 1.1, `GeistMono-OFL.txt` |
| JetBrains Mono | Regular, Bold | `JetBrainsMono-Regular.ttf`<br>`JetBrainsMono-Bold.ttf` | 273,900<br>277,828 | https://github.com/JetBrains/JetBrainsMono | `v2.304` | 2.304 | SIL OFL 1.1, `JetBrainsMono-OFL.txt` |
| Inter | Regular, SemiBold | `Inter-Regular.ttf`<br>`Inter-SemiBold.ttf` | 411,640<br>419,744 | https://github.com/rsms/inter | `v4.1` | 4.001 | SIL OFL 1.1, `Inter-OFL.txt` |

The release tag is the name of the upstream release the files were taken from.
The font version is the version written inside the font files themselves. For
IBM Plex Mono and Geist Mono the two numbers differ, which is normal for those
projects.

Total added size: **2,260,264 bytes (about 2.26 MB)** for the ten font files,
plus 22,114 bytes (about 22 KB) for the five license texts. Inter is the
largest family at 831,384 bytes for its two files.

Each family's full license text sits next to its fonts as `<Family>-OFL.txt`,
copied unchanged from the same release. `assets/fonts/LICENSE.txt` lists every
bundled font with its source, release and license, and it also covers the
icon fonts that were already there: the Phosphor icon font
(`Phosphor-Fill.ttf`) and the two Kenney keyboard fonts. The package installs
that file as `/usr/share/doc/netdeck/third-party-assets-license.txt`.

### Which theme uses which font

| Theme | Fonts |
|---|---|
| Instrument | Space Grotesk for the big words and numbers, IBM Plex Mono for everything else |
| Constellation | Geist Mono |
| Terminal | JetBrains Mono |
| General interface text | Inter |

JetBrains Mono is also installed on the device as a system font. We bundle it
anyway, so the simulator and the device use exactly the same file and render
the same way.

## Symbol coverage

The themes use a few special characters. This table shows which fonts contain
them. "yes" means the font has a drawing for that character, "no" means it does
not. Both weights of every family were checked separately and gave identical
results, so each family has one row.

| Family | Braille U+2800–U+28FF | ↑ U+2191 | ↓ U+2193 | ← U+2190 | → U+2192 | ● U+25CF | › U+203A | · U+00B7 | − U+2212 | ✓ U+2713 | ✕ U+2715 | ─ U+2500 | │ U+2502 | ┌ U+250C | ┐ U+2510 | └ U+2514 | ┘ U+2518 | ├ U+251C | ┤ U+2524 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Space Grotesk | 0/256 | yes | yes | yes | yes | no | yes | yes | yes | no | no | no | no | no | no | no | no | no | no |
| IBM Plex Mono | 0/256 | yes | yes | yes | yes | no | yes | yes | yes | yes | no | yes | yes | yes | yes | yes | yes | yes | yes |
| Geist Mono | 0/256 | yes | yes | yes | yes | yes | yes | yes | yes | no | no | yes | yes | yes | yes | yes | yes | yes | yes |
| JetBrains Mono | 0/256 | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes | yes |
| Inter | 0/256 | yes | yes | yes | yes | yes | yes | yes | yes | yes | no | no | no | no | no | no | no | no | no |

The characters, in plain words:

- Arrows: ↑ up, ↓ down, ← left, → right.
- Marks: ● filled dot, › small right-pointing angle, · middle dot, − minus
  sign (wider than a hyphen), ✓ check mark, ✕ multiplication X.
- Box drawing: ─ horizontal line, │ vertical line, ┌ ┐ └ ┘ corners, ├ ┤ T
  junctions.
- Braille: the 256 braille patterns, which terminal programs often use to draw
  small graphs out of dots.

The same results, counted per group:

| Family | Arrows | Marks | Box drawing | Braille | Missing |
|---|---|---|---|---|---|
| Space Grotesk | 4/4 | 3/6 | 0/8 | 0/256 | ● ✓ ✕, all box drawing, all braille |
| IBM Plex Mono | 4/4 | 4/6 | 8/8 | 0/256 | ● ✕, all braille |
| Geist Mono | 4/4 | 4/6 | 8/8 | 0/256 | ✓ ✕, all braille |
| JetBrains Mono | 4/4 | 6/6 | 8/8 | 0/256 | all braille |
| Inter | 4/4 | 5/6 | 0/8 | 0/256 | ✕, all box drawing, all braille |

## What this means

- **No bundled font contains a single braille character.** The Terminal theme
  mockup draws traffic graphs with braille dots, but none of the five families
  can show them. Graphs must therefore be drawn with LVGL shapes (lines, bars or
  a canvas) instead of braille text, in every theme.
- **Safe in every font:** the four arrows ↑ ↓ ← →, the angle › (U+203A), the
  middle dot · (U+00B7) and the minus sign − (U+2212). These can be used as
  plain text anywhere.
- **Missing in some fonts:**
  - ✕ (U+2715) is missing everywhere except JetBrains Mono.
  - ● (U+25CF) is missing in Space Grotesk and IBM Plex Mono.
  - ✓ (U+2713) is missing in Space Grotesk and Geist Mono.
  - The box-drawing lines are missing in Space Grotesk and Inter. The three
    monospaced families (IBM Plex Mono, Geist Mono, JetBrains Mono) have all
    eight.
- **JetBrains Mono is the only family with every non-braille symbol** on the
  list.
- **What a missing character looks like:** the rest of the text is still
  drawn, but in place of the missing character the font draws its own
  "missing character" sign: an empty rectangle in IBM Plex Mono and Geist
  Mono, a rectangle with a slanted line in JetBrains Mono, a box with a
  question mark in Space Grotesk, and a narrow solid bar in Inter. The font
  does not borrow the character from another font on its own.
- **Ways to fill the gaps** (to be decided in the theme work, not here):
  - use an icon from the bundled Phosphor icon font;
  - draw dots, check marks and lines as LVGL shapes;
  - give the font an LVGL fallback font that has the missing characters.
- **No kern table:** none of the ten files has the old-style `kern` table.
  Kerning means small spacing fixes between letter pairs such as "AV". The
  three monospaced families have no kerning at all, which is normal for
  monospaced fonts. Space Grotesk and Inter do have kerning, but only in the
  newer `GPOS` table. LVGL asks FreeType for kerning, and the FreeType we
  measured with reported no kerning for any of the ten files, so text should
  be expected to draw without kerning. Whether a given FreeType build reads
  `GPOS` kerning depends on how it was compiled, so this is worth a look on
  the device.
- **A possible later saving:** the same JetBrains Mono release also contains
  "JetBrains Mono NL" files, a variant without programming ligatures. The two
  NL files are 132,164 bytes smaller in total. LVGL never applies ligatures, so
  they would look the same here. They are not used for now.

## How this was measured

Each font's character map was read with FreeType from the exact files in
`assets/fonts/` on 2026-11-21. For every code point listed above, and for all
256 code points from U+2800 to U+28FF, `FT_Get_Char_Index` was asked for the
glyph; a result of 0 means the font has no drawing for that character and is
shown as "no". The same check confirmed that all ten files are static fonts
(not variable fonts) and that none has a `kern` table. The font versions were
read from each file's name table, and the kerning notes come from the list of
features in each file's `GPOS` table.

## Looking at the fonts

The app has six developer pages that draw the bundled fonts through LVGL,
the same way the real screens will. They exist only for checking the fonts:
they can be drawn into a PNG with snapshot mode, but nothing in the app links
to them.

| Page | What it shows |
|---|---|
| `dev-fonts` | All ten font files, one line each at 12 px, every line drawn in the font it names |
| `dev-fonts-space-grotesk` | Space Grotesk in detail |
| `dev-fonts-ibm-plex-mono` | IBM Plex Mono in detail |
| `dev-fonts-geist-mono` | Geist Mono in detail |
| `dev-fonts-jetbrains-mono` | JetBrains Mono in detail |
| `dev-fonts-inter` | Inter in detail |

Each family page has the same lines, so the pictures can be compared with
each other and with the tables above:

1. the regular weight at 12 px, with the digits and "Ag";
2. the heavier weight (Bold or SemiBold) at 12 px;
3. the four arrows and the six marks;
4. the eight box-drawing pieces and six sample braille characters;
5. a count line such as `arrows 4/4 marks 6/6 box 8/8 braille 0/256`;
6. an address at 28 px in the regular weight;
7. traffic numbers with arrows, a dot, a check mark and an X at 28 px in the
   heavier weight.

The count line is computed by the app when the page is drawn. It asks LVGL
for every character in the groups above and for all 256 braille characters,
using the same lookup LVGL uses to draw text, so it is an independent check
of the coverage table. For the files listed here it matches the table
exactly. A character the font does not have shows up as the font's "missing
character" sign described above.

To draw the pages, build the simulator and run, from the repository root:

```sh
./build/darwin-arm64/Debug/netdeck --snapshot build/shots/fonts.png --page dev-fonts
./build/darwin-arm64/Debug/netdeck --snapshot build/shots/fonts-terminal.png --page dev-fonts-jetbrains-mono --theme dark
```

If a font file cannot be found or loaded, its line says `MISSING` followed by
the file name in red, instead of quietly using another font.
