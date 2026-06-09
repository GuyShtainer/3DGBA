# Gen-3 smart-touch features — implementation spec

_Verified `gen3-touch-features-research` workflow (2026-06-09)._

I now have the complete picture: the existing code's conventions (cell encoding, write-both-slots select, deterministic tick-based A press, the `walk_update` axis-follow, the `GameProfile`/`GameState`/`TouchSmart` structs), the bus accessor signatures, and key constants. The verified RE findings are extensive and cross-checked. I have everything needed to produce the single source-of-truth spec aligned to this exact codebase.

One important reconciliation I notice: the existing `gamestate.c` profiles use **`gBattleMons` EM 0x02024084 / FRLG 0x02023BE4**, while the target-select findings reference `gActiveBattler` EM 0x02024064 / FRLG 0x02023BC4, etc. — these are consistent (adjacent symbols). I'll fold the new addresses into the spec using the same `GameProfile` extension pattern the code already uses.

Here is the spec.

---

# DUAL-GBA SMART-TOUCH — IMPLEMENTATION SPEC (Gen-3 Pokémon: Emerald + FRLG)

Single source of truth for three new SMART-touch features. Aligns to the existing `source/gamestate.{c,h}` + `source/touch.c` conventions (cell encoding `cell = (col&1) | (row<<1)`, write-cursor-then-pulse-A deterministic select, tick-based A press, `walk_update` axis-follow, `GameProfile`/`GameState`/`TouchSmart`). All addresses are absolute GBA bus addresses, US v1.0/rev0, verified against pret's byte-matched `symbols`-branch sym maps. **FRLG = FireRed; LeafGreen (BPGE) shares the FireRed RAM map** but its target-select / party-menu / overworld addresses below are FRLG-derived and **LeafGreen-UNVERIFIED for the NEW symbols** (see Risks). Screen space is GBA pixels: x 0..239, y 0..159. Bottom-game core only.

Bus accessors (from `gbacore.h`): `gbacore_read8/16/32(core, addr)`, `gbacore_write8(core, addr, val)`. Note **only `write8` exists** — multi-byte writes are 2× `write8` (little-endian: low byte first). Reads/writes are main-thread-only and **skipped while a link is active** (existing rule, gen3-ram-touch.md). Key bit indices: `GBAKEY_A=0 B=1 SELECT=2 START=3 RIGHT=4 LEFT=5 UP=6 DOWN=7 R=8 L=9`; mask = `1<<GBAKEY_x`.

---

## 0. SHARED CHANGES — extend GameProfile, GameState, GameCtx

### 0.1 New contexts (`gamestate.h`, extend the `GameCtx` enum)
```c
typedef enum {
    GCTX_NONE = 0,
    GCTX_OVERWORLD,
    GCTX_BATTLE_ACTION,
    GCTX_BATTLE_MOVE,
    GCTX_BATTLE_TARGET,   // NEW: double-battle "choose a target" (HandleInputChooseTarget live)
    GCTX_PARTY,           // NEW: party menu open (choose / send-out / use-item)
    GCTX_BATTLE_OTHER
} GameCtx;
```
Keep `GCTX_BATTLE_OTHER` last-resort. `GCTX_BATTLE_TARGET` must be tested **before** `GCTX_BATTLE_ACTION/MOVE` doesn't apply — target-select is its own controller-func state and coexists with `gBattleTypeFlags != 0`; detect it by the controller-func pointer (§3), not by `bg0y`.

### 0.2 Extend `GameProfile` (`gamestate.h`) — all new addresses in one struct
Append these fields (keep existing 6). EM = BPEE, FR = BPRE/BPGE.

