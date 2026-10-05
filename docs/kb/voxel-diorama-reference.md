# Voxel-diorama 3D world — external reference (2026-08-31)

**Trigger:** Guy shared a Facebook post (facebook.com/share/17MxrpaoyD — Gilliam Germi,
"🩷 Pokemon Emerald Gen2Recomped with Dramatic_Shape Voxels — yes, we will have Emerald…")
showing Pokémon Emerald's overworld as a full 3D diorama: extruded buildings/trees with
real volume, 2D billboard sprites, night lighting, cast shadows, tilt-shift depth-of-field.
**"This is exactly what we want for our 3D world."** Inspiration screenshot saved at
`projects/_reference/inspiration-emerald-voxel-fb-post.jpg`.

Extends `external-projects-teardown.md` §1 (gen1recomp, 2026-08-03) with the ecosystem
that has since exploded, and — the important part — the **Gen-3/Emerald-side sources**.

---

## 1. The ecosystem map (who does what)

| Project | What | Where |
|---|---|---|
| **gen1recomp** (bryanthaboi) | The base runtime the viral videos run on: hand-written Lua/LÖVE2D re-creation of Gen 1 (Gen 2 in progress); game data decoded from the user's own SHA-1-verified ROM at import. 3.4k★, press: Polygon/Kotaku/Digital Foundry. | github.com/bryanthaboi/gen1recomp |
| **DramaticShapeVoxelMod** | THE voxel-diorama mod (for gen1recomp). Original author deleted it ("now-defunct"); survives via community archives. Written in Lua. | archives: scottcandy34/DramaticShapeVoxelMod-latest (last update, 82★); ChristianSilvermoon/DramaticShapeVoxelMod-Archive (all source ≤ v1.6.0); linkfy/DramaticShapeVoxelModBackup; maintained fork artyrambles/DRAMALESS_SHAPE (119★) |
| **Gen2Recomped** (UNDERdecoded) | Gen 2 fork of gen1recomp + a DramaticShapes port to Gen 2 (Gen2Recomped-DramaticShapes). The FB post's title names this. | github.com/UNDERdecoded/Gen2Recomped |
| **EmeraldRecomp** (mstan) | *Static recompilation* of Emerald's ARM7TDMI code to native C (+ interpreter→JIT fallback that caches to disk) on the author's `gbarecomp` framework. Siblings: FireRedLeafGreenRecomp, RubySapphireRecomp. Early (v0.0.1) but boots to gameplay. Uses only *symbol metadata* from pret, never its source. | github.com/mstan/EmeraldRecomp, github.com/mstan/gbarecomp |
| **pokeemerald-multiplatform** (gradenGnostic) | pret/pokeemerald PC/SDL2 port fork (Windows/Linux/Android) **with an experimental 2.5D voxel renderer for the real Emerald maps** (`--voxel`). This is the closest thing to "the FB screenshot, in C, from Gen-3 map data". | github.com/gradenGnostic/pokeemerald-multiplatform |

The FB post is a tease of Emerald entering this pipeline; no finished public "Emerald
voxel" release exists yet (2026-08-31). The two C-side sources above are where that work
will come from — and where we can learn today.

## 2. Local archives (the mod already died once — don't trust the URLs)

Cloned under **`projects/_reference/`** (git-ignored by the toolkit, outside the 3DGBA repo):

- `DramaticShapeVoxelMod-latest/` — full Lua source, last released version.
- `Gen2Recomped-DramaticShapes/` — the Gen-2 port of the mod.
- `pokeemerald-multiplatform/` — sparse clone, only `src/platform/voxel/` (~2,400 lines of C).
- `inspiration-emerald-voxel-fb-post.jpg` — the screenshot that started this.

## 3. How the Emerald voxel renderer works (pokeemerald-multiplatform, C)

12 files, ~2,400 lines, plain OpenGL. Architecture (verified by reading the source):

