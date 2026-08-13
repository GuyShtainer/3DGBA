# Phase 20 — `SPEC.md`: the peer's GENUINE trainer sprite, read live from their own machine

**Status:** design, complete. No code written by this document; no emulator was taken.
**Supersedes:** `docs/phase15-presence/PHASE.md` invariant 7 and
`docs/kb/coop-shared-overworld.md` §2 "Asset" / §4 "Sprite art" — see §0.2.
**Depends on:** phase 15 (`presence*.{c,h}`), phase 14 (`tilt.h`), phase 18 (`gamestate.c` profiles,
the `g_presDiag` readout, `see`/`native.py` pixel capture).
**Freeze list (diffs MUST stay empty):** `source/gbacore.c`, `source/gbacore.h`, `source/celiolink.c`,
`source/netlink.c`, `source/audio.c`, `tilt.v.pica`, `warp.v.pica`.

---

## 0. What this phase is, and the decision it overturns

### 0.1 The ask

After seeing `docs/phase18-crisp/evidence/coop-both-screens.png` — both screens, each game drawing the
other player walking around the same Mauville Pokémon Center 2F — the user asked, verbatim:

> "Oh wow. Show more locations and fix the player image to the geniuene one"

Two deliverables:

1. **The genuine sprite** (§S1–§S5, §S7): replace the phase-15 magenta placeholder with the peer's
   *actual, currently-displayed* overworld trainer frame.
2. **The location sweep** (§S6): capture co-op in a set of visibly different places, plus motion.

### 0.2 The cost judgement that is being overturned, explicitly

Phase 15 shipped a deliberately-fake magenta walker (`presence_art.c` `build_cell`, tells at
`PRES_ART_MAGENTA`) and wrote the reason down twice:

