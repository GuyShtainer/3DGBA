# Gen-3 field-menu (sMenu) + bag touch — implementation spec

_Verified workflow (2026-06-09)._

Confirmed: only `gbacore_write8` exists, and the underlying mGBA core exposes `busWrite8`. For the bag (u16 list-task fields), the SPEC must add a `gbacore_write16` (mGBA's `busWrite16`) OR write two bytes. Row values are 0..7, so the high byte is always 0 — two `write8`s (low byte = row, high byte = 0) is the safe, minimal approach. I'll specify both options.

I now have everything. Here is the complete implementation SPEC.

---

# IMPLEMENTATION SPEC — GCTX_FIELDMENU (sMenu) + GCTX_BAG, Emerald + FRLG

Single source of truth for coding. All hex is GBA-bus absolute (EWRAM `0x02…`, IWRAM `0x03…`, ROM `0x08…`). All rects are GBA pixels: bottom game frame is 240×160; `gx∈[0,239]`, `gy∈[0,159]`; `gvalid=false` ⇒ tap was off-frame. Conventions mirror the existing party/target handlers exactly: hit-test → set static `{val,tick}` → `select_pulse`-style write-then-A. Thumb function pointers are compared masked with `& ~1u` (as the existing party/target code does).

Game codes already in `PROFILES[]`: `BPEE`=Emerald, `BPRE`=FireRed, `BPGE`=LeafGreen. FRLG share one RAM map (rows 17–19 of gamestate.c are identical for BPRE/BPGE) — every address below applies to **both** FR and LG.

---

## 0. New bus-write primitive (REQUIRED for the bag; not needed for the field menu)

The bag's live cursor fields are **u16** inside the ListMenu task. Only `gbacore_write8` exists today. Two options, in order of preference:

- **Option A (recommended): add `gbacore_write16`.** In `gbacore.h` after the `write8` decl:
  `void gbacore_write16(GbaCore* c, uint32_t addr, uint16_t val);`
  In `gbacore.c`: `void gbacore_write16(GbaCore* g, uint32_t a, uint16_t v){ g->core->busWrite16(g->core, a, v); }` (mGBA's `mCore` exposes `busWrite16` alongside `busWrite8`).
- **Option B (no new primitive): two `write8`s.** Row values are 0..7 (max 5–7), so the high byte is always 0. Write low byte = row at `+off`, high byte = 0 at `+off+1`. Functionally identical for these ranges. Use this only if `busWrite16` turns out unavailable in this mGBA build.

The field menu (sMenu) writes only **u8** cursorPos — no new primitive needed there.

---

## 1. GENERAL FIELD MENU — `GCTX_FIELDMENU` (sMenu shared cursor)

Covers the overworld START menu, script MULTICHOICE, and YES/NO — all driven by the single 12-byte `struct Menu sMenu`. The A-handler returns `sMenu.cursorPos` verbatim. **Select = write `sMenu.cursorPos` (u8) + pulse A.** For the **START menu only**, also write the mirror `sStartMenuCursorPos` (A dispatches on it).

### 1a. Addresses — sMenu (12-byte struct; offsets +1 top, +2 cursorPos, +4 maxCursorPos, +5 windowId, +8 optionHeight)

| Field | Emerald (BPEE) | FRLG (BPRE/BPGE) |
|---|---|---|
| sMenu base | `0x0203CD90` | `0x0203ADE4` |
| cursorPos (s8, +2) **← WRITE** | `0x0203CD92` | `0x0203ADE6` |
| top (u8, +1) | `0x0203CD91` | `0x0203ADE5` |
| maxCursorPos (s8, +4) = count-1 | `0x0203CD94` | `0x0203ADE8` |
| windowId (u8, +5) | `0x0203CD95` | `0x0203ADE9` |
| optionHeight (u8, +8) = row pitch px | `0x0203CD98` | `0x0203ADEC` |

### 1b. Addresses — gWindows (12-byte stride; +1 tilemapLeft, +2 tilemapTop, +3 width, +4 height, +0 bg). WINDOW_NONE = `0xFF`.

| | Emerald | FRLG |
|---|---|---|
| gWindows base | `0x02020004` | `0x020204B4` |

Entry = `gWindows_base + 12*windowId`. Read `tilemapLeft(+1)`, `tilemapTop(+2)`, `width(+3)` from the **live** entry `gWindows[sMenu.windowId]`.

### 1c. Addresses — START menu dispatch + detection callbacks

| | Emerald | FRLG |
|---|---|---|
| START mirror `sStartMenuCursorPos` (u8) **← also write for START** | `0x0203760E` | `0x020370F4` |
| START active flag (fn-ptr global) | `gMenuCallback` `0x03005DF4` | `sStartMenuCallback` `0x020370F0` |
| …equals (Thumb, masked-compare target after `& ~1u`) | `HandleStartMenuInput` `0x0809FAC4` | `StartCB_HandleInput` `0x0806F280` |
| gMain.callback2 (already in profile as `mainCb2`) | `0x030022C4` | `0x030030F4` |

(`gSpecialVar_Result` EM `0x020375F0` / FR `0x020370D0` is informational only — not used for select.)

### 1d. DETECTION (in `game_read`, field/overworld branch — `!inBattle`)

The field menu is an **overworld** overlay. Currently `!inBattle` returns `GCTX_OVERWORLD` immediately (gamestate.c:50). Insert the field-menu check **before** that return so the menu takes precedence over tap-to-walk.

Robust signal (per the findings, you cannot enumerate active tasks from RAM): **`sMenu.windowId` points to a live window** AND, for START, the callback-pointer test. Concretely:

```
wid = read8(sMenu.windowId)
if (wid != 0xFF) {
    win = gWindows_base + 12*wid
    bg  = read8(win + 0)                  // WINDOW_NONE/freed -> 0xFF
    if (bg != 0xFF) {
        // a live sMenu window exists in the overworld
        maxc = (int8_t)read8(sMenu.maxCursorPos)
        if (maxc >= 0 && maxc <= 9) {     // sane choice count (1..10) -> menu is real
            out->ctx = GCTX_FIELDMENU;
            // stash for the handler:
            out->menuWinLeft   = read8(win+1);
            out->menuWinTop    = read8(win+2);
            out->menuWinWidth  = read8(win+3);
            out->menuTop       = read8(sMenu.top);          // +1
            out->menuPitch     = read8(sMenu.optionHeight); // +8  (16/15/14 per menu)
            out->menuMaxCursor = maxc;
            // START detection (so the handler also writes the mirror):
            uint32_t cb = read32(p->startCb) & ~1u;         // gMenuCallback / sStartMenuCallback
            out->menuIsStart = (cb == p->startCbInput);     // HandleStartMenuInput / StartCB_HandleInput
            return true;
        }
    }
}
out->ctx = GCTX_OVERWORLD; return true;   // existing path
```

**Risk / FLAG (medium):** the `bg != 0xFF` liveness check is a heuristic. A stale `sMenu.windowId` pointing at a window that was reallocated for an unrelated UI element could false-positive. The `maxCursorPos` sanity bound (0..9) plus the START callback test reduce this. If field-menu touch ever fires spuriously in the overworld, tighten by additionally requiring (for non-START menus) that a MULTICHOICE/YES-NO handler delay has elapsed — but those task vars (EM `sProcessInputDelay 0x02039F90`, FR `sDelay 0x02039988`; YES/NO 5-frame arm via task data) are not cleanly RAM-pollable, so this is **DEFERRED**: ship the heuristic, validate on hardware, tighten only if it misfires.

### 1e. HANDLER (touch.c) — write cursorPos (+ START mirror) then pulse A

Hit-test maps a tap to a row index using the **live** window template + sMenu pitch (no scroll — visible row == index):

```
rowYbase = menuWinTop*8 + menuTop
rowX0    = menuWinLeft*8                 // arrow column; text ~+8px right
rowW     = menuWinWidth*8
i = (gy - rowYbase) / menuPitch          // integer div
if (gx < rowX0 || gx >= rowX0 + rowW)  return -1   // outside window x-band -> no hit
if (i < 0 || i > menuMaxCursor)        return -1   // outside any row
return i
```

Select (new statics `s_fmenu=-1, s_fmenuTick=0`, reset fn `fmenu_reset`):

```
// in dispatch GCTX_FIELDMENU branch: reset the others, then:
if (newPress && gvalid) { int i = hit_fieldmenu(...); if (i >= 0) { s_fmenu = i; s_fmenuTick = 0; } }
// select_pulse-style, but conditionally write the START mirror too:
if (s_fmenu < 0 || !core) return 0;
gbacore_write8(core, sm->menuCursorAddr, (uint8_t)s_fmenu);          // sMenu.cursorPos
if (sm->menuIsStart) gbacore_write8(core, sm->startCursorAddr, (uint8_t)s_fmenu); // sStartMenuCursorPos
u16 k = (s_fmenuTick == 1) ? (1<<GBAKEY_A) : 0;
if (++s_fmenuTick >= 2) fmenu_reset();
return k;
```

This is the existing `menu_select` idiom (which already writes two addresses for the battle 2×2 grid) — here the second write is gated on `menuIsStart`.

### 1f. YES/NO — same sMenu, 2 rows. **row 0 = YES, row 1 = NO.** Pick YES → write cursorPos=0; NO → write cursorPos=1; then A.

Prefer the generic 1e formula reading the **live** `gWindows[sMenu.windowId]` (CreateWindowFromRect already bakes in the +1-tile border, so the live tilemapLeft/Top are screen-correct). The constant rects below are for reference / sanity:

- **Emerald** YES/NO window: tilemapLeft=21, tilemapTop=9, width=5 → origin (168,72) 40×32; top=1, pitch=16.
  YES ≈ x[168..207] y[73..88]; NO ≈ x[168..207] y[89..104].
- **FRLG** YES/NO window: tilemapLeft=21, tilemapTop=9, width=6 → origin (168,72) 48×32; top=2, pitch=14.
  YES ≈ x[168..215] y[74..87]; NO ≈ x[168..215] y[88..101].

### 1g. Per-menu top/pitch (informational — handler reads pitch LIVE from `sMenu.optionHeight`)

| Menu | EM top / pitch | FR top / pitch |
|---|---|---|
| START | 9 / 16 | 0 / 15 |
| MULTICHOICE | 1 / 16 | 2 / 14 |
| YES/NO | 1 / 16 | 2 / 14 |

Always trust the live `sMenu.optionHeight (+8)` and live window template rather than these constants (script multichoice positions vary by caller). These are only for cross-checking on hardware.

---

## 2. BAG MENU — `GCTX_BAG`

**Architecture (this dictates the select method):** the state structs `gBagPosition` (EM) / `gBagMenuState` (FR) are **OUTPUT MIRRORS** — the A-handler reads the **live** scroll/row inside the generic ListMenu **task**, not those globals. Writing the global alone does **not** move the selection. So the select must write the live list-task field, exactly as the findings (and the adversarial verify) conclude.

**Select = write the live in-window ROW field (list-task data offset +26), leave SCROLL (+24) as-is, pulse A next frame.** The A path returns `items[scroll+row].id/.index`, sets `gSpecialVar_ItemId`, opens the context menu.

### 2a. Locating the live ListMenu task (the one indirection)

```
gTasks base:  EM 0x03005E00,  FR 0x03005090.   Task stride = 40 bytes; data[] begins at task+8.
Bag handler Task_BagMenu_HandleInput (Thumb, masked target):
    EM 0x081ABD28 (compare read32 & ~1u == 0x081ABD28),  FR 0x08108F0C.
Step 1: scan the 16 gTasks entries; find the one whose func (read32 at task+0, & ~1u) == handler
        AND isActive (read8 at task+4) != 0.            // isActive is the first of 4 single bytes after func
Step 2: read that task's data[0] (s16 at task+8) = listTaskId.
Step 3: live list-task base = gTasks_base + 40*listTaskId + 8.
        live ROW   = base + 26   (u16)   ← WRITE this
        live SCROLL= base + 24   (u16)   ← read-only (leave as-is)
```

Field-name note (does not change the math — both fields live at the same +24/+26 offsets in the ListMenu struct):
- **EM**: +24 = `scrollOffset` (scroll), +26 = `selectedRow` (row).
- **FRLG**: names are **reversed** — +24 = `cursorPos` (which is the **scroll**), +26 = `itemsAbove` (which is the **row**). You still write +26 for the row in both games.

### 2b. State-struct mirrors (for detection + pocket index; NOT the select target)

| | Emerald (`gBagPosition` @ `0x0203CE58`, size 0x1C) | FRLG (`gBagMenuState` @ `0x0203ACFC`, size 0x14) |
|---|---|---|
| pocket | u8 @ `0x0203CE5D` (+0x05) | **u16** @ `0x0203AD02` (+0x06) |
| SCROLL[pocket] | `scrollPosition` u16 @ `0x0203CE58 + 0x12 + 2*pocket` | `cursorPos` u16 @ `0x0203AD0E + 2*pocket` (name reversed = scroll) |
| ROW[pocket] | `cursorPosition` u16 @ `0x0203CE58 + 0x08 + 2*pocket` | `itemsAbove` u16 @ `0x0203AD08 + 2*pocket` (name reversed = row) |
| bagOpen flag | — | bool8 @ `0x0203AD01` (+0x05) = TRUE while bag is up |

(`gBagMenu` ptr EM `0x0203CE54` / `sBagMenuDisplay` ptr FR `0x0203AD10` hold item counts — needed only for advanced cancel/clamp math; not required for v1.)

### 2c. DETECTION (new top-level branch in `game_read`)

The bag is **not** gated by `inBattle` (battle "ITEM" and field both use the same list/handler). Add a check **before** the `!inBattle` overworld return, ideally right after the SaveBlock read, so the bag is caught whether opened from the field or from battle:

```
// callback2 == CB2_BagMenuRun (Thumb, masked). Reuses the profile's mainCb2 (gMain.callback2).
uint32_t cb2 = read32(p->mainCb2) & ~1u;
if (cb2 == p->cb2BagRun) {                                 // EM 0x081AAD5C, FR 0x08107EE0
    // FR: also require bagOpen byte == TRUE for extra safety (EM has no such flag)
    bool fr = (p->code[2] == 'R' || p->code[2] == 'G');    // BPRE/BPGE
    if (!fr || read8(p->bagOpen) /*0x0203AD01*/ ) {
        // Confirm a live handler task exists & grab listTaskId (2a). If none active, bag not tappable yet.
        if (find_bag_list_task(c, p, &listTaskId)) {
            out->ctx = GCTX_BAG;
            out->bagListTaskBase = gTasks_base + 40*listTaskId + 8;  // +26 row, +24 scroll
            out->bagVisibleRows  = (fr ? 6 : 8);                     // maxShowed cap; see 2d
            return true;
        }
    }
}
```

`cb2BagRun` and `bagOpen` are **new GameProfile fields** (see §3). `gTasks_base` and the bag handler Thumb addr can be new profile fields too, or constants keyed off the game code — prefer profile fields for cleanliness.

### 2d. LAYOUT — visible-row rects, pitch, count

Row pitch is **16 px in both games**. Map a tap to a visible row `r`, then write `live ROW = r`.

| | Emerald | FRLG |
|---|---|---|
| list window | WIN_ITEM_LIST: left=14,top=2,w=15,h=16 | window 0: left=11,top=1,w=18,h=12 |
| x-band (GBA px) | `[112 .. 232)` | `[88 .. 232)` |
| y-band (GBA px) | `[16 .. 144)` | `[8 .. 104)` |
| pitch | 16 | 16 |
| visible rows (maxShowed cap) | 8 | 6 |
| row r band | y ∈ [16+16r, 32+16r) | y ∈ [8+16r, 24+16r) |
| tap → row | `r = (gy-16)/16` | `r = (gy-8)/16` |

Hit-test:
```
// EM:
if (gx < 112 || gx >= 232 || gy < 16 || gy >= 144) return -1;
r = (gy - 16) / 16;
// FR:
if (gx < 88 || gx >= 232 || gy < 8 || gy >= 104) return -1;
r = (gy - 8) / 16;
// both: clamp 0 .. bagVisibleRows-1
if (r < 0) r = 0; if (r >= visibleRows) r = visibleRows-1;
return r;
```

**Note (clamp vs. actual shown count):** `bagVisibleRows` is the *maximum* (8/6). The actual rows shown is `min(max, nItems+1)`. Tapping a blank area below the last real row maps to the highest visible row — which, when the pocket is short, is the **CANCEL/CLOSE BAG** row. That is acceptable behavior (tap empty space → close). If you want to suppress taps below the real list, read the item count from the heap struct (EM `gBagMenu→numItemStacks[pocket]`, FR `sBagMenuDisplay→nItems[pocket]`) — **DEFERRED** as a polish item; not required for v1.

### 2e. SELECT HANDLER (touch.c) — write live ROW (+26) then pulse A

New statics `s_bag=-1, s_bagTick=0`, reset `bag_reset`:

```
// dispatch GCTX_BAG branch: reset others, then:
if (newPress && gvalid) { int r = hit_bag(gx, gy, sm); if (r >= 0) { s_bag = r; s_bagTick = 0; } }
if (s_bag < 0 || !sm->core || !sm->bagListTaskBase) return 0;
gbacore_write16(sm->core, sm->bagListTaskBase + 26, (uint16_t)s_bag);   // live ROW  (Option A)
//  -- OR Option B (no write16): write8(base+26, (u8)s_bag); write8(base+27, 0);
u16 k = (s_bagTick == 1) ? (1<<GBAKEY_A) : 0;
if (++s_bagTick >= 2) bag_reset();
return k;
```

Optional mirror-write (consistency only; not read by A): also write the state-struct ROW (EM `cursorPosition[pocket]`, FR `itemsAbove[pocket]`) — skip for v1.

### 2f. CANCEL / CLOSE BAG

The bottom list row is **CLOSE BAG**. Selecting it returns LIST_CANCEL (EM: that row's `.id = LIST_CANCEL`; FR: synthetic CANCEL entry at index == `nItems[pocket]`) → handler sets `gSpecialVar_ItemId = ITEM_NONE` and closes. **Mechanism note (corrected by adversarial verify):** it is NOT "default-case index numItemStacks-1"; it is the row carrying `LIST_CANCEL`. **Outcome for us is the same:** tapping the bottom visible row closes the bag, no special-casing needed in the handler — write that row + A.

### 2g. BAG SCROLLING — **DEFERRED**

v1 selects **only currently-visible rows**; SCROLL (+24) is left untouched. Off-screen items require D-pad scroll injection or a +24 write with a matching screen redraw, which the A-handler timing and the ListMenu redraw path make racy. Provide L/R-driven pocket switch and scroll later. **Do not write +24 in v1.** Pocket switching (L/R only) is likewise out of scope for touch v1.

---

## 3. STRUCT / WIRING CHANGES (so the new contexts plumb through cleanly)

**gamestate.h `GameCtx`** — add:
```
GCTX_FIELDMENU,   // overworld sMenu (START / MULTICHOICE / YES-NO) up
GCTX_BAG,         // bag menu open (field or battle)
```

**gamestate.h `GameProfile`** — add fields (EM, then FR/LG values):
```
uint32_t startCb;        // gMenuCallback 0x03005DF4 / sStartMenuCallback 0x020370F0
uint32_t startCbInput;   // HandleStartMenuInput 0x0809FAC4 / StartCB_HandleInput 0x0806F280  (compare masked)
uint32_t sMenuBase;      // 0x0203CD90 / 0x0203ADE4   (cursorPos=+2, top=+1, max=+4, windowId=+5, optHeight=+8)
uint32_t gWindowsBase;   // 0x02020004 / 0x020204B4
uint32_t startCursor;    // sStartMenuCursorPos 0x0203760E / 0x020370F4
uint32_t gTasksBase;     // 0x03005E00 / 0x03005090
uint32_t cb2BagRun;      // CB2_BagMenuRun 0x081AAD5C / 0x08107EE0   (compare masked)
uint32_t bagHandler;     // Task_BagMenu_HandleInput 0x081ABD28 / 0x08108F0C  (compare masked)
uint32_t bagOpen;        // FR gBagMenuState.bagOpen 0x0203AD01; EM unused -> set 0
```
(`mainCb2` already present is reused for the bag callback2 read.)

**gamestate.h `GameState`** — add the per-frame snapshot fields the handlers need:
```
// field menu (sMenu)
int  menuWinLeft, menuWinTop, menuWinWidth;   // live window template (tiles)
int  menuTop, menuPitch, menuMaxCursor;        // live sMenu +1/+8/+4
bool menuIsStart;                              // write sStartMenuCursorPos too
uint32_t menuCursorAddr, startCursorAddr;      // sMenuBase+2 ; profile startCursor
// bag
uint32_t bagListTaskBase;                      // gTasksBase + 40*listTaskId + 8
int  bagVisibleRows;                           // 8 EM / 6 FR
```
(Or carry the raw profile in `TouchSmart->prof`, which is already passed, and have touch.c read `prof->sMenuBase`/`prof->startCursor` directly; only the *computed* per-frame values — window template, listTaskBase, isStart, pitch — must come through GameState since they're live reads. Either split is fine; minimize new fields by keeping addresses in `prof` and only snapshotting the live-read values.)

**touch.h `TouchSmart`** — mirror whichever GameState fields the handlers read (same pattern as the existing `actionAddr`/`moveAddr`/`partyLayout` mirroring): the field-menu live values (`menuWinLeft/Top/Width`, `menuTop`, `menuPitch`, `menuMaxCursor`, `menuIsStart`) and the bag (`bagListTaskBase`, `bagVisibleRows`). `core` and `prof` are already present.

**main.c (≈line 468–477)** — extend the `game_read` → `sm` copy block to forward the new fields, exactly like the existing `sm.partyCount = gsr.partyCount;` lines.

**touch.c dispatch (`touch_update` switch)** — add two cases mirroring the party/target cases (reset the others, hit-test on `newPress && gvalid`, then the write+A pulse):
```
case GCTX_FIELDMENU: battle_reset(); walk_reset(); party_reset(); target_reset(); bag_reset();   ... fmenu select ...
case GCTX_BAG:       battle_reset(); walk_reset(); party_reset(); target_reset(); fmenu_reset();  ... bag select ...
```
and add `fmenu_reset()`/`bag_reset()` to `all_reset()`.

---

## 4. RISK / DEFER FLAGS (read before coding)

- **FLAG (medium) — field-menu detection is heuristic.** `sMenu.windowId` live + `gWindows[wid].bg != 0xFF` + `maxCursorPos∈[0,9]` is the only RAM-pollable signal (active-task enumeration isn't feasible). START adds the callback-ptr test. Validate on hardware that it doesn't misfire in the plain overworld; if it does, tighten per §1d. The MULTICHOICE/YES-NO startup-delay arming (EM `sProcessInputDelay 0x02039F90`, FR `sDelay 0x02039988`; YES/NO 5-frame arm) is **DEFERRED** — pressing A a frame or two early just no-ops in those handlers, so the cost of skipping it is low.
- **FLAG (medium) — bag select needs the live-task indirection,** not the convenient global. Must (1) add/confirm `gbacore_write16` (or use the two-write8 fallback, §0 Option B), and (2) scan gTasks for the handler to get `listTaskId`. Both are deterministic with the offsets above. If the scan finds no active handler task, treat the bag as not-yet-tappable and fall through (don't force `GCTX_BAG`).
- **DEFER — bag scrolling + pocket switch** (§2g): v1 is visible-rows-only; never write SCROLL (+24).
- **DEFER — bag tap-below-last-row suppression** (§2d note): acceptable to let an empty-area tap hit the highest visible row (often CLOSE BAG). Add item-count clamp later if desired.
- **CONFIRMED, low risk:** all sMenu/gWindows/bag addresses, the 16-px bag pitch (both games), the EM 8 / FR 6 visible-row counts, the FRLG `cursorPos`/`itemsAbove` name reversal, YES/NO row mapping (0=YES, 1=NO), and that `gBagPosition`/`gBagMenuState` are output mirrors (write the live task, not them). Every address re-derived and matched against the sym maps in the supplied verification.
- **Anchor note (not a bug):** FR `gMain` base is `0x030030F0`; the profile's `mainCb2 = 0x030030F4` (gMain+0x04) is correct and already in `PROFILES[]`. Reuse it for the bag callback2 read.

---

## 5. Files to edit
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/gamestate.h` — `GameCtx` enum, `GameProfile` fields, `GameState` fields.
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/gamestate.c` — `PROFILES[]` rows (new per-game hex), `game_read` field-menu + bag detection branches.
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/touch.h` — `TouchSmart` mirror fields.
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/touch.c` — `hit_fieldmenu`/`hit_bag`, `fmenu_reset`/`bag_reset`, the two dispatch cases, `all_reset`.
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/main.c` — extend the `gsr → sm` copy block (~L468–477).
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/gbacore.h` + `gbacore.c` — add `gbacore_write16` (§0 Option A) unless using the two-write8 fallback.