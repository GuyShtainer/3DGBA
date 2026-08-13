# phase18-crisp — INDEPENDENT VERIFICATION

Re-verification of the three phase-18 slices (crisp / door / co-op) against the user's three
hardware complaints. **Nothing in the slice BUILDLOGs is taken on trust** — every claim below is
re-derived from the shipped bytes, my own state reads, or my own captures.

Evidence: `tools/emutest/runs/p18-verify/`.
Binary under test: `3DGBA.3dsx` 5,418,228 B (2026-08-13 01:05), `make -j8` reports up to date
against the current working tree.

---

## V0 — static audit of the text path (no emulator needed)

### V0.1 the ladder itself checks out

I re-parsed the seven shipped `data/fnt_*.bin` CFNT headers myself (FINF at the `FINF` tag,
`lineFeed` at `+9`, TGLP `cellWidth/cellHeight` at `+8/+9`):

| file | lineFeed | cell | role px (typography.h) | px == lineFeed |
|---|---|---|---|---|
| `fnt_sg_bold_17.bin` | 17 | 18×19 | TXT_TITLE 17 | ✅ |
| `fnt_sg_bold_12.bin` | 12 | 13×15 | TXT_BUTTON 12 | ✅ |
| `fnt_sg_med_12.bin` | 12 | 13×14 | TXT_BODY 12 | ✅ |
| `fnt_sg_med_10.bin` | 10 | 11×12 | TXT_SEG 10 | ✅ |
| `fnt_jbm_med_9.bin` | 9 | 8×13 | TXT_SECTION 9 | ✅ |
| `fnt_jbm_med_7.bin` | 7 | 6×9 | TXT_CHIP 7 | ✅ |
| `fnt_jbm_bold_11.bin` | 11 | 9×14 | TXT_VALUE 11 | ✅ |

So for **text that goes through `assets_text`**, the crispness law (`px == lineFeed`) holds in the
bytes that shipped. That part of the slice is real.

### V0.2 — but `assets_text` is not the only text path, and the spec's premise for that is wrong

`SPEC-crisp` **C3.4** says: *"The fallback path (`ui_text*`, the system font, **used only when the
asset pack fails to load**) stays LINEAR … Documented degradation, not a regression."*

That premise is false. `ui_text*` and raw `C2D_TextParse` + `C2D_DrawText` (system font, arbitrary
fractional scale, LINEAR filter) are still on **unconditional** live paths — see V1 for the
per-site list and the on-screen measurement.

---

## V-H1 — HARNESS: `see` was never broken. The **display was asleep**.

Three phase-18 slices recorded "`see` returns pure black" as a reproducible environmental
defect (SPEC-crisp **Q1**, six reproductions) and shipped without pixel evidence because of it.

It is not an Azahar bug and not a TCC problem:

```
tools/emutest/.venv/bin/python -c "import Quartz as q; d=q.CGMainDisplayID();
    print(q.CGDisplayIsAsleep(d), q.CGDisplayIsActive(d))"
  -> asleep 1  active 0          # every capture is black, whatever you point it at
caffeinate -u -t 2
  -> asleep 0  active 1
tools/emutest/run see shot both ...
  -> top 2567 distinct colours, bottom 1728      # the UI, in full
```

`screencapture` of the **whole display** also returned a 1-colour image while asleep, which is
the cheap one-line check nobody ran. Every slice that hit this ran late at night on an idle
machine. **`nohup caffeinate -u -t <secs> &` before a capture session is the whole fix**;
`see.py` should assert `CGDisplayIsAsleep == 0` and exit 75 (SKIP) instead of writing a black
PNG that looks like a capture.

This matters beyond hygiene: the "no screen-pixel evidence was available, through no fault of
this slice" caveat in the crisp and co-op BUILDLOGs is void, and everything below is measured on
real framebuffers.

### V-H2 — a byte-exact framebuffer channel, for free

`fbdump.py` (framebuffer over GDB) was abandoned as unusable. It is not needed. Pinning
`Layout/filter_mode=false` (the NEAREST screen filter; the harness only pins
`use_integer_scaling=false`) makes Azahar's window upscale a **verbatim pixel copy**, so the
3DS framebuffer can be recovered from an ordinary `see shot`:

* `tools/emutest/runs/p18-verify/native.py` samples one device pixel per source pixel;
* proof it is exact: reconstructing at sample offsets 0.3, 0.5 and 0.7 of the cell gives
  **0 differing pixels**, and the reconstruction contains **exactly the same 609 colours** as
  the 900×540 capture. Nearest never invents a colour, so the 400×240 image *is* the
  framebuffer.

All measurements below are on these reconstructions. Tools: `native.py`, `crisp.py`, `segs.py`,
`rows.py` in the evidence dir.

---

## V1 — TEXT: the ladder is real, but the user's most-visible text is still soft

### V1.1 the measure

