# SPEC-coop — phase 18: make same-console co-op actually work

**Scope:** the user's ask #3 — *"the co-op on same console doesnt work, lets start by making same
game work (emerald&emerald, ruby&sapphire and firered&leafgreen)"*.

**Not in scope:** the blur (SPEC-crisp) and smart-touch door approach (SPEC-door). No wireless
(M4) — phase 15's transport seam stays exactly where it is.

**Binding rules inherited:** no edits to emulation, threading, `celiolink.c`, `netlink.c`,
`gbacore` SIO, or the HD-2D render passes. Never ship a guessed RAM address. All six themes.
Suites stay green and grow.

---

## 0. THE VERDICT — read this before touching any code

**Co-op presence is not broken. It works.** I booted it under the emutest harness with two
same-universe games and the peer avatar drew, with a nameplate, at the correct anchor, with the
gate reporting `ok`:

![Emerald + Emerald, peer avatar drawn](evidence/coop-em-em-drawing.png)

```
CO-OP me 10-6@9,4 | GUYA#03850 10-6@9,4 f1/1 d+0,+0 a0 h0 L2 ok @120,88
```
(`evidence/coop-em-em-readout.png`; gs-log ring read over GDB in the same run:
`d_prLive=2 (ACTIVE) d_prDrawn=1 d_prReason=0 (ok) d_prMapG=10 d_prMapN=6 d_prPx=9 d_prPy=4`.)

**What is actually wrong is that the user cannot get into that state with the ROMs they own.**
Their `sdmc:/dual-gba/` holds exactly two carts — `gameA.gba` = **BPEE Pokémon Emerald** and
`gameB.gba` = **BPRE Pokémon FireRed rev1** (ROM header bytes 0xAC..0xAF / 0xBC, read this
session). That pair is a **map-universe mismatch**, and the gate closes on it — correctly. I
proved that too, in a second harness run with the same build:

```
d_prLive=2 (ACTIVE)  d_prDrawn=0  d_prReason=5 == PRES_OFF_UNIVERSE
self  (Emerald)  map 10-6 @ (9,4)      peer (FireRed) map 13-1 @ (9,4)
```

So the phase's job is **not** "find the bug in presence.c". It is four separate, smaller things:

| # | The real defect | Owner |
|---|---|---|
| A | Nothing on screen tells the user *why* there is no peer unless they open the pause menu at the exact moment they toggle the row, or read a 0.32-scale debug line | §P2 |
| B | The pref ships OFF, lives on the pause menu's LINK tab as its 7th widget, and is unreachable from the pre-game settings screen | §P2 |
| C | Same-title co-op (Emerald + Emerald) needs **two distinct `.gba` files**, because the `.sav` path is derived from the ROM path — and the picker happily puts one file in both slots, which points two live mGBA cores at one save file | §P2 |
| D | **Ruby + Sapphire cannot work at all**: `gamestate.c` has no `AXVE`/`AXPE` rows, and RS is not just "Emerald with different addresses" — it has **no `gSaveBlock1Ptr` at all** | §P3 |

Three of the user's three named pairs land differently: **Emerald+Emerald works today** (proven),
**FireRed+LeafGreen works today by construction** (both map to `PRES_GAME_KANTO`,
`presence.c:125`) and must be proven by §P4, **Ruby+Sapphire needs §P3 built**.

---

## P1. What the investigation found, and how to re-run it

### P1.1 — Findings, each with its evidence

- **P1.1.1** The producer runs and populates **both** seats. `s_ctlIn[0..1]` read over GDB in the
  EM+EM run: seat 0 `emuFrame=3765 fieldValid=1 px=9 py=4 map=(10,6)`, seat 1 `emuFrame=3769
  fieldValid=1 px=9 py=4 map=(10,6)`. The parked-window fill/publish block (`main.c:3159-3199`) is
  not the problem.
- **P1.1.2** The gate resolves to **`ok` (`PRES_OFF_NONE`)** for a same-universe pair, the avatar
  draws, the nameplate decodes (`GUYA`), the identity latch works (`#03850`), liveness reaches
  tier 2 (`L2` = `PRES_LIVE_ACTIVE`), and the anchor is `@120,88` — i.e. exactly
  `PRES_ANCHOR_X/Y`, which is what a `d+0,+0` tile delta must produce. The whole D5 chain is
  self-consistent on the emulator.
- **P1.1.3** The gate resolves to **`universe` (5)** for Emerald + FireRed. The prompt's
  hypothesis "the seat/self test at `presence.c` ~354 could reject a same-title pair" is
  **REFUTED**: there is no seat/self identity test anywhere in `gate_reason`. Line 354 is the
  `NOPROF` rule (`se->gameId == PRES_GAME_NONE || !ps->have[slot] || pe->gameId ==
  PRES_GAME_NONE`); `PRES_OFF_SELF` is the *self side of the FIELD predicate*, not an identity
  check. Two Emeralds with byte-identical saves published, gated and drew.
- **P1.1.4** The pref is **default-OFF** (`settings_load`, `main.c:2176`; SPEC-avatar A6.3.4 says
  so on purpose) and its only control is `{PK_TOG,ACT_PRESENCE,0, 276,196,34,18,"Co-op presence"}`
  — the **7th** row of the pause menu's LINK tab (`main.c:2292`). `run_settings` (the pre-game
  settings screen) exposes Display/Audio/Enhance/Touch only, so the row is **unreachable before a
  session starts** (`main.c:4616` says this in a comment and accepts it).
