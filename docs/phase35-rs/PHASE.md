# PHASE 35 — Ruby / Sapphire (RS) for romgen

Status: **S0 built** (romgen ROM layer + the four files for Ruby and Sapphire rev 2). Renderer still refuses RS
(stays 2D). Nothing hardware-run.

## Goal

Bring the voxel diorama to Pokemon Ruby and Sapphire the way Phase 34 brought it to FireRed / LeafGreen: romgen reads the
user's own cartridge at runtime and writes `regions.bin`, `signposts.bin`, `relief.bin` and `buildings.bin`; the
renderer then binds an RS profile and draws them. No ROM data, decomp text or ROM-derived art is committed.

## Scope and non-goals

In scope for the phase: Ruby and Sapphire **rev 2** (AXVE / AXPE, the revision the user owns), outdoor maps, romgen
outputs, then the renderer anchors, then RS-specific building recipes.

Non-goals (until measured on a real cartridge): rev 0 / rev 1 (ROM addresses drift between revisions; refused by the
profile gate), RS interiors in 3D (`interiors3d` false, as on FRLG), the drawn (FULL) relief (Emerald-only; RS gets the
ledges relief, like FRLG), Japanese / European carts.

## Recon (measured, not guessed)

All numbers below were measured on the user's dumps with throwaway Python scripts outside the repo (scratchpad), reading
the ROM bytes only. `projects/_reference/pokeruby` was read for numbers only (behaviour numbering, the rev 2 SHA-1 files);
nothing there was executed or copied.

**Carts.** `roms/ruby.gba` = AXVE rev 2, SHA-1 `5b64eacf892920518db4ec664e62a086dd5f5bc8`; `roms/sapphire.gba` = AXPE
rev 2, SHA-1 `89b45fb172e6b55d51fc0e61989775187f6fe63c` (both equal the pokeruby `*_rev2.sha1` reference values).

**Tables** (found by searching each ROM for the 4-byte-aligned pointer table every map header agrees with: header word 0
= the table slot of its layout id):

| | Ruby | Sapphire |
|---|---|---|
| gMapGroups | 0x083085A0 | 0x08308530 |
| gMapLayouts | 0x08304F30 | 0x08304EC0 |
| General primary tileset | 0x08286D0C | 0x08286C9C |
| Building primary tileset | 0x08286E5C | 0x08286DEC |
| Petalburg secondary | 0x08286D24 | 0x08286CB4 |
| Rustboro secondary | 0x08286D3C | 0x08286CCC |

Sapphire's tileset structs sit exactly 0x70 below Ruby's.

**Format.** Emerald's: map header 0x1C bytes, layout 24 bytes, tileset attributes u16 at tileset +0x10, 512 primary
metatiles and tiles, 6 primary palettes. The behaviour numbering is Emerald's (pokeruby and pokeemerald
`metatile_behaviors.h` differ in 42 names but not in the values the romgen and renderer sets use), so the Emerald
behaviour sets serve unchanged.

**World.** 34 map groups, sizes {54,5,5,6,7,7,8,7,7,13,8,17,10,24,13,13,14,2,2,2,3,1,1,1,86,44,12,2,1,13,1,1,3,1} = 394
maps; 332 layout slots, all present. The layout table is followed directly by map headers, whose first word is a valid
layout pointer: an unbounded walk read a phantom layout 333, so the walk is now capped at the profile's `layoutSlots`.
56 tilesets, 55 primary/secondary pairs, 75 outdoor maps.

**Ruby vs Sapphire.** 332/332 layouts have byte-identical blockdata; 56/56 tilesets have identical content. Only the
addresses differ, so every romgen output is byte-identical between the two carts.

**Ruby vs Emerald.** 237 Ruby layouts equal some Emerald layout's blockdata, 75 of them at the same id. The Emerald
recipe anchors: exteriors Petalburg (1), Mauville (3), Littleroot (10), Route 104 (20) match at the same id; Rustboro (4)
and Oldale (11) do not. Interiors 54, 59, 60, 61, 71 match; 55-58, 62, 63, 94, 432 do not. Tileset content vs Emerald's
same-role tileset (metatiles / attributes / tiles / palettes equal): General 511/512, 512/512, 446/512, 16/16;
Petalburg 110/144, 144/144, 70/159, 14/16; Rustboro 344/350, 349/350, 309/498, 0/16; Building 8/8, 8/8, 411/502, 16/16.
So RS art is close to Emerald's but not identical (Emerald redrew many tiles and the Rustboro palettes).

