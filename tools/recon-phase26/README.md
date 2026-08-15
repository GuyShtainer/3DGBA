# Phase-26 lane W recon tools (read-only)

Five tiny host programs used to derive `docs/phase21-touch-census/SPEC-hm-waterfall.md` from the
user's own cartridges. Every one of them reads the ROM through the **shipped** readers
(`fieldtrav_rom_map` + `fieldtrav_rom_bus` + `fieldpath_behaviour_at`), never a re-implementation —
which is the whole reason their numbers are worth anything.

Build (from the project root):

    clang -std=c11 -O2 -I source tools/recon-phase26/<tool>.c source/fieldtrav.c source/fieldpath.c -o /tmp/<tool>

| tool | question it answers |
|---|---|
| `wfscan.c`   | every MB_WATERFALL column in a ROM: base tile, height, the tile below, the landing, collision |
| `mapdump2.c` | behaviour / collision / elevation for a rectangle of one map |
| `reach2.c`   | SURF-layer flood from a tile (this engine's surfable set, waterfalls excluded) |
| `foot.c`     | FOOT flood at a given elevation, plus distances to named tiles |
| `bscan.c`    | every `ObjectEventTemplate` in a ROM with a given `graphicsId` (e.g. 87 = pushable boulder) |

`gMapGroups` per title (gamestate.c's own profile values): BPEE `08486578`, BPRE rev1 `08352718`,
BPGE rev1 `083526F8`, AXVE rev2 `083085A0`.

Map-group counts (from each decomp's `data/maps/map_groups.json`) — the tools that sweep every map
take them as arguments, because `fieldtrav_rom_map`'s guards are deliberately loose and walking past
a group's end resolves garbage that LOOKS like a valid header:

* Emerald (34): `57 5 5 6 7 8 9 7 7 14 8 17 10 23 13 15 15 2 2 2 3 1 1 1 108 61 89 2 1 13 1 1 3 1`
* FireRed (43): `5 123 60 66 4 6 8 10 6 8 20 10 8 2 10 4 2 2 2 1 1 2 2 3 2 3 2 1 1 1 1 7 5 5 8 8 5 5 1 1 1 2 1`
* Ruby (34):    `54 5 5 6 7 7 8 7 7 13 8 17 10 24 13 13 14 2 2 2 3 1 1 1 86 44 12 2 1 13 1 1 3 1`