- **P1.1.5** The explanation the app already has is **too transient**. `main.c:3583-3598` writes
  `"Co-op: on — Hoenn vs Kanto: no peer"` into the pause-menu `status` line **only in the frame
  the row is toggled**. Turn it on, close the menu, play: the CO-OP chip is drawn dim
  (`main.c:4219-4225`) and the only machine-readable reason is the debug readout at
  `y=226, scale 0.32` (`main.c:4276-4285`) — which, per the user's own ask #1, is the blurriest
  text on the screen.
- **P1.1.6** **The gate's own reason is not readable over GDB.** `presSt`, `presSelf`, `presOut`
  and `presDraw` are all `run_session` **stack locals**. To answer "which rule refused" I had to
  hand-decode the 148-byte `GsLogEntry` ring (`s_gsLog`) offset by offset. That is exactly the
  failure phase 17 §0.2 fixed for the picker and the pause menu with `g_pickDiag` / `g_menuDiag`.
  See **P1.3**.
- **P1.1.7** Incidental, worth recording: in both runs the games were still in Emerald's
  **title-screen intro demo**, and it read as `ctx = GCTX_OVERWORLD, sb1Valid=1, px=9, py=4`, with
  `gObjectEvents[0]` **agreeing** with `SaveBlock1.pos` — so P-G6 and P-G7 both passed and the
  peer avatar was drawn over the demo. This is the known `GCTX_OVERWORLD` fall-through
  (`gamestate.c:183-184`, `ctxResolved = false`) reaching further than SPEC-data D4.6.2 assumed:
  the obj-agreement discriminator does **not** catch it, because the demo runs the real field
  engine. Cosmetic, bounded, and specified as optional hardening in **P2.8**.

### P1.2 — The exact harness recipe (so this is re-runnable, not a story)

```bash
T=tools/emutest
# 1. fixtures. gameA.gba/gameB.gba are COPIES in sdmc:/3DGBA (azctl stage_fixtures).
#    For a same-universe pair, overwrite the staged gameB.gba/.sav with a copy of gameA's.
# 2. turn the pref on WITHOUT driving the pause menu: settings.bin is 25 * s32,
#    `presence` is word 24 (offset 96), `hudMode` is word 6 (offset 24, needs bit 0 for the readout).
# 3. movie: ["wait",900] then A (Resume this pairing, because recent.bin exists),
#    A/DOWN/A/X only if the picker is actually reached, then a long idle tail.
$T/run azctl boot --gdb --movie pick_coop.ctm && $T/run gdbio resume
$T/run gdbio read-u32 s_ctlIn+0 ... +36     # both seats' emuFrame/fieldValid/px/py/map
$T/run gdbio read-u32 s_gsLogN              # then read s_gsLog + N*148 and decode
$T/run see shot top shot.png                # + zoom --rect 0,495,900,45 for the CO-OP readout
$T/run azctl stop && $T/run azctl clean-fixtures
```

- **P1.2.1** `GsLogEntry` stride is **148 bytes** and `d_prLive/d_prDrawn/d_prReason/d_prFace` sit
  at **+116/+117/+118/+119**, `d_prMapG/N/Px/Py` at **+120/+122/+124/+126** (verified against live
  memory this session). Any change to `GsLogEntry` invalidates those offsets — which is the second
  argument for P1.3.
- **P1.2.2** Harness caveat found this session: `see shot` returned an **all-black window capture**
  on most attempts and a correct one on others, with `g_renderSeq` advancing throughout. Treat a
  black crop as "capture failed", never as "the app drew black" — cross-check with
  `gdbio read-u32 g_renderSeq` and with the gs-log ring before believing any screenshot-shaped
  claim. (SKILL.md §8 covers "shows the game list"; it does not cover this.)
- **P1.2.3** `recent.bin` shortcuts the picker: `rompicker_run` calls `recent_prompt` **before**
  scanning, and the first `A` in a movie selects "Resume this pairing", so `g_pickDiag.magic` stays
  0 and the movie's remaining picker taps land in the session. Do not read that as "the picker
  never ran"; delete or account for `recent.bin` when a run must exercise the picker.

### P1.3 — REQUIRED: a GDB-readable presence diagnostic mirror

- **P1.3.1** Add `g_presDiag` to `main.c` beside `g_pickDiag`/`g_menuDiag`: file-scope, written
  **once per frame** at the end of the presence solve loop, **logging only** — nothing reads it
  back, no branch depends on it, and it ships in the release build for the same reason
  `g_pickDiag` does (the harness is the regression gate).
- **P1.3.2** Fixed offsets, contract (gdbio reads them by number):

  ```c
  typedef struct {           // offset
      int32_t magic;         // 0x00  'PRS1' = 0x50525331
      int32_t frame;         // 0x04  g_renderSeq at write time
      int32_t enabled;       // 0x08  the pref
      int32_t reason[2];     // 0x0c  PRES_OFF_* per GAME index (0 = emuA)
      int32_t drawn [2];     // 0x14
      int32_t live  [2];     // 0x1c  PRES_LIVE_*
      int32_t gameId[2];     // 0x24  PRES_GAME_* of each game's own record
      int32_t selfMap[2];    // 0x2c  (mapGroup << 8) | (mapNum & 0xFF), -1 = none
      int32_t peerMap[2];    // 0x34
      int32_t dTile [2];     // 0x3c  (dTileX << 16) | (dTileY & 0xFFFF)
      int32_t artOk;         // 0x44  s_presenceOk
  } PresDiag;
  ```
- **P1.3.3** `_Static_assert` every offset, exactly as `Settings` pins its own ladder
  (`main.c:2112-2116`), so a future field insert is a build error rather than a harness that
  silently reads the wrong word.
- **P1.3.4** Acceptance: `tools/emutest/run gdbio read-u32 g_presDiag+0x0c` must print `5` for an
  Emerald+FireRed session and `0` for an Emerald+Emerald one, with no rebuild between them.

---

## P2. Fix what the investigation says is broken