```c
typedef struct {
    char     code[5];
    uint32_t sb1ptr;        // gSaveBlock1Ptr            EM 0x03005D8C  FR 0x03005008
    uint32_t battleFlags;   // gBattleTypeFlags          EM 0x02022FEC  FR 0x02022B4C
    uint32_t actionCursor;  // gActionSelectionCursor[0] EM 0x020244AC  FR 0x02023FF8
    uint32_t moveCursor;    // gMoveSelectionCursor[0]   EM 0x020244B0  FR 0x02023FFC
    uint32_t battleMons;    // gBattleMons[0]            EM 0x02024084  FR 0x02023BE4
    uint32_t bg0y;          // gBattle_BG0_Y             EM 0x02022E16  FR 0x02022976
    // --- NEW: party menu ---
    uint32_t partyMenu;     // gPartyMenu base           EM 0x0203CEC8  FR 0x0203B0A0
    uint32_t partyCount;    // gPlayerPartyCount (u8)    EM 0x020244E9  FR 0x02024029
    uint32_t mainCb2;       // gMain.callback2 (u32)     EM 0x030022C4  FR 0x030030F4
    uint32_t cb2UpdParty;   // CB2_UpdatePartyMenu       EM 0x081B01B0  FR 0x0811EBA0
    uint32_t cb2InitParty;  // CB2_InitPartyMenu         EM 0x081B01E0  FR 0x0811EBD0
    uint32_t newKeys;       // gMain.newKeys (u16,+0x2E) EM 0x030022EE  FR 0x0303011E
    // --- NEW: double-battle target select ---
    uint32_t ctrlFuncs;     // gBattlerControllerFuncs[] EM 0x03005D60  FR 0x03004FE0  (4 u32 ptrs)
    uint32_t chooseTarget;  // HandleInputChooseTarget   EM 0x08057824  FR 0x0802E674  (compare |1)
    uint32_t multiCursor;   // gMultiUsePlayerCursor (u8)EM 0x03005D74  FR 0x03004FF4
    uint32_t battlerPos;    // gBattlerPositions (u8[4]) EM 0x02024076  FR 0x02023BD6
    uint32_t battlersCount; // gBattlersCount (u8)       EM 0x0202406C  FR 0x02023BCC
    uint32_t absentFlags;   // gAbsentBattlerFlags (u8)  EM 0x02024210  FR 0x02023D70
    uint32_t activeBattler; // gActiveBattler (u8)       EM 0x02024064  FR 0x02023BC4
    // --- NEW: overworld pathfinding map ---
    uint32_t mapLayout;     // gBackupMapLayout / VMap   EM 0x03005DC0  FR 0x03005040  (s32 w, s32 h, u16* map)
} GameProfile;
```

### 0.3 Profile table (`gamestate.c`) — replace `PROFILES[]`
```c
static const GameProfile PROFILES[] = {
  // code   sb1ptr      battleFlags  actionCur   moveCur     battleMons  bg0y
  // partyMenu   partyCount  mainCb2     cb2Upd      cb2Init     newKeys
  // ctrlFuncs   chooseTgt   multiCursor battlerPos  battlersCnt absentFlg   activeBat    mapLayout
  { "BPEE", 0x03005D8Cu, 0x02022FECu, 0x020244ACu, 0x020244B0u, 0x02024084u, 0x02022E16u,
            0x0203CEC8u, 0x020244E9u, 0x030022C4u, 0x081B01B0u, 0x081B01E0u, 0x030022EEu,
            0x03005D60u, 0x08057824u, 0x03005D74u, 0x02024076u, 0x0202406Cu, 0x02024210u, 0x02024064u, 0x03005DC0u },
  { "BPRE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u },
  { "BPGE", 0x03005008u, 0x02022B4Cu, 0x02023FF8u, 0x02023FFCu, 0x02023BE4u, 0x02022976u,
            0x0203B0A0u, 0x02024029u, 0x030030F4u, 0x0811EBA0u, 0x0811EBD0u, 0x0303011Eu,
            0x03004FE0u, 0x0802E674u, 0x03004FF4u, 0x02023BD6u, 0x02023BCCu, 0x02023D70u, 0x02023BC4u, 0x03005040u },
};
```
**BPGE risk:** these new addresses are FireRed values copied to LeafGreen. They are *expected* equal (shared engine) but **not verified against pokeleafgreen.sym**. Verify before shipping LG (see Risks).

### 0.4 Extend `GameState` (`gamestate.h`)
```c
typedef struct {
    bool    valid;
    GameCtx ctx;
    int     px, py;
    int     actionCursor;
    int     moveCursor;
    bool    moveValid[4];
    // --- NEW party ---
    int     partySlotSel;     // gPartyMenu.slotId (s8), -1 if N/A
    int     partyCount;       // gPlayerPartyCount, -1 if N/A
    int     partyLayout;      // 0=SINGLE 1=DOUBLE 2=MULTI, -1 if N/A
    // --- NEW target ---
    int     targetCursor;     // gMultiUsePlayerCursor (battler index 0..3), -1 if N/A
    int     activeBattler;    // gActiveBattler, -1 if N/A
    int     battlersCount;    // -1 if N/A
    uint8_t absentMask;       // gAbsentBattlerFlags
    uint8_t battlerPos[4];    // gBattlerPositions[0..3] (only valid if battlersCount>=0)
} GameState;
```

