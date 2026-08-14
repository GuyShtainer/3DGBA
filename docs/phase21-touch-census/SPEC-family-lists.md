# SPEC — touch family: VERTICAL LISTS (one design, five screens)

_Phase 21/22 design spec. NO implementation in this document. Written 2026-08-14 from the
banked census + pret source (pokeemerald local clone; symbol maps in the session
scratchpad `syms/`). Coordinates are GBA-frame pixels (240×160)._

**Scope (the five instantiations asked for):** bag pockets (E3), mart buy/sell (I1/I2),
Pokédex list (F1 EM / F6 FR), PC item list (E13), party context menus (E2b popups).
The same driver will later absorb every other `ListMenu` screen for free (mailbox E14,
berry pouch E4, TM case E5, move relearner E17, pyramid bag D5, Teachy TV K1 …) — listed
in §6, out of v1 scope.

**Games:** BPEE first (live-positive anchors), BPRE r1 second (blocked on the phase-22
rev1 anchor promotion — VISITED-firered headline table), BPGE = BPRE with
`pokeleafgreen_rev1.sym` addresses. **RS deferred** (menu detection absent by design in
the AXVE/AXPE profiles; rev-2 re-derivation is OVERNIGHT2 S1c).

---

## 1. The one design

Gen-3 has ONE engine-wide list widget: `struct ListMenu` (pret `include/list_menu.h:60-91`),
a 24-byte template + `scrollOffset`(+24) + `selectedRow`(+26), embedded **inside a task's
data area**: `ListMenuInit` creates a dummy task and stores the struct at
`gTasks[listTaskId] + 8` (data[0..15]; `struct Task` = func 0x0, flags 0x4-0x7,
data +0x8, stride 0x28). The shipped bag driver already exploits exactly this (write
"+26" then A, scroll by injected keys, never touch +24 — touch.c:565-598, GCTX_BAG
live-positive on EM). **This spec generalizes that working pattern into one driver plus a
per-screen anchor table, instead of five bespoke handlers.**

### 1.1 The generic ListMenu driver (tier L-A)

- **L1. Discovery per screen:** resolve the owning task by fingerprint (the existing
  `task_active`/`find_bag_list_task` machinery, gamestate.c:242-263), read the u16
  `listTaskId` from the anchor's documented data slot, then
  `listBase = gTasks + 0x28*listTaskId + 8`. Validate `listTaskId < 16` and the list
  task's `isActive`.
- **L2. Live geometry — read, don't hardcode:** from `listBase`: `totalItems` (+12, u16),
  `maxShowed` (+14, u16), `windowId` (+16, u8) → window rect from
  `gWindows + 12*windowId` bytes +1..+4 (tilemapLeft/Top/width/height ×8 px) — the
  identical live-window read `hit_fieldmenu` ships for sMenu (touch.c:534-546). Row pitch
  = 16 px on all five screens (formula: font max height + `itemVerticalPadding`; both are
  16+0 here — the §4 table records expected values as a cross-check, the driver trusts
  the live read).
- **L3. Tap = select:** tap-vs-drag uses the picker convention — **6 px slop, one-way
  latch** (`UIHIT_DRAG_PX`, uihit.h:34; bag uses the same 6). On a clean tap in the list
  window: `row = (gy - y0) / pitch`; **clamp `row < min(maxShowed, totalItems - scrollOffset)`**
  (read +24 for the clamp only) — a tap in blank space below a short list is DEAD. This
  fixes the accepted-wart "tap blank → hits CLOSE BAG" (COVERAGE §3) for every screen at
  once. Then the house 2-tick pulse: write u16 `listBase+26 = row`, A on tick 1.
- **L4. Drag = scroll, honest to key-repeat:** vertical drag injects one UP/DOWN edge per
  **14 px** (the shipped bag constant; ≈1 row per row-height with finger-jitter margin).
  The game moves its own cursor+scroll. **Never write scrollOffset (+24)** — racy against
  the game's redraw (the standing bag rule).