### P2.1 — The universe gate stays. It is not the bug.

- **P2.1.1** Do **not** relax `presence_game_id` / P-G5 to make Emerald + FireRed "work".
  `(mapGroup, mapNum)` is only meaningful inside one game's map table; the gate exists so that a
  cross-universe pair fails *loudly and correctly* instead of drawing a friend walking around an
  unrelated map. `presence.h:73-79` already argues this and §P3.1 re-proves it with data.
- **P2.1.2** No change to `presence.c`'s ladder, anchor math, hold, filter or cull is authorised by
  this spec. They are proven working (P1.1.2).

### P2.2 — Say why, on the game screen, without the debug line

The user's complaint is *"doesn't work"*, and the honest translation is *"nothing told me why"*.

- **P2.2.1** When `presenceOn` is turned on **and** the pair can never produce a peer for a
  structural reason — `PRES_OFF_NOPROF` or `PRES_OFF_UNIVERSE`, the only two reasons that are
  constant for the whole session — raise a **toast** (`toast`/`toastTimer`, the existing
  mechanism, `main.c:4235`), not just the pause-menu `status` line that dies with the menu:
  - `noprof` → `"Co-op: this pair has no game profile"`
  - `universe` → `"Co-op needs two games from the SAME world (Hoenn/Kanto)"`
  It must also fire **once at session start** when the pref is already on from `settings.bin`,
  because that is the path a returning user takes and it is the path the pause-menu status line
  can never cover.
- **P2.2.2** The CO-OP chip gains a **reason suffix** when it is dim and the reason is structural:
  `CO-OP` → `CO-OP ×` for `noprof`/`universe`. Two glyphs, drawn from the same
  `ui_chip_measure`/`ui_chip` pair, no layout move. A chip that is dim for a *transient* reason
  (`field`, `map`, `cull`, `live`, `menu`, `link`) keeps the bare `CO-OP` — the distinction the
  user needs is "this will never work with these two carts" vs "walk closer".
- **P2.2.3** The debug readout stays exactly as it is. It is the harness's surface and it is the
  M1 acceptance test; making it prettier is SPEC-crisp's business, not this spec's.

### P2.3 — Make the pref reachable before a game starts

- **P2.3.1** Add the LINK tab's index to `run_settings`'s `TABS[]` (`main.c:4626`) **or** move the
  CO-OP row to a tab that screen already shows. Whichever is chosen, the acceptance is: from a cold
  boot with no session, the user can reach and flip "Co-op presence" and the value survives into
  the next session. `run_settings` already loads and re-saves the word (`main.c:4622`), so the
  persistence half is done.
- **P2.3.2** If the LINK tab is added to `run_settings`, its live-session-only rows
  (`ACT_LINK`, `ACT_NETLINK`, `ACT_WIRELESS`, `ACT_SAVEST`, `ACT_LOADST`, `ACT_LOADSAV`) must be
  drawn **disabled**, not merely inert — a pre-game screen offering "Save state" is a worse defect
  than the one being fixed.

### P2.4 — Same-title co-op needs two distinct ROM FILES (the `.sav` hazard)

This is the one that will bite the user the moment they try Emerald + Emerald.

- **P2.4.1** `gbacore_load_rom` derives the battery save as `<rompath>` with the extension replaced
  by `.sav` (`gbacore.c:107-112, 165-171`). Two cores loaded from the **same** path therefore open
  the **same** `.sav`, both hold a writable `VFile` on it, and both flush a whole save image over
  each other. That is save corruption, not a cosmetic clash.
- **P2.4.2** `rompicker_run` has **no duplicate guard**: `if (confirmRow) { if (idxA < 0 || single)
  idxA = sel; else idxB = sel; }` (`rompicker.c:509`) and the slot cards `if (fire == PICK_SLOT_A)
  idxA = ...` (`:503-504`) let the same row land in both slots. The shipped Tier-B movie
  (`tests/fixtures/movie_pick_play_quit.json`) actually walks through that state.
- **P2.4.3** **Requirement:** the picker must refuse `idxA == idxB`. On the assignment that would
  duplicate, keep the assignment out and show the existing hint band text for ~2 s:
  `"Same file in both slots — copy it (emerald1.gba / emerald2.gba)"`. Refusing is correct rather
  than paternalistic here: there is no configuration in which one file in two slots is what the
  user wanted, because the two games would also share one save.
- **P2.4.4** The refusal must be by **resolved path**, not by list row, so a future
  rescan/re-sort cannot let the same file through under two indices.
- **P2.4.5** Document the workflow in `README.md`: *"To play the same game twice, put two copies on
  the card (`emerald1.gba`, `emerald2.gba`). Each gets its own `.sav`."*
- **P2.4.6** `g_pickDiag` gains nothing new; `idxA`/`idxB` already expose the state the harness
  needs to assert P2.4.3.

### P2.5 — The degenerate same-tile case is fine; say so and leave it

- **P2.5.1** Two copies of the same save put the peer at `d+0,+0`, i.e. the avatar draws exactly on
  the player's own sprite. `presence_art_ysort` orders them and nothing misbehaves (observed).
  No fix. The §P4 fixtures deliberately separate the two saves so the test proves *position*, not
  just *presence*.

### P2.6 — Do not regress what works

- **P2.6.1** `presence.c`, `presence_art.c`, `presence_ui.c` and `fieldgate.h` are **frozen** for
  this phase except for the one additive constant in P3.1. Every behaviour change lands in
  `main.c` (chrome + toast + diag mirror), `rompicker.c` (P2.4) and `gamestate.{c,h}` (P3).
- **P2.6.2** `test/host/test_presence.c` must stay green and gain the P3.1 and P4 cases. Compile
  line is in the file header (`clang -std=c11 -Wall -Wextra -O0 -g -I source
  test/host/test_presence.c -o /tmp/tp && /tmp/tp`).

