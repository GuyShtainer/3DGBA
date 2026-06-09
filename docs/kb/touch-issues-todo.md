# Touch — open issues + plan (resume here next session)

_From hardware testing on 2026-06-10. The touch suite was built fast from decomp research without
hardware-in-the-loop, and several pieces are unreliable on device. Fix these with diagnostics + one
feature at a time, verified on hardware before moving on. **Do NOT batch more blind changes.**_

## Verified this session (so address-correctness is NOT the problem — the logic/edge-cases are)
All against the pret byte-matched sym maps:
- `gTasks` EM `0x03005E00` / FR `0x03005090`; `gBackupMapLayout` EM `0x03005DC0` / `VMap` FR `0x03005040`.
- Party/yes-no/multichoice/bag **task handlers** (EM: ChooseMon `0x081B1370`, YesNo `0x080E215C`,
  Multi `0x080E2058`, Bag `0x081ABD28`; FR: ChooseMon `0x0811FB28`, YesNo `0x0809CE54`,
  Multi `0x0809CC98`, Bag `0x08108F0C`), `sMenu`, `gWindows`, all battle addrs. All correct.
- Detection is now task-based + fail-safe (commit 6381e7c) — yet the walk bug persists, so the cause
  is **logic/edge-case**, not a bad address.

## The 6 reported bugs (with diagnosis)
1. **Walk still broken.** Detection addrs verified, so suspect: (a) the BFS/continuous-retarget logic
   in `walk_update` (touch.c), or (b) a task-scan false-positive making the overworld read as a menu.
   **FIRST STEP: add an on-screen debug of `ctx` + `s_walking` + goal so we can SEE what the emulator
   thinks on hardware — stop debugging blind.** Then likely **simplify the walk** (see plan).
2. **Naming keyboard touch** — not implemented. Tapping letters on the name-entry screen. Needs the
   keyboard cursor var + key-grid layout (RE).
3. **Bag: tap the pocket directly** (currently horizontal-swipe switches pockets). Needs the pocket-tab
   on-screen rects. User said "all games/all bags" — cross-game is out of scope (per-game RAM); confirm
   they mean all Pokémon bags (field/battle/shop/PC).
4. **Pokémon summary**: can't switch pages by touch ("touch the dots of the cards"). Not implemented;
   needs the summary page-switch RE (tap page indicator → L/R page change).
5. **Popup menus don't register** (party SUMMARY/SWITCH/ITEM/CANCEL popup, berry menus). These are
   `sMenu` popups shown WHILE the party task is still active → `game_read` returns `GCTX_PARTY` (slot
   rects), not the popup. Need to detect the sMenu popup-over-party and route to the field-menu
   hit-test (give the sMenu/popup precedence when one is up).
6. **Bike: moves but wrong direction.** The BFS continuous-retarget desyncs on fast bike movement.
   Plan: while touching, use **simple directional steering** (dominant axis from the player's screen
   center toward the touch) instead of BFS path-follow — robust regardless of speed.

## Plan for next session (reliability-first)
1. **Add a debug overlay** (ctx name + s_walking + goal + detected menu) so hardware behavior is
   visible. This is the #1 thing missing — we've been flying blind.
2. **Simplify the WALK:** while touching → directional (toward the touch, dominant of 4 axes) — fixes
   #1 + #6 reliably (no map reads, no pathfinding desync). Keep BFS only as an optional quick-tap
   "route to this tile," or drop it if it stays flaky. Directional steering is what #6 literally asks.
3. **Sub-menus one at a time, hardware-verified each:** popup-over-party (#5), bag pocket tap (#3),
   summary pages (#4), naming keyboard (#2). No more batching.
4. **Keep the virtual gamepad (TOUCH_PAD) solid** as the universal reliable fallback for anything the
   smart pointer doesn't cover.

## Lesson
Smart-touch RAM-pointing is powerful but fragile to build blind. Prefer: fail-safe detection (done),
on-device diagnostics, simple-robust over clever, and verify each piece on hardware before adding the
next. The deterministic write-cursor-then-A pattern (battle action/move) is the proven-reliable one.
