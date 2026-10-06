# Voxel look backlog (Guy's notes from Emerald screenshots, 2026-10-06)

Guy, on the Emerald 3D look across several Hoenn places: "gorgeous but not perfect, but it will do for now". The open
points, with the cause found in the code:

| # | Guy's note | Cause (checked 2026-10-06) | Fix direction | Size |
|---|---|---|---|---|
| L1 | "the trees near the entrance to the cave in Dewford are flat on top" | `voxel_tree.c` `VoxelTree_Part` knows only the General-tileset tree metatiles (0x1D4-0x1E7, 0x1EC/0x1ED, the small trees 0x016/0x017/0x0C6/0x0C7/0x1F4/0x1F5). The Route 106 / Dewford shrubs on sand are metatiles 0x124, 0x239, 0x242, 0x243 (`romgen author emerald.gba art 22 42 17 12 3`). They are not in the table, so they render as flat ground art. Seen in `docs/phase33-romgen/evidence/s37-full-r106.png` (foreground). | Scan all outdoor layouts for foliage metatiles missing from the table (the T1 foliage rule: at least 50 % foliage pixels), then add them as VOXEL_TREE_SMALL or as a new "shrub" part. Emerald table only; the FRLG table comes from T1. | small |
| L2 | "the grass is still flat" | Upstream renders tall grass as ground texture. Only the rustle field effect is a sprite. There is no grass geometry. | Tall-grass cells (behaviour set) get low crossed or billboard blade cards from the cell's own art, swaying optionally. Budget: many cells per screen, so instance it cheaply and test the frame time on hardware. | medium |
| L3 | "the lighting is somewhat off. The shadow has a single direction which doesn't add up: the NPCs are lit from the front" | `voxel_lighting.h`: a fixed sun in the northwest (`VOXEL_SUN_DX 0.85`, `DZ 0.55`), so shadows fall southeast, towards the camera side. The GBA sprite art is shaded as if lit from the front and above. The baked terrain shadows and the sprite shading disagree. | Move the sun in front of the scene (south, high), so cast shadows fall away from the camera. Check that `voxel_lighting.c`'s ray march does not assume a northwest sun (its reach box is built from DX/DZ signs). | small-medium |
| L4 | "maybe for future: live lighting that changes with the day (use the 3DS clock or the RTC for RSE)" | The lighting is baked once into the chunk colours. | Sun angle and colour from the time of day. The source is the 3DS clock (osGetTime), which is also what mGBA's RTC reads for RSE, so all games agree. FRLG have no RTC, so they use the 3DS clock. Re-bake chunks incrementally when the sun has moved enough (minutes, not frames), plus a global tint (dawn, day, dusk, night). Future, after L3. | large |

Order: L1 → L3 → L2 → L4, after the FRLG milestones M0/M1 that are running now. Each one is emulator-checked, then
hardware-checked (frame time).