### P2.7 — Optional hardening: the title-screen / intro-demo false positive (P1.1.7)

- **P2.7.1** **Only if** it can be done without touching `presence.c`'s ladder: extend
  `game_read`'s context resolution so the Gen-3 **title-screen intro demo** does not fall through
  to `GCTX_OVERWORLD`. The discriminator is `gMain.callback2` — the gs log already records it
  (`cb2` column, `resolved=0` marks every fall-through), and the phase-13 logger exists precisely
  to promote such a value into a `GameProfile` column.
- **P2.7.2** This needs a **captured cb2 value per game**, not a guess. Until one exists, the
  documented behaviour is: *a peer may be drawn over the other game's attract-mode demo.* Say it
  in the BUILDLOG; do not ship an address to remove it.

---

## P3. Ruby + Sapphire — authoring `AXVE` / `AXPE` properly

Everything in this section was read this session from **pret's byte-matched `symbols` branch**
(`raw.githubusercontent.com/pret/pokeruby/symbols/{pokeruby,pokesapphire,pokeruby_rev1,
pokesapphire_rev1}.sym`, all four downloaded) and from `pret/pokeruby` `master` sources. No
address below is derived from Emerald.

### P3.1 — Universe id: **a separate id, and it is proven, not assumed**

- **P3.1.1** `AXVE`/`AXPE` get **`PRES_GAME_HOENN_RS = 3`**, appended to `presence.h` after
  `PRES_GAME_KANTO`. They must **NOT** share `PRES_GAME_HOENN` with Emerald.
- **P3.1.2** The proof (not the intuition): `pret/pokeruby` and `pret/pokeemerald`
  `data/maps/map_groups.json` were both fetched and compared positionally. Ruby has **394** maps,
  Emerald **518**; **344** of the 394 common-prefix `(group, num)` slots name a *different map*.
  The first divergence is inside group 0 at index 50 (`Underwater1` vs `Underwater_Route124`),
  which shifts every later group; group 5 index 1 is `FallarborTown_ContestLobby` in Ruby and
  `FallarborTown_BattleTentLobby` in Emerald. A shared universe id would put an Emerald player's
  avatar on a Ruby player's screen at a completely unrelated location.
- **P3.1.3** Ruby and Sapphire share **one** id because they are **one build**: `pret/pokeruby` is
  a single decomp for both, one map table, and (see P3.3) their RAM symbol maps are byte-identical.
- **P3.1.4** `presence_game_id` gains `if (!strncmp(code4,"AXVE",4) || !strncmp(code4,"AXPE",4))
  return PRES_GAME_HOENN_RS;`. `test_presence.c` TEST 6 (map universes) grows the four new cases:
  AXVE+AXPE = same, AXVE+BPEE = different, AXPE+BPEE = different, AXVE+BPRE = different.

### P3.2 — The structural blocker: **Ruby/Sapphire have no `gSaveBlock1Ptr`**

This is the finding that makes "RS is Emerald with different addresses" false, and it must be
fixed before any address is useful.

- **P3.2.1** RS uses **static structs**, not pointers: `pokeruby.sym:106
  02025734 g 00003ac0 gSaveBlock1` and `:105 02024ea4 g 00000890 gSaveBlock2` (identical in
  `pokesapphire.sym`). There is **no** `gSaveBlock1Ptr`/`gSaveBlock2Ptr` symbol in either map — the
  pointer indirection is an Emerald/FRLG-era change. `include/global.h:668,841` declares
  `extern struct SaveBlock1 gSaveBlock1;` / `extern struct SaveBlock2 gSaveBlock2;` with the
  addresses in the comment.
- **P3.2.2** Consequence if ignored: `game_read` does
  `uint32_t sb1 = gbacore_read32(c, p->sb1ptr); out->sb1Valid = (sb1 >> 24) == 0x02;`
  (`gamestate.c:115-116`). Pointed at `0x02025734` that reads `pos.x | (pos.y << 16)`, whose high
  byte is not `0x02`, so **`sb1Valid` would be false forever** and presence would report
  `PRES_OFF_FIELD`/`SELF` on every frame. Same for `ident_refresh` (`presence_read.c:24-25`).
- **P3.2.3** **Requirement:** append `uint8_t sbDirect;` to `GameProfile` (appending is the only
  safe edit — `PROFILES[]` is positionally initialised; the D2 `vblankCtr` block is the
  precedent). `0` = the address is a pointer to deref (BPEE/BPRE/BPGE, unchanged), `1` = the
  address **is** the struct (AXVE/AXPE).
- **P3.2.4** Exactly **two** call sites change, and they are the only two in the tree
  (`grep sb1ptr\|sb2ptr source/`):
  ```c
  // gamestate.c game_read
  uint32_t sb1 = p->sbDirect ? p->sb1ptr : gbacore_read32(c, p->sb1ptr);
  // presence_read.c ident_refresh
  uint32_t sb2 = p->sbDirect ? p->sb2ptr : gbacore_read32(c, p->sb2ptr);
  ```
  The `(x >> 24) == 0x02` sanity test is kept in both — for a direct block it is a constant-true
  EWRAM check, which is honest: it says "mapped", not "loaded".
- **P3.2.5** **The validity signal is weaker on RS and must be compensated, not hidden.**
  `gSaveBlock1` exists (zeroed) from boot, so `sb1Valid` no longer distinguishes "a save is
  loaded". The identity latch is where that matters: `ident_refresh` currently rejects only
  `nm[0] == 0xFF` (`presence_read.c:31`), and a zeroed `gSaveBlock2` gives `nm[0] == 0x00`, which
  the charmap decodes to `' '` — so RS would latch a blank name **forever** (D2.3 keeps the first
  well-formed result). **Requirement:** for `sbDirect` profiles, additionally reject a name whose
  8 bytes are **all `0x00`**. Cheap, exact, and it cannot reject a real name (Gen-3 name entry
  cannot produce eight leading spaces followed by no terminator).