A bcfnt sheet is **A4** — 4-bit alpha, so the only alphas in the file are the 16 multiples of
17. A run drawn `C2D_WithColor` in one ink over one background can therefore contain **at most
16 distinct colours** if and only if every screen pixel samples exactly one texel (texel scale
1.0, integer origin). Counting distinct colours in a single-ink run is thus a *binary* test for
"resampled", with no model and no tolerance. `segs.py` splits a band into single-ink groups so
the count means something.

### V1.2 the ladder path: PROVEN 1:1 on the real framebuffer

`g_txtTexelScale` / `g_txtLineFeed` read live over GDB out of the running app:

```
0x00627398  00 00 80 3f 00 00 80 3f 00 00 80 3f 00 00 80 3f
0x006273a8  00 00 80 3f 00 00 80 3f 00 00 80 3f       -> 1.0f x7
0x0062737c  11 00 00 00 0c 00 00 00 0c 00 00 00 0a 00 00 00
0x0062738c  09 00 00 00 07 00 00 00 0b 00 00 00       -> 17 12 12 10 9 7 11
```

and on screen (`sweep1/f13.top.nat.png`, in-game; `px2.top.native.png`, boot):

| run | role | distinct colours | verdict |
|---|---|---|---|
| "Pokemon FireRed" (picker card) | TXT_BODY | **16**, OFF=0 | 1:1 |
| "Pokemon Emerald" (picker card) | TXT_BODY | 17, OFF=1 | 1:1 |
| "●FOCUS" chip | TXT_CHIP | **10** | 1:1 |
| "29fps" | TXT_SECTION | **14** | 1:1 |
| "02:01" | TXT_SECTION | **14** | 1:1 |
| "3D" badge | TXT_CHIP | **10** | 1:1 |

So the S1 slice's central claim holds, and now it holds against the framebuffer rather than
against a Python re-implementation of the sampler.

### V1.3 the defect that remains — see finding F1

Same HUD bar, same 12 px band, same capture (`segs.py sweep1/f13.top.nat.png 1 13`):

```
band y=1..13  bg=(6, 5, 10)
  SOFT x= 16.. 59  "Pokemon"  distinct= 82        <- system font, C2D_DrawText scale 0.4
  SOFT x= 65..101  "Emerald"  distinct= 73
  1:1  x=109..138  "●FOCUS"   distinct= 10        <- assets_text TXT_CHIP
  1:1  x=328..347  "29fps"    distinct= 14
  1:1  x=355..374  "02:01"    distinct= 14
```

and identically on the bottom screen for "Pokemon FireRed" (82 / 71 distinct).

`hudMode` defaults to **3** (both screens, `main.c:2971`), so this is the text that is on
screen for the entire session, on both screens, in every theme. It is drawn by
`C2D_DrawText(&tHudTop, …, 0.4f, 0.4f, …)` (`main.c:4374`, `:4585`) against citro2d's shared
system font — never converted to the ladder.

### V1.4 — the finding that matters most: **scale 1.0 did not make the text sharp** (F2)

The distinct-colour test proves "unresampled". It does **not** prove "sharp", and the two came
apart here. `ramp.py` measures the alpha histogram instead — `solid` = share of lit pixels at
≥75 % ink, `mid` = share stranded between 25 % and 75 %, `ramp` = mean length of a mid-tone run
across an edge. That is comparable across A4 glyphs, 8-bit baked art and the system font:

```
HUD 'Pokemon'   SYSTEM font 0.4x   solid= 32.6%  mid= 37.2%  ramp=1.67px
HUD '29fps'     TXT_SECTION 1:1    solid= 27.8%  mid= 54.2%  ramp=1.73px
'Pokemon FireRed' TXT_BODY 1:1     solid= 17.2%  mid= 70.0%  ramp=2.39px   <- softest on screen
'Aspect-fit'    TXT_SEG 1:1        solid= 30.1%  mid= 44.5%  ramp=1.85px
boot title      BAKED PLATE        solid= 46.5%  mid= 42.7%  ramp=1.35px   <- sharpest on screen
pause 'SCALE'   BAKED PLATE        solid= 32.7%  mid= 53.8%  ramp=1.67px
```

The ladder text is the **softest text on the screen** — softer than the system font it was
supposed to beat, and much softer than the baked plate art next to it. The cause is in the bake,
not in the draw. Alpha maps, framebuffer on the left, the shipped `.bcfnt`'s own texels on the
right (they agree exactly, which validates both instruments):

```
TXT_BODY "Pokem" on screen           data/fnt_sg_med_12.bin glyph 'P'
   .++=.       .-                       .++=.
   -#==@.      :*                       -#-=@.
   -#  #- :**. :* =- :#+                -#  #-
   -%==@ :%:-% :*=# :#:=#:               -%-=@
   -%==. ==  #::@%  =%##@                -%=-.
   -#    =*  @.:##= =*                   -#
   -#     *%%= :* #= ##%=                -#
```

The stem of 'P' is `-#` — one column at ~20 % and one at ~70 %. **No pixel in it reaches the ink
colour.** Over the whole alphabet, per shipped face:

