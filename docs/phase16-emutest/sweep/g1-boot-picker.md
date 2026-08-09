# Sweep g1 — boot splash / resume prompt / game-select / (standalone settings)

Harness: `tools/emutest` (Azahar 2125.1.2). All evidence under
`tools/emutest/runs/sweep-g1-boot-picker/`. **Every claim below is from a PNG I captured**;
nothing here is inferred from source alone (source is cited only as a suspected cause).
Emulator stopped, fixtures cleaned, `sdmc/dual-gba` originals re-hashed untouched.

## What I drove (repro recipes, from the project root)

| run | movie / method | evidence dir |
|---|---|---|
| R1 splash | `azctl boot --gdb`; start `see rec` **before** `gdbio resume` | `rec-splash2/` (frames 48–64 are the splash) |
| R2 picker sweep | `movie_picker_sweep.json` → `picker_sweep.ctm`, `azctl boot --gdb --fresh-sd-fixtures --movie …` + `mkroms.py` (11 extra dummy ROMs so the list scrolls) | `rec-picker/` (4 fps) |
| R3 resume+settings attempt | `movie_resume_settings.json` (recent.bin hand-written to force the prompt) | `rec-settings/` |
| R4 tap probe | `movie_tap_probe.json` — Y, dead-zone tap, Y, settings-chip tap | `rec-tap/` (3 fps) |
| R5 ROM-less | `azctl clean-fixtures` + `azctl boot --gdb` + `gdbio resume` | `02-post-boot.*.png` |

Helper scripts I wrote for this sweep (kept with the evidence):
`mkroms.py` (dummy ROM fixtures + `clean`), `scanframes.py` (change-point scan of a
`see rec` series).

---

## F1 — CRITICAL — the selected segment of a segmented control prints "Aspect-fit" on top of its real label

**Pixels:** on the game-select bottom screen the highlighted (yellow) segment of the
1 Game / 2 Games control renders **two different strings stacked on the same baseline** —
the sprite's baked demo label "Aspect-fit" and the live label ("2 Games" / "1 Game") —
an unreadable black smear. The *unselected* segment is clean, so the one thing the control
must communicate (which mode is active) is exactly the thing that is destroyed. Present in
both modes and on every frame the picker is visible.

- Evidence: `rec-picker/bottom_00060.png` (2 Games selected), `rec-tap/bottom_00250.png`
  (1 Game selected); zooms `zoom-seg-garbled.png`, `zoom-tap-after-1game.png`.
- Repro: R2/R4, any frame after the picker appears.
- Suspected source: `assets_seg()` (source/assets.c:195) draws the `seg-active` sprite and
  then centres the option text over it; the shipped `seg-active.png`
  (`design_handoff_3dgba_ui/assets_3ds/widgets/indigo/seg-active.png`) is the design's
  *example* pill with "Aspect-fit" baked into the art. Every `assets_seg` caller inherits this.

## F2 — MAJOR — a clean tap anywhere on the picker's bottom screen is processed as a tap at (0,0): the "settings · ZR" chip does not open settings, it flips the mode to "1 Game"