1. **`voxel_world.c` — semantic map reader.** Reads `gMapHeader → mapLayout` (width/height,
   metatiles), **metatile behavior** bytes, and **collision**, plus `gMapHeader.connections`
   to instance neighbouring maps at the right offsets (seamless connected world,
   `MAX_VOXEL_MAP_INSTANCES 16`; DIVE/EMERGE skipped). Player pos/facing from
   `gObjectEvents[gPlayerAvatar.objectEventId]`.
2. **`VoxelWorld_ClassifyTile` — the key algorithm.** Maps each tile to a
   `VoxelVisualShape` (FLAT / DECAL / LOW / LEDGE / WALL / TREE / BUILDING / ROOF /
   FURNITURE / BED / TABLE / COUNTER / SIGN / STAIRS / WATER / VOID) using:
   - behavior predicates (`MetatileBehavior_IsSurfableWater/IsTallGrass/IsJump*/IsDoor…`),
   - indoor-specific behavior/metatileId special cases (PC, TV, bookshelf, counter, mats, beds),
   - **the load-bearing heuristic: collision-neighbourhood extrusion** — a solid tile with a
     solid tile *above* it (screen-Y) = WALL (facade/cliff face); solid with solid *below* =
     ROOF (top surface); isolated solid = LOW (fence/rock/sign). Non-solid surrounded N/E/W
     by solid = wall alcove. That one rule turns a flat collision map into 3D massing.
3. **`voxel_structure.c` — structure grouping.** Flood-fills contiguous
   BUILDING/TREE tiles into `VoxelStructure`s (≤256, ≤1024 tiles each) so a house is
   extruded as one volume (single roof plane, coherent walls) rather than per-tile pillars.