| face (role) | texels at α=255 | ≥75 % | stranded 25–75 % |
|---|---|---|---|
| `sg_bold_17` (TXT_TITLE) | 30.6 % | 42.2 % | 38.8 % |
| `sg_bold_12` (TXT_BUTTON) | 11.1 % | 30.2 % | 54.8 % |
| `jbm_bold_11` (TXT_VALUE) | 11.3 % | 32.1 % | 49.8 % |
| `sg_med_12` (TXT_BODY) | **2.0 %** | 19.3 % | 63.9 % |
| `sg_med_10` (TXT_SEG) | **0.4 %** | 15.2 % | 66.1 % |
| `jbm_med_9` (TXT_SECTION) | **0.0 %** | 3.4 % | 75.6 % |
| `jbm_med_7` (TXT_CHIP) | **0.0 %** | 1.3 % | 87.7 % |

`TXT_SECTION` and `TXT_CHIP` — section labels, the HUD clock/fps, every chip, badge and ROM code,
every hint line — contain **not one fully-opaque texel in the entire alphabet**. 88 % of
`TXT_CHIP`'s ink is stranded between 25 % and 75 %. `mkbcfnt` rasterises the TTF with unhinted
grayscale AA, so at 7–12 px the Medium weights' stems straddle two pixel columns and never land
on one. Phase 18 made the app reproduce that mush *exactly* — which is why the user still sees
blur after "way better".

### V1.5 — a third of the app's visible text is BAKED into the plate art

`"RESUME LAST PAIRING?"`, `"choose on the touch screen ↓"`, the pause menu's rail
(`Session/Display/Audio/Enhance/Link/Touch`) and its section captions (`SCALE · TOP`,
`FILTER · FOCUSED`, `MIX MODE`, `HUD`) do not exist as strings in `source/` — they are pixels in
`design_handoff_3dgba_ui/assets_3ds/plates/indigo/*.png` (`main.c:2320` even calls one *"the
baked `SCALE · TOP`"*). The PNGs are authored at exactly 400×240 / 320×240 and `tex3ds -f rgba8`
does not resample, so they land 1:1 — and they measure as the **sharpest** text in the app.

Two consequences worth recording:

* No font change can affect them. Any future "make the text sharp" work has to state which
  half of the text it is talking about.
* **SPEC-crisp's own evidence table measures a string the app never draws.** Its headline row
  `TXT_SECTION "SCALE · TOP" … after 1.000 off 0` re-renders that string through the
  `TXT_SECTION` face in `tools/fontlab`; on screen those pixels come from the plate. The
  measurement is not wrong, it is about a different thing.

### the three levers

The three levers that would actually answer the complaint (none attempted by the phase):
hinted / stem-snapped rasterisation, a heavier weight at the small sizes (`sg_bold_12` is 5×
better than `sg_med_12` on the same rung), or an alpha-contrast pass in the bake. The
design-handoff's own baked plate text — the sharpest text in the app — is the existence proof
that these sizes *can* be crisp.

### V1.6 — no layout regression found

The likeliest regression from a font-size change is overflow/clipping. Across every screen I
captured (boot/resume prompt, ROM picker, in-game HUD on both screens, pause tabs
Session/Display/Audio and the footer hint) **nothing overflowed, clipped or collided**. Two
positive checks: the widest single run measured is the `CO-OP …` HUD readout, still inside 400 px;
and the HUD's right-hand cluster (`3D` badge, fps, clock, battery) is laid out right-to-left from
`rx = 394` and lands at x=302…394 with clean gaps (`segs.py`, V1.3). The one *cosmetic* oddity is
that the ladder's ink is dimmer than the plate art it sits on, which is the softness of V1.4, not
a layout fault.

---

## V2 — DOOR: what I could and could not reproduce

### V2.0 the harness facts that shaped this (both cost me a run)

* **Azahar PAUSES emulation when a CTM movie ends.** Symptom: both screens go black, `gdbio`
  times out ("waiting for stub data"), `ps` shows the process at ~0.6 % CPU. It is not a crash and
  not a hang. Consequence: **a movie must outlast every measurement you intend to take**, and the
  app's on-SD plan/touch log (dumped only on Quit) is unreachable unless the movie itself performs
  the in-app QUIT. `SKILL.md` §8 should carry this.
* **`evidence/door/place.py` (relocating the fixture save) DESYNCHRONISES the engine from the
  save.** With a relocated save the game loads *a different map* from the one `SaveBlock1.location`
  reports, because `warp1`/`lastHealLocation` still point at the old one. The router then reads
  `SaveBlock1`'s (x, y) against a grid for another map and everything degrades: `beh = -1`,
  `pElev = 0`, every tile unreachable. **Screenshot proof:** `runs/p18-verify/d3/s05.bottom.nat.png`
  — `SaveBlock1` says Mauville City 0/2 (23,6) while the screen shows a Pokémon-Center interior.
  Every "the door classifier is broken" reading I took on a relocated save is void, and so is the
  S2 BUILDLOG's run-6/7 conclusion that used the same tool.