**Emerald recipes on Ruby** (`romgen author ROM check all`, Ruby): 21 specs; 12 pass (littleroot_house_w/e,
pokemon_center 15 placements, poke_mart 11, briney_house, flower_shop, kit_house_4 10, kit_house_5 3, gym 3, railing,
sea_rock 953, sand_boulder 147, sea_stack 555); `littleroot_lab` builds (pin matches) but fails the art check (24 and 28
wrong pixels: RS draws the lab roof differently); `hedge` fails the author round trip on Emerald too (pre-existing
tool artefact); 6 are not built because their layout pin does not match (oldale_house, rustboro_stone, rustboro_olive,
gym_rustboro, devon_corporation, rustboro_fountain) and fall back to the extruded box.

## Slice plan

| Slice | What | Done when |
|---|---|---|
| **S0** (built) | romgen ROM layer: AXVE/AXPE rev 2 profile rows (romgen-only, `rendererOn` false), layoutSlots cap, Emerald exterior recipes retargeted (interiors dropped, components expanders pointed at the RS Petalburg/Rustboro tilesets), art-failing models left out on RS, Emerald signpost heuristic, ledges relief | host suite `test_romgen_rs_world` green with pins; all Emerald/FRLG pins unchanged; device builds link (with and without `ROMGEN_DEV_HOOK=1`) |
| **S1** (built) | RS renderer anchors + self-check: gSaveBlock1 is a DIRECT struct (no pointer, needs a profile flag), gMain/gMapHeader/gPlayerAvatar/gSprites/palette buffers (the `RS_PROFILE_BODY_RAM` values in gamestate.c are a start, from .sym), weather, gfxInfo and field-effect tables harvested from rev 2 ROM literal pools, cb2Overworld measured live | anchor check passes in Azahar on both carts; wrong ROM disables cleanly |
| **S2** (built) | renderer on (`rendererOn`), Azahar captures of Littleroot and Rustboro in 3D | test_voxel_* counts unchanged + an RS row; shots |
| S3 | RS tree / shrub / grass / prop tables (ROM-measured with `romgen author ROM shrubs/props`) | host pins + Azahar |
| S4 | RS-specific recipes: Littleroot lab (RS roof), Oldale, Rustboro set (stone, olive, gym, Devon, fountain) | per-recipe gates 0/0/0, placements pinned |
| S5 | interiors (if wanted), rev 0 / 1 profiles (measure first) | |

## Risks

- **Renderer anchors (S1)** are the real work: RS keeps SaveBlock1 at a fixed address (no pointer), so code that
  dereferences Emerald's `gSaveBlock1Ptr` needs a branch; RAM layout differs from Emerald. gamestate.c already warns
  not to promote RS ROM addresses that were not measured on the cart.
- **Art drift.** A recipe whose layout pin matches can still miss on art (the lab). S0 drops such models on RS so the
  device draws the fallback box rather than a wrong roof; the ortho/density gate is the guard.
- **Revisions.** Only rev 2 is measured; rev 0/1 addresses move. The gate refuses them.
- **Memory.** Ruby's buildings.bin is 2.39 MB (52 models, 2067 placements: the sea rocks/stacks dominate). On-device
  generation was proven for Emerald on New 3DS only (O3DS ran out of memory); RS is in the same range.

## Done-gate for S0

- Host: `make -C tools/romgen test` (28 baseline suites + `test_romgen_rs_world`) and `vtest` (10 suites), 0 failures,
  every pre-existing pin byte-identical (Emerald buildings 6d321c3a, regions 007a370f, signposts 38515605, relief
  21a837f0, ledges relief eb25a383; FR = LG buildings 5ba2cc16, regions 3716874d, signposts ba2fde45, relief 32c24146).