**Pixels:** R4 taps the settings chip at (40,225). Before the tap the mode pill is on the
right ("2 Games", `rec-tap/bottom_00185.png`); after it the **settings screen never appears**
and the pill has jumped to the left ("1 Game", `rec-tap/bottom_00250.png`, still the picker
30 s later). The same movie's earlier dead-zone tap at (300,120) — a coordinate with no
control at all — likewise produced only a "1 Game" result, and in R2 a tap on the *right*
half of the segmented control ("2 Games") also produced "1 Game".
Contrast: on the resume prompt a tap at (160,110) works correctly (it selects "Pick new
games", `rec-settings/bottom_00070.png` → picker), so this is specific to the picker.

- Evidence: `rec-tap/bottom_00185.png` → `rec-tap/bottom_00250.png`, zooms
  `zoom-tap-before-2games.png`, `zoom-tap-after-1game.png`, `zoom-tap-after-chip.png`.
- Consequence seen: START / LINK A FRIEND / slot cards / settings chip are all untappable;
  every tap silently collapses the mode to 1 Game.
- Suspected source: `rompicker_run` acts on the touch **release** edge
  (`kUp & KEY_TOUCH`) but reads `hidTouchRead` on that same frame, when HID reports
  (0,0) — that lands in the `tp.py < 42` branch with `px < 160` ⇒ `gameMode = 1`.
  The resume prompt reads on the press edge (`k & KEY_TOUCH`) and is fine.

## F3 — MAJOR — the "settings · ZR" chip is painted over the plate's baked footer hint

**Pixels:** bottom-left of the game-select screen: the opaque chip covers the first ~13
characters of the baked hint, leaving "…ove ↑ · ⚡ linked = trade / battle-ready"
(2-game mode) and "…ull 3D + touch · link locally or online" (1-game mode). The chip's right
edge cuts a word in half; chip text and hint text sit on the same line and read as one
broken sentence.

- Evidence: `rec-picker/bottom_00060.png`, `rec-picker/bottom_00105.png`;
  zoom `zoom-settings-chip-overlap.png`.
- Design refs `03-game-select.png` / `game-select-single.png` and the plate art
  `select-dual-bot.png` show the hint unobstructed (the design has no chip there at all).

## F4 — MINOR — splash: the drawn "TAP TO START" label never composites; the button shows the sprite's baked "▶ START"

**Pixels:** the splash's yellow button reads "▶ START"; the design reference
(`01-boot-splash.png`) reads "▶ TAP TO START", and no second label appears anywhere on the
button — i.e. the code-drawn text is behind the button image (citro2d depth: the image is
drawn at depth 0.6, the text at 0.0).

- Evidence: `rec-splash2/bottom_00052.png`, zoom `zoom-splash-btn.png`.
- Same asset-family cause as F1 (`btn-primary.png` is a *labelled* example sprite). It is
  legible here, so this is cosmetic — but it is the same bug one step away from F1.

## F5 — MINOR — ROM-less start has no empty state at all

**Pixels:** with no `.gba` present, no picker and no "no games found" message is ever
shown; the app drops straight into a session of two black screens with a HUD reading
"gameA" / "gameB" · 59fps · 02:00. Nothing explains why the screens are black.

- Evidence: `02-post-boot.top.png`, `02-post-boot.bottom.png` (R5).

## F6 — POLISH — every ROM row draws the same cartridge chip; the per-game colour never appears

**Pixels:** all 8 list rows show an identical dark-purple cart chip with a green cap,
regardless of game. The design (`03-game-select.png`) shows per-title chip colours.

- Evidence: `rec-picker/top_00060.png`, zoom `zoom-list-chips.png`.
- Suspected source: `cart_tint(codes[gi])` is computed in `rompicker.c` but the row draws the
  untinted `cart-tinted` sprite via `assets_draw_wgt`.

---

## What looked CORRECT (checked, no defect found)

- **Boot splash top** (`rec-splash2/top_00052.png`): logo, "3DGBA" wordmark, "TWO CORES"
  divider, "NEW 3DS · mGBA · GPLv3" — clean, centred, matches `01-boot-splash.png`.
- **Resume prompt** (`rec-settings/top_00070.png`, `bottom_00070.png`): title, A/B cards
  with real game names, three buttons + the A/X/START hint row — matches
  `02-resume-prompt.png`; no truncation, no overlap, buttons legible. Touch works here.
- **ROM list rows** (`rec-picker/top_00060.png`): 8 rows on a clean 23.5 px grid, selected
  row highlight aligned, A/B badges land in the right-hand column, game codes right-aligned.
  A deliberately long name ("zz-emutest-Some Homebrew Demo (USA, Europe) (Rev 2) [!] long")
  renders in full without spilling past the plate or colliding with the code column.
- **Drag-scroll** (`rec-picker/top_00210.png`, `top_00240.png`): a bottom-screen drag does
  scroll the top list (rows 1–8 → rows 5–12), rows stay on the grid mid-scroll, no tearing,
  no half-drawn rows, header/footer stay put.
- **Slot cards / buttons on the picker bottom** (`rec-picker/bottom_00180.png`): A/B cards,
  9-slice borders, START / START—LINKED and START / LINK A FRIEND labels are centred and
  crisp; the disabled→ready colour change (dim → ink/accent) is clearly visible.

## NOT COVERED (blocked — flagged, not a defect claim)

The **standalone settings screen and its four tabs could not be reached** in this harness:
its only two routes are ZR (absent from the CTM record type — ZL/ZR are ir:rst, and movie
boots are pinned to Old 3DS) and the bottom-left chip tap, which F2 shows is broken.
Live keyboard injection is unavailable (osascript → System Events is not authorised on this
machine). Whoever covers the pause-menu tabs sees the same `PTABS` controls; the standalone
screen additionally draws its own top-screen header, which remains uninspected.
