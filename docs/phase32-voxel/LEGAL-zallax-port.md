# LEGAL: porting ZallaxDev/pokeemerald-3Ds-dualscreen voxel code into 3DGBA (2026-10-05)

Not a lawyer. Risk framing. Epistemic labels: [settled] / [persuasive] / [folklore] / [unresolved].

## Verdicts
| # | Use | Verdict |
|---|-----|---------|
| 1 | Vendor voxel/*.c,*.h + voxel.v.pica (+ stereo/bottom-UI ideas), modified, MIT notice, in GPLv3 repo, own replacement header | OK-with-conditions |
| 1b | Stereo bits of 3ds_video.c / bottom_ui layout | stereo: OK-with-conditions (re-express, don't paste); bottom-UI: OK as ideas only (file is pret-entangled) |
| 2a | Vendor generator scripts into tools/, run on user's ROM | OK-with-conditions |
| 2b | User runs Zallax builder, we read emerald3ds.pak | OK-with-conditions (legal GREEN-ish; practical coupling is the issue) |
| 2c | Redistribute recipe / generated .bin | NO |
| 3 | Attribution | see conditions |
| 4 | "Voxel 3D" UI name | OK (GREEN) |

## Provenance chain (read directly)
- Zallax LICENSE-PORT.md: MIT, scoped to port-authored code only; expressly does NOT relicense Emerald, pret decomp, ROM-generated data (emerald3ds.pak), third-party parts.
- NOTICE.md + voxel/NOTICE.md: voxel code is "derived from" gradenGnostic/pokeemerald-multiplatform src/platform/voxel/ @db1cab3d, whose LICENSE is MIT *scoped to its "Port Modifications" only* and disclaims upstream pret code. So chain = pret(unlicensed) -> multiplatform (MIT on additions only) -> Zallax (MIT on additions only). Each MIT grant is only as good as the grantor's authorship of that file. Voxel renderer files look like original additions (new directory, GPU/mesh code), which is the best case. [persuasive, cannot be fully verified]
- Zallax's AI_DISCLOSURE.md: AI-assisted code. Authorship/copyrightability of AI-assisted portions is unsettled (US Copyright Office: human authorship required) - at worst this means *less* copyright to infringe, not more; no action, just don't claim to "verify" it.
- Zallax's own release tooling (tools/release_audit.py, gen_recipe.py) is evidence of good-faith asset hygiene.

## Spot-check: verbatim pret-derived bodies in voxel/*.c (3ds_port/src/voxel, ~15k lines)
Read: voxel_world.c, voxel_entities.c, voxel_battle.c (includes/idents), grep of all static tables.
Found NO copied pret function bodies or data tables. Everything is port logic (mesh, atlas, lighting, camera, arena, relief, GPU) written AGAINST pret symbols. Pret touchpoints (all must be cut):
- Headers: global.h, main.h, overworld.h, fieldmap.h, decompress.h, metatile_behavior.h, event_object_movement.h, field_player_avatar.h, sprite.h, battle.h, battle_anim.h, field_weather.h, palette.h, constants/{map_types,metatile_behaviors,rgb,field_effects,field_weather,weather}.h, gba/io_reg.h (voxel_world.c, voxel_entities.c, voxel_battle.c, voxel_atlas.c).
- Symbols/structs used: gMapHeader, gBackupMapLayout, gSaveBlock1Ptr->location, gObjectEvents[]/struct ObjectEvent (currentCoords/previousCoords/spriteId/hasReflection), gPlayerAvatar, gSprites[]/struct Sprite (oam.tileNum, data[]), gMain.inBattle, gTileset_Fortree, MB_* values, MAP_TYPE_*, MAPGRID_* masks/shifts, MAP_OFFSET, NUM_METATILES_IN_PRIMARY, UNPACK_BEHAVIOR macro, LZDecompressWram, GetBattlerSide/B_SIDE_PLAYER, VRAM_/REG_DISPCNT.
- Mild items: voxel_entities.c:33 sStepFrames {16,8,6,4,2} "matching sStepTimes[] in event_object_movement.c" (five integers, a game fact; trivial, but re-derive/measure it yourself and cite path only); voxel_atlas.c sSolidColors (check it is port-chosen colours, not game palette - it is a 16-bit colour table; confirm before vendoring); voxel_world.c:535 special case on gTileset_Fortree puddle (a game fact - express as "layout id/tileset id N" from our own docs).
- No verbatim pret code found. Residual risk = struct/identifier names (interface facts; Sega/Oracle-line [persuasive]). Our own header must be written from SPEC-data (our observation), declaring offsets/sizes, using OUR field names where practical; do NOT paste pret's global.h/sprite.h struct definitions.
- ctr_voxel.c (5946 lines) and 3ds_video.c are deeply coupled to the in-process decomp port (they read live pret structs/VRAM). Legally same posture; practically most of the data-access layer must be rewritten against mGBA memory reads.

## 2. Generators / recipe / pak
- Scripts (gen_voxel_*.py, voxel_*.py): no embedded ROM bytes. Largest hex counts are single digits (metatile IDs/colours). They reference pret NAMES only as identifiers (LAYOUT_* up to 19 in building_specs, MB_* behaviour names, MAP_TYPE_*) plus coordinates/rectangles - identifier lists and positions = GREEN per policy (names, 37 CFR 202.1a/Feist). voxel_building_specs.py is Zallax's original authored art-spec prose.
- BUT: generators read a *decomp-layout tree* (data/layouts/layouts.json, per-map map.json, tiles.4bpp, palettes, metatiles.bin, include/constants/metatile_behaviors.h). builder vtree.py + tools/vtree_manifest.py reconstruct that tree from the ROM using recipe `inputs` (ROM offsets/LZ77 streams, CRC-checked) and a manifest including [name,value] metatile-behaviour list, map group/num names, headers. For us: do NOT require the user to have a pret checkout, and never vendor/point at pret's tree. Reconstruct from ROM (own offsets from our sym docs) exactly as vtree.py does. Generated tree lives only in a temp/user dir.
- Recipe (emerald3ds.recipe): offsets/sizes/CRCs/engine pointers PLUS a literal pool. gen_recipe.py allows up to 4096 bytes of "original game's own objects" literal and up to 65536 unexplained literal bytes per recipe. So a recipe MAY legitimately embed a few KB of Game Freak bytes. Voxel inputs, however, must be fully ROM-found (gen_recipe fails on any voxel literal). Conclusion: never vendor or redistribute the recipe; never put offsets-plus-literals in git. Offsets/CRC-only tables we derive ourselves are facts (GREEN).
- Option (b) (user runs Zallax builder; our app reads voxel entries from emerald3ds.pak): legally clean for us - we ship none of it; pak is user-local derived data (Zallax: (c) Nintendo, never shipped); reading a documented file format = interface. Conditions: do not mirror/host the builder, recipe or pak; don't link them from release assets as a bundle (linking to Zallax's repo in README is fine); don't give instructions that distribute the pak. Practical cons: supports one ROM SHA1 (f3ae0881..., Emerald USA/Europe), voxel bins are CRC-pinned to Zallax's exe version, builds a full game data pack we don't need, depends on a third party staying up. Option (a) is cleaner for 3DGBA (own generator = our tool, user's ROM only) and equals the policy's "local-build art pipeline" YELLOW-GREEN class.
- Output: regions/signposts/buildings/relief.bin are ROM-derived (map layout + tile-derived relief/art-rect data). Gitignore + never in release assets/.cia/romfs. Check that the .cia's RomFS does not embed them (release-legal-audit gate; repo-clean-vs-release-dirty is the house failure mode). Generate at runtime/first-run on-device from the user's ROM if possible, or via PC tool writing to sdmc:/.
- Zallax bundled tree art (3ds_port/assets/voxel/trees/*.png): hand-drawn original, MIT; allowed with attribution. Their vtree "recipe" tree art in builder: same.

## 3. Attribution (conditions)
1. NOTICE (3DGBA root): entry "Voxel renderer: derived from ZallaxDev/pokeemerald-3Ds-dualscreen @ <commit 42085a1 or the one vendored>, MIT, (c) 2026 ZallaxDev and Pokémon Emerald 3Ds Dual Screen contributors" + full MIT text (LICENSE-PORT.md section) + SECOND entry: "which in turn derives from gradenGnostic/pokeemerald-multiplatform @ db1cab3d..., MIT (c) 2026 pokeemerald-multiplatform contributors" with that MIT text (copy voxel/NOTICE.md). Both notices carry "included in all copies or substantial portions" - must travel with every source and binary distribution (put in-app credits/About too: .cia is a binary distribution; MIT does not strictly require binary notices in all readings but GPL-world norm and safest = ship NOTICE in the repo + release zip + app credits).
2. File headers: keep the existing top-of-file comments naming the MIT origin; add "Modified for 3DGBA (GPLv3), <year>". Do not strip upstream copyright lines. Keep a copy of voxel/NOTICE.md as source/voxel/NOTICE.md.
3. License posture: combined work is distributed under GPLv3 (MIT -> GPLv3 one-way compatible). Keep the MIT texts for those files; say in README "contains MIT-licensed code, see NOTICE". Also mGBA MPL-2.0 handling unchanged.
4. README: honest provenance paragraph; state the 3D data is generated locally from the user's own ROM and never distributed; non-affiliation disclaimer (Nintendo/Game Freak/Creatures/TPC); no claim of "clean-room" - say "documented reverse engineering"; do NOT say the vendored code is pret-free beyond "contains no pret source in our tree, we removed the includes" (state what is true). Do not claim endorsement by Zallax; courtesy-notify Zallax (optional but wise; their README invites contributions).
5. The pret caveat travels: state that MIT grants cover only the authors' own contributions (mirrors upstream wording).

## 4. Name "Voxel 3D"
Generic descriptive term; no mark conflict known. GREEN. Hygiene: nominative only for Pokémon; don't reuse Zallax's project name ("Pokémon Emerald 3Ds Dual Screen") or logo in our UI; no franchise trade dress in menu art; no TM filings. Not a trademark of Zallax either that I found.

## Tripwires checked
No TPM/key/decrypt (ROM SHA1 check only). No monetization. No pre-built payloads (bins generated locally). No forks of targeted projects (vendoring a copy != GitHub fork; fine).

## Open / unresolved
- Chain-of-title of multiplatform/Zallax MIT grants vs pret expression: [unresolved] cannot be verified; mitigated by no pret code found + own header.
- AI-authored portions copyrightability [unresolved].
- Whether a ROM-reconstructed tree + generated bins are "derivative works" of Emerald in Japan/Israel: irrelevant while local-only [unresolved but not shipped].