- **P3.2.6** The layouts RS shares with Emerald, so nothing else moves — all VERIFIED-SRC from
  `pret/pokeruby master`:
  - `struct SaveBlock1`: `pos` (`Coords16 s16 x,y`) at `+0x00`, `location` (`WarpData`: `s8
    mapGroup` `+0x04`, `s8 mapNum` `+0x05`) at `+0x04`, `mapLayoutId` at `+0x32`
    (`include/global.h:668-681`). Identical to Emerald → `game_read`'s `px/py/mapGroup/mapNum`
    reads are unchanged.
  - `struct SaveBlock2`: `playerName[8]` `+0x00`, `playerGender` `+0x08`, `playerTrainerId[4]`
    `+0x0A` (`include/global.h:841-846`). Identical → the whole D1.2 identity read is unchanged.
  - `struct ObjectEvent`: `active:1` at bit 0 of `+0x00`, `currentCoords` at `+0x10`,
    `facingDirection` in the low nibble of `+0x18`, `previousElevation` in the high nibble of
    `+0x0B`, stride `0x24` (`include/global.fieldmap.h:161-203`; `gObjectEvents` symbol size
    `0x240` = 16 × `0x24`). Identical → P-G7 and the facing fold are unchanged.
  - `struct Main`: `callback1` `+0x00`, `callback2` `+0x04`, `vblankCounter1` `+0x20`,
    `vblankCounter2` `+0x24`, `newKeys` `+0x2E` (`include/main.h:10-44`). Identical → `mainCb2`
    is `gMain+4`, `hbCtr` is `gMain+0x24`, and (unlike FR/LG) RS's `vblankCounter1` is a real
    `u32`, so the D2 hang watch would actually work there.
  - `struct FieldCamera`: `curMovementOffsetX` `+0x10`, `curMovementOffsetY` `+0x14`, size `0x18`
    (`include/field_camera.h:4-12`; symbol size `0x18`). Identical → the D1.3 sub-tile read is
    unchanged.

### P3.3 — The address table (RAM only; **all four maps agree, byte for byte**)

- **P3.3.1** Two whole-map comparisons were run this session, and both matter:
  - **Ruby vs Sapphire, every `0x02`/`0x03` symbol: 0 differences.**
  - **rev0 vs rev1, every `0x02`/`0x03` symbol: 0 differences.**
  (ROM is a different story: ~90 000 `0x08` symbol lines differ between Ruby rev0 and rev1, and
  Ruby vs Sapphire differ too — e.g. `Task_StartMenu` is `0x08071254` / `0x08071258` /
  `0x08071274` / `0x08071278` across the four maps.)
- **P3.3.2** **Therefore: one row body serves `AXVE` and `AXPE` and both revisions, provided the
  row contains no ROM address.** This is a rule, not a convenience: any `0x08…` value in an RS row
  is wrong for three of the four cartridges it will meet.
- **P3.3.3** The values. Every line cites `pokeruby.sym` (line numbers are identical in
  `pokesapphire.sym`), status **VERIFIED-SYM** (four maps agree) unless noted.

  | `GameProfile` field | RS value | Symbol | `pokeruby.sym` |
  |---|---|---|---|
  | `sb1ptr` (+`sbDirect=1`) | `0x02025734` | `gSaveBlock1` | :106 |
  | `sb2ptr` (+`sbDirect=1`) | `0x02024EA4` | `gSaveBlock2` | :105 |
  | `mainCb2` | `0x03001774` | `gMain`+4 | :526 + `main.h:13` |
  | `vblankCtr` | `0x03001790` | `gMain`+0x20 | :526 + `main.h:23` |
  | `hbCtr` | `0x03001794` | `gMain`+0x24 | :526 + `main.h:24` |
  | `newKeys` | `0x0300179E` | `gMain`+0x2E | :526 + `main.h:29` |
  | `gTasksBase` | `0x03004B20` | `gTasks` | :642 |
  | `mapObjects` | `0x030048A0` | `gObjectEvents` | :634 |
  | `fieldCamera` | `0x03004880` | `gFieldCamera` | :631 |
  | `fieldMsgMode` | `0x030005A8` | `sMessageBoxMode` | :464 |
  | `mapHeader` | `0x0202E828` | `gMapHeader` | :126 |
  | `mapLayout` | `0x03004870` | `gBackupMapLayout` | :630 |
  | `battleFlags` | `0x020239F8` | `gBattleTypeFlags` | :23 |
  | `actionCursor` | `0x02024E60` | `gActionSelectionCursor` | :94 |
  | `moveCursor` | `0x02024E64` | `gMoveSelectionCursor` | :95 |
  | `battleMons` | `0x02024A80` | `gBattleMons` | :37 |
  | `bg0y` | `0x030042A0` | `gBattle_BG0_Y` | :599 |
  | `battlersCount` | `0x02024A68` | `gBattlersCount` | :30 |
  | `battlerPos` | `0x02024A72` | `gBattlerPositions` | :32 |
  | `absentFlags` | `0x02024C0C` | `gAbsentBattlerFlags` | :54 |
  | `activeBattler` | `0x02024A60` | `gActiveBattler` | :28 |
  | `ctrlFuncs` | `0x03004330` | `gBattlerControllerFuncs` | :608 |
  | `partyCount` | `0x03004350` | `gPlayerPartyCount` | :613 |
  | `startCb` | `0x03004AE8` | `gMenuCallback` | :637 |
  | `startCursor` | `0x0202E8FC` | `sStartMenuCursorPos` | :162 |
  | `sMenuBase` | `0x020388B8` | `sMenu` | :312 |
  | `linkStatus` | `0x03002A60` | `gLinkStatus` | :558 |
  | `linkErr` | `0x0300295C` | `gLinkErrorOccurred` | :549 |