### 0.5 Detection ordering in `game_read()` (`gamestate.c`)
Within the `gBattleTypeFlags != 0` branch, test contexts in this priority:
1. **Target-select** (§3 detector) → `GCTX_BATTLE_TARGET`. Test first; it's an independent controller state.
2. Else `bg0y == 160` → `GCTX_BATTLE_ACTION` (existing).
3. Else `bg0y == 320` → `GCTX_BATTLE_MOVE` (existing).
4. Else **party-menu-in-battle** detector (§1) with `menuType` low-nibble == 1 → `GCTX_PARTY`.
5. Else `GCTX_BATTLE_OTHER`.

In the overworld branch (`gBattleTypeFlags == 0`), also run the **field party-menu** detector (§1); if live → `GCTX_PARTY` (party can open in the field for item use). Otherwise `GCTX_OVERWORLD`.

`TouchSmart` (touch.h) gets a `const GameProfile* prof;` pointer added so the touch handlers can reach the new addresses (or copy the specific addrs into `TouchSmart` as is done today for `actionAddr`/`moveAddr` — pick one; passing `prof` is cleaner given the field count). Main fills `sm->prof`, `sm->core`, and the GameState-derived fields each frame.

---

## 1. PARTY-MENU TOUCH

### 1.1 Detection — "is the party menu interactive right now?"
Two reads; require BOTH (robust against fade frames).

```c
// (a) callback2 currently the party-menu updater (mask Thumb bit0)
uint32_t cb = gbacore_read32(core, p->mainCb2) & ~1u;
bool cbParty = (cb == p->cb2UpdParty);          // interactive
// (during fade-in it's cb2InitParty — treat as "opening", not yet tappable; ignore)

// (b) menuType nibble at gPartyMenu+0x08; 1 == IN_BATTLE, 0 == FIELD
uint8_t mt8 = gbacore_read8(core, p->partyMenu + 0x08);
uint8_t menuType = mt8 & 0x0F;
uint8_t layout   = (mt8 >> 4) & 0x03;           // 0 SINGLE, 1 DOUBLE, 2 MULTI
```
Party menu is live iff `cbParty`. `menuType==1` ⇒ in-battle send-out (battle branch); `menuType==0` ⇒ field menu (overworld branch). `layout` selects the rect table (§1.4).

**No false positive on battle dialog:** a battle text box has `callback2` pointing at a battle CB, so `cbParty` is false. Good.

### 1.2 slotId — the cursor, and the select method
- **slotId address** = `gPartyMenu + 0x09` (s8). **EM 0x0203CED1 / FR 0x0203B0A9.** This is the value the A-press reads (via `GetCurrentPartySlotPtr` → `&slotId` for choose/send-out/use-item actions; only SWITCH/SOFTBOILED use slotId2, which we do not drive).
- **slotId meaning:** `0..5` = party mons; `6` = Confirm (only present when chooseHalf — not on a normal send-out, ignore); `7` = Cancel.
- **SELECT METHOD = write slotId, then pulse A** (same deterministic pattern as `menu_select()` in touch.c — write the cursor straight to RAM, then one-frame A; no closed loop). This reuses the game's own selection path; the handler reads live `slotId` at A-press, there is no separate authoritative cursor.
  - A injection: write `gMain.newKeys` (`p->newKeys`, **EM 0x030022EE / FR 0x0303011E**), value `A_BUTTON = 0x0001`. It's a u16 → two `write8`: low byte 0x01 at `newKeys+0`, high byte 0x00 at `newKeys+1`. (Cohabits with the worker's own key plumbing the same way the existing battle path injects A via the returned key mask — **prefer returning `1<<GBAKEY_A` from the touch handler** rather than writing `newKeys` directly, to match how this codebase injects A everywhere else. Write only `slotId` to RAM; let the returned mask carry A. This keeps one injection path.)
- **Cosmetic caveat (accept):** the highlight sprite won't animate to the written slot (`AnimatePartySlot` only runs on D-pad moves). Selection logic is correct regardless. Do not attempt D-pad cursor-walk for v1.

