# PHASE 34: FRLG. The Zallax-style voxel look for FireRed / LeafGreen

Status: plan (2026-10-06). No C written. The lead commits. Companion documents: `SPEC.md` (implementation),
`SURVEY.md` (measured facts, M1 = measured on Guy's ROMs).

## Goal
FireRed and LeafGreen get the same 3D voxel overworld that Emerald has: the vendored Zallax renderer (`source/voxel/`)
plus ROM-generated data (`regions.bin`, `signposts.bin`, `buildings.bin`, `relief.bin`) from Phase 33's romgen. Kanto
buildings are **hand-authored recipes at Zallax/Emerald quality**: real roofs, gables, eaves and facades built from the
ROM's own art and proven pixel-exact by the ortho/density gates. They are not generic boxes.

Everything is generated from the user's own cartridge dump. No Zallax builder, no pak, and no game data in the repo.

## Scope
In:
- **FR rev 1 (BPRE) and LG rev 1 (BPGE), together.** One game profile with two address rows. Measured on Guy's ROMs
  (SHA-1s in SURVEY header): both have the same 425 maps and 384 layout slots, **byte-identical blockdata in all 384
  layouts, and identical palettes in all 63 tilesets** (M1, 2026-10-06). So one recipe table and one set of layout
  pins serve both games.
- The **outdoor overworld**: the 76 outdoor maps (map types town/route), with terrain, water, regions, trees,
  signposts and ledges, and every outdoor building done by a recipe.
- Mainland Kanto first, in story order. Then the Sevii Islands, island by island.
- A host-only **recipe-authoring toolchain** (census, art-to-PNG, recipe preview, gate report), so that agents can
  author ~77 building models well.
- Kanto ledges in `relief.bin`. Drawn cliffs (Mt. Moon slopes, Victory Road) come later and are optional.

Out (this phase):
- **Interiors.** On FRLG they stay flat GBA graphics: indoors the voxel path hands back to the normal 2D frame.
  Emerald interiors are unchanged.
- **rev 0** cartridges. Not available here, so they cannot be verified. The profile leaves room for the rows, and the
  runtime search for `gMapLayouts` already covers them. They come in a later slice when a dump exists.
- Caves (map type 4, 87 maps): they are not outdoor and are not generated. They render as they do today.
- Ruby/Sapphire, and any ROM hack. A layout whose fingerprint pin fails skips its recipe, and the building falls back
  to the extruded box.

## Milestones
| # | Name | What the player sees | Exit |
|---|---|---|---|
| **M0** | Kanto boots in 3D | Pallet Town in voxel mode with **no data files**: flat ground, extruded boxes for the houses, trees as the renderer's fallback. Indoors shows the flat GBA frame. FR and LG. | The renderer profile (R0-R2) proven in Azahar on both ROMs. No garbage reads: the anchor self-check passes, and a wrong ROM disables voxel cleanly. |
| **M1** | **Pallet Town in 3D** | Pallet with terrain, water (the south pond), regions, Kanto trees, the five signposts/mailboxes as cut-out signs, **and Pallet's buildings from recipes**: the two identical houses (one model, 2 placements) and Oak's Lab. Route 1 next door gets terrain, trees and signs. | R0-R2 + G1-G2 + T1 + B0 + K1 green on host (gates 0/0/0, consumer round trip). Azahar visual check on FR and LG. **Then a hardware run on Guy's New 3DS.** |
| M2 | Landmarks everywhere | Pokemon Center, Poke Mart and Gym recipes, placed in every town that draws them (Center at least 16 placements, Mart 11, Gym 7 by exact match). Kanto ledges on Routes 1, 2 and 22. | K2 + L1, Azahar tour of four towns, hardware run. |
| M3 | Mainland Kanto | Every mainland outdoor building by recipe (about 59 models, 108 placements), town by town: K3-K11. | Per-town gates, Azahar per town, one hardware run per two towns. |
| M4 | Sevii Islands | Islands 1-7 plus Navel Rock / Birth Island (about 18 models, 44 placements). | KS1-KS3, Azahar, hardware. |
| M5 | Release gate | Adversarial review, `release-legal-audit`, voxel setting enabled for FRLG in the UI. | Audit passes. Hardware sign-off for FR **and** LG. |

Optional, after M3: drawn Kanto relief (cliffs), L2.

## Principles
1. **Emerald must not move.** Every slice that touches shared code first records the SHA-1 of the four Emerald CLI
   outputs and every host-suite check count. It ends with them **byte-identical and count-identical**. This is a hard
   gate with no "close enough": a slice that changes one Emerald byte is not done.
2. **One game profile, two layers.** romgen and the renderer read the same `GameProfile` struct (ROM layout constants,
   behaviour sets, table pointers). The renderer additionally reads the RAM anchors. Emerald is one row of it, FR and
   LG are two more. There is no `#if FIRERED`.
3. **Self-validating anchors.** Each address is checked against the live ROM/RAM before use. A failed check disables
   voxel with a named reason; it never reads garbage. This is the `gamestate` probe idea.
4. **Pure-C cores** (CLAUDE.md rule 4). All new romgen and profile code dual-compiles on the PC. The authoring tools
   are host-only.
5. **Recipes are proven, not eyeballed.** A recipe is done when ortho = 0 wrong / 0 missing / 0 extra, the density
   list is empty, it is placed where the census says, it round-trips through the vendored consumer, and it looks right
   in Azahar. Preview PNGs are an authoring aid, not the gate.
6. **Determinism without upstream mimicry.** Emerald's generators replicate CPython orders because they must match
   Zallax byte for byte. Kanto has no upstream output, so FRLG-only code paths use a plain deterministic order (layout
   id, then (y, x)). They must not route through the Emerald name-order tables.
7. **Coexist with the in-flight Emerald slices** (S3.4-S3.8). The file-ownership plan in `SPEC.md` section 0 says
   which Phase 34 slices may land when. The renderer track never touches a romgen file.
8. Sequential agents, one at a time, with the model chosen per role (Guy's rule 7). Every agent banks its findings to
   disk as it goes.

## Legal and provenance rules (binding: the ip-legal verdict)
- Committed code and docs carry **numbers only**: ids, addresses, value sets, permutations, pixel rectangles and
  colours we measured. Short identifier names may appear in comments.
- **No decomp text anywhere.** No struct bodies, comments, script text or map/layout JSON copied from pret. Place names
  (Pallet Town) are fine; they are facts of the game world.
- Every citation **pins the commit**: pret/pokefirered `037335f4c725d7c9aecdac87066f2002b4bd7e14`, pret/pokeemerald
  `731ad5b`, Gummygamer gen1recomp-voxel-frlg `7a55b21` (MIT), FireRedHD2D `1aea16b` (MIT, idea-level only),
  Zallax pokeemerald-3Ds-dualscreen `c330c0a` (MIT).
- **One `docs/PROVENANCE.md` row per table**: the symbol, the decomp paths consulted (path only), the derivation, and
  the ROM assertion that re-checks it on the user's dump. Tables measured from the ROM by our own tools (tree ids, rock
  ids, recipe rectangles) still get a row, with "ROM-measured, no decomp" as the source.
- **Assert against the user's ROM** wherever the ROM can witness a value. RAM anchors are asserted at runtime by the
  self-check, and in Azahar at each milestone.
- Zallax-derived code keeps the MIT header plus "Modified for 3DGBA (GPLv3)". The Kanto recipes are **our own work**
  (new builder functions over the ported part library): they carry the GPLv3 header and no Zallax header unless a
  function is a port.
- Gummygamer's ideas (foliage >= 50 % green rule, run-height boxes), if re-implemented, are credited in a comment and
  in PROVENANCE. If any text is copied, the MIT notice goes in `source/voxel/NOTICE.md`.
- **Never execute** Zallax's Python, Gummygamer's Lua or FireRedHD2D's Rust, by any route. We read them as
  specification only.
- **Generated data and authoring renders are ROM-derived**: never committed, never in romfs, the `.cia` or release
  assets. They go under git-ignored paths (`tools/romgen/out/`, the scratchpad). `release-legal-audit` checks this
  at M5.

## Done = real New-3DS hardware
CLAUDE.md hard convention 6: anything timing-sensitive is **not done until it runs on Guy's New 3DS**. For this phase
that covers each milestone's frame budget with FRLG data loaded, the on-device romgen run time and memory, and the
look itself (Azahar's GPU path is not the PICA200). Azahar is the iteration tool: a milestone that has passed Azahar
only is reported as "emulator-verified, hardware-pending". The release gate needs a hardware pass on **both** FR and
LG cartridges' dumps.

## Guy's decisions (2026-10-06) and lead defaults
Binding (Guy):
- Buildings: **full hand recipes for every Kanto building**, at Zallax quality, with an authoring workflow built for
  agents (art-to-PNG CLI, recipe preview, `rg_bcheck` as the gate, per-town batches in story order, shared landmarks
  first).
- Interiors: **out** for the first version.

Lead defaults (Guy may veto):
- rev 1 only (rev 0 later).
- FR and LG ship together: one profile, two address rows. The M1 measurement above makes this nearly free.
- Sevii after the mainland, town by town.
- Missing RAM/ROM anchors may come from pret (numbers only), asserted at runtime and in tests where possible.
- FRLG 3D sits behind the **same voxel setting** as Emerald. The UI string stops saying "Emerald only".