- **P3.3.4** `fieldMsgMode` = `sMessageBoxMode` is **VERIFIED-SYM + VERIFIED-SRC**: `pokeruby
  src/field_message_box.c:11` declares `static u8 sMessageBoxMode;` with values
  `FIELD_MESSAGE_BOX_HIDDEN / NORMAL / AUTO_SCROLL` — the exact analogue of Emerald's
  `sFieldMessageBoxMode`, and the `!= 0` test in `game_read:188` carries over unchanged.
- **P3.3.5** `partyCount`: the RS symbol is in IWRAM with size 4 while the field is read as a `u8`.
  It is only read inside the `partyTask` branch, which RS never enters (P3.4.2), so it is inert —
  but it is **verify-on-hw-pending**, and it must not be promoted to VERIFIED without a run.

### P3.4 — Columns that must be **0**, each with its named degradation

Never a guess, and never a plausible-looking wrong pointer. Each of these is `0` because the
symbol does not exist in the RS maps, or because shipping it would be rev-wrong.

- **P3.4.1 ROM function pointers → 0.** `startCbInput`, `partyTask`, `yesNoTask`, `multiTask`,
  `selMenuTask`, `startMenuTask`, `cb2BagRun`, `bagHandler`, `bagOpen`, `chooseTarget`,
  `mapNameTask`, `cb2UpdParty`, `cb2InitParty`. Reason: rev- and title-sensitive (P3.3.1), and
  several (`Task_HandleChooseMonInput`, any `Task_Bag*`, any `MapNamePopup*`,
  `HandleInputChooseTarget`) **do not exist in the RS decomp at all** — pokeruby still has those
  regions as `sub_XXXXXXXX`.
  **Degradation:** `task_active` returns `false` for a `0` handler (`gamestate.c:85`) and
  `find_bag_list_task` finds nothing, so RS reports `GCTX_OVERWORLD` where Emerald would report
  `GCTX_FIELDMENU`/`GCTX_PARTY`/`GCTX_BAG`. Concretely: **smart touch's menu/bag/party/target
  features do not work on Ruby/Sapphire**, and a peer avatar can be drawn while the RS player has
  the START menu open. Both are cosmetic, both are fail-safe, and both are recorded rather than
  papered over.
- **P3.4.2 THE ONE ROM ADDRESS THAT IS SHIPPED:** `battleMainCb = 0x0800F808` (`BattleMainCB2`,
  `pokeruby.sym:1378`) — and it is shipped **only because all four maps give the identical value**
  (`pokeruby`, `pokesapphire`, `pokeruby_rev1`, `pokesapphire_rev1` = `0800f808`). It is
  load-bearing: without it `inBattle` is always false, RS always reports `GCTX_OVERWORLD`, and the
  peer avatar would be painted over battle screens. Status: **VERIFIED-SYM ×4,
  verify-on-hw-pending** — one hardware/emulator run that enters an RS battle and sees `ctx` leave
  `field` promotes it.
- **P3.4.3 `gRemoteLinkPlayersNotReceived`, `sLinkErrorBuffer`, `gWindows`, `gMultiUsePlayerCursor`
  → 0.** No such symbols in the RS maps. `game_read` already guards each with `p->x ? read : 0`
  (`gamestate.c:131-135`). Degradation: two of the five link-diagnostic columns are blank in the gs
  log for RS. Logging only.
- **P3.4.4 `spriteCoordOff` → 0.** `gSpriteCoordOffsetX` is `0x030024D0` (`:535`) and
  `gSpriteCoordOffsetY` is `0x030027E0` (`:540`) — **`0x310` apart, not `+2`**. The field's whole
  contract is "Y = this + 2" (`gamestate.h`), so a value here would make the diagnostic read a
  wrong address. It is diagnostic-only (SPEC-data D1.4: the shipped sub-tile source is
  `fieldCamera+0x10/+0x14`, which RS has and which is verified above), so `0` costs nothing.
  If Open Q4 ever promotes the pan term, `GameProfile` needs a separate Y column first.

### P3.5 — The row, and how it is added

- **P3.5.1** Two rows, `"AXVE"` and `"AXPE"`, with **identical bodies** (P3.3.1). Write them as
  one `#define`d initialiser list or two literal copies with a comment pinning them together —
  never one row matched by a prefix, because the 4-char code is the app's only game identity and
  `profile_for` compares all four bytes (`gamestate.c:79`).
- **P3.5.2** The rows go **after** `BPGE`, and `sbDirect` is **appended** to `GameProfile` after
  `hbCtr`, so every existing positional initialiser keeps its meaning (`sbDirect` defaults to `0`
  for BPEE/BPRE/BPGE by C's zero-fill rule — but write the `0` explicitly in all three rows anyway;
  an implicit zero in a table this long is how a future append goes wrong).
- **P3.5.3** `rom_display_name` already knows `AXVE`/`AXPE` (`rompicker.c:44-45`), so the picker
  labels them correctly with no edit.
- **P3.5.4** Marking discipline in the source comment, verbatim from the house rule: every value
  gets `VERIFIED-SYM` + its `pokeruby.sym` line; `battleMainCb` additionally gets
  `verify-on-hw-pending`; `partyCount` gets `verify-on-hw-pending`; every `0` gets its
  degradation named in one clause.

### P3.6 — How the RS rows are proved before anything is built on them

