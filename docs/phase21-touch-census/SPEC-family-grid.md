# SPEC — touch family: GRID (PC storage boxes)

_Phase 22.2 design spec, written 2026-08-14 (overnight lane). NO implementation in this
document. Ground truth: pret pokeemerald LOCAL clone (`gba-toolkit/projects/PokeDNA/
daycare map/pokeemerald/src/pokemon_storage_system.c`, 10 059 lines, read this session) +
the census storage captures (`evidence/emerald/E12*-*.png`) + `pokeemerald.sym` (local copy
`gba-toolkit/projects/rec2mp4/local/pokeemerald.sym`). Coordinates are GBA-frame pixels
(240×160)._

**Scope:** the Emerald boxes UI (`CB2_PokeStorage`, census-harvested [exact] 0x080C7D54 —
currently a bare GCTX_FULLUI row). v1 = **EM only**; FR/LG's storage is a different module
(`CB2_PokeStorage` FR 0x0808CDD8 harvested, but the FRLG statics were not re-derived this
slice) → their rows ship 0 = named degradation, boxes stay GCTX_FULLUI there. RS: the
ROM/statics ban, 0s.

**Master-plan honesty rule:** tap-tap-move. **NO drag-and-drop this slice** — a tap moves
the game's own cursor and presses the game's own A. Nothing writes storage data; the game
performs every move itself through its own input handler.

---

## 1. Engine model (all cited from pokemon_storage_system.c)

### 1.1 State (EWRAM statics, contiguous — pokeemerald.sym)

| Symbol | Addr | Meaning |
|---|---|---|
| `sStorage` | 0x02039D08 | ptr to PokemonStorageSystemData (not needed by the driver) |
| `sInPartyMenu` | 0x02039D0C | party panel up (u8) — `sStorage`+4 |
| `sCurrentBoxOption` | 0x02039D0D | 0 WITHDRAW / 1 DEPOSIT / 2 MOVE_MONS / 3 MOVE_ITEMS (:55-59) |
| `sCursorArea` | 0x02039D78 | 0 IN_BOX / 1 IN_PARTY / 2 BOX_TITLE / 3 BUTTONS (:200-204) |
| `sCursorPosition` | 0x02039D79 | box 0..29 / party 0..6 / buttons 0..1 |
| `sIsMonBeingMoved` | 0x02039D7A | a mon is in the hand |
| `sMovingMonOrigBoxId` | 0x02039D7B | held mon's source box (14 = party) |
| `sMovingMonOrigBoxPos` | 0x02039D7C | held mon's source slot |
| `gPokemonStoragePtr` | 0x03005D94 | → +0 currentBox u8, +4 boxes[14][30] (BoxPokemon 80 B) |