- New pins (Ruby = Sapphire): regions `1a09cd5f…` (256,216 B), signposts `9b4d379c…` (224 records), relief
  `215a12d9…` (22,872 B, ledges), buildings `a03a3b74…` (2,389,332 B, 52 models, 2067 placements, littleroot_lab left
  out).
- Device: `make -j8` and `make -j8 ROMGEN_DEV_HOOK=1` link.
- Azahar: the app boots Ruby in 2D (renderer refuses RS by design until S1/S2). With the dev hook, romgen runs on the
  emulated device; see "Evidence".
- **Hardware: not run.**

## Evidence

**Azahar, 2026-10-07** (private emutest instance, New 3DS mode, `make ROMGEN_DEV_HOOK=1` build, `--stage-roms ruby`
= a fixture copy of ruby.gba/.sav, deleted afterwards): the app boots Ruby rev 2 and plays it in 2D (the intro ran at
~20 fps); ~10 s after load the dev hook ran romgen on the emulated ARM11 and wrote `sdmc:/3ds/3DGBA/voxel/AXVE/`. All
four files are **byte-identical to the host pins** (buildings a03a3b74, regions 1a09cd5f, relief 215a12d9, signposts
9b4d379c). Device report: `evidence/s0-ruby-romgen-device-azahar.txt` (total 10.2 s emulated, heap peak delta ~6.8 MB,
1 gate failure = littleroot_lab, left out). No voxel.log was written (the renderer never binds RS in S0), so there is no
"chunk scratch full" line to check. A 3D town capture needs S1 + S2.

The boot screenshot (`evidence/s0-ruby-boot-2d.top.png`) is kept locally and not committed (ROM-derived art).

## S1 + S2 (built, 2026-10-07)

### Anchors and how each was measured

Measured on the user's rev 2 carts (Ruby AXVE, Sapphire AXPE). "Literal count" = how many ROM literal-pool words hold the
address (the same count on both carts, the RAM block is one value); every address was cross-checked against the PokeDNA
pokeruby rev-2 symbol list (numbers only, nothing copied). The live column is the in-app self-check (`rg_anchor.c`) in
Azahar.

| anchor | Ruby | Sapphire | method | live (Azahar) |
|---|---|---|---|---|
| gMain | 0x03001770 | same | literal count 694 | checks 11, 20 |
| gSaveBlock1 (direct struct, `sb1Direct`) | 0x02025734 | same | literal count 339; RS has no SaveBlock1 pointer | checks 12, 14, 16 |
| backupLayout | 0x03004870 | same | literal count 24 | checks 13, 14 |
| backupMap | 0 (derived) | same | read from backupLayout.map (gBackupMapData 0x02029828, 4 literals) | check 13 |
| mapHeader | 0x0202E828 | same | literal count 99 | check 15 |
| objEvents (IWRAM) | 0x030048A0 | same | literal count 276 | check 16 |
| playerAvatar (0x24 bytes) | 0x0202E858 | same | literal count 202 | checks 16, 17 |
| sprites | 0x02020004 | same | literal count 1298 | check 17 |
| plttUnfaded | 0x0202EAC8 | same | literal count 72 | (palette path) |
| paletteFade | 0x0202F388 | same | literal count 371; it is the literal at CB2_Overworld +0x28 | checks 10, 18 |
| mainFlagsOff (inBattle) | 0x43D | same | the gMain literal pools hold 0x43D 48 times, 0x439 never | check 20 |
| weatherPtr (ROM word -> 0x0202F7E8) | 0x08396FDC | 0x08396E24 | the ROM word holding gWeather, 108 references | checks 9, 19 |
| weatherOff | {0x6D0, 0x6C6, 0x730, 0x6FB, 0x724} | same | Emerald's; ROM immediates: 0x6D0 as `mov #0xDA; lsl #3` (11), 0x730 as `mov #0xE6; lsl #3` (4), 0x6C6 (20), 0x6FB (4), 0x724 (4) in pools | check 19 |
| gfxInfoPtrs (218) | 0x0836DC70 | 0x0836DC00 | pointer run whose records carry a ROM pointer at +0x1C; slot 218 is the field-effect table (34 literal refs), so 218 records | check 7 |
| fldeffTemplates (36) | 0x0836DFD8 | 0x0836DF68 | the table right after; thumb callbacks; palette tags in Emerald's index order | check 8 |
| cb2Overworld | 0x080543C5 | 0x080543C9 | `push {lr}`, the &gPaletteFade literal at +0x28, 8 thumb references; live: check 20 passes on the overworld | checks 10, 20 |
| cb2OverworldBasic | 0x080543B9 | 0x080543BD | 0xB500 `push {lr}` thunk, 1 reference | checks 10, 20 |

