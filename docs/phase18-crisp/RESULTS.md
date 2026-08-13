# Phase 18 — results, against your three hardware findings

You tested the phase-17 UI on your New 3DS and reported three things. This is what happened to
each, with the evidence, and with the parts that are **not** fixed stated up front rather than
buried.

Build under test: `3DGBA.3dsx` 5,417,868 B / `3DGBA.cia` **2,128,832 B** (2026-08-13).

---

## Read this first — what is NOT fixed

| | honest status |
|---|---|
| **Ruby + Sapphire co-op** | The profiles now exist and every address was verified against pret's byte-matched symbol maps — but **no Ruby or Sapphire ROM exists on this machine**, so not one of those addresses has ever been *executed*. It is verified-on-paper, unproven in motion. Ruby+Sapphire is the pair most likely to still misbehave. |
| **The door fix** | Proven by 1,808 host assertions driven from **your own ROMs' real map data**, and its central rule re-derived from pret's source this session. It has **never been watched working in an actual overworld** — seven earlier emulator attempts failed for reasons unrelated to the fix (below). Your hardware is the first real test. |
| **Text sharpness** | Fixed and measured on real framebuffers (numbers below). The one thing a capture cannot judge is how it reads on the physical panels — that is your call. |
| **Battles over co-op / wireless** | Still unsupported, unchanged, and expected: it needs the input-sync tier, not this phase. |

Nothing in this phase touches emulation, threading, the link/net drivers or the HD-2D render
passes. `git diff` on `celiolink.c`, `netlink.c` and `gbacore.c` is **empty**, as it has been
since phase 13.

---

## 1. "mostly very blurry — text can and should be sharp"

**Verdict: fixed, and there were TWO causes, not one.** The second one is why the earlier
attempt at this ("way better") still left you looking at soft text.

### Cause A — every draw was resampled

Text was drawn at sizes that did not match the size it was baked at. `assets_text` computed a
scale from a number that *looked* like the font's pixel height but was citro2d's normalised
30-px height — the same value for every font — so the "measure and divide" check confirmed a
scale that was wrong. Every one of the 16 (face, size) pairs in the app landed between **0.577×
and 1.049×**, and a bitmap font at any scale but 1.0 is blurred by construction.

Fix: seven faces, each baked at exactly the size it is drawn at, addressed by *role* rather than
by a pixel number, so no call site can retype the defect. `px` is gone from the drawing API.

Proof, read live out of the running app over the debugger:

```
g_txtTexelScale[0..6] = 1.0f x7          (every role, exactly 1.0)
g_txtLineFeed[0..6]   = 17 12 12 10 9 7 11
```

### Cause B — the bitmap itself was mush (this is the part that was still blurry)

Scale 1.0 only stops the app *resampling* the bitmap. It cannot fix a bitmap that was soft when
it was baked — and `mkbcfnt` rasterises with unhinted grayscale antialiasing and offers no
hinting, gamma or contrast switch. At 7–12 px a Medium-weight stem is about half a pixel wide,
so it straddles two pixel columns and lands on neither. Measured over the alphabet of the faces
that shipped:

| face (role) | fully-opaque ink | ink stranded between 25 % and 75 % |
|---|---|---|
| `jbm_med_7` (chips, badges, hints) | **0.0 %** | **87.7 %** |
| `jbm_med_9` (section labels, HUD clock/fps) | **0.0 %** | 75.6 % |
| `sg_med_10` (segmented controls) | 0.4 % | 66.1 % |
| `sg_med_12` (list rows, game names, prose) | 2.0 % | 63.9 % |

Not one fully-opaque pixel in the entire alphabet of the two smallest faces. Phase 18's first
pass made the app reproduce that grey mush *perfectly*.

Fix: a **stem-snap pass** (`tools/fontlab/sharpen.py`, run automatically by the asset bake) —
the part of hinting that needs no outline surgery. It is a monotone **16-entry alpha lookup
table per face**; a glyph sheet is 4-bit alpha, so 16 entries is the complete space of
order-preserving alpha edits. Nothing but alpha changes: every advance width, the cell grid and
`lineFeed` are byte-identical, so the scale-1.0 law and every layout measurement still hold.
Entry 0 maps to 0, so backgrounds stay background and letter counters cannot fill in.

| face | opaque before → after | solid (≥75 %) before → after | ink mass |
|---|---|---|---|
| `jbm_med_7` | 0.0 % → **35.3 %** | 1.3 % → 70.4 % | ×1.80 |
| `jbm_med_9` | 0.0 % → **26.7 %** | 3.4 % → 44.9 % | ×1.37 |
| `sg_med_10` | 0.4 % → 16.3 % | 15.2 % → 38.4 % | ×1.16 |
| `sg_med_12` | 2.0 % → 21.8 % | 19.3 % → 39.1 % | ×1.07 |
| `sg_bold_17` | 30.6 % → 45.3 % | 42.2 % → 59.6 % | ×0.98 |