**Guard (necessary):** reject `slot >= partyCount` unless `slot == 7` (Cancel) — `HandleChooseMonSelection` has no internal bound check and would act on an empty slot. `partyCount` = `gPlayerPartyCount` (`p->partyCount`, **EM 0x020244E9 / FR 0x02024029**, u8).

### 1.3 The select state machine (mirror `menu_select`)
Add a party analog to touch.c:
```c
static int s_partyTarget = -1, s_partyTick = 0;
static void party_reset(void){ s_partyTarget = -1; s_partyTick = 0; }

// called while ctx==GCTX_PARTY; base=p->partyMenu, slotAddr=base+0x09
static u16 party_select(GbaCore* core, uint32_t slotAddr){
    if (s_partyTarget < 0 || !core) return 0;
    gbacore_write8(core, slotAddr, (uint8_t)s_partyTarget);   // tick0: write slot; tick1: write+A
    u16 k = (s_partyTick == 1) ? (1 << GBAKEY_A) : 0;
    if (++s_partyTick >= 2) party_reset();
    return k;
}
```
On a new tap (newPress && gvalid): map pixel → slot via the layout table (§1.4) or Cancel rect; apply the partyCount guard; if valid set `s_partyTarget = slot; s_partyTick = 0;`.

### 1.4 Per-layout slot rects (GBA px) — rect = WindowTemplate{L,T,W,H}×8, EM/FRLG byte-identical
Hit-test: a tap at (gx,gy) is in slot s iff `x∈[L,R) && y∈[T,B)`. Reject slots `>= partyCount`.

**SINGLE layout (`layout==0`; single-battle send-out & most field menus)** — `sSinglePartyMenuWindowTemplate`:

| slotId | rect (px) x:[L..R) y:[T..B) |
|---|---|
| 0 | x 8..88,   y 24..80  |
| 1 | x 96..240, y 8..32   |
| 2 | x 96..240, y 32..56  |
| 3 | x 96..240, y 56..80  |
| 4 | x 96..240, y 80..104 |
| 5 | x 96..240, y 104..128|

**DOUBLE layout (`layout==1`; double-battle send-out)** — `sDoublePartyMenuWindowTemplate`:

| slotId | rect (px) |
|---|---|
| 0 | x 8..88,   y 8..64   |
| 1 | x 8..88,   y 64..120 |
| 2 | x 96..240, y 8..32   |
| 3 | x 96..240, y 40..64  |
| 4 | x 96..240, y 72..96  |
| 5 | x 96..240, y 104..128|

**CANCEL** — `sCancelButtonWindowTemplate {24,17,6,2}` → **x 192..240, y 136..152** (both layouts, both games). Tap → set `s_partyTarget = 7`, pulse A (returns B/cancel internally).

Pick the table from the live `layout` nibble read in §1.1, not from battle flags. (MULTI layout `layout==2`: not handled in v1 — its Cancel sits at y 144..160; fall through to GCTX_BATTLE_OTHER / advance-text. Confirm button at y 128..144 only exists for chooseHalf and is not mapped.)