* `docs/kb/coop-shared-overworld.md:131` — *"ship one generic 16×32 4-direction overworld sprite as our
  own C2D sheet (ripping the peer's VRAM frames is not worth it — swapped VRAM window + CPU frame DMA)"*
* `docs/kb/coop-shared-overworld.md:152` — *"Ship a generic overworld sprite; pick gender from `sb2`.
  95 % convincing, zero VRAM-ripping complexity. The peer's real frames are not worth chasing."*
* `docs/phase15-presence/PHASE.md` invariant 7 — *"Our own sprite art. … Do NOT rip the peer's VRAM frames"*

**That was a cost judgement made before anyone had seen the feature work, and the user has now seen it
and asked for the real thing. Their call overrides it.** This document therefore replaces invariant 7
with a narrower one (§0.4 I7'), and it says plainly where the cost actually lands (§S3.6): the honest
bill is **28 emulated-bus reads per game per frame in steady state**, a **~90-read burst about seven
times a second while the peer walks**, and **~2 KB of CPU pixel work per burst**. There is no "swapped
VRAM window" (that phrase described a hardware GBA link, not two cores in one address space) and no
"CPU frame DMA" (we copy 512 bytes, not a framebuffer). The original estimate was wrong by roughly two
orders of magnitude, which is why the judgement it supported does not survive contact.

### 0.3 The insight that makes it cheap

**Both games run in our own process.** The peer's trainer has already been decompressed, palette-loaded,
animated and DMA'd into *their* emulated OBJ VRAM by *their* own game, this frame. We do not ship art, we
do not decompress ROM graphics, we do not guess an appearance, and we do not reimplement the animation
system. We read the 512 bytes their game is displaying right now. The walk cycle, the turn frames, the
horizontal mirror on East, the bike/surf/Acro forms and the correct gender all arrive **for free**,
because they are properties of the bytes, not of our code.

### 0.4 Invariants (binding; the phase-15 list, amended)

| # | Invariant |
|---|---|
| **I1** | **Read-only with respect to both games.** `gbacore_read8/16/32` only. No `write8`, no `write16`, no exceptions. This is the property that makes presence unable to corrupt a save; a phase that adds a write is a phase that has failed. |
| **I2** | **All cross-game reads stay inside the existing parked window** (`main.c` ~3230–3360, "Workers are parked here -> touch RAM access safe"). No new sync primitive, no read from a worker. |
| **I3** | **Fail to the placeholder, never to garbage.** Every address is bounds-checked and every failure has a named reason code that reaches `g_presDiag` and the HUD (§S4). A wild deref is a crash, not a cosmetic bug. |
| **I4** | **No bundled Pokémon-like art.** The IP posture *improves*: we ship nothing, and read the user's own ROM at runtime on their own device, exactly as the game itself does. Do not add a sprite asset. |
| **I5** | Pure-C, host-tested (CLAUDE.md #4). Every byte-level decision — detile, 4bpp→RGBA8, BGR555, flip, the 3DS tiled-texture encode, the resolve predicate — lives in a header-free module with golden tests (§S7). |
| **I6** | Composes with the phase-14 tilt exactly as the placeholder does: foot anchor projected, sprite **upright and UNSCALED**, y-sorted on foot y (§S5). |
| **I7'** | *(replaces phase-15 invariant 7)* **The peer's own frame is the source of truth when it can be read; our own generic art is the fallback, not the goal.** Ranked: live VRAM → drop-in baked art → generated placeholder. The magenta tells stay in the placeholder untouched. |
| **I8** | Suites stay green and grow (14th host suite, §S7). `make` clean, zero warnings in new files. Dated `BUILDLOG.md` entry per slice. Hardware-final (CLAUDE.md #6). |

### 0.5 Concurrency note (phase 19)

Phase 19 (`docs/phase19-legible/`) is running at the same time and has `source/main.c`,
`source/assets.c`, `source/typography.h`, `source/uihit.h`, `test/host/test_typography.c`,
`test/host/test_uihit.c` and `tools/build_assets.sh` modified in the working tree. Phase 20's `main.c`
edits are confined to three regions that phase 19 does not touch — the presence art block (~1345–1455),
the presence fill site in the parked window (~3317–3356), and `presence_draw_screen` (~1557–1700) —
plus one appended `g_presDiag` block. **Do not reformat, re-indent or reflow anything else in `main.c`.**
Phase 20's new host suite is a new file, so the suites cannot collide. Only one Azahar may run: do all of
§S1–§S5 and §S7 with no emulator, then take it for §S6.

---

## S1. Source of truth for the sprite — the chain, end to end

Everything below is cited against pret's byte-matched `symbols` branch and pret master sources. Symbol
values were re-read and cross-map-diffed **this session** from the maps already on this machine at
`/tmp/pret/*.sym` plus `pokeruby`/`pokesapphire` (both revisions) fetched fresh.

### S1.1 The chain

```
gObjectEvents[0]                        ← the PLAYER's object event, slot 0 (already in our profile
  +0x00 bit0  active                       as GameProfile.mapObjects; gamestate.c:277 reads it today)
  +0x04       u8  spriteId              ← NEW read
  +0x05       u8  graphicsId            ← NEW read (form: normal / bike / surf / …)
        │
        ▼
gSprites[spriteId]                      ← NEW profile column `sprites`; stride 0x44
  +0x00 struct OamData oam  (8 bytes)   ← NEW read: attr0 / attr1 / attr2
  +0x2A u8  animNum                     ← NEW read  ┐ together these ARE the current frame index,
  +0x2B u8  animCmdIndex                ← NEW read  ┘ and therefore the change key (§S3.5)
  +0x3E u16 flags: inUse:1 coordOffsetEnabled:1 invisible:1 …   ← NEW read
        │
        ├── attr2 bits 0-9   tileNum      →  OBJ VRAM  0x06010000 + 32*tileNum
        ├── attr2 bits 12-15 paletteNum   →  OBJ palette bank
        ├── attr0 bits 14-15 shape  ┐
        ├── attr1 bits 14-15 size   ┘     →  w × h  (16×32 for a standard trainer)
        ├── attr1 bit 12     hFlip        →  THE MOONWALK BIT (§S1.5)
        ├── attr1 bit 13     vFlip
        ├── attr0 bit 8      affineMode   →  must be 0, else attr1[9:13] is a matrix index, not flips
        └── attr0 bit 13     bpp          →  must be 0 (4bpp), else 256-colour OBJ

OBJ palette bank n  →  gPlttBufferUnfaded[256 + 16n + i]   (NEW profile column `plttUnfaded`)
                       fallback: hardware PLTT 0x05000200 + 32n + 2i
```

### S1.2 Struct offsets, with citations

**`struct ObjectEvent`** — `pret/pokeemerald include/global.fieldmap.h:194-254`, and
`pret/pokefirered include/global.fieldmap.h:238-...`; both read from the copies at
`/tmp/pret/em/global.fieldmap.h` and `/tmp/pret/fr/global.fieldmap.h` this session, **byte-identical**
for every field below. `pokeruby`'s copy is identical too (verified in phase 18, BUILDLOG S3).

| offset | field | status in our source |
|---|---|---|
| `+0x00` bit 0 | `active:1` | **already read** — `gamestate.c:277`, `main.c:3266` |
| `+0x04` | `u8 spriteId` | **NEW** |
| `+0x05` | `u8 graphicsId` | **NEW** |
| `+0x0B` hi nibble | `previousElevation:4` | already read — `main.c:3269` |
| `+0x10 / +0x12` | `currentCoords.x / .y` | **already read** — `gamestate.c:278-279` |
| `+0x18` low nibble | `facingDirection:4` | **already read** — `gamestate.c:280` |
| stride | `0x24` | already used — `main.c:3265` (`gObjectEvents` sym size `0x240` = 16 × `0x24`) |

**`struct Sprite`** — `pret/pokeemerald include/sprite.h:194-242` (fetched to `/tmp/pret/em_sprite.h`).
Size `0x44`; `gSprites` sym size is `0x1144` = **65 × 0x44** in *all nine* symbol maps checked
(`MAX_SPRITES + 1`, `include/sprite.h:5`), which is itself strong evidence the struct is unchanged
across RS / FRLG / Emerald.

| offset | field | why we read it |
|---|---|---|
| `+0x00` | `struct OamData oam` (8 B) | tileNum, paletteNum, shape/size, flips, affineMode, bpp |
| `+0x2A` | `u8 animNum` | change key |
| `+0x2B` | `u8 animCmdIndex` | change key |
| `+0x3E` | `u16` bitfield: bit0 `inUse`, bit1 `coordOffsetEnabled`, bit2 `invisible` | liveness / hidden |
| `+0x42` | `subspriteTableNum:6`, `subspriteMode:2` | **read for diagnostics only** — see §S1.4 |

**`struct OamData`** — `pret/pokeemerald include/sprite.h`, the standard GBA OBJ attribute triple:

```
attr0 (u16 @ +0x00):  y:8 | affineMode:2 | objMode:2 | mosaic:1 | bpp:1 | shape:2
attr1 (u16 @ +0x02):  x:9 | matrixNum:5                          | size:2
attr2 (u16 @ +0x04):  tileNum:10 | priority:2 | paletteNum:4
```

### S1.3 The addresses — new `GameProfile` columns

Three new columns, **appended** (the table is positional-initialised; `gamestate.h` says appending is the
only safe edit, and phase 18's `mapHeaderPath` / `sbDirect` are the precedent).

| column | BPEE | BPRE | BPGE | AXVE / AXPE | status |
|---|---|---|---|---|---|
| `sprites` (`gSprites`) | `0x02020630` | `0x0202063C` | `0x0202063C` | `0x02020004` | **VERIFIED-SYM** |
| `plttUnfaded` (`gPlttBufferUnfaded`) | `0x02037714` | `0x020371F8` | `0x020371F8` | `0x0202EAC8` | **VERIFIED-SYM** |
| `playerAvatar` (`gPlayerAvatar`) | `0x02037590` | `0x02037078` | `0x02037078` | `0x0202E858` | **VERIFIED-SYM** |

Provenance, re-derived this session and required to be re-derived again by the implementer (house rule —
never ship someone else's word, not even this document's):

```
gSprites            pokeemerald 02020630 | pokefirered 0202063c = _rev1 | pokeleafgreen 0202063c = _rev1
                    pokeruby 02020004 = pokesapphire = pokeruby_rev1 = pokesapphire_rev1   (4/4 AGREE)
gPlttBufferUnfaded  pokeemerald 02037714 | pokefirered 020371f8 = _rev1 | pokeleafgreen 020371f8 = _rev1
                    pokeruby 0202eac8 x4                                                    (4/4 AGREE)
gPlayerAvatar       pokeemerald 02037590 (size 0x24) | FR/LG 02037078 (size 0x20, both revs)
                    pokeruby 0202e858 (size 0x24) x4                                        (4/4 AGREE)
```

`0` in any of the three ⇒ that game gets the placeholder, with reason `PSPR_R_NOPROF` (§S4). All five
existing profiles get real values, so no game degrades — but Ruby/Sapphire remain **VERIFY-ON-HW** for the
same reason phase 18 gave: no RS ROM exists on this machine.

`gPlayerAvatar` is used **only** as a corroborating cross-check (`gPlayerAvatar.spriteId` at `+0x04` must
equal `gObjectEvents[0].spriteId`) and to fill `PeerPresence.avatarFlags` from `gPlayerAvatar.flags` at
`+0x00`, which phase 15 reserved and shipped as 0 (`presence.h:0x14`, D1.6: *"gPlayerAvatar is NOT in the
profile until a bike/surf art variant exists"* — it now is one, because the art variant is whatever the
peer's own VRAM holds). Both structs put `flags` at `+0x00` and `spriteId` at `+0x04`
(`global.fieldmap.h:342-352` EM, `:365-375` FR — read this session; FR's struct diverges only *after*
`+0x08`, which we never touch).

### S1.4 Is slot 0 the player, and is it one 16×32 OBJ? — yes, and the subsprite question is answered

**Slot 0.** `gObjectEvents[0]` is the player in practice and our whole presence stack already depends on
it (`gamestate.c:277-280`, hardware-exercised by smart touch and the phase-18 co-op runs). The
`gPlayerAvatar.spriteId` cross-check above turns "in practice" into a per-frame assertion at a cost of one
`read8`: disagreement ⇒ `PSPR_R_MISMATCH` ⇒ placeholder.

**Subsprites — the trap, and why it is not one.** A Gen-3 overworld character *does* carry a subsprite
table (`gObjectEventGraphicsInfo_BrendanNormal.subspriteTables = sOamTables_16x32`), and
`sprite->subspriteTableNum` is driven by elevation and long grass
(`event_object_movement.c:7701,7744,7750` in `/tmp/pret/em_eom.c`). That splits the *hardware OAM draw*
into up to three entries. It does **not** re-tile the pixels. `src/data/object_events/object_event_subsprites.h:89-183`
(fetched this session) shows every `sOamTable_16x32_*` covering the same 8-tile block with
`tileOffset` 0 / 4 / 6 and `x = -8, y = -16` throughout — the tables exist to give the character's upper
and lower halves *different OBJ priorities*, which is how a Gen-3 player walks behind a ledge or into
long grass. **The pixel block at `tileNum … tileNum+7` is always the whole, un-clipped 16×32 sprite.**

And `AddSubspritesToOamBuffer` (`src/sprite.c:1679-1712`) reads `sprite->oam` and writes *copies* into the
OAM buffer, leaving `sprite->oam` intact — so `gSprites[id].oam` is authoritative regardless of subsprite
state. `subspriteTableNum` is therefore read for **diagnostics only** (it tells a future reader "the peer
was in long grass / on a different elevation plane when this frame was captured") and never gates anything.

**Where the frame pixels come from.** `src/sprite.c:936-939`:

```c
if (sprite->usingSheet)  sprite->oam.tileNum = sprite->sheetTileStart + imageValue;
else                     RequestSpriteFrameImageCopy(imageValue, sprite->oam.tileNum, sprite->images);
```

and `RequestSpriteFrameImageCopy` (`:802-811`) DMAs to `(u8 *)OBJ_VRAM0 + TILE_SIZE_4BPP * tileNum`.
Overworld object events take the **second** branch (`tileTag = TAG_NONE`), so `tileNum` is a *fixed*
allocation for the life of the sprite and the animation changes the **VRAM contents**, not the index.
Both branches are handled identically by "read the tiles at `tileNum` now", which is the whole point:
we never have to know which branch a given graphicsId took.

### S1.5 The h-flip — the moonwalk bit

Gen-3 authors South / North / West and produces **East by mirroring West**. `sAnimCmd_FaceEast` is a frame
command with `.hFlip = TRUE` (`struct AnimFrameCmd`, `include/sprite.h:47-55`), and `AnimCmd_frame` applies
it through `SetSpriteOamFlipBits`, which packs the flips into `oam.matrixNum`:

```c
/* src/sprite.c — SetSpriteOamFlipBits, and read back verbatim at :1708-1709 */
hFlip = ((s32)oam->matrixNum >> 3) & 1;
vFlip = ((s32)oam->matrixNum >> 4) & 1;
```

`matrixNum` occupies attr1 bits 9–13, so **`hFlip` = attr1 bit 12, `vFlip` = attr1 bit 13** — the ordinary
GBA OBJ flip bits. Ignoring them gives a trainer who faces East and walks backwards. This spec **bakes the
flip into the decoded pixels** (§S3.3) rather than passing it to the draw, for two reasons: the flip then
becomes a pure-C, golden-tested transform instead of a render-path parameter, and it avoids re-entering
`presence_art_clip`'s mirrored-source-offset subtlety (the one `presence_art.h` warns about at length and
TEST 28 pins).

`affineMode != 0` means attr1[9:13] is a real matrix index and the flip bits do not exist — a rotated or
scaled sprite. An overworld trainer is never affine; if we ever see one, we refuse (`PSPR_R_AFFINE`).

### S1.6 Tile count, tile order, and the 1D/2D question

A standard trainer is `shape = 2` (TALL), `size = 1` ⇒ **16 × 32 px = 2 × 4 = 8 tiles, 32 bytes each,
256 bytes total**. Sizes we accept generally: `w, h ∈ {8, 16, 32}`, i.e. at most `4 × 4 = 16` tiles /
512 bytes (a surfing or bike form is 32×32; §S2.4).

**Mapping mode.** `DISPCNT` bit 6 selects 1D (`1`) or 2D (`0`) OBJ character mapping. Gen-3 Pokémon runs
the overworld in **BG mode 0 with `DISPCNT_OBJ_1D_MAP`**, which is also the only mode in which
`RequestSpriteFrameImageCopy`'s "copy `images[i].size` bytes to `tileNum`" makes sense — the `SpriteFrameImage`
blob is authored as a linear run of the sprite's tiles. So:

```
1D (bit 6 set):   tileOffset(tx, ty) = ty * (w/8) + tx
2D (bit 6 clear): tileOffset(tx, ty) = ty * 32     + tx      /* OBJ VRAM is a 32-tile-wide grid */
byte address      = 0x06010000 + 32 * (tileNum + tileOffset)
```

**We do not assume — we read `DISPCNT` at `0x04000000`** (`gbacore_read16`; mGBA routes
`GBA_REGION_IO → GBAIORead`, `external/mgba/src/gba/memory.c:563`, so this is a supported read) and:

* `mode = DISPCNT & 7` must be `≤ 2`. In bitmap modes 3–5 the OBJ character base moves to `0x06014000`
  and tiles 0–511 are unusable; refuse with `PSPR_R_MODE`.
* `DISPCNT & 0x1000` (OBJ enable) must be set, else the peer is not drawing sprites at all.
* `map1d = (DISPCNT >> 6) & 1`, used as above.
* **If the read comes back exactly `0x0000` or `0xFFFF`** — the signature of an IO path that is not
  wired — set the diag bit `PSPR_D_NOIO` and fall back to the pret-cited default (`mode 0, 1D`), which is
  a *documented property of these five games*, not a guess. The bit reaches `g_presDiag`, so a run that
  ever takes that path says so.

**Bounds.** `tileNum` is 10 bits (0…1023) and the last tile we touch must satisfy
`tileNum + maxTileOffset ≤ 1023`; the resulting address range `0x06010000 + 32*1023 + 31 = 0x06017FFF`
is the last byte of VRAM, so a passing bounds check makes an out-of-range read impossible. Refuse with
`PSPR_R_TILE` otherwise.

### S1.7 The palette

`paletteNum` (attr2 bits 12–15) is the OBJ palette **bank** 0…15. Sixteen BGR555 colours, **colour 0 is
transparent** and must never be painted.

Primary source: **`gPlttBufferUnfaded`**, a `u16[512]` covering BG banks 0–15 then OBJ banks 0–15, so

```
byte offset of OBJ bank n colour i  =  512 + 32*n + 2*i      (from gPlttBufferUnfaded)
```

Fallback when `plttUnfaded == 0`: hardware palette RAM, `0x05000200 + 32*n + 2*i`.

**Why unfaded is the primary.** `gPlttBufferFaded` (and hardware PLTT) carry the peer's *screen-wide*
fades. When the peer walks through a door their whole palette ramps to black over ~16 frames; reading the
faded buffer would turn the avatar on **our** screen into a black silhouette and back, for a transition
that is happening on someone else's screen. The unfaded buffer is the sprite's own colours. Our screen's
own grading still applies, because the avatar is drawn *before* `light_pass` (`main.c` A2.1 ordering) and
is multiplied by the time-of-day tint like the world it stands in. The trade is that a genuine in-game
palette effect on the peer's side (Flash in a cave) is not reflected; that is the correct call and is
recorded here so nobody "fixes" it later.

### S1.8 Everything read, in one table

| # | address | width | new? | purpose |
|---|---|---|---|---|
| 1 | `mapObjects + 0x00` | 32 | no (already read) | `active` bit |
| 2 | `mapObjects + 0x04` | 8 | **yes** | `spriteId` |
| 3 | `mapObjects + 0x05` | 8 | **yes** | `graphicsId` |
| 4 | `playerAvatar + 0x00` | 8 | **yes** | `PlayerAvatar.flags` → `avatarFlags` |
| 5 | `playerAvatar + 0x04` | 8 | **yes** | cross-check `spriteId` |
| 6–8 | `sprites + 0x44*id + 0/2/4` | 16×3 | **yes** | attr0 / attr1 / attr2 |
| 9 | `sprites + 0x44*id + 0x2A` | 16 | **yes** | `animNum` + `animCmdIndex` (one halfword) |
| 10 | `sprites + 0x44*id + 0x3E` | 16 | **yes** | `inUse` / `invisible` |
| 11 | `sprites + 0x44*id + 0x42` | 8 | **yes** | `subspriteTableNum` (diagnostics only) |
| 12 | `0x04000000` | 16 | **yes** | `DISPCNT` |
| 13–28 | palette bank, 16 colours | 16×16 | **yes** | the OBJ palette |
| — | *on change only* | 32 × 64…128 | **yes** | the tile bytes (§S3.5) |

**Steady state: 12 header reads + 16 palette reads = 28 bus reads per game per frame.** Compare
`build_depth_grid`'s ~150 in the same window. Burst on change: **+64 `read32` (16×32) or +128 (32×32)**.

---

## S2. The alternatives, weighed

### S2.1 (a) Live VRAM/OAM read — **RECOMMENDED, this spec**

**What it gets right.** It is the *actual displayed frame*, so it is correct by construction for every
question we would otherwise have to answer ourselves: which of the three walk frames, which facing, is
East mirrored, is the trainer male or female, are they on a bike, are they surfing, have they got the
Devon Scope-era outfit, is this a hacked ROM with replaced graphics. All of it is already in the bytes.
It also *deletes* work: `presence_walk_step`'s 4-beat cycle, `presence_art_cell`'s facing→row table and
the gender variant selection all become fallback-only paths.

**Failure modes, honestly.**

| situation | what happens | verdict |
|---|---|---|
| mid-animation | we read whatever frame is up. That is the *point*. | correct |
| on a bike / surfing / underwater / Acro wheelie | `graphicsId` changes, VRAM changes, `w×h` may become 32×32; we follow it | correct, free |
| doorway fade | unfaded palette ⇒ colours hold; the *position* is whatever the game reports; when the map changes the phase-15 same-map gate closes and nothing is drawn anyway | correct |
| peer opens a menu / start menu / bag | presence's P-G2/P-G ladder already refuses to draw (`ctx != OVERWORLD`), so we never even read | already handled |
| **peer enters a battle** | `ResetSpriteData` frees the field sprites and `gSprites[spriteId]` is *reused by a battle sprite*. Reading it would give a Pokémon's OAM. | **handled**: the sprite read only runs when the peer's own `GameState.ctx == GCTX_OVERWORLD` and `sb1Valid`, i.e. the exact same guard the anchor uses; plus `inUse`, the `gPlayerAvatar.spriteId` cross-check and the `w,h ≤ 32` test. Four independent refusals. |
| peer's sprite is `invisible` (cutscene, hidden player) | `sprite->invisible` set ⇒ `PSPR_R_HIDDEN` ⇒ placeholder | acceptable; rare, and the peer is not really "there" |
| **peer is on the other end of a UDS link (M4)** | we cannot read another console's VRAM | **placeholder**, by design; §S2.5 |
| unmapped game (no profile) | already no presence at all | already handled |

**Costs.** §S1.8 and §S3.6. There is no GPU stall, no `C3D_SyncDisplayTransfer` on the per-frame path, and
no new texture — the decoded cell is written straight into the *existing* 128×128 presence sheet (§S3.4).

### S2.2 (b) Decode from ROM via `gObjectEventGraphicsInfoPointers`

Chain: `graphicsId` → `gObjectEventGraphicsInfoPointers[graphicsId]` (EM `0x08505620`, FR `0x0839FDB0`
rev0 / `0x0839FE20` rev1, LG `0x0839FD90` / `0x0839FE00` — note the **revision-dependent ROM addresses**,
which is exactly the class `gamestate.c`'s house rule bans without per-rev evidence) → `struct
ObjectEventGraphicsInfo` → `.images[frame].data` → 512 uncompressed bytes; palette via `.paletteTag` →
a linear search of `gObjectEventSpritePalettes[]`.

**Verdict: strictly worse, for a reason that is easy to miss.** It looks "stable and frame-independent",
but *it is not frame-independent*: to pick `images[frame]` you still need the current frame index, which
lives in `sprite->animNum`/`animCmdIndex` and requires walking a `const union AnimCmd *const *anims` table
in ROM. So (b) is (a) plus a ROM pointer-chase plus per-revision ROM addresses plus an LZ77 path for the
`.compressed` graphics — for identical output.

It has exactly one genuine advantage worth recording: **it works when the peer is not on the overworld**,
because ROM does not get reused. If a future phase wants a *static portrait of the peer* on the nameplate
card or in the pause menu (where "they're in a battle right now" is the interesting case), (b) is the right
tool for that surface. Not for this one.

### S2.3 (c) Keep a shipped generic sprite

Already built and already the fallback: `presence_art.c`'s generated placeholder, plus the drop-in baked
path (`peer-walk-m.png` / `peer-walk-f.png` through `assets_wgt`, `main.c:1409-1416`). Its failure mode is
the one the user just reported — it is not them. Keeping it as the *fallback* is right and costs nothing;
keeping it as the *product* is what this phase is undoing.

### S2.4 The recommendation

**(a), with (c) as the fallback, ranked live → baked → placeholder.** (b) is documented and deliberately
not built.

### S2.5 What this means for M4 (wireless)

The live read is **same-console only**. Over UDS the peer's VRAM is on another device, so an M4 session
falls back to the placeholder with reason `PSPR_R_REMOTE`. That is not a regression (M4 does not exist
yet) and the future upgrade is cheap and specified here so it is not re-invented: **292 bytes** — a
12-byte header (`graphicsId`, `w`, `h`, flips, key) + 256 B of 4bpp tiles + 32 B of BGR555 palette —
pushed on the same beacon channel *only when the key changes*, i.e. ~7 times a second while walking,
≈ 2 KB/s. `PeerPresence` stays 48 bytes; this is a second, larger, lower-rate record. Flag for `ip-legal`
before it ships: transmitting decoded ROM graphics between two consoles is a different question from
reading them locally, even when both users own the cartridge.

---

## S3. Conversion and upload

### S3.1 4bpp tile → indices

Each tile is 32 bytes: 8 rows of 4 bytes; within a byte the **low nibble is the LEFT pixel**.

```
index(x, y) within a tile  =  (byte[ y*4 + (x>>1) ] >> ((x & 1) * 4)) & 0x0F
```

### S3.2 BGR555 → RGBA8

```c
/* returns 0xRRGGBBAA — presence_art.h's convention, NOT C2D_Color32's */
uint32_t pspr_bgr555_to_rgba8(uint16_t c) {
    uint32_t r5 =  c        & 0x1F, g5 = (c >> 5) & 0x1F, b5 = (c >> 10) & 0x1F;
    uint32_t r8 = (r5 << 3) | (r5 >> 2);   /* 5→8 with the low bits replicated: 31 → 255 exactly */
    uint32_t g8 = (g5 << 3) | (g5 >> 2);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);
    return (r8 << 24) | (g8 << 16) | (b8 << 8) | 0xFFu;
}
```

**Index 0 is transparent**: emit `0x00000000` (A = 0), never colour 0's RGB at A = 255.

### S3.3 Detile + flip, in one pass

```
for ty in 0 .. h/8-1:
  for tx in 0 .. w/8-1:
    tile = raw + 32 * tileOffset(tx, ty, w/8, map1d)          /* §S1.6 */
    for py in 0..7: for px in 0..7:
        i  = nibble(tile, px, py)
        sx = hFlip ? (w-1 - (tx*8+px)) : (tx*8+px)
        sy = vFlip ? (h-1 - (ty*8+py)) : (ty*8+py)
        out[sy*w + sx] = i ? pspr_bgr555_to_rgba8(pal[i]) : 0x00000000
```

The flip is applied to the **destination** coordinate, which is what makes it a single pass and what keeps
the draw path flip-free (§S1.5).

### S3.4 Edge bleed (on by default)

Under tilt the presence draw uses `GPU_LINEAR` (`main.c:1662/1679`, A1.6.3). A LINEAR tap that straddles
the silhouette blends an opaque texel with a transparent one whose RGB is `0x000000`, producing a dark
fringe. Real trainer art has far more silhouette per pixel than the placeholder's flat blocks, so:

```
pspr_bleed_edges(px, w, h):
  for every texel with A == 0 that has at least one 4-neighbour with A != 0:
      copy that neighbour's RGB, leave A == 0
```

Alpha is never modified — the test pins that (§S7 T6), so this can never accidentally paint a halo.

### S3.5 When to re-decode: the change key

```c
typedef struct {                 /* every field comes from a header read, no pixels */
    uint8_t  ok, reason;
    uint8_t  spriteId, graphicsId;
    uint16_t tileNum;
    uint8_t  pal, hFlip, vFlip;
    uint8_t  w, h;
    uint8_t  animNum, animCmdIndex;
    uint8_t  map1d, subTbl;      /* subTbl: diagnostics only */
} PsprHdr;
```

Re-read pixels **iff** `memcmp(&newHdr, &cache.key, sizeof)` differs **or** the 32 palette bytes differ.

**Why `animNum` + `animCmdIndex` and not `tileNum` alone.** For the overworld's non-sheet sprites
`tileNum` is *constant* across the whole walk cycle (§S1.4) and only the VRAM contents change — so keying
on `tileNum` would freeze the avatar on its first frame forever. `(animNum, animCmdIndex)` is 1:1 with
`imageValue` within a `graphicsId` (`AnimCmd_frame`, `src/sprite.c:934-939`), and `graphicsId` is in the
key, so the pair is a complete frame identity. `tileNum` stays in the key anyway, because it *is* the
discriminator for the `usingSheet` branch and costs nothing.

**Observed rate.** A Gen-3 walk cycle advances a frame roughly every 8 emulated frames, so the burst fires
about **7 times a second while the peer walks and zero times a second while they stand**.

### S3.6 The upload — CPU-encode straight into the existing sheet

**No new texture, no GX transfer on the per-frame path.**

The 128×128 RGBA8 presence sheet (`s_peerTex`, `PRES_SHEET_DIM`) uses rows 0–95 for the two placeholder
variants (`PRES_ART_ROWS * PRES_CELL_H = 96`). **Rows 96–127 are free across the full width** — exactly
four 32×32 **live cells** at x = 0 / 32 / 64 / 96, which is the `PRES_MAX_PEERS` ceiling (3–4 player) with
no further layout work.

```
#define PSPR_LIVE_Y      96
#define PSPR_LIVE_DIM    32
#define PSPR_LIVE_X(slot) ((slot) * PSPR_LIVE_DIM)
```

A 3DS tiled texture is 8×8 blocks in row-major order, each block Morton (z-order) internally — the exact
layout `test/host/test_typography.c:150-164` already decodes for A4 font sheets, and the same `morton8`
is reused verbatim:

```c
int pspr_morton8(int x, int y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
         | ((x & 4) << 2) | ((y & 4) << 3);
}
/* byte offset of texel (x, y) in a DIM×DIM GPU_RGBA8 texture */
offset = ((y/8) * (DIM/8) + (x/8)) * 256 + pspr_morton8(x & 7, y & 7) * 4;
```

Bytes at that offset are **A, B, G, R ascending** — the same `put_px` order `presence_art.c` uses and
documents. Writing is a plain CPU store; **no GPU command is issued**, so this is legal *inside*
`C3D_FrameBegin/End` and must be placed there: right beside `presence_solve` (`main.c:4154`), which runs
after `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` has already waited for the previous frame to finish reading the
texture. Doing it earlier — in the parked window, before `FrameBegin` — would race the still-in-flight
previous frame (the hazard `main.c:1141` names). **The RAM reads stay in the parked window (I2); only the
decode+blit moves.**

**Cache flush.** The four live cells occupy tiled block rows 12–15 = the **last 16 KB** of the 64 KB
texture, contiguously (`12 * 16 * 256 = 49152`). Flush exactly that range:
`GSPGPU_FlushDataCache((u8*)s_peerTex.data + 49152, 16384)` — not `C3D_TexFlush`, which would flush all
64 KB every time. Assert the arithmetic with a `_Static_assert` so a change to `PRES_SHEET_DIM` cannot
silently invalidate it.

### S3.7 The tiling proof — how we know the encoder is right without a screenshot

The one genuine risk in §S3.6 is that the CPU encoder's layout (Morton order, block-row order, byte order,
and whether `GX_TRANSFER_FLIP_VERT(0)` inverts rows) does not match what
`C3D_SyncDisplayTransfer` actually produced for the placeholder. Getting it wrong yields an upside-down or
channel-swapped or scrambled sprite, and the failure is only visible in a photo.

**So we prove it at init, on the real device, with a `memcmp`:**

1. `presence_art_ensure` already builds the placeholder into a linear 64 KB `stage` and transfers it into
   `s_peerTex` (`main.c:1418-1436`). That transferred result is the **oracle**.
2. Allocate a second 64 KB `probe`, run the new CPU encoder over `stage` into `probe`.
3. `s_liveTileOk = (memcmp(probe, s_peerTex.data, PRES_SHEET_BYTES) == 0)`.
4. Free both.

If they match, the encoder's tiling **and** byte order **and** row order are proven identical to the path
that the phase-18 screenshot already validated visually. If they differ, `s_liveTileOk` stays false, the
live path is off for the whole run, the diag says `PSPR_R_TILEFMT`, and the placeholder — which came from
the trusted transfer — still draws. Cost: one 64 KB encode + one 64 KB compare + 64 KB of transient linear
heap, **once per session**, on the lazy path that only runs when the co-op pref is on.

### S3.8 Cost summary — the honest bill

| item | steady state (peer standing) | while the peer walks |
|---|---|---|
| emulated-bus reads / game / frame | **28** (12 header + 16 palette) | 28, **+ ~64–128 `read32` about 7× / s** |
| CPU pixel work | 0 | 512–1024 texels decoded + bled + Morton-stored, ~7× / s |
| texture memory | **0 new** (rows 96–127 of the existing 64 KB sheet) | — |
| GPU work | **0** — no transfer, no extra bind, no extra draw call | — |
| cache flush | 0 | 16 KB, ~7× / s |
| init | 64 KB encode + 64 KB memcmp + 64 KB transient linear, once | — |

For scale: the same parked window already performs ~150 reads in `build_depth_grid` and an OAM scan of up
to 128 entries × 2 `read16`. This phase adds roughly a **fifth** on top, and nothing on the GPU. If a
hardware run shows the frame budget move at all, the first lever is polling the palette every 8th frame
instead of every frame (−16 reads/game/frame), and the second is capping bursts to one game per frame.

---

## S4. Fallback and safety

### S4.1 The resolve predicate — `ok` iff ALL of these

```
1.  the co-op pref is on and presence_solve already decided to DRAW this peer
2.  the peer's producer is a SAME-CONSOLE core (not an M4 UDS record)      -> else PSPR_R_REMOTE
3.  profile has sprites != 0 (and plttUnfaded != 0 or the PLTT fallback)   -> else PSPR_R_NOPROF
4.  s_peerTexOk && s_liveTileOk (the §S3.7 proof passed)                   -> else PSPR_R_TILEFMT / NOSURF
5.  the peer's own GameState: valid && ctx == GCTX_OVERWORLD && sb1Valid   -> else PSPR_R_CTX
6.  gObjectEvents[0].active                                               -> else PSPR_R_NOOBJ
7.  spriteId < 64 (MAX_SPRITES) and != SPRITE_NONE (0xFF)                 -> else PSPR_R_BADID
8.  playerAvatar == 0  ||  gPlayerAvatar.spriteId == that spriteId        -> else PSPR_R_MISMATCH
9.  sprite.inUse                                                          -> else PSPR_R_NOTINUSE
10. !sprite.invisible                                                     -> else PSPR_R_HIDDEN
11. attr0 affineMode == 0                                                 -> else PSPR_R_AFFINE
12. attr0 bpp == 0 (4bpp)                                                 -> else PSPR_R_BPP
13. shape != 3 && w <= 32 && h <= 32                                      -> else PSPR_R_SIZE
14. DISPCNT: OBJ enabled && (DISPCNT & 7) <= 2   (or PSPR_D_NOIO default) -> else PSPR_R_MODE
15. tileNum + maxTileOffset <= 1023                                       -> else PSPR_R_TILE
16. a decode has actually produced a live cell this session               -> else PSPR_R_PENDING
```

Anything other than `PSPR_R_OK` ⇒ **draw the existing placeholder for that peer, unchanged**. Never draw a
partially-decoded cell, never draw a stale cell whose key no longer matches, never skip the draw entirely
(a peer who vanishes is worse than a peer who is magenta).

### S4.2 Reason codes

```c
#define PSPR_R_OK        0
#define PSPR_R_REMOTE    1   /* M4 peer: their VRAM is on another console          */
#define PSPR_R_NOPROF    2   /* GameProfile.sprites == 0                            */
#define PSPR_R_NOSURF    3   /* s_peerTex never built                               */
#define PSPR_R_TILEFMT   4   /* the §S3.7 init proof failed -> live path off, run   */
#define PSPR_R_CTX       5   /* peer not in a readable overworld state              */
#define PSPR_R_NOOBJ     6   /* gObjectEvents[0] inactive                           */
#define PSPR_R_BADID     7   /* spriteId out of range / SPRITE_NONE                 */
#define PSPR_R_MISMATCH  8   /* gPlayerAvatar.spriteId disagrees                    */
#define PSPR_R_NOTINUSE  9   /* gSprites[id].inUse == 0                             */
#define PSPR_R_HIDDEN   10   /* gSprites[id].invisible                              */
#define PSPR_R_AFFINE   11   /* affineMode != 0 -> no flip bits                     */
#define PSPR_R_BPP      12   /* 256-colour OBJ                                      */
#define PSPR_R_SIZE     13   /* shape 3, or larger than 32x32                       */
#define PSPR_R_MODE     14   /* DISPCNT bitmap mode / OBJ disabled                  */
#define PSPR_R_TILE     15   /* tile range would leave OBJ VRAM                     */
#define PSPR_R_PENDING  16   /* resolved but no decode has landed yet (first frame)  */
```

### S4.3 The readout — so a future failure is diagnosable, not mysterious

**`g_presDiag` gains an APPENDED block** (existing offsets unchanged, `magic` stays `'PRS1'` so the phase-18
harness recipe keeps working; new `_Static_assert`s for every new offset, matching the fourteen that are
already there):

```c
int32_t sprReason [2];   /* PSPR_R_* per GAME (0 = live sprite drawn)               */
int32_t sprW      [2];   /* decoded cell size actually in the sheet                 */
int32_t sprH      [2];
int32_t sprTile   [2];   /* tileNum                                                 */
int32_t sprPal    [2];   /* (paletteNum) | (hFlip<<8) | (vFlip<<9) | (map1d<<10)     */
int32_t sprGfxId  [2];   /* graphicsId — the FORM (normal / bike / surf)            */
int32_t sprAnim   [2];   /* (animNum<<8) | animCmdIndex                             */
int32_t sprUploads[2];   /* decode+blit count this session (should track walking)    */
int32_t sprTileOk;       /* the §S3.7 proof: 1 = the encoder matched the GX oracle   */
int32_t sprFlags;        /* bit0 PSPR_D_NOIO (DISPCNT unreadable, defaults assumed)  */
```

**HUD.** The existing `CO-OP` line gains a compact suffix, so a photograph is enough:

```
CO-OP me 10-6@9,4 | GUYA@10-6@11,4 N/G d+2,+0 …   spr:OK 16x32 t0148 p5 F  u37
                                                  spr:AFFINE                 (on refusal)
```

`spr:` is present whenever presence draws; `OK` plus size / tileNum / palette / flip / upload count when
live, the reason name otherwise. `presence_off_reason()`'s sibling `pspr_reason_name()` is the one place
those strings live.

**gs log.** `GsDepth` gains `uint8_t prSprReason; uint8_t prSprW, prSprH; uint16_t prSprGfx;` appended
size-tolerantly, exactly as the phase-14 tilt and phase-15 presence blocks were.

### S4.4 The safety argument, stated once

Every address in §S1.8 is either a profile constant (compile-time), a constant offset from one, or a
`0x44 * spriteId` where `spriteId < 64` has been checked. The tile address is
`0x06010000 + 32*(tileNum + off)` with `tileNum + off ≤ 1023` checked, which is inside VRAM by
construction. The palette address is `plttUnfaded + 512 + 32*pal + 2*i` with `pal ≤ 15` (4 bits) and
`i ≤ 15`. **There is no unchecked pointer in this phase**, and `gbacore_read*` cannot fault anyway — it
routes through mGBA's `busRead*`, which masks every region to its size
(`external/mgba/src/gba/memory.c:535-590`). The checks exist to prevent *wrong pixels*, not segfaults.

---

## S5. Tilt / overlay integration

**Nothing about the draw path changes structurally.** The live sprite is one more source rect in the same
sheet, and it must slot into `presence_draw_screen` (`main.c:1557`) at exactly the same three points the
placeholder does.

### S5.1 The anchor is identical — and that is provable, not asserted

The placeholder's convention is bottom-centre: sprite top-left `= (footX - 8, footY - 32)`
(`PRES_FOOT_DX/DY`, `presence_art_rect`). Generalised:

```c
void presence_art_rect_wh(float footX, float footY, int w, int h, float* sprX, float* sprY) {
    *sprX = footX - (float)w * 0.5f;
    *sprY = footY - (float)h;
}
```

For `w=16, h=32` this is **bit-identical** to `presence_art_rect` — asserted in the host suite (§S7 T10),
and `presence_art_rect` is reimplemented as a one-line call to it so the two cannot drift.

**And bottom-centre is the game's own convention, not a coincidence.** `object_event_subsprites.h` places
every 16×32 table entry at `.x = -8, .y = -16` and every 32×32 entry at `.x = -16, .y = -16` — i.e. both
shapes hang the same 32-px-tall box off the sprite's centre with the **same bottom edge**. So a 32×32 surf
or bike form anchored bottom-centre at the same foot point lands exactly where the game puts it, 8 px wider
on each side. No per-form offset table is needed, and none may be added without evidence.

### S5.2 Clip

`presence_art_clip` is hard-wired to `PRES_CELL_W/H`. Generalise:

```c
int presence_art_clip_wh(float sprX, float sprY, int w, int h, int mirror, float margin, PresArtDraw* out);
int presence_art_clip   (float sprX, float sprY,               int mirror, float margin, PresArtDraw* out)
    { return presence_art_clip_wh(sprX, sprY, PRES_CELL_W, PRES_CELL_H, mirror, margin, out); }
```

TEST 27/28 keep passing unmodified (that is the point of the wrapper), and the new suite adds the 32×32
cases. **`mirror` is always 0 on the live path** — the flip is baked (§S1.5, §S3.3) — so TEST 28's
mirrored-source-offset subtlety is untouched by this phase.

### S5.3 Cell selection

```c
typedef struct { int x, y, mirror; } PresArtCell;   /* unchanged */

void presence_art_live_cell(int slot, PresArtCell* out) {   /* new */
    out->x = PSPR_LIVE_X(slot);   /* 32 * slot */
    out->y = PSPR_LIVE_Y;         /* 96 */
    out->mirror = 0;
}
```

In `presence_draw_screen`, for each peer slot:

```
if (live sprite resolved for this peer)  cell = presence_art_live_cell(slot);  w,h = decoded size
else                                     cell = presence_art_cell(gender, dir, pose); w,h = 16,32
```

Everything after that — `presence_art_rect_wh` → `presence_art_clip_wh` → `tilt_project` (foot anchor,
pure translation, `q` thrown away) → `presence_art_ysort` on foot y → `calc_xform` → the subtexture
`u = (cell.x + d.cx)/D, v = 1 - (cell.y + d.cy)/D` → `C2D_DrawImageAt` — is **the same code, unchanged**.
The y-sort key is the foot anchor, which does not depend on `w`/`h`. The tilt spill widening already
handles a 32-wide sprite because it widens the *box*, not the cell.

### S5.4 Which peer's sprite

`presence_draw_screen` is called with `&presOut[presTopGame]` and `&presSt[presTopGame].rec[0]`. The record
in `presSt[g]` was **published by game `g ^ 1`** (`main.c:3353`). **The live cell for that peer is therefore
`s_pspr[g ^ 1]`.** Get this backwards and each screen shows its own trainer standing next to itself, which
looks plausible and is wrong — so `main.c` passes an explicit `const PsprCell* live` parameter resolved at
the call site with the `^ 1` written *once*, next to the comment that explains it.

### S5.5 What becomes fallback-only

`presence_walk_step` / `PRES_WALK_CYCLE` / `presence_art_cell`'s facing→row table / the gender variant
selection all stop mattering when the live sprite resolves — the peer's own engine already did that work.
**Keep computing the pose anyway**: it costs a handful of integer ops, it is what the placeholder path needs
the moment the live path refuses, and deleting a working animation because a newer path usually covers it is
how a fallback rots. Say so in a comment at the call site.

---

## S6. The location sweep — the user's other ask

### S6.1 The mechanism, already built

* **`azctl boot --fresh-sd-fixtures`** stages **copies** of the user's `dual-gba/` ROMs and saves. The
  user's originals are never touched — and `docs/phase18-crisp/evidence/door/place.py` refuses any path
  containing `dual-gba`.
* **`place.py <fixture.sav> <group> <num> <x> <y>`** rewrites `SaveBlock1.pos` + `location` in every save
  slot's section 1 and recomputes the sector checksum with pret's `CalculateChecksum`. This is how the
  phase-18 co-op runs were positioned (`dTile = +3,+2` fixture, BUILDLOG S3 run 4). **This is the whole
  sweep**: two `place.py` calls, one boot, two screenshots.
* **`sdmc drop move <seat> "<script>"`** (the D4 channel, `control.h`) drives tile-exact walking in-game
  for the motion capture. `CTL_D4_ENABLE` is already 1.
* **`see shot both`** + **`native.py`** reconstruct the exact 400×240 / 320×240 framebuffers from the
  window capture (`--verify` proves it byte-exact). This is the channel that produced
  `coop-both-screens.png`; phase-18's earlier "both pixel channels are dead" note is **stale** — it works
  now, and this sweep depends on it.
* **`sheet.py`** assembles one captioned HTML artifact per claim.

### S6.2 The shortlist — six captures, one pair, Emerald + Emerald

Emerald + Emerald is the pair to use: it is the one the phase-18 runs proved, and the two saves are copies
of one save so both games agree on the map universe. All `(group, num)` values below were read from pret's
`data/maps/map_groups.json` this session; all suggested tiles are **one tile off a real warp event** taken
from that map's own `map.json`, so they are walkable by construction rather than by guess.

| # | what it shows | map | `(group, num)` | seat A | seat B | why this one |
|---|---|---|---|---|---|---|
| 1 | **the before/after anchor** | Mauville PC 2F | `10, 6` | `(9,4)` | `(12,6)` | the *exact* fixture the phase-18 run used (`dTile = +3,+2`, BUILDLOG S3 run 4) — the only honest A/B against `coop-both-screens.png` |
| 2 | outdoors, town | Mauville City | `0, 2` | `(8,6)` | `(10,6)` | street outside the PC door (warp `(8,5)`); daylight palette, wide open |
| 3 | indoors, shop | Mauville Mart | `10, 7` | `(3,8)` | `(5,8)` | door warps `(3,7)/(4,7)`; a tight room where the peer is unmissable |
| 4 | tall grass / route | Route 117 | `0, 32` | verify | verify | grass overlaps the sprite's lower half — the priority/subsprite case (§S1.4) rendered honestly as one flat quad, i.e. our **documented ceiling**, visible |
| 5 | cave | Rusturf Tunnel | `24, 4` | `(4,11)` | `(6,11)` | warp `(4,10)`; dark palette against a bright sprite |
| 6 | elevation / stairs | Granite Cave 1F | `24, 7` | `(37,13)` | `(36,13)` | inside the `(37,12)` entrance warp; the `(35,3)` ladder is on the same map for a follow-up shot |

**Keep the two seats within ~4 tiles of each other.** Presence culls a peer more than ~9 tiles away
(`presence_solve`'s D5.7 cull) and the visible frame is 15 × 10 tiles — two seats parked at opposite ends
of Granite Cave would produce a correct `reason 0` and an empty screen. Every pair above is 1–3 tiles apart
for that reason, and the one deliberate exception is #1, which reuses the proven `+3,+2` fixture.

Plus **one motion capture**: at #2, `see rec --seconds 8 --fps 4 --screen both` while
`sdmc drop move 1 "R4 W20 D3 W20 L4"` walks seat B past seat A. The claim to prove is *"the walk
animation and the facing come from their game, not ours"* — so the frame set must show a **turn** (the
sprite's facing changing) and a **stride** (two different walk frames), and the caption must point at
`sprAnim` / `sprUploads` from `g_presDiag` at those frames, read with `see rec --with-state`.

Route 117's grass tiles are the one entry with no derived coordinate: take them from one boot (the gs log
prints `px/py` every edge) rather than guessing, and if that costs a run, drop #4 — five locations plus
motion already answers the ask.

### S6.3 The gender/form stretch goal (optional, cheap, decisive)

Both fixture saves are copies of one save, so both trainers are Brendan and the two screens show the same
sprite. A **`gender.py`** sibling of `place.py` that flips `SaveBlock2.playerGender` (`+0x08`, section id 0,
same checksum recompute) in **seat B's fixture copy only** makes seat B's player May: `InitPlayerAvatar`
picks the graphics from `gSaveBlock2Ptr->playerGender`, so `graphicsId` changes, VRAM changes, and the two
screens draw *visibly different trainers*. That is the single most decisive image in the set — it cannot be
produced by any placeholder, and it proves the whole chain in one frame. Cosmetically the trainer card will
disagree with the sprite; it is a fixture copy, and that is fine.

Do **not** chase a bike or surf capture in this phase: both need in-game progression the fixture save may
not have. Record them as the hardware-run checklist item instead (`graphicsId` in `g_presDiag` will say
whether the form changed even without a photograph).

### S6.4 Emulator etiquette

Take Azahar only for §S6, after §S1–S5 and §S7 are done and the suite is green. One boot covers captures
1–3 (all three are two `place.py` calls and a relaunch); a second covers 4–6. `azctl stop` between them.
If the lock is held, do more host work and retry — never force it, never kill another Azahar, never
hand-launch one.

---

## S7. Host-test plan

**A new, 14th suite: `test/host/test_peersprite.c`**, driving `source/peersprite.c` (pure C, no
`<3ds.h>`, no citro, `<stdint.h>` + `<string.h>` only). A new file rather than growth in
`test_presence.c` keeps it clear of phase 19's concurrent edits.

```
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_peersprite.c source/peersprite.c -o /tmp/tps && /tmp/tps
```

| TEST | what it pins | golden input |
|---|---|---|
| **T1** | `pspr_bgr555_to_rgba8` over the whole 5-bit ramp | `0x0000 → 0x000000FF`, `0x7FFF → 0xFFFFFFFF`, `0x001F → 0xFF0000FF`, `0x03E0 → 0x00FF00FF`, `0x7C00 → 0x0000FFFF`, and every `v5` satisfying `v8 == (v5*255+15)/31` |
| **T2** | 4bpp nibble order | a synthetic tile where byte `n` = `0x21, 0x43, …` decodes to indices `1,2,3,4,…` left→right — the low-nibble-is-left rule, which is the classic way to get a mirrored sprite without noticing |
| **T3** | tile ordering, **1D** | 8 tiles each filled with a distinct index; `pspr_decode` of a 16×32 must place tile `k` at `(8*(k&1), 8*(k>>1))` |
| **T4** | tile ordering, **2D** | the same 8 tiles with `map1d = 0` must read from `tileNum + ty*32 + tx`; a 2D image decoded with the 1D rule (and vice versa) must NOT match — the "tile soup" regression |
| **T5** | flips | `hFlip` on an asymmetric golden = the column-reversed reference, exactly; `vFlip` = row-reversed; both = 180°; neither = identity. Asserted at 16×32 **and** 32×32 |
| **T6** | transparency + bleed | index 0 ⇒ `A == 0` everywhere, before and after `pspr_bleed_edges`; bleed changes only RGB of `A == 0` texels; an all-opaque and an all-transparent cell are both fixed points |
| **T7** | the change key | `pspr_hdr_changed` fires on each of `graphicsId / tileNum / pal / hFlip / vFlip / w / h / animNum / animCmdIndex / map1d`, and does **not** fire on `subTbl` alone (diagnostics-only field) or on an identical struct; `pspr_pal_changed` on any of the 16 colours |
| **T8** | **the frozen-frame regression** | two headers differing **only** in `(animNum, animCmdIndex)` with identical `tileNum` must be reported CHANGED. This is the exact bug §S3.5 exists to prevent: keying on `tileNum` alone freezes a walking peer on frame 1 forever |
| **T9** | the resolve ladder | all 16 clauses of §S4.1, each driven to its own reason code from a synthetic header, plus "every reason code has a name" (`pspr_reason_name` returns non-NULL and unique for 0…16) |
| **T10** | the anchor identity | `presence_art_rect_wh(fx, fy, 16, 32)` == `presence_art_rect(fx, fy)` bit-for-bit over a sweep; `..._wh(fx, fy, 32, 32)` puts the bottom edge at the same `fy` (§S5.1) |
| **T11** | the generalised clip | `presence_art_clip_wh(…, 16, 32, …)` == `presence_art_clip(…)` over a sweep including every edge and both mirror values; then the 32×32 cases at all four frame edges and fully outside (returns 0) |
| **T12** | the tiled encode | `pspr_blit_tiled` of a golden gradient into a 128×128 buffer, read back with an **independently written** decoder (Morton recomputed from first principles, not the shipped `pspr_morton8`), byte-exact; plus: the write touches **only** bytes in `[49152, 65536)` for a cell at `(0,96)` — the flush-range `_Static_assert` proven in software |
| **T13** | the §S3.7 proof harness | `pspr_verify_tiling` returns 1 for a matching pair and 0 for a pair differing in exactly one byte, one channel, one row, or with rows inverted — i.e. the oracle can actually go red |
| **T14** | bounds | `tileNum = 1023` with a 32×32 sprite is refused (`PSPR_R_TILE`); `tileNum = 1023` with 8×8 is accepted; `spriteId = 64`, `= 0xFF` refused; `shape = 3` refused; `pal = 15` accepted and addresses `plttUnfaded + 512 + 480` |

**Two additions to `test/host/test_presence.c`** (it owns `presence_art.c`, so the identities belong
there too): **TEST 40** = T10's identity, **TEST 41** = T11's identity — asserted in the suite that would
notice if `presence_art_rect`/`_clip` were ever edited without the `_wh` twins.

**Mutation gate** (phase-18 discipline: a green suite proves nothing until it can go red). Each of these
must produce ≥ 1 failure: drop the `hFlip` term from `pspr_decode`; swap the 1D tile formula for the 2D
one; swap the nibble order; drop `animCmdIndex` from the key; write RGBA instead of ABGR in
`pspr_blit_tiled`; invert the block-row order in the Morton offset.

**Suite count after this phase: 14.** (`test_control`, `test_diag`, `test_fieldpath`,
`test_netlink_reliability`, `test_presence`, `test_profiles`, `test_theme`, `test_tilt`,
`test_trace_replay`, `test_typography`, `test_uigeom`, `test_uihit`, `test/test_celiolink`, **+
`test_peersprite`**.)

---

## Slices (suggested; one `BUILDLOG.md` entry each)

| slice | content | needs Azahar |
|---|---|---|
| **P1** | `source/peersprite.{c,h}` + `test/host/test_peersprite.c` (T1–T9, T12–T14) + the mutation gate | no |
| **P2** | `GameProfile` columns (§S1.3, all five rows, re-derived) + `presence_read.c` reader + `test_profiles.c` growth | no |
| **P3** | `presence_art.{c,h}` `_wh` generalisation + `presence_art_live_cell` + `test_presence.c` TEST 40/41 | no |
| **P4** | `main.c`: the §S3.7 init proof, the decode+blit beside `presence_solve`, the draw-path branch, `g_presDiag` + HUD + gs-log readout | no (builds only) |
| **P5** | §S6 sweep: six captures + the motion recording + `sheet.py` artifact | **yes** |

---

## Open Questions

**Q1 — Does `gbacore_read16(c, 0x04000000)` actually return `DISPCNT` under this mGBA build?**
mGBA's `GBALoad16` routes `GBA_REGION_IO → GBAIORead` (`external/mgba/src/gba/memory.c:563`), so it should.
But no code in this project has ever read an IO register, so it is unproven *here*. §S1.6 ships a named
fallback (`PSPR_D_NOIO` → assume mode 0 / 1D, which is what these five games do) so the answer only
changes a diagnostic, never correctness. **Resolved by:** one boot; `g_presDiag.sprFlags` bit 0.

**Q2 — Does the §S3.7 tiling proof pass?** If `GX_TRANSFER_FLIP_VERT(0)` does something other than "row 0
stays row 0", the CPU encoder must be row-inverted and the proof will say so by failing. The fix is one
line in `pspr_blit_tiled`; the design deliberately makes the wrong answer safe rather than making the right
answer certain. **Resolved by:** `g_presDiag.sprTileOk` on the first boot of P4.

**Q3 — Ruby / Sapphire.** All three new addresses AGREE across all four RS maps and `gSprites`' symbol
size (`0x1144`) matches Emerald's, which is strong evidence `struct Sprite` is unchanged. But no RS ROM
exists on this machine, so the row is **VERIFIED-SYM / VERIFY-ON-HW**, exactly like phase 18's. The cheap
proof is the same one P3.6 named: boot Ruby beside Sapphire and check `g_presDiag.sprReason == 0`.

**Q4 — Fringing under tilt.** §S3.4's edge bleed is specified ON by default on the argument that real
trainer art has far more silhouette than the placeholder's flat rectangles. That argument is not a
measurement. If the hardware pass shows no visible difference with it off, turning it off saves a pass over
1 KB seven times a second — trivial either way, but the *decision* should be settled by the photo, not left
open. **Resolved by:** one A/B capture at §S6 #5 (dark cave = maximum contrast against a bright sprite).

**Q5 — 32×32 forms.** §S5.1 argues from `object_event_subsprites.h` that bottom-centre anchoring is exactly
the game's own convention for both 16×32 and 32×32 shapes. No 32×32 form has been *observed* in this app
(the fixture saves may not even own a bike). If a later run shows a surfing peer sitting too high or too
low, the fix is a per-shape offset — but do not add one speculatively; the subsprite tables say it is not
needed.

**Q6 — M4.** §S2.5 specifies the 292-byte wire record for shipping the peer's sprite over UDS, deliberately
not built. Two things gate it: HW run #13, and an `ip-legal` consult on transmitting decoded ROM graphics
between two consoles (reading them locally, which is all this phase does, is not the same question).

**Q7 — The 4-player ceiling.** Four 32×32 live cells fit in sheet rows 96–127 with no layout change, and
`PsprCache` is already an array. But `PRES_MAX_PEERS` is 1 and the 3–4 player work is deferred until
2-player is fully clean (HANDOFF Next steps #3). The layout is chosen so that raising the bound is a bound
change; nothing here should be built for four today.

**Q8 — Does the peer's `graphicsId` ever change *without* the pixels changing?** It is in the key, so a
spurious change costs one wasted 90-read burst, not a wrong frame. Only worth revisiting if
`g_presDiag.sprUploads` climbs while the peer stands still — which is exactly why that counter is in the
readout.