- **L5. Fling = held key, not fake physics:** if release velocity over the last 4 touch
  frames exceeds ~3 px/frame, HOLD the direction key for `min(60, v×8)` frames; the
  game's own key-repeat (`JOY_REPEAT` cadence) does the fast scroll. That is the honest
  ceiling of what the seam + engine can express — no pixel-interpolated momentum, no
  overshoot animation. Any new touch during a fling-hold cancels it (returns 0 that
  frame, then normal processing).
- **L6. Horizontal swipe ≥30 px** (bag constant): LEFT/RIGHT injection, only on screens
  whose table row marks it meaningful (bag = pocket switch — shipped; buy/PC lists =
  ignored; Pokédex = page jump, §3).
- **L7. Scroll-arrow / page-indicator taps:** the engine draws scroll arrows via
  `AddScrollIndicatorArrowPair` at the window's top/bottom edge. Touch targets: a 16-px
  band above y0 and below y0+maxShowed*pitch inside the window's x-band → one UP/DOWN
  per tap; held touch there = held key (repeat-rate paging). No new art: the game's own
  arrows are the affordance.
- **L8. Sub-menus and quantity pickers over a list** are NOT this driver:
  - context popups (item USE/GIVE/TOSS, party SUMMARY/SWITCH…) are `sMenu` windows →
    the existing GCTX_FIELDMENU `hit_fieldmenu` path (live window read) — see L-C;
  - quantity pickers (buy/sell/toss "how many"): tap upper half of the qty window = UP
    (+1), lower half = DOWN (−1), vertical drag ±1 per 14 px, horizontal drag ±10 per
    30 px (the game maps LEFT/RIGHT to ±10), tap outside qty window on the money/total
    strip = A (confirm). Geometry: the qty window is small and per-screen; v1 uses the
    per-screen rects in §4 marked capture-derived, verify-on-emulator.
- **L9. Reset discipline:** all driver state (pending row, ticks, drag latch, fling hold)
  clears on any ctx change and on `all_reset()` — same lifecycle as `s_bag*`.
- **L10. Safety:** if any validation in L1-L2 fails (missing task, listTaskId ≥16, window
   0xFF, pitch ≤0), the screen's taps emit NOTHING while its GCTX is positive (never fall
  through to walk-key residual). Same core property as the keyboard family.

### 1.2 Seam verdict

**No injection-seam change needed.** The driver uses: per-frame masks (pulses, holds —
first-class), u16 RAM writes (+26 via write16), and multi-step LEFT/RIGHT sequences for
tab jumps (handler-internal queue, one edge per 2 frames — the same pattern as every
`s_*Tick`). Additive-only injection is compatible: we never need to suppress physical
keys. Nothing needs write32.

---

## 2. Per-screen instantiation — tier L-A (generic driver)

### 2.1 Bag (E3) — SHIPPED baseline, upgraded