- **P3.6.1** The pre-existing proof the KB already specified (`coop-shared-overworld.md:121`) is
  the right one and it is cheap: **boot Ruby in one core and Sapphire in the other and confirm
  smart touch's tap-to-walk works on both.** Tap-to-walk uses `sb1ptr` + `mapObjects` + `mapLayout`
  — exactly the three columns presence depends on — so it byte-proves them before a single avatar
  is drawn.
- **P3.6.2** Under the harness, the same proof is a GDB read with no ROM interaction:
  `s_ctlIn[0]`/`s_ctlIn[1]` must show `fieldValid=1` with `px/py` that **change as the player
  walks** and a `(mapGroup, mapNum)` that changes at a warp. A static `px/py` that never moves is
  the signature of a wrong `sb1ptr`, and `fieldValid=0` forever is the signature of a missed
  `sbDirect`.
- **P3.6.3** `g_presDiag.gameId[0..1]` must read `3, 3` for an RS pair and `reason` must reach `0`
  once both are on the same map.

---

## P4. The test scenarios that prove the user's three pairs

Each scenario names its fixture, its driver, and **what a state read must show** — a screenshot
alone is not a pass (P1.2.2).

### P4.1 — Fixtures (the setup problem, solved explicitly)

- **P4.1.1** Stage via `azctl boot --fresh-sd-fixtures`, then overwrite the staged **copies** in
  `sdmc:/3DGBA/` — never `sdmc:/dual-gba/`, which is read+copy-only user data
  (`azctl.assert_sd_writable`; `stop` re-hashes the originals and says so).
- **P4.1.2** **EM+EM (the easiest first proof):** `gameB.gba` ← copy of `gameA.gba`,
  `gameB.sav` ← copy of `gameA.sav`. Both games boot to the same tile, so the expected screen
  result is `d+0,+0` and the avatar exactly under the player. This is the scenario already run
  this session; it is the regression baseline.
- **P4.1.3** **EM+EM, separated (the scenario that proves *position*):** as P4.1.2, then walk one
  seat away with the D4 channel — `run sdmc arm-control` once, then
  `run sdmc drop move 2 "R3 D2"` — and require the readout's `d` term to equal the walked delta.
  D4 walk tokens are closed-loop on the seat's own `SaveBlock1` coordinates, so this is exact, not
  timing-dependent.