### 1.5 Touch dispatch addition (touch.c `touch_update`)
Add a branch (before the BATTLE_OTHER fallback):
```c
if (sm && sm->valid && sm->ctx == GCTX_PARTY) {
    battle_reset(); walk_reset();
    uint32_t slotAddr = sm->prof->partyMenu + 0x09;
    if (newPress && gvalid) {
        int slot = hit_party(gx, gy, sm->partyLayout);      // §1.4; returns 0..5, 7(Cancel), or -1
        if (slot == 7 || (slot >= 0 && slot < sm->partyCount)) { s_partyTarget = slot; s_partyTick = 0; }
    }
    return party_select(sm->core, slotAddr);
}
```
Reset `party_reset()` in the PAD/OFF branches and whenever ctx leaves GCTX_PARTY (add to the other branches' resets, same as `battle_reset()/walk_reset()` today).

---

## 2. TAP-TO-WALK PATHFINDING (BFS)

The current `walk_update` is a **greedy axis-follow** (X then Y, stall→A). This spec **replaces the routing** with BFS over the live collision grid so it routes around walls; the **follow loop, stall detection, tap-self→A, and arrival** logic stay as-is. Map→screen anchor and tap-self handling are unchanged from the existing code.

### 2.1 Map-layout struct in RAM
`struct BackupMapLayout` (12 bytes, EM `gBackupMapLayout` / FR `VMap`), base = `p->mapLayout` (**EM 0x03005DC0 / FR 0x03005040**):
```c
int32_t  width  = (int32_t)gbacore_read32(core, p->mapLayout + 0x00);  // padded width  (real + 15)
int32_t  height = (int32_t)gbacore_read32(core, p->mapLayout + 0x04);  // padded height (real + 14)
uint32_t mapPtr = gbacore_read32(core, p->mapLayout + 0x08);           // u16* into EWRAM, row-major
```
**Re-read all three on every BFS (re)plan** — they change on map transitions. Sanity: `(mapPtr>>24)==0x02`, `width`/`height` in (0, 256] say; bail to greedy/stall if not.

### 2.2 Collision read + walkability
`MAP_OFFSET = 7`. SaveBlock1 player coords `(px,py)` are 0-based map-local; bias by +7 to index the grid:
```c
// block u16 at world tile (wx, wy):
int gx_ = wx + 7, gy_ = wy + 7;
if (gx_ < 0 || gx_ >= width || gy_ < 0 || gy_ >= height) return BLOCKED; // border
uint32_t a = mapPtr + 2u * (uint32_t)(gx_ + width * gy_);
uint16_t block = gbacore_read16(core, a);
if (block == 0x03FF) return BLOCKED;                 // MAPGRID_UNDEFINED
uint8_t collision = (block & 0x0C00) >> 10;          // bits 10-11
// walkable v1: collision == 0
```
Masks (both games): metatileId `&0x03FF`, collision `(>>10)&3` via `&0x0C00`, elevation `(>>12)` via `&0xF000`, sentinel `0x03FF`. **walkable(v1) = `collision==0 && block!=0x03FF`.**

### 2.3 Player coords (already in GameState)
`gSaveBlock1Ptr` (`p->sb1ptr`), deref (high byte must be 0x02), first two s16 = `pos.x` (+0x00), `pos.y` (+0x02). Already read into `out->px/py`. BFS start = `(px,py)`.

### 2.4 Screen→tile (unchanged anchor)
Camera pins the player at on-screen tile **(col 7, row 5)**:
```c
tcx = gx / 16; tcy = gy / 16;       // tapped on-screen cell, gx∈0..239 gy∈0..159
worldTileX = px + (tcx - 7);
worldTileY = py + (tcy - 5);
if (tcx == 7 && tcy == 5) -> tap self -> A (keep existing s_aPulse path)
```
**Row anchor (5) is UNCERTAIN ±1** — calibrate once on hardware (column 7 is solid). Keep it a named constant for easy tweak.

### 2.5 BFS plan (compute once per tap; store the path)
- **Grid bound for search:** clamp to a window around the player to keep it cheap and avoid scanning a huge padded map. Recommended search box: world tiles within **±32** of the player on each axis (65×65 = 4225 cells max). A tap maps to at most the visible 15×10 area anyway; ±32 is generous headroom for routing around obstacles.
- **Visited / parent storage:** static arrays sized to the search box. Use `int16_t parent[BOX*BOX]` (or store came-from direction as `int8_t dir[BOX*BOX]`) + `uint8_t visited[BOX*BOX]`. With BOX=65 that's ~4 KB for parent — fine. Index local coords `(lx,ly)=(wx-px+32, wy-py+32)`.
- **Queue:** ring buffer `int16_t q[BOX*BOX]` storing the packed local index `ly*BOX+lx`. Max size = number of cells = 4225; size it to BOX*BOX.
- **Neighbors:** 4-connected, order RIGHT, LEFT, DOWN, UP (matches the existing X-first bias; cosmetic). Step is ±1 in exactly one axis. **No diagonals** (the engine has none).
- **Goal:** the tapped `(worldTileX, worldTileY)`. If the goal tile is itself BLOCKED, **fail the plan** (do nothing, or optionally snap to nearest walkable neighbor of the goal — v1: just ignore the tap).
- **Reconstruct:** walk parent pointers goal→start, push directions, reverse into a path of **directions** (`int8_t pathDir[N]`, each a GBAKEY_{RIGHT,LEFT,DOWN,UP}); store length and a cursor. Cap N at BOX*BOX.
- **Cost:** uniform; BFS = shortest path. (A* unnecessary at this scale.)

```c
#define WBOX 65
#define WHALF 32
static int8_t  s_pathDir[WBOX*WBOX];
static int     s_pathLen, s_pathPos;
// scratch (reuse across plans): parent[], visited[], queue[]
static bool plan_bfs(GbaCore* core, const GameProfile* p,
                     int startX, int startY, int goalX, int goalY); // returns true & fills s_pathDir/s_pathLen
```

### 2.6 Follow loop (reuse existing stall logic, drive by path)
Replace `walk_update`'s greedy axis pick with: emit `s_pathDir[s_pathPos]` (hold that D-pad). Keep everything else:
- **Step complete:** when `(px,py)` reaches the *next node's* world tile (i.e. `pos` changed by the expected ±1), advance `s_pathPos`. Equivalently keep the existing "pos changed → advance" pattern but advance the path cursor instead of recomputing axis.
- **Arrival:** `s_pathPos >= s_pathLen` (or `px==goalX && py==goalY`) → `walk_reset()`.
- **Stall:** unchanged — if `(px,py)` unchanged for **>24 frames** while holding a direction, the tile was blocked by something not in the grid (NPC, ledge one-way, elevation, closed door). Action: **stop, drop key, re-plan BFS from the new pos once**; if the re-plan also stalls at the same cell, `walk_reset()` + face-and-A pulse (existing `s_aPulse=3`) so a tapped NPC/sign still gets interacted with. Cap re-plans (e.g. 2) to avoid thrash.
- **Map changed mid-walk:** if `width/height/mapPtr` differ from the plan's snapshot (door/warp), `walk_reset()` (don't blindly continue a stale path).
- **Tap-self → A:** unchanged (`s_aPulse`).