The bold faces gain sharpness at ×1.0 ink — pure edge hardening, no bolding. The 7 px face
genuinely gets heavier (×1.80); that is what snapping a half-pixel stem onto a whole pixel
*means*, and it is the difference between "grey smear" and "readable".

→ **`evidence/sheet-stemsnap.html`** — the shipped font bytes, rendered through the PICA200's
own sampler, before and after.

### The objective measure, on the real screen

Phase 17 (the build you tested, rebuilt from that commit) and phase 18 were booted in the same
emulator, driven by the **same synthesized input movie** to the same pause screen, and captured
with the window filter pinned to NEAREST so the capture is a verbatim copy of the 3DS
framebuffer. Same pixels, same place, both builds:

| region on screen | ink that reads as ink (≥75 %) | blur ramp |
|---|---|---|
| top-screen status pills (`3D / DoF / Bloom / Light / Tilt / Touch OFF / Co-op / Link`) | 54.3 % → **80.2 %** | 2.54 px → **1.45 px** |
| pause footer hint (`L/R tab · A select · B resume`) | 12.2 % → **50.5 %** | 2.61 px → **1.45 px** |
| the `DIORAMA · TILT` sub-label | 10.1 % → **48.0 %** | 2.34 px → **1.57 px** |
| the `Off / Low / Mid / Max` segment row | 67.1 % → 72.9 % | 2.39 px → 2.21 px |

The blur ramp is the mean run of half-lit pixels across a stroke edge — literally "how many
pixels wide is the smear". It roughly halved.

→ **`evidence/sheet-crisp.html`** — captioned before/after crops at 9× plus the full screens.

### One thing worth knowing about your UI

About a third of the text you see is **not a font at all** — `RESUME LAST PAIRING?`, the pause
menu's `Session / Display / Audio / Enhance / Link / Touch` rail and its section captions
(`SCALE · TOP`, `MIX MODE`, `HUD`) are pixels baked into the plate artwork. They were already
1:1 and are already the sharpest text in the app; no font change can affect them. If some text
looks sharper than the rest on your device, that is why.

---

## 2. "standing right next to a door, touching the door makes the player tackle the wall"

**Verdict: fixed in code, host-proven, not yet seen running.**

Your diagnosis was right, and it is not a tie-break accident — it is a hard rule in the game.
Re-derived from pret this session rather than taken on trust:

```c
// pokeemerald src/field_control_avatar.c
static bool8 TryDoorWarp(struct MapPosition *position, u16 metatileBehavior, u8 direction)
{
    if (direction == DIR_NORTH)      // <-- a door ONLY opens when entered from the SOUTH
    ...
```

So a diagonally-adjacent door taken "up then left" enters from the **east** and is just a wall.
The arrival direction had to become a **constraint of the route**, not a by-product of search
order. It now is: for a door the router walks to the tile *south* of it and holds UP.

Checking the rest of the warps rather than assuming they all want south turned up two things
that were **100 % broken** and are now fixed:

* **Arrow warps** (the exit mat in every building) need you to *stand on* the tile and *hold* a
  direction — `TryArrowWarp` → `IsArrowWarpMetatileBehavior(behaviour, direction)`. Routing onto
  one and stopping does nothing. `MB_SOUTH_ARROW_WARP` is the single most common warp tile in
  both games (512 in Emerald, 263 in FireRed — counted on your own ROMs), i.e. *"tap the exit to
  leave a building"* never worked once.
* **FireRed's directional stair warps** are the same shape with a different direction, and the
  behaviour numbers are **not shared between engines** (RSE `0x6C` is a water door, FRLG `0x6C`
  is an up-right stair) — a common table would have been a new bug.

Two more defects fell out of the same work: the router treated deep **water** as walkable (Gen-3
water is impassable by *elevation*, not by collision, so it plotted courses across ponds), and a
tap on the wall directly above a door — the door graphic is two tiles tall — now retargets onto
the door instead of failing.

Also fixed: tapping a **sign or an NPC** used to take three times longer than it should. That
route deliberately ends on a blocked tile, so its last step can never "complete", the stall
detector fired at the destination and re-planned twice before pressing A — a full extra second
of bumping the sign. The re-plan is now suppressed at the terminal of exactly that kind of route.