Anchor: `bagHandler` task + list task data[0] (+8) — already in both profiles
(EM live-positive; FR dead until the rev1 anchor fix 0x08108F84). Upgrades over the
shipped handler, in this driver's terms:
- **L11.** blank-row clamp (L3) replaces "tap blank hits highest visible row".
- **L12.** pocket tabs become direct targets (issue #3 in COVERAGE §3):
  - EM: the pocket indicator dot-strip under the title (top-left area; 5 pockets) — tap
    dot *i* → inject |Δ| LEFT/RIGHT edges sequenced one per 2 frames (Δ = i − current
    pocket; current pocket readable from the bag struct, or derive by counting — mark:
    address capture at implementation, `gBagPosition.pocket` in pret, sym
    `gBagPosition` **verify-pending**). Rects: capture-derived from
    `evidence/emerald/E3-bag-*.top.png`, **verify-on-emulator**.
  - FR: the visible ◄ ► arrows beside the pocket name → single LEFT/RIGHT per tap.
- **L13.** everything else (drag/fling/swipe) = L4-L6 unchanged; sell-mode reuses this
  screen (I2 below).

### 2.2 Mart buy menu (I1)

| Item | BPEE | BPRE r1 |
|---|---|---|
| owning task | `Task_BuyMenu` `0x080E0AC8` (pokeemerald.sym, **sym-derived ⏳**) | `Task_BuyMenu` `0x0809BBD4` ⏳ |
| listTaskId slot | `data[7]` → u16 at taskBase+22 (shop.c:412 `#define tListTaskId data[7]`, :539) | same slot per FR shop.c (**verify in FR source at implementation**) |
| expected window | (14,2,15,16) → x112..231, y16..144, 8 rows, pitch 16 (shop.c WIN_ITEM_LIST :274-282) | live-read (capture `firered/I1-martbuy.*` shows x≈88 band, ~6 rows) ⏳ |
| cb2 (optional extra gate) | not harvested (EM I1 missed the visit — VISITED honest-not-visited) ⏳ | not harvested ⏳ |

- **L14.** CANCEL is the last real row (`totalItems` includes it) — the L3 clamp makes
  the whole window body honest; no special case.
- **L15.** qty picker per L8; confirm/decline popups = sMenu (L-C).
- **L16. Sell (I2) = the bag** (`CB2_GoToSellMenu` re-enters the bag task) → GCTX_BAG
  already covers it on EM; FR after the rev1 fix. Requirement: verify GCTX_BAG stays
  positive in sell mode on emulator (one probe), else add the sell task to the anchor
  table.

### 2.3 PC item list (E13)

| Item | BPEE | BPRE r1 |
|---|---|---|
| owning task func | `ItemStorage_ProcessInput` `0x0816C30C` ⏳ (player_pc.c:1214ff) | `Task_ItemPcMain` `0x0810DF18` ⏳ (item_pc.c) |
| listTaskId slot | `data[5]` → +18 (player_pc.c:391) | FR item_pc keeps its list state in `sListMenuState`/`sStateDataPtr` (0x0203ADCC/0x0203ADBC ⏳) — **resolve the FR slot from item_pc.c at implementation; if the list task id is not in task data, the discovery probe (§5 P-D) finds the ListMenu task by scanning gTasks for the dummy-task func** |
| secondary proof channel | `gPlayerPCItemPageInfo` `0x0203BCB8` (GLOBAL — cursorPos/itemsAbove mirror) | `sListMenuState` |

- **L17.** WITHDRAW/DEPOSIT/TOSS top menus and the mailbox share the driver once their
  anchors land; v1 targets the item WITHDRAW list (the E11→E13 route banked in
  VISITED-emerald).

### 2.4 Party context menus (E2b) — tier L-C, already shipped

The popups are `sMenu` windows, NOT ListMenu → the existing GCTX_FIELDMENU handler
(live window+pitch read, write `sMenu.cursorPos`, A) covers them and was **live-positive
on EM this census** (VISITED rows B2/E2b/E11/I6b). Requirements here:
- **L18.** FR rev1: dead until `Task_HandleSelectionMenuInput` 0x08122CD4 (+ yesNo/multi
  and `startMenuTask` 0x0806F204) promote — the census fix list. No new design.
- **L19.** the E3b audit finding (bag sub-menu shows `field` residual on EM: the sMenu
  popup over the bag failed both bag and fieldmenu detection) must be root-caused during
  implementation — expected fix: detection precedence, not geometry (the popup-over-party
  precedent a661b80). Acceptance: E3b's route re-probes as fmenu-positive.

---

## 3. Tier L-B: the custom list — Pokédex

### 3.1 EM Pokédex list (F1) — no ListMenu; cursor model is bespoke (pokedex.c)

Ground truth: `sPokedexView` (EWRAM ptr `0x02039B4C` ⏳) → `struct PokedexView`:
`pokedexList[387]` (4 B each) = +0x000..+0x60B, `pokemonListCount` +0x60C u16,
`selectedPokemon` +0x60E u16 (pokedex.c:160-180). Input task `Task_HandlePokedexInput`
`0x080BB7D4` ⏳; scroll redraws via `TryDoPokedexScroll` — **a raw `selectedPokemon`
write does NOT redraw → write-then-A is off the table; this screen is key-injection
only.**

- **L20.** Gestures: vertical drag/fling per L4/L5 (UP/DOWN edges); horizontal swipe →
  LEFT/RIGHT (the dex's fast ±page scroll). Tap on the **highlighted row** = A (open
  entry — only valid when `seen`, the game enforces). Tap N rows above/below the
  highlight = a sequenced burst of N UP/DOWN edges (one per 2 frames, N ≤ 4), no A —
  two taps select-then-open, phone-list style.
- **L21.** Locating the highlight on screen: the selection slot is not fixed near the
  list edges (capture `emerald/F1-dexlist.top.png`: slot 0 highlighted at the top for
  selectedPokemon=0). The slot formula must come from the draw code
  (`listVOffset`/`initialVOffset`, PokedexView +offsets above) — **derive on emulator
  during implementation (read the three fields at several scroll positions), do NOT
  ship a guessed formula.** Until derived, L20's relative taps stay disabled and only
  drag/fling/tap-highlight ship (still a full replacement of today's residual).
- **L22.** Footer chips are native buttons: "START MENU" chip → START, "SELECT SEARCH"
  chip → SELECT (rects capture-derived, bottom-left strip). Search UI itself (F5,
  criteria grid) is a GRID-family screen — out of scope here.
- Detection: cb2 `CB2_Pokedex` 0x080BB774 **live-harvested [exact]** ✅; sub-screen
  discrimination (list vs entry/area/cry/size/search) via the harvested `taskFp[8]`
  ring — the F1 list = `Task_HandlePokedexInput` active.

### 3.2 FR Pokédex (F6)

FR's dex is a different module (`pokedex_screen.c`, cb2 `CB2_PokedexScreen` 0x0810254C
**live-harvested** ✅) and — per the sym neighborhood (`sPokedexScreenData` 0x0203ACF0,
`ItemPc`-style ListMenu helpers all over the module) — **believed ListMenu-backed** for
the numeric/habitat lists. Requirement **L23:** run the §5 P-D discovery probe on the FR
dex list first; if it finds a live ListMenu task, F6 joins tier L-A with zero new design;
only if not does it get a bespoke adapter. (Capture `firered/F6b-dex-list.*` banked; the
TOC + habitat pages have their own shapes — grid family for the portrait grid.)

---

## 4. Expected-geometry table (cross-check values; the driver reads live — L2)

| Screen | Game | x-band | y0 | rows×pitch | Source | Status |
|---|---|---|---|---|---|---|
| Bag list | EM | 112..231 | 16 | 8×16 | shipped touch.c:565-571 + windows | ✅ live-proven |
| Bag list | FR | 88..231 | 8 | 6×16 | shipped touch.c:565-571 | ⏳ dead until rev1 anchors |
| Buy list | EM | 112..231 | 16 | 8×16 | shop.c WIN_ITEM_LIST (14,2,15,16) | source-derived ⏳ |
| Buy list | FR | ~88..231 | ~16 | ~6×16 | capture I1-martbuy | capture-derived ⏳ |
| PC items | EM | live-read | | ×16 | player_pc window templates | ⏳ |
| PC items | FR | live-read | | ×16 | item_pc | ⏳ |
| Dex list | EM | right pane (capture F1) | | ~5-6 visible ×16 | pokedex.c draw + capture | ⏳ (L21) |
| Party popup | EM/FR | live sMenu read | | live | shipped hit_fieldmenu | ✅ EM / ⏳ FR |

**L24.** Any mismatch between a live L2 read and this table logs one line (the touch
logger) — the table is a tripwire for wrong-anchor resolution, not a source of truth.

---

## 5. Emulator PROOF PLAN (phase-22 lane A; census workflow owns the emulator until then)

The universal proof symbol chain (tier L-A): resolve anchor task → `listTaskId` →
`listBase` → **read `+26` (selectedRow) and `+24` (scrollOffset) before/after each
gesture** over gdb. The touch logger's existing pre/post-cursor capture wires the same
reads into the SD log for offline audit.

- **P-A (tap-select, per screen):** tap row r → `+26` becomes r on tick 0/1 and the A
  lands (inj column); the screen advances (buy: qty window task appears in the gs-ring
  taskFp; PC: action popup; bag: sub-menu). Negative: tap blank below a 3-item pocket →
  `+26` unchanged, zero A (proves the L3 clamp — the old wart is gone).
- **P-B (drag):** drag 5 rows → `+24`+`+26` sum advances by exactly 5 (one edge per
  14 px in the log); fling → monotone advance at the game's repeat cadence, stops within
  1 row of the hold's end; `+24` was never WRITTEN by us (code inspection + the write
  log — the driver has no write path to +24).