### 2.7 Hazards to IGNORE for v1 (documented limitations)
Collision-bit walkable does NOT capture these; v1 routes as if they're walkable and relies on **stall→replan/A** to recover:
- **Ledges** (one-way jumps): walkable by the bit, enforced by metatile *behavior* (`MB_JUMP_*`). v1 may route onto/over a ledge and stall. Acceptable. (Optional hardening later: read metatile behavior via the tileset attribute table and mark `MB_JUMP_*` non-walkable — heavier, deferred.)
- **Directionally-impassable metatiles** — behavior-driven, same as ledges.
- **Elevation mismatch** (bits 12-15) — bridges/stairs. v1 ignores elevation. (Optional cheap guard: only step between tiles whose elevation matches OR either side is 0 or 15.)
- **Object events / NPCs** — dynamic, not in the grid. v1 stalls then replans/interacts. This is the desired NPC-tap behavior.

### 2.8 Re-read rule
Read `width/height/mapPtr` at plan time and store a snapshot; re-read at each replan and to detect map change. Per-tile collision uses `read16` at `mapPtr + 2*idx`.

---

## 3. DOUBLE-BATTLE TARGET SELECT

### 3.1 Detection — "is target-select active?"
The authoritative detector is the **controller-func pointer**, not `bg0y`. `gBattlerControllerFuncs` (`p->ctrlFuncs`, **EM 0x03005D60 / FR 0x03004FE0**) is an array of 4 u32 function pointers. While picking a target, the active battler's entry == `HandleInputChooseTarget` (`p->chooseTarget`, **EM 0x08057824 / FR 0x0802E674**), **with Thumb bit0 set** → compare against `chooseTarget | 1`.

**Do NOT assume the human is battler index 0/2** (battler index ≠ position). **Scan all battlers:**
```c
int battlersCount = gbacore_read8(core, p->battlersCount);     // EM 0x0202406C / FR 0x02023BCC
bool targetSelect = false;
for (int b = 0; b < battlersCount && b < 4; b++) {
    uint32_t fn = gbacore_read32(core, p->ctrlFuncs + 4u*b) & ~1u;   // mask Thumb bit0
    if (fn == p->chooseTarget) { targetSelect = true; break; }
}
// targetSelect && gBattleTypeFlags!=0  -> GCTX_BATTLE_TARGET
```
`gActiveBattler` (`p->activeBattler`, **EM 0x02024064 / FR 0x02023BC4**) equals the choosing battler during its input frame (read for debug/diagnostics).