All sym-derived → **verify-in-emulator** (the proof run's live reads are the verification).

### 1.2 Input semantics per cursor area (the navigator's transition table)

- **IN_BOX (0)**, pos p, c=p%6, r=p/6 (`InBoxInput_Normal`):
  UP: r>0 → p−6, r==0 → TITLE. DOWN: r<4 → p+6, r==4 → BUTTONS pos c/3.
  LEFT/RIGHT: wrap **within the row** (never leaves the box). START → TITLE.
  A → selection menu iff mon present or holding (`SetSelectionMenuTexts`).
- **BOX_TITLE (2)** (`HandleInput_OnBox`): HELD LEFT/RIGHT → **scroll box ±1**;
  UP → BUTTONS 0; DOWN → IN_BOX pos 2; A → box options menu (JUMP/WALLPAPER/NAME/CANCEL).
- **BUTTONS (3)** (`HandleInput_OnButtons`): LEFT/RIGHT toggle 0↔1; UP → IN_BOX 24/29;
  DOWN|START → TITLE; A: pos 0 = SHOW PARTY, pos 1 = CLOSE BOX.
- **IN_PARTY (1)** (`HandleInput_InParty`): UP/DOWN cycle 0..6 (6 = the back/CANCEL slot);
  RIGHT from pos>0 → back to box (HIDE_PARTY); A on 0..5 → selection menu.
- **SELECT toggles auto-action** (`ToggleCursorAutoAction`) — the driver must NEVER inject
  SELECT in this context.

### 1.3 The popup menus are the GLOBAL sMenu

`HandleMenuInput` (:8024) reads `Menu_GetCursorPos()` on A and moves with
`Menu_MoveCursor` — i.e. the storage popups (mon MOVE/SUMMARY/…, held PLACE/…, box-title
JUMP/WALLPAPER/NAME/CANCEL) are standard menu.c menus over `sMenu` +
`InitMenuInUpperLeftCornerNormal` (:8011). **The shipped GCTX_FIELDMENU write-then-A
driver works on them unchanged**; geometry via the live `sMenu.windowId` → gWindows read.
Window shape (`AddMenu` :8001-8007): bottom-right anchored at tile (29,15) → right edge
x=232, bottom y=120, rows 16 px.

**Stale-window trap:** menu.c never resets `sMenu.windowId` on RemoveMenu → the storage
handler's menu-open gate must also require the window slot to be LIVE (gWindows[wid]
bg != 0xFF after RemoveWindow frees it) + rect sanity, not just wid != 0xFF.

### 1.4 On-screen geometry (sources + capture E12-storage-boxes.top.png)

- Box grid: mon icon centers x=100+24c, y=44+24r (`CreateBoxMonIconAtPos` :4484-4485)
  → cell rects 24×24 at x∈[88+24c,112+24c), y∈[32+24r,56+24r); grid body x 88..232, y 32..152.
- Box scroll arrows: sprites at (92,28) and (228,28) (`CreateBoxScrollArrows` :5644) →
  arrow bands 16×16 grown to x 84..100 / 220..236, y 20..36; title band between
  (x 100..220, y 20..36).
- Top buttons: cursor anchors x=pos*88+120 (`GetCursorCoordsByPos` :5849-5851) →
  PARTY POKÉMON band x 96..176, CLOSE BOX band x 180..236, both y 0..18
  (capture-derived widths, verify-in-emulator).
- Party panel (only when `sInPartyMenu`): cursor anchors pos0 (104,52), pos1-5
  (152,(p−1)*24+4), pos6 back (152,132) (:5828-5843) → tap rects pos0 x 88..120 y 44..76;
  pos1-5 x 136..168, y (p−1)*24+8 .. +32; pos6 x 128..176 y 124..148 (capture-derived,
  verify-in-emulator). While the panel is up, grid cells at x<176 (cols 0-3) are occluded
  → DEAD; cols 4-5 stay live (capture E12e shows them visible).

---

## 2. Requirements

- **G1 (detection):** new `GCTX_STORAGE`, matched POSITIVELY by `gMain.callback2 ==
  storageCb` (profile column; EM 0x080C7D54 census [exact]), tested with the naming test
  BEFORE the cb2FullUi loop (more specific wins; the fullui list keeps the value — TEST 11
  pins it). All other games: storageCb=0 → never fires.
- **G2 (closed-loop tap-tap-move):** a tap on a grid cell arms target (area 0, slot). The
  driver then emits ONE d-pad edge per step chosen from the §1.2 table against the LIVE
  `sCursorArea/sCursorPosition` read each frame, waiting for the cursor to actually move
  (or an edge timeout) before the next edge. When live == target → 2-tick A pulse.
  **A is NEVER emitted while live != target** (no mis-slot actions, the safety property);
  a target unreached within ~240 frames is dropped silently.
- **G3 (row/col order):** vertical first, then horizontal (vertical moves can exit the box
  only at the extremes, which the target excludes; horizontal wraps stay in the row).
- **G4 (box tabs):** tap left/right arrow band → navigate to TITLE (START edge if in box)
  then ONE held LEFT/RIGHT (3 frames — HandleInput_OnBox wants JOY_HELD) = one box scroll
  per tap. Tap the title band itself → navigate to TITLE + A = the game's own box options
  menu (JUMP/WALLPAPER/NAME/CANCEL — then §G6 taps drive it).
