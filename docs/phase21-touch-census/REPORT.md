# Phase 21 — Screen census & touch master plan — REPORT

_2026-08-14. Your ask: find every screen in all 5 games, photograph them, audit what touch
does today, and plan making every screen feel touch-native. Done — the plan is
`TOUCH-PLAN.md`; this is the summary._

## The numbers

| Metric | Count |
|---|---|
| Screens catalogued (all 5 games, from the pret decomps) | **143 rows** (≈70 touch-distinct interactive surfaces) |
| Visited live in the emulator | **61 distinct catalog rows** — 43 Emerald + 42 FireRed, **24 in both** (43 + 42 − 24 = 61). _Corrected 2026-08-15, `EVIDENCE-INTEGRITY.md`: the first draft's "41 Emerald + 41 FireRed, 21 FR-only" does not reconcile (41 + 21 = 62), and **41 + 41 = 82 is a sum with overlap** — it is not a number of screens and must not be quoted as one._ |
| …identified with certainty | **to the callback family, not to the screen.** The live `gMain.callback2` read is exact, but one read covers a whole family: `CB2_Pokedex` covers all 6 Emerald dex sub-screens, `CB2_Overworld` covers 32 FireRed rows. Within a family the screen identity rests on the picture alone (§ `EVIDENCE-INTEGRITY.md` §3) |
| Census-row captures banked | **170 subject frames** (74 Emerald tops + 96 FireRed bottoms), all distinct, **+1 withdrawn** (`firered/withdrawn/I4c-slotspin`). Plus 183 `emerald/aux/` working captures and, in every pair, a companion screen photographing the OTHER, undriven game — a companion carries no claim |
| Live cb2 fingerprints harvested (zero guesses, all [exact] on pret symbol maps) | **~60** |
| Screens already touch-native today | **8 GCTX contexts — on Emerald only** |
| Screens where a full touch mapping is worth building | **≈42**, collapsing into **8 design families** |
| Screens where tap-to-advance is enough | **≈52** |
| Deferred (2-player: 13 · event-data: 3 · new-game one-shots/cutscenes: ~7) | **≈25** |

Every visited screen was identified by reading the game's live `gMain.callback2` over GDB
and resolving it against pret's byte-matched symbol files — certain identifications, never
guesses. Browse the whole census visually in **`evidence/sheets/`** (per-family contact
sheets) — best single shots: `emerald/E10-naming.top.png` (the keyboard),
`emerald/E12-storage-boxes.top.png` (PC boxes), `emerald/D1-frontierpass.top.png` (the
Frontier Pass free cursor), `firered/F6e-dex-habitat.bottom.png` (FR habitat grid),
`emerald/B8-flymap.top.png` (tap-to-fly target).

## The two headline findings

1. **FireRed touch is silently dead on your cart — root cause found.** Your FR is rev1;
   the shipped profile carries rev0 ROM addresses, and rev1 moved the code (5 anchors,
   shifts 0x14–0x78, all live-proven). That's why FR battles/menus never reacted to touch
   on hardware while walking worked (RAM addresses didn't move). The exact rev1
   replacements are harvested; same bug + ready fix for LeafGreen. **Fix = phase-22
   slice 0**, already built overnight on a branch, waiting to merge.
2. **Undetected screens don't just lack touch — they're hostile.** Any unrecognized
   full-screen UI (Pokédex, summary, naming keyboard, PC boxes, PokeNav, shops…) falls
   through to "overworld", so taps leak *walking keys* into menus. The fix (detect
   everything we photographed + a safe tap=A default) uses the ~60 harvested fingerprints
   and lands in the same slice 0.

## What's already good (per you)

Overworld tap-to-walk (with door/stair routing) and battles — confirmed by the audit,
staying as-is. Also already working on Emerald: start menu/yes-no popups, party taps, bag
taps with drag-scroll and swipe-pockets.

## The plan (phase 22+), in one breath

**0)** Kill the residual + revive FR/LG (merge the promotion branch — one merge, ~40
screens improve) → **1)** the naming **keyboard** (the single biggest touch win — full tap
keyboard) → **2)** one generic **list** driver (bag polish, mart, PC items, berry
pouch/TM case, Pokédex) → **3)** **PC boxes** (tap-tap-move, then drag-and-drop v2) →
**4)** **tap-to-fly** on the region map → **5)** summary/trainer-card **page viewers** →
**6)** your **HM + through-doors traversal** (spec written: Surf/Cut/Strength auto-use +
the Lavaridge hot-spring route) → **7)** Emerald handhelds (PokeNav, Frontier Pass free
cursor) → **8)** minigames long tail. Ruby/Sapphire/LeafGreen run as a parallel lane —
your rev2 carts are now paper-verified (RS-REV2-VERIFICATION.md), so their first-ever
boot is unblocked.

Full specs already written for keyboard, lists, and traversal. The injection seam needs
**zero changes** for any of it.

## Ten questions for you

In `TOUCH-PLAN.md` §4 — the big ones: (a) OK to defer 2-player screens and new-game-only
screens (would need a scratch save)? (b) PC boxes: ship honest tap-tap-move first, or
wait for drag-and-drop? (c) page turns: swipe, tap-arrows, or both? (d) hold-to-B on
dialogs — wanted? (e) slots: EM ignored our injected B (bug found live) — investigate or
park?

## Honest gaps

Not photographed this pass: contests/blender/frontier facility interiors (Emerald —
reachable, time-boxed out), Sevii Islands leg, mart-buy on EM (shape covered by FR),
doubles target-select, mail, whiteout, 2P/link screens (except what the trade work
already proved on hardware). The EM slot machine wouldn't exit on injected B, and EM
Game Corner navigation ended that session — both flagged for phase 22. RS/LG were never
booted (no ROMs staged this phase) — catalogued from decomps + rev-verified on paper.