- **P4.1.4** **FR+LG:** needs a LeafGreen ROM, which the user's card does not have. Until one
  exists the pair is proved in **two halves**: (a) `presence_game_id("BPGE") == presence_game_id
  ("BPRE") == PRES_GAME_KANTO` as a host-suite assertion (already true, `presence.c:125`), and
  (b) a **BPRE + BPRE** harness run (two copies of FireRed, P4.1.2's recipe) which exercises the
  identical `gameId`-equality path plus the FR/LG profile addresses. State honestly in the
  BUILDLOG that FR+LG itself is *proved by construction plus a BPRE+BPRE run*, not directly.
- **P4.1.5** **RS+RS:** needs Ruby and Sapphire ROMs, which the card does not have. §P3 is
  therefore **specified but hardware/ROM-blocked**; its host-suite half (P3.1.4) can and must land
  regardless.
- **P4.1.6** Teardown is mandatory and already automated: `azctl stop` → `azctl clean-fixtures`
  (removes only manifest-listed files, restores the pre-existing `recent.bin`, re-hashes the
  originals), plus restoring `sdmc:/3DGBA/settings.bin` from the snapshot the run took. Verified
  clean this session.

### P4.2 — Scenario CO-1: Emerald + Emerald draws a peer

- **P4.2.1** Setup P4.1.2, `settings.bin` word 24 = 1 (co-op on), word 6 (hudMode) bit 0 set.
- **P4.2.2** Pass requires **all** of:
  `g_presDiag.magic == 'PRS1'`; `enabled == 1`; `gameId[0] == gameId[1] == PRES_GAME_HOENN (1)`;
  `reason[0] == reason[1] == 0`; `drawn[0] == drawn[1] == 1`; `live[*] == 2`;
  `selfMap[0] == peerMap[1]` and `selfMap[1] == peerMap[0]`; `artOk == 1`.
- **P4.2.3** And one visual: the top-screen crop shows the avatar plus a non-empty nameplate, with
  `g_renderSeq` read immediately before and after the capture to prove the frame is live.

### P4.3 — Scenario CO-2: the peer moves when the peer moves

- **P4.3.1** Setup P4.1.3. After the D4 script is consumed
  (`run sdmc wait-consumed move 2`), `g_presDiag.dTile[0]` must equal the walked delta
  (`+3, +2` for `"R3 D2"`), and `reason[0]` must still be `0`.
- **P4.3.2** Walk far enough to leave the 15×10 window and `reason[0]` must become
  `PRES_OFF_CULL (11)` — **not** `0` with an off-screen anchor. That is the D5.7 contract and the
  fix-pass finding 5 regression guard.

### P4.4 — Scenario CO-3: Emerald + FireRed refuses, loudly

- **P4.4.1** Setup: the user's real pair (staged copies of both `dual-gba` ROMs, unmodified).
- **P4.4.2** `g_presDiag.reason[0] == reason[1] == 5 (PRES_OFF_UNIVERSE)`,
  `gameId[0] == 1`, `gameId[1] == 2`, `drawn == 0`. (Reproduced this session via the gs-log ring;
  P1.3 makes it a one-line read.)
- **P4.4.3** **New:** the session-start toast (P2.2.1) is present — assert it by capturing the top
  screen within the toast window, or by mirroring `toastTimer != 0` into `g_presDiag` if the
  capture channel proves too flaky (P1.2.2).

### P4.5 — Scenario CO-4: the picker refuses one file in two slots

- **P4.5.1** ROM-less-plus-one-fixture SD state, movie taps A twice on the same row.
  `g_pickDiag.idxB` must stay `-1` and `g_pickDiag.startN` must stay `0` while the hint is up.

### P4.6 — Host-suite growth (runs on the PC, no emulator)

- **P4.6.1** `test_presence.c` TEST 6 grows the four AXVE/AXPE universe cases (P3.1.4).
- **P4.6.2** A new TEST 38: the `sbDirect` semantics as a table — for each of the five profile
  codes, "is the save block a pointer or a struct", asserted against `PROFILES[]`'s own data so a
  future row cannot get it wrong silently. (`gamestate.c` is not host-compilable today; if that
  cannot be changed cheaply, assert it instead in a 20-line `test/host/test_profiles.c` that
  `#include`s `gamestate.h` and a copy of the table guarded by `_Static_assert`s — and say in the
  BUILDLOG which of the two was done.)
- **P4.6.3** All ten existing host suites plus `bash tools/emutest/tests/run_host_tests.sh` stay
  green.

---

## P5. What remains hardware-only after all of this

Stated plainly, because Azahar cannot prove any of it (CLAUDE.md #6, SKILL.md §3):

- **P5.1** **Frame budget.** Presence adds three game-RAM reads per game per frame plus one
  sprite composite per screen. Azahar does not model core-2 contention, the 804 MHz clock or L2,
  so "co-op is free" is only true when a **New 3DS running the `.cia`** says so. The measurement is
  the existing fps HUD with co-op on vs off, same save, same map.
- **P5.2** **`PRES_ANCHOR_Y = 88.0f`** is still the one number in the module that could be 8 px
  wrong (`presence.h:54-57`). The emulator run confirms `@120,88` is *computed*, not that it is
  *right*: the M2 calibration photo — both players on the same tile, peer avatar vs the host's own
  character — is a hardware judgement about pixels and has not been taken.
- **P5.3** **`facingDirection`** (`gamestate.c:151`) is still marked verify-on-hw. Promotion needs
  one run reading `1/2/3/4` as the player walks D/U/L/R; the readout already prints the folded and
  raw values side by side (`f1/1` in the capture above).
- **P5.4** **Every RS address in §P3.3** is VERIFIED-SYM but **not one has been executed**, because
  no Ruby or Sapphire ROM exists on this machine. P3.6's tap-to-walk proof is the gate before
  anything ships as "Ruby/Sapphire supported".
- **P5.5** **`battleMainCb` for RS** (P3.4.2) is the single shipped ROM address and the single
  thing that keeps a peer off an RS battle screen. It needs one run that enters an RS battle.
- **P5.6** **The `.sav` sharing hazard** (P2.4) is a *filesystem* claim: it is argued from
  `gbacore.c:107-171` and mGBA's write-back model, and P2.4.3 removes the possibility rather than
  testing it. If anyone wants it tested instead of prevented, that test destroys a save file and
  must never run against `sdmc:/dual-gba/`.
- **P5.7** **The intro-demo false positive** (P1.1.7/P2.7) needs a captured `cb2` per game from a
  real session before any address can be promoted.

---

## Open Questions

1. **Is the toast the right surface for "these two carts can never co-op", or should the picker
   say it?** The picker is where the pair is *chosen* and it already knows both game codes
   (`codes[]`, `rompicker.c:102`) — a `⚡ co-op` marker on the two slot cards when the codes share a
   universe would answer the question before the session starts, which is strictly better than
   explaining it afterwards. It is also more UI work and it touches phase-17's freshly-fixed
   layout. **Recommendation: ship P2.2's toast now, and log the picker marker as the follow-up.**
2. **Should `run_settings` grow the LINK tab, or should the CO-OP row move to ENHANCE?** P2.3
   allows either. ENHANCE is measurably full (its five toggles end at y=188, the tilt segment takes
   y198..224, the hint sits at y=231 — `main.c:2278-2286`), so moving the row needs plate art that
   does not exist. Growing `TABS[]` needs the disable pass in P2.3.2. **Neither is obviously
   cheaper; decide with the plate art in hand.**
3. **Does Ruby/Sapphire's `GCTX_OVERWORLD`-only context (P3.4.1) need a menu discriminator before
   RS ships?** A peer drawn while the RS player has the START menu open is the worst visible
   consequence. `gMenuCallback` (`0x03004AE8`) is a RAM address we *do* have, and Emerald's profile
   already uses the equivalent `startCb` — but `game_read`'s START detection was deliberately
   changed from a callback compare to a task compare because `gMenuCallback` is not cleared on
   close (`gamestate.c:179-182`), which false-positived on Emerald. **Does RS clear it?** Unknown;
   answering it needs the pokeruby source read that this spec did not do.
4. **Is `PRES_MAX_PEERS == 1` a constraint the same-console path should keep?** It is today, and
   nothing here changes it. But three of the user's flagged futures (3-4 players, real room
   players, correct partner names) all begin at that bound. Out of scope, flagged.
5. **Should the `.sav` derivation change instead of the picker?** An alternative to P2.4.3 is to
   derive the save as `<rom>-A.sav` / `<rom>-B.sav` per slot, which would make one file in two
   slots *work*. It would also silently orphan every existing `.sav` on every user's card. **Not
   recommended — but it is the only design in which "pick the same ROM twice" is a feature, and it
   should be rejected on the record rather than never considered.**
6. **Why did `see shot` return black on most attempts this session (P1.2.2)?** `g_renderSeq` was
   advancing and one capture in the same run was perfect, so it is a capture-path problem, not an
   app problem. If phase 18 leans on visual evidence at all, this needs ten minutes of triage
   first — a harness that fails open (black = "looks wrong") is worse than one that SKIPs.