- **G5 (buttons + party):** tap PARTY POKÉMON / CLOSE BOX bands → target (3,0)/(3,1) + A.
  Party slot taps only while `sInPartyMenu` (G2 loop with area 1 targets; navigation
  within party = UP/DOWN cycling; cross-area box↔party targets route via the game's own
  transitions: RIGHT leaves party; the PARTY button enters it).
- **G6 (popups):** while a live sMenu window is up (§1.3 gate), grid navigation is
  suspended (pending targets dropped) and taps go through the shipped fieldmenu path
  (live window read → write `sMenu.cursorPos` + A). Taps outside the popup emit NOTHING
  (no blind B — mis-cancels are worse than ignored taps).
- **G7 (safety/reset):** all state clears on ctx change (`all_reset` lifecycle). No key
  is emitted with no target armed. SELECT never emitted (§1.2). `sCurrentBoxOption == 3`
  (MOVE ITEMS) → driver emits NOTHING (named v1 limit; its A semantics differ).
- **G8 (instrumentation):** g_touchDbg grows a storage mirror stamped every GCTX_STORAGE
  frame: area/pos/held/origBox/origPos/boxId/boxOption/inParty/menuOpen/target +
  a 30-bit occupancy mask of the current box (hasSpecies bit — BoxPokemon +19 bit 1,
  unencrypted) — the complete gdb proof channel for a move (src bit clears, dst bit sets).
- **G9 (seam):** no injection-seam change — per-frame masks only, RAM writes only via the
  existing write8 on sMenu.cursorPos (G6). Nothing writes storage state, ever.

## 3. Host proofs (test_touchgeom growth)

- storgeom_hit rect sweep: every px of 240×160 × (party up / not) classifies to exactly
  one target or NONE; cells map to the right slot; occluded cols dead under the panel.
- stornav_step convergence oracle: a pure-C model of §1.2's transition table; from EVERY
  (area,pos) to EVERY box/button/title target: the emitted-edge walk arrives ≤ 24 steps,
  never emits SELECT/B, never exits the box except via the modeled title/buttons edges,
  and emits 0 (arrived) exactly when live == target.

## 4. Emulator proof plan (EM, instance a)

- **P-G1 (recon boot):** D4 route (Battle Arena lobby save → Frontier Center → PC → E11
  "LANNETTE'S PC" → MOVE POKEMON) → boxes. gdb: ctx=GCTX_STORAGE resolves [exact];
  g_touchDbg mirror sane (area/pos/boxId/occupancy vs the capture). Safe taps: title
  band → box options popup opens (G4) → popup CANCEL row tap (G6, fmenu delegation).
  Harvest current box + occupancy for P-G2's slot choice.
- **P-G2 (the move):** same route; touch-only inside the boxes: tap occupied slot s →
  cursor walks (gdb: pos trail) → A → popup; tap MOVE row → `sIsMonBeeingMoved=1`,
  origBox/origPos = s; tap empty slot d → cursor walks → A → PLACE popup → tap PLACE →
  occupancy mask: bit s cleared, bit d set; captures at every stage → evidence/impl/.
- Hardware gate: standing done-gate rule — the family ships only after a hardware pass.

## 5. Open questions

- Q1: edge pacing vs the cursor slide anim (`UpdateCursorPos` steps) — the closed loop
  absorbs it; tune the edge timeout on emulator.
- Q2: party-strip rect y-bands (capture-derived) — verify on the first live party tap.
- Q3: FR/LG storage module statics (their own slice; P-D-style discovery NOT applicable —
  not ListMenu).
- Q4: ChooseBoxMenu (the box-JUMP selector, E12f) is its own widget — v1 emits nothing
  useful there (taps still resolve vs the §1.2 model but the selector consumes its own
  input); a JUMP adapter is deferred.