Struct offsets are Emerald's (pokeruby numbers only): MapHeader, ObjectEvent, Sprite, PlayerAvatar, PaletteFadeControl,
ObjectEventGraphicsInfo, the backup layout, Weather. The ROM checks 5 / 6 read the General / Building primaries from
maps (0, 0) and (1, 0) on RS (FRLG: (3, 0) / (4, 0)).

### Results

- Host: `make -C tools/romgen test` 29 suites, `vtest` 11 suites (new `test_voxel_rs`), 0 failures, 0 skipped. Changed
  counts only: test_romgen_gameprof 5799 -> 5925 (RS row pins, the ROM checks on both real carts with one corrupted
  copy per check, synthetic RAM checks through the RS layout), test_romgen_rs_world 8341 -> 8349 (renderer detection,
  rev 2 only), test_voxel_rs 96 (new: Littleroot 0.9 and Rustboro 0.3 built from each ROM through snapshot -> adapter ->
  world; instances 0.9 0.16 0.10 and 0.3 0.30 0.19 0.31 0.29 0.20 0.0 0.14). Every other count identical.
- Pins unchanged (romgen CLI): Emerald buildings 6d321c3a, regions 007a370f, signposts 38515605, relief 21a837f0,
  ledges eb25a383; FR = LG buildings 5ba2cc16, regions 3716874d, signposts ba2fde45, relief 32c24146; Ruby = Sapphire
  buildings a03a3b74, regions 1a09cd5f, signposts 9b4d379c, relief 215a12d9.
- Device: `make -j8` and `make -j8 ROMGEN_DEV_HOOK=1` link.
- **Hardware: not run.**

### Evidence (Azahar, 2026-10-07)

Private emutest instance q (New 3DS, `--keep-n3ds`, the S0 romgen output staged in `voxel/AXVE` and `voxel/AXPE`,
voxel on), warp-save copies of ruby.sav / sapphire.sav (continue-game warp; the originals are untouched):

| run | voxel.log | look |
|---|---|---|
| Ruby, Littleroot (0.9) at (10, 10) | `vx: rom anchors ok (AXVE rev 2)`, `vx: anchors ok (AXVE rev 2) map 0.9` | 3D: both houses stand as models with roofs, the lab is the fallback box (left out in S0), flowers and ground flat |
| Ruby, Rustboro (0.3) at (16, 30) | `... map 0.3` | 3D: the town's buildings stand as models / boxes, the lamp posts and NPCs as sprites |
| Sapphire, Littleroot (0.9) | `vx: rom anchors ok (AXPE rev 2)`, `... (AXPE rev 2) map 0.9` | same as Ruby |
| Ruby with the header revision forged to 1 | `vx: not detected: game AXVE rev 1 (supported: ...)` | plain 2D, no crash |

No "chunk scratch full" line in any run. Memory: the newlib malloc high-water mark is 36,773,888 B with the renderer on
(the same in all three towns) against 35,602,432 B with it refused (the rev-1 run), +1.1 MiB; linear free at voxel init
14,174,208 B, VRAM free after the first atlas 205,824 B. The Old 3DS was not tried (S0: on-device romgen ran out of
memory there).

Looks honestly: trees, shrubs and props are flat until S3 (no RS tables yet); RS-only buildings without a recipe (the
lab, Rustboro's stone set, Devon, the gym) are boxes or missing until S4. Shots are kept locally, not committed
(ROM-derived art): `evidence/s2-ruby-littleroot.top.png`, `s2-ruby-rustboro.top.png`, `s2-sapphire-littleroot.top.png`,
`s2-ruby-rev1-refused.top.png`; the voxel.log of each run is committed beside them (`*.voxel.log.txt`).