- **P-C (EM dex):** `sPokedexView`→`+0x60E` steps by exactly the injected edge count;
  tap-highlight → A → gs-ring cb2 stays `CB2_Pokedex`, taskFp edge to the entry-page
  task; L21 derivation session: log `selectedPokemon`/`listVOffset`/`initialVOffset` at
  10 scroll positions and fit the slot formula (banked to the census dir before any
  relative-tap code).
- **P-D (discovery probe, FR dex + any unknown ListMenu screen):** scan the 16 gTasks
  for the ListMenu dummy-task signature (func == the `ListMenuDummyTask` address from
  the game's sym map — FR r1 value resolved at implementation ⏳), and validate the
  candidate by sanity: totalItems ≤ 1024, maxShowed ≤ 16, windowId < 32, window rect
  on-screen. One boot per screen answers "is this a ListMenu" with zero guessing.
- **P-E (mart E2E):** buy 1 Poké Ball scripted: tap row 0 → qty picker tap-confirm →
  yes/no (fmenu) → money strip redraw in the capture + `+26`/taskFp trail — the full
  gesture chain across all three sub-modes (list, qty, sMenu) in one proof.
- **P-F (FR parity):** after the rev1 anchor promotion lands (worktree S1a), repeat
  P-A/P-B on FR bag + buy; the FR item-pc slot (L17) and FR dex (L23) probes.

Hardware gate: the family ships only after one hardware session covering bag scroll-feel
(drag + fling cadence at real frame pacing) — frame-count constants (14 px, fling cap 60)
are re-tuned there if the emulated-vs-804MHz pacing differs (the standing SPEC-door Q6
caveat).

## 6. Deferred rows (same driver, later anchor work)

Mailbox E14, berry pouch E4 (FR `CB2_BerryPouchIdle` ✅ harvested), TM case E5
(`CB2_Idle` ✅ harvested — the census's catalog-gap #4 correction), move relearner E17,
pyramid bag D5, Teachy TV K1 (✅ harvested), decoration lists B17/I3, lottery/prize
multis (sMenu). Each needs: anchor task + listTaskId slot + one P-A/P-D probe. None need
new design.

## 7. Open questions

- Q1: fling-hold cap (60 frames) vs Gen-3 repeat start-delay — tune on emulator, confirm
  on hardware.
- Q2: EM pocket-index address (`gBagPosition`) vs derive-by-Δ-counting (L12) — pick at
  implementation.
- Q3: does GCTX_BAG survive sell mode (L16) and the qty picker (L15) or do those need
  their own task rows? One probe each.
- Q4: E3b detection hole root cause (L19).
