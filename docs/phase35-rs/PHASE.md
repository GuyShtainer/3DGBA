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
| S1 | RS renderer anchors + self-check: gSaveBlock1 is a DIRECT struct (no pointer, needs a profile flag), gMain/gMapHeader/gPlayerAvatar/gSprites/palette buffers (the `RS_PROFILE_BODY_RAM` values in gamestate.c are a start, from .sym), weather, gfxInfo and field-effect tables harvested from rev 2 ROM literal pools, cb2Overworld measured live | anchor check passes in Azahar on both carts; wrong ROM disables cleanly |
| S2 | renderer on (`rendererOn`), Azahar captures of Littleroot and Rustboro in 3D | test_voxel_* counts unchanged + an RS row; shots |
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