4. **`voxel_mesh.c` — per-shape geometry.** Extrudes each classified tile with the tile's
   own texture (from the game's tileset atlas) on the visible faces.
5. **`voxel_camera.c` + `voxel_renderer.c`** — smooth-follow orbit camera
   (pitch/yaw/distance/fov) + optional first-person; renderer owns GL state and the frame.

**Their data source is in-process decomp globals. Ours is the same data via EWRAM reads** —
the gamestate logger already reads `gMapHeader`, mapId, avatar coords/facing
(`gen3-ram-touch.md`); metatile behaviors + collision come from the same
`mapLayout`/metatile-attribute tables in ROM/EWRAM, reachable through `gbacore_read16/32`.

## 4. What the Lua mod adds on top (DramaticShapeVoxelMod)

Beyond extrusion — the polish that makes the FB screenshot look the way it does:
tilt-shift DOF ("T-SHIFT", the miniature look), real cast shadows (second scene pass from
the sun), water with waves + sky/scene reflections, world curvature ("V-CURVE"),
supersampled AA, render-distance culling to the camera trapezoid ("FIT"), day/night
atmosphere, 3D-staged battles with a solved over-the-shoulder camera, billboard sprites
that show the correct facing frame for the camera angle, free-roam 1st/3rd-person where
grid-walk becomes continuous camera-relative movement **while collision/warps/encounters
still run through the game's own machinery**.

Data assets worth studying (in the archive): `data/voxel_heights.lua` (284 KB per-tile
height/extrusion table — the hand-tuned part), `data/map_atmosphere.lua`, and
`assets/docs/buidling_to_voxel/sprite_to_voxel_methodology.md` — a genuinely good worked
method for turning ¾-view overworld sprites into voxel models (band-classify sprite
regions by which 3D facing they depict → per-band geometric op; every voxel color sampled
from a real sprite pixel).

## 5. How this changes our 3D-world picture

`emerald-3d-depth.md` (2026-06-10) rejected "true layered 3D" **within the constraint of
re-using the composited GBA framebuffer** (A1/A2 = extra emulation passes = dead; verdict
was hybrid sprite-pop C + subtle warp B). The voxel-diorama approach is a **fourth path
that study never considered, and it sidesteps the killer constraint entirely**:

- **Zero extra emulation passes.** The renderer never touches the composited frame — it
  reads *map semantics* (metatiles/behavior/collision/connections, all static per map) and
  renders its own scene. Scene rebuild happens only on map change; per-frame cost is
  GPU-side, on the PICA200, which sits idle today. The ARM11 budget objection to A1/A2
  does not apply.
- **We already read every input it needs.** mapId/avatar/facing/mapObjects (logger),
  walkability (`touch.c` `map_read`); metatile behaviors are one more table walk.
- Texturing: tile art from the game's tileset (VRAM/ROM reads), atlas'd into POT textures —
  same upload path we already run (convention #3).
- **Fit:** top screen = voxel diorama (with real stereoscopic 3D — the diorama has true
  depth, so the 3DS slider becomes honest), bottom screen = the flat game/touch UI.
  Plays with, not against, the HD-2D tilt work (phase 14): tilt was the faux version;
  this is the real geometry it was faking.
- **Honest scope:** classification + structure grouping + per-shape meshes + camera is
  ~2.4k lines in their C. A citro3d port is a real phase, not a weekend; and it is
  overworld-only (battles/menus stay 2D or use the B-plan). Indoor maps need the special-case
  table. Perf on PICA200 (a few thousand textured quads + billboards) is very plausible but
  unproven — hardware gate applies (convention #6).

## 6. Licensing / IP (before ANY code reuse — `ip-publishing-policy.md` applies)

- **DramaticShapeVoxelMod:** README states redistribution of non-derivative code **post-v1.6.0
  is prohibited without permission**. The ≤ v1.6.0 archive's terms need checking before any
  reuse. Treat ALL of it as **study-only: learn the techniques, re-express in our own C.**
  Do not vendor Lua or port files line-by-line into public GPLv3 3DGBA.
- **pokeemerald-multiplatform:** fork of pret/pokeemerald (decomp, no clean license) and the
  voxel files `#include` decomp headers — **algorithm reference only, never copy code text**
  into 3DGBA. The classification rules/heuristics are ideas; re-implement clean against our
  EWRAM-read layer.
- **EmeraldRecomp/gbarecomp:** no license declared; reference only. (Its symbol-metadata-only
  stance vs pret is itself a useful precedent for what we already do with sym maps.)
- Our screenshot copy + clones are private research material; nothing from
  `projects/_reference/` ships or gets committed to a public repo.

## 7. If/when we build it — suggested attack order

1. Metatile-behavior + collision reader over EWRAM/ROM (extend `gamestate.c`; verify against
   pret maps for EM + FR, mind the FR `gMain+0x20`-is-a-pointer gotcha).
2. Port `ClassifyTile` (the collision-neighbourhood rule + behavior predicates) as pure C —
   PC-testable against dumped map data, per convention #4.
3. Structure grouping (flood fill) — pure C, PC-testable.
4. citro3d scene: extruded quads from a tileset atlas, billboard sprites, orbit camera at a
   fixed pleasing pitch; rebuild mesh on mapId change only.
5. Polish ladder, strictly optional: stereo depth (near-free once real geometry exists) →
   tilt-shift DOF → shadows → water. Each rung hardware-gated.

---

# Addendum (2026-08-31, same day) — second FB post: the collage + pokeemerald-3d

**Trigger:** Guy shared facebook.com/share/p/1DnJJYGNGC — a "Pokemon Hacks (Gba, Nds, Etc.)"
group post captioned only "Emerald Decomp — This looks Crazy": a collage of ~21 thumbnails,
every Hoenn town + interiors (OLDALE…EVERGRANDE, IN_BRENDAN/IN_CENTER/IN_MART/IN_GYM_*)
as 3D dioramas. Saved: `projects/_reference/inspiration-emerald-diorama-collage-fb-post2.jpg`.

## What the collage actually is (zoomed analysis)

The textures are the **real GBA tile art extruded** into beveled 3D (Pokémon Center, gym
roofs, fences all keep their pixel art), sprites are billboards, tilt-shift blur on top.
Lavaridge/Sootopolis tiles are still **flat unconverted pixel art** → an in-progress
*automated* map renderer with per-map coverage, i.e. the DramaticShape pipeline reaching
Emerald — almost certainly the same effort the first FB post teased. **Its code is NOT
public yet** (checked 2026-08-31): Buiko32/EmeraldVoxelRecomp is an empty README-only
placeholder; Donwaztok/Gen3Recomp is a real C++ Gen-3 recomp *host* (Ruby/Sapphire/Emerald,
Tauri launcher) but contains no diorama renderer; StonedModder on X teases "Gen3recomp is
coming sooner than you'd think". Watch those three + Gilliam Germi's page for the release.

## The real find: tripplyons/pokeemerald-3d (new, archived)

github.com/tripplyons/pokeemerald-3d ("Pokemon Emerald in 3D (in WebAssembly)", Aug 2026,
44★, site 3d.pokeemerald.com — domain currently not resolving; companion:
tripplyons/pokeemerald-wasm). Archived the whole 3D layer at
`projects/_reference/pokeemerald-3d/` (web/ + src/wasm_display.c + docs).

Architecture (source-read) — a THIRD approach, distinct from voxel extrusion and from our
framebuffer warp:

- **Game-side (C, `src/wasm_display.c`, ~1.5k lines):** the decomp itself re-renders the
  overworld into side-channel buffers each frame: an overscanned world RGBA (bigger than
  the screen), **per-pixel layer IDs** (BG1/2/3 via metatile layer-type SPLIT/COVERED/
  NORMAL), BG priorities, a **separate OAM object atlas** (sprites never baked into the
  world), camera/pan recovery to map-space, and a per-pixel height buffer.
- **Web-side (WebGPU/WGSL, `web/presenter.js`, ~1.1k lines):** heightfield terrain mesh
  from the height buffer, perspective camera + fog, depth-tested upright **billboard
  sprites**, projected cast-shadow mask, bloom, color grading, and an HD-2D tilt-shift
  whose focus band tracks the playable middle plane. Sliders: shading / perspective / zoom.

**The headline lesson — a shipped NEGATIVE result:** the height buffer is deliberately
`*height = 0` everywhere, with this comment: *"Collision, behavior, and elevation are
gameplay masks rather than a visual height map. Treating them as geometry tears buildings
and decorations apart and can sink actors into walkable mountain-top cells. Preserve
authored pixel-art depth on one coherent perspective plane."* They TRIED semantic
extrusion from gameplay masks and rejected it. So:

1. **Vindicates phase-14:** their whole shipped look = tilted plane + layer separation +
   sprite billboards + shadows/bloom/DOF — our HD-2D tilt + planned sprite-pop (approach C)
   + post-fx is the same recipe. Their polish stack (focus band at the play plane, bloom,
   fog, shadow mask) is directly imitable on PICA200.
2. **Calibrates the voxel path:** naive per-pixel extrusion from collision/behavior fails.
   The projects that DO get real volume pay for it with hand-tuned per-tile data
   (DramaticShape's 284 KB `voxel_heights.lua`) or per-tile classification + structure
   grouping (pokeemerald-multiplatform's flood fill) — per-TILE massing with grouped
   structures, never per-pixel heights.

## Licensing (the user's "can we use it legally?")

**No.** tripplyons' repos carry **no license** (default all-rights-reserved) *and* are
pret-derived forks, same standing as pokeemerald-multiplatform: **study-only; re-implement
ideas clean** (ideas/algorithms aren't copyrightable; per `~/.claude/ip-publishing-policy.md`
consult `ip-legal` before any gray-zone reuse). The unreleased collage renderer has no
code to take at all yet. Optional soft path: tripplyons has a Discord (discord.gg/u24yh5b83N)
— permission could be asked for the presenter's WGSL post-fx, but our C re-implementation
on citro3d would differ enough that it buys little.