**Evidence:** `test/host/test_fieldpath.c`, **1,808 assertions**, driven from map data extracted
from your own ROMs — both engines' full behaviour tables, the door approach, the arrow/stair
hold, water, the two-tile door head, and the replan rule (mutation-tested: reverting it produces
3 failures).

**Why there is no video.** Seven emulator attempts across earlier sessions failed for reasons
that had nothing to do with the fix: the tool used to place the test save in front of a door
**desynchronises the game from the save** (the engine loads the map named by an unrelated field,
so the player is standing in a different map from the one the save reports — screenshotted), and
Azahar **pauses emulation the moment an input movie ends**, which killed the measurements that
did get taken. Both are now written down for the next session. Your hardware is the honest test.

---

## 3. "co-op on the same console doesn't work — start with same game (EM+EM, R+S, FR+LG)"

**Verdict: one real bug found and fixed; Ruby+Sapphire newly supported but unproven; Emerald+
Emerald and FireRed+LeafGreen were already allowed by the gate, so if they still fail on your
device I need a specific symptom.**

* **Ruby and Sapphire had no profile at all.** The game table only knew `BPEE`, `BPRE` and
  `BPGE` — `AXVE`/`AXPE` were simply absent, so Ruby+Sapphire could never have worked. Both rows
  now exist. Every address was taken from pret's byte-matched symbol maps for **all four**
  cartridges (Ruby, Sapphire, and both revisions); I re-downloaded those maps and re-checked
  them myself this session: the RAM layout is **identical across all four** (729 symbols, zero
  differences), and every address in the row resolves to exactly the symbol its comment claims
  (`gSaveBlock1` `0x02025734`, `gMapHeader` `0x0202E828`, `gObjectEvents` `0x030048A0`,
  `BattleMainCB2` `0x0800F808`, …).
  Ruby/Sapphire have **no** `gSaveBlock1Ptr` — the save block is the struct itself, not a pointer
  — which is handled explicitly. Where a symbol genuinely does not exist in the RS decomp the
  row ships a **zero and a named degradation**, never a guess: on Ruby/Sapphire the smart-touch
  *menu / bag / party* features are inert and only walking works. That is deliberate, documented,
  and fail-safe.
* **A real save-corruption hazard, fixed.** The one-tap "resume last pairing" prompt bypassed the
  guard that stops the same ROM file being loaded into both cores. Two mGBA cores would then
  hold a writable handle on **one save file** — and "emerald & emerald" is the exact path into
  it. The guard is now applied at both ends (loading the saved pairing *and* writing it), and the
  pairing degrades to single-player rather than silently corrupting a save. Proven live, both
  directions, by staging a hostile pairing file and reading the app's own diagnostics over the
  debugger.

**What is not proven:** no Ruby or Sapphire ROM exists here, so the RS row has never executed.
The cheap first test on your device is not co-op at all — boot Ruby in one core and Sapphire in
the other and check that **smart touch walks** on both. That single check exercises the three
columns co-op depends on before any peer avatar is drawn.

---

## What to check on the hardware

1. **Text, on the physical panels** — the pause footer hint, the top-screen status pills, chips
   and badges. That is where the numbers moved most. Watch for the opposite failure too: the
   7 px chip face is now ~80 % heavier, so tell me if anything reads *too* heavy or blobby.
2. **All six themes**, not just Indigo — nothing theme-related changed, but the ink is darker now.
3. **The door case you reported**: stand diagonally next to a door and tap it. Then the case
   that was never possible before — stand inside any building and tap the exit mat.
4. **Tap a sign / an NPC** — it should react about a second sooner than it used to.
5. **Ruby + Sapphire**: walking via smart touch first, co-op second.
6. **Emerald + Emerald**: confirm the resume prompt no longer offers the same file twice.

If something is still wrong, the most useful thing you can send is the on-SD touch/plan log from
that session (`sdmc:/cias/netlogs/`) plus what you tapped.

---

## Suites

Twelve host suites, all green, none shrinking:

| suite | checks |
|---|---|
| test_control | 6,897 |
| test_diag | 376 |
| test_fieldpath | 1,808 |
| test_netlink_reliability | 66 |
| test_presence | 49,773 |
| test_profiles | 505 |
| test_theme | 83,444 |
| test_tilt | 1,723 |
| test_trace_replay | 58 |
| **test_typography** | **1,016** (+35: the new stem-snap floor, T13) |
| test_uigeom | 18,332 |
| test_uihit | 1,718 |
| harness host tests | **149 OK** (+2) |

`make -j8` and `make cia` clean. Warning audit done by building **this tree and the phase-17
commit side by side** and diffing the warning sets: phase 18 adds **zero** new warnings and
removes one (a dead function deleted).
