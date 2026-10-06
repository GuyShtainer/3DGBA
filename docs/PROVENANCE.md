# Provenance of decomp-derived number tables

Phase 33 ROMGEN, `source/romgen/rg_rtables.c`. Rules (SPEC-S3 section 3.0): the code carries numbers only (a short
identifier may stand beside a number in a comment); every table names the decomp files consulted (path only), pinned at
**pokeemerald@731ad5b**, the derivation, and the ROM assertion that re-checks it on the user's dump
(`rg_rtables_check`, run by `test_romgen_relief_ledge.c` and `test_romgen_rtables.c` on the real ROM). The decomp is reference-only, never vendored.

| table | symbol (file) | decomp paths consulted @731ad5b | derivation | ROM assertion |
|---|---|---|---|---|
| upstream-outdoor layouts, name order (A.3) | `RG_OUTDOOR_BY_NAME` (rg_rtables.c) | `data/layouts/layouts.json`, `data/maps/<folder>/map.json` | layouts used by a map of type ROUTE/TOWN/CITY/UNDERWATER/OCEAN_ROUTE plus the A.4 alternates; ordered by layout name, `LC_ALL=C` code-point sort; stored as layout ids | T2 (set equality with the ROM's map headers; the order itself is decomp-only) |
| outdoor alternates (A.4) | `RG_OUTDOOR_ALTS` (rg_rtables.c) | `data/layouts/layouts.json` | the five outdoor layouts whose name ends `_alt`, paired with the layout the suffix-less name resolves to | T4 (same width, height and tileset pair as the base) |
| ENABLED layouts (A.1) | `RG_ENABLED` (rg_rtables.c) | `data/layouts/layouts.json` | the ids of the two layouts upstream names in its ENABLED list, in list order (id = 1-based list position) | T1 (width and height of 20 and 4) |
| WORLD_ROOT, ALIAS_REFERENCE (A.1) | `RG_WORLD_ROOT`, `RG_ALIAS_REFERENCE` (rg_rtables.h) | `data/layouts/layouts.json` | ids of the layouts upstream names as the world's root and as the colour reference (id = 1-based list position) | T1 (width and height of 10 and 32) |
| ALIAS_LAYOUTS (A.1) | `RG_ALIAS_LAYOUTS` (rg_rtables.c) | `data/layouts/layouts.json` | ids of the three layouts upstream lists as drawn with the Lavaridge rock, list order | T1 (dims of 13, 136, 292), T6 (their secondary tileset) |
| Lavaridge secondary users (A.2) | encoded as "secondary == secondary of layout 13" in `check_t6` and `rg_alias_of` (rg_rtables.c, rg_ralias.c) | `data/layouts/layouts.json` | layouts whose secondary tileset is the Lavaridge one: 13, 28, 136, 292, 293, 336-341, 379, 380 | T6 (exact set equality) |
| General primary for the outdoor set (A.2) | encoded as "primary == primary of layout 10" in `check_t5` | `data/layouts/layouts.json` | every A.3 layout uses the General primary | T5 |
| DRAWN_EXCLUDED group seed (A.1) | `RG_EXCLUDED_GROUP_SEED` (rg_rtables.h) | `data/layouts/layouts.json` | id of the layout (route122) that names the excluded drawn group, its first seed in name order | T1 (dims of 38); group existence asserted by `test_romgen_relief_drawn.c` (S3.3): the group seeded by 38 exists and is dropped |
| WRAP_GROUPS seeds (A.1) | `RG_WRAP_GROUP_SEEDS` (rg_rtables.c) | `data/layouts/layouts.json` | ids of the three layouts (route104/105/106) that name the wrap groups | T1 (dims of 20, 21, 22); group existence asserted by `test_romgen_relief_drawn.c` (S3.3: three singleton groups) |
| outdoor maps in folder order (A.5) | `RG_OUTDOOR_MAPS_BY_FOLDER` (rg_rtables.c) | `data/maps/map_groups.json`, `data/maps/<folder>/map.json` | maps of type ROUTE/TOWN/CITY/UNDERWATER/OCEAN_ROUTE, ordered by folder name (`LC_ALL=C` code-point sort), as {group, num, layout id} | T2 (layout set), T3 (each map's layout id in the ROM header) |
| direction sort key (A.7) | `rg_dir_sort_key` (rg_rtables.h) | none (upstream's own direction strings sort down < left < right < up; ROM dir 1 (down) is 0, 3 (left) is 1, 4 (right) is 2, 2 (up) is 3) | string order of the four direction words | T7 (census of ROM dir codes: down 27, up 27, left 40, right 40, dive 7, emerge 7), T8 (Route 104's connection order) |
| behaviour sets (A.6) | `rg_is_flat_behaviour`, `rg_is_sand`, `RG_MB_WATERFALL`, `RG_MB_BERRY_TREE_SOIL` (rg_behavior.h) | `include/constants/metatile_behaviors.h` | enum counted from 0 (count 0xF0): FLAT = names containing BRIDGE, `_LOG_`, ending `_DOOR`, plus the no-running mat, minus the bridge reflection; SANDS = names containing SAND | T9 (FLAT values below 0xF0, none water; Route 101 has jump cells) |

## Phase 34 (FireRed / LeafGreen), pokefirered@037335f

Numbers only (ids, addresses, value sets); no decomp text. Reference clone is study-only.

| table | symbol (file) | source consulted | derivation | ROM assertion |
|---|---|---|---|---|
| FRLG behaviour sets (SPEC-P34 section 2) | `build_frlg_sets` (rg_gameprof.c) | pokefirered@037335f `include/constants/metatile_behaviors.h` | the values of the water, ledge-jump, door, sand, tall-grass, signpost, surfable, reflective, ice, shallow-flowing and furniture behaviours, as numbers; 0x2A/0x2B excluded from water, 0x8B/0x8D from doors | `test_romgen_frlg_world.c` (every b in 0..511 against an independent list; Pallet doors 0x69; pond 0x15; behaviour census; largest used value 0xE0) |
| FRLG ROM layout constants (primary 640/640/7, attrs u32 at tileset +0x14, MapLayout 26 B, 43 groups, group sizes) | `sFireRed`, `sLeafGreen` (rg_gameprof.c) | ROM-measured on the user rev 1 dumps (SURVEY M1); struct shapes cross-checked against pokefirered@037335f | counts, offsets and addresses measured; no decomp text | `test_romgen_frlg_world.c` census (43 groups + sizes, 425 maps, 384 slots/18 NULL/309 used, 63 tilesets, 76 outdoor, 1294 warps, gMapLayouts search 0x0834EBFC / 0x0834EBDC, General/Building primaries) |