### 3.2 Cursor — the targeted battler index + select
- **Cursor** = `gMultiUsePlayerCursor` (`p->multiCursor`, **EM 0x03005D74 / FR 0x03004FF4**, u8). Holds the **battler INDEX** (0..3) currently targeted — NOT a position.
- On A, `HandleInputChooseTarget` emits `gMoveSelectionCursor[gActiveBattler] | (gMultiUsePlayerCursor << 8)` — it reads `gMultiUsePlayerCursor` **directly** at A-press, no validity recheck.
- **SELECT = write the battler index into `gMultiUsePlayerCursor`, then pulse A** (same deterministic pattern as `menu_select`). Return `1<<GBAKEY_A` for the A press (matches the codebase's single A-injection path); only the cursor goes to RAM.
- **Cosmetic:** the on-screen target highlight/bounce won't move to the written battler that frame (the sprite swap lives in the D-pad branches); the emitted target is correct. Optionally write the cursor one frame before A (the tick0/tick1 split already does this).

### 3.3 Battler pixel centers + index↔position mapping
`sBattlerCoords[IS_DOUBLE][position]` (sprite CENTERS, GBA px, identical both games):

| position | name | center (x,y) |
|---|---|---|
| 0 | PLAYER_LEFT | (32, 80) |
| 1 | OPPONENT_LEFT | (200, 40) |
| 2 | PLAYER_RIGHT | (90, 88) |
| 3 | OPPONENT_RIGHT | (152, 32) |

Position enum: `PLAYER_LEFT=0, OPPONENT_LEFT=1, PLAYER_RIGHT=2, OPPONENT_RIGHT=3`.

**Hit rects** (±28 px half-extent, clamped to screen; tune on hardware — sprites up to 64×64):

| position | rect x:[..] y:[..] |
|---|---|
| PLAYER_LEFT (0) | x 4..60, y 52..108 |
| PLAYER_RIGHT (2) | x 62..118, y 60..116 |
| OPPONENT_LEFT (1) | x 172..228, y 12..68 |
| OPPONENT_RIGHT (3) | x 124..180, y 4..60 |

These rects don't overlap (foe-right x=152 is left of foe-left x=200).

**Tap → battler index:**
1. Tap pixel → nearest/containing hit rect → **position p** (0..3).
2. **position → battler index:** scan `gBattlerPositions` (`p->battlerPos`, **EM 0x02024076 / FR 0x02023BD6**, u8[4]); find index `i (i<battlersCount)` where `read8(battlerPos+i) == p`. If none found, **no valid target — ignore the tap** (don't write `battlersCount` or a garbage index).
3. **Validity:** `i < battlersCount`, and **not absent**: `gAbsentBattlerFlags` (`p->absentFlags`, **EM 0x02024210 / FR 0x02023D70**, u8) bit i clear → `(absentMask & (1<<i)) == 0`. Mirror the game's own skip.
4. `gbacore_write8(core, p->multiCursor, (uint8_t)i)`; pulse A.

(Index→position, for debug/visualization only, is the inverse: `read8(battlerPos+i)`.)

**Optional live-center override (more accurate than the static table, accounts for bounce):**
```c
// gBattlerSpriteIds u8[4] EM 0x020241E4 / FR 0x02023D44 ; gSprites EM 0x02020630 / FR 0x0202063C, stride 0x44
uint8_t id = gbacore_read8(core, spriteIds + i);
uint32_t spr = gSprites + (uint32_t)id * 0x44u;
int16_t x  = (int16_t)gbacore_read16(core, spr + 0x20);
int16_t y  = (int16_t)gbacore_read16(core, spr + 0x22);
int16_t x2 = (int16_t)gbacore_read16(core, spr + 0x24);   // signed offsets
int16_t y2 = (int16_t)gbacore_read16(core, spr + 0x26);
int cx = x + x2, cy = y + y2;                              // live center
```
Not required for v1 (static centers are the resting centers and adequate for tap hit-testing); use only if highlight fidelity matters. These two extra addresses are NOT in the profile struct above — add `spriteIds`/`gSprites` only if you implement live centers.

### 3.4 The select state machine + dispatch (mirror `menu_select`)
```c
static int s_tgtBattler = -1, s_tgtTick = 0;
static void target_reset(void){ s_tgtBattler = -1; s_tgtTick = 0; }

static u16 target_select(GbaCore* core, uint32_t multiCursorAddr){
    if (s_tgtBattler < 0 || !core) return 0;
    gbacore_write8(core, multiCursorAddr, (uint8_t)s_tgtBattler);
    u16 k = (s_tgtTick == 1) ? (1 << GBAKEY_A) : 0;
    if (++s_tgtTick >= 2) target_reset();
    return k;
}
```
Dispatch branch in `touch_update` (before BATTLE_OTHER):
```c
if (sm && sm->valid && sm->ctx == GCTX_BATTLE_TARGET) {
    battle_reset(); walk_reset(); party_reset();
    if (newPress && gvalid) {
        int pos = hit_battler(gx, gy);                 // §3.3 rect → position 0..3, or -1
        if (pos >= 0) {
            int idx = battler_index_for_pos(sm, pos);  // scan battlerPos[]; -1 if none/absent/>=count
            if (idx >= 0) { s_tgtBattler = idx; s_tgtTick = 0; }
        }
    }
    return target_select(sm->core, sm->prof->multiCursor);
}
```
`battler_index_for_pos` uses `sm->battlerPos[]`, `sm->battlersCount`, `sm->absentMask` (filled in §0.4 from `game_read`). Reset `target_reset()` in PAD/OFF and other branches.

---

## 4. RISKS / UNCERTAIN (flag in code)

1. **LeafGreen (BPGE) new addresses are UNVERIFIED.** `partyMenu, partyCount, mainCb2, cb2Upd/Init, newKeys, ctrlFuncs, chooseTarget, multiCursor, battlerPos, battlersCount, absentFlags, activeBattler, mapLayout` were copied from FireRed. Expected equal (shared engine) but **must be checked against `pokeleafgreen.sym`** before shipping LG. Until verified, consider falling back to GCTX_BATTLE_OTHER for BPGE in the new contexts.
2. **Tap-to-walk row anchor = 5 is ±1 UNCERTAIN.** Column 7 is solid; the vertical sprite anchor (5 vs 6) needs one on-hardware calibration. Keep it a named constant.
3. **Thumb bit0 on function-pointer compares (CB2 and HandleInputChooseTarget) is inferred-but-standard.** Always `& ~1u` the read before comparing. Verify against one live table entry at runtime the first time.
4. **Hit-rect half-extents (party Cancel exact; battler ±28; party slots exact)** — battler ±28 is a heuristic from 64px sprites; tune on hardware. Party-slot and Cancel rects are exact (tile×8) and need no tuning.
5. **Write-then-A timing.** The worker runs AFTER the main-thread touch handler, so the RAM write (cursor/slotId) is in place before the frame reads it — same guarantee the existing `menu_select` relies on. The tick0(write)/tick1(write+A) split keeps A a fresh press and lets the highlight/cursor settle a frame before A.
6. **Link active ⇒ all RAM reads/writes skipped** (existing rule). Target-select, party touch, and BFS all require RAM access, so they are inert while two cores are linked (workers free-run). Smart-touch + link are mutually exclusive by design.
7. **MULTI party layout (`layout==2`) and Confirm button (chooseHalf) are not mapped** in v1 — they fall through to advance-text. Only SINGLE/DOUBLE send-out and field party menus are handled.
8. **BFS goal on a blocked tile** is ignored (no path) in v1. NPC/sign taps are handled by the stall→A path, not by BFS routing onto them.
9. **rev0/v1.0 US only.** rev1 data symbols are essentially identical; add a rev1 profile only if a v1.1 cart misreads (per gen3-ram-touch.md).

## 5. FILES TO TOUCH
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/gamestate.h` — extend `GameCtx`, `GameProfile`, `GameState` (§0.1, §0.2, §0.4).
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/gamestate.c` — replace `PROFILES[]` (§0.3); extend `game_read()` detection + new field reads (§0.5, §1.1, §3.1).
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/touch.h` — add `const GameProfile* prof;` (and the new GameState-derived fields if not passed via prof) to `TouchSmart`.
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/touch.c` — add `hit_party`/`party_select`, BFS `plan_bfs` + rewrite of `walk_update`'s routing, `hit_battler`/`battler_index_for_pos`/`target_select`, and the three new dispatch branches in `touch_update` (+ resets).
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/source/main.c` — fill the new `TouchSmart`/`GameState` fields each frame from `game_read()`.
- `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/dual-gba/docs/kb/gen3-ram-touch.md` — append the new address tables (party / target / map-layout) with the same sym-branch sourcing note.