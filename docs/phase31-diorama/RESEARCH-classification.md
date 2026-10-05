# Phase 31 diorama — tile classification & voxel massing (clean-room idea spec)

**Status:** skeleton — sections filled incrementally as the reference is read.

**What this is:** a clean-room, ideas-only distillation of the tile-classification and
voxel-massing techniques in the archived `pokeemerald-multiplatform` voxel renderer
(`projects/_reference/pokeemerald-multiplatform/src/platform/voxel/`, ~12 C files).
The reference is unlicensed / pret-derived: **no code text, no comments, no data-table
values were copied** — only algorithms, decision rules, parameter roles, and rough
magnitudes, each with a `file:line` provenance pointer for later audit. This document
is the sole input the implementer will read; the reference code stays closed.

**Provenance pointer convention:** `[world.c:123]` means
`projects/_reference/pokeemerald-multiplatform/src/platform/voxel/voxel_world.c` line 123
(similarly `mesh.c`, `structure.c`, `camera.c`, `renderer.c`, and `*.h`). Pointers mark
where an idea was observed; they are traceability breadcrumbs, not citations of text.

---

## 1. Inputs read from game state (and where each conceptually lives)

Everything the renderer consumes is **static-per-map semantic data plus a handful of
live scalars**. No pixel of the composited GBA frame is ever read. The complete input
list:

| Input | Conceptual source (pret/Gen-3 terms) | Used for | Provenance |
|---|---|---|---|
| Map availability gate | current game-mode callback equals the overworld main callback (either variant), AND the current map-layout pointer is non-null | only build/draw the diorama while the overworld is actually running | [world.c:16-22] |
| Map dimensions | current map header → map layout → width/height (playable area, border excluded) | mesh extent, instance placement | [world.c:24-33] |
| Metatile grid, current map | the **live working copy** of the map grid (pret's "backup map layout": the bordered, in-RAM copy that reflects dynamic edits — opened doors, secret-base changes). Indexed with the border offset added back | so the diorama tracks door animations / dynamic tile swaps on the active map | [world.c:126-133] |
| Metatile grid, connected maps | the **static ROM layout** of each neighbor map (no live copy exists for them) | neighbor-map massing | [world.c:134-139] |
| Per-cell fields of a grid entry | 16-bit cell: low 10 bits = metatile id; next 2 bits = collision code (0 = passable) | classification | [world.c:168-169, 183-184] |
| Metatile **behavior** byte | metatile-attributes table of the owning tileset: ids below the primary/secondary split (512) index the primary tileset's table, ids 512..1023 index the secondary's at (id − 512); behavior = low 8 bits of the attribute | the semantic predicates (water, ledge, door, furniture…) | [world.c:187-208] |
| Map type | map header field (indoor / secret base vs outdoor kinds) | selects the indoor vs outdoor rule branch | [world.c:220] |
| Map connections | map header → connection list (count + array of {direction, offset, target map}) | instancing neighbor maps for the seamless world (§6) | [world.c:59-89] |
| Current map identity | saved-game location (map group/num) | labeling instance 0 | [world.c:52-53] |
| Player tile coords | the player's object-event entry (index = player-avatar's object-event id) → current coords, minus the border offset | camera target, cutaway test | [world.c:35-40] |
| Player facing | the game's own facing query when the avatar is controllable, else default "south". Encoding 1=S, 2=N, 3=W, 4=E | first-person camera, sprite | [world.c:157-163] |
| Tileset art | the metatile art rendered into a 512×512 atlas, 16 px per metatile, 32 per row, indexed directly by metatile id (supports the full 1024-id space) | all face texturing | [structure.c:156-157, 207-210] |

Object events / NPCs are read elsewhere (renderer-side billboards, §7 area) — the
classification/massing layer itself only needs the table above.

## 2. Coordinate conventions

Get these wrong and every rule below inverts. The reference's conventions
[world.c:37-39, 75-87, 144-155; structure.c:216-219]:

- **Map space:** x grows east (right), **y grows south (screen-down)** — standard GBA
  map order. "Above" in every rule below means **y − 1** (north / up-screen); "below"
  means **y + 1** (south / down-screen).
- **World space:** the current map sits at origin (0,0); each connected map gets an
  integer (originX, originY) tile offset (§6). World coords = map coords + origin.
- **Border offset:** the live map copy is padded by a fixed border (7 tiles in Gen-3);
  player coords and grid indexing into the live copy add/subtract that constant
  [world.c:38-39, 128-129].
- **Render space:** worldX → render X (east), worldY → render **Z** (south), and render
  **Y is height** (up). One tile = 1.0 world unit in X/Z, and one "storey" = 1.0 unit
  in Y. A tile's quad spans [wx, wx+1] × [wz, wz+1].
- **Facing encoding:** 1=south, 2=north, 3=west, 4=east (the game's own values).

## 3. The ClassifyTile decision procedure (full rule list)

Signature: (worldX, worldY) → shape class. Inputs gathered up front: owning map
instance (null → VOID), metatile id, behavior byte, collision code
[world.c:211-218]. Then one of two branches by map type: **indoor** (indoor or
secret-base map types) or **outdoor** (everything else) [world.c:220].

### 3.1 Shape classes

The full enum (order irrelevant, listed for completeness) [world.h:9-27]:

FLAT, DECAL, LOW, LEDGE, WALL, TREE, BUILDING, ROOF, FURNITURE, BED, TABLE,
COUNTER, SIGN, STAIRS, WATER, VOID.

**Observed dead weight:** ClassifyTile never actually *returns* TREE, BUILDING, or
STAIRS — those three are declared but unreachable (consequence: the tree pipeline in
§4/§5 is dead code in practice; trees classify as generic WALL/ROOF and get massed as
"buildings"). The reference's own comment acknowledges trees are currently
indistinguishable [structure.c:80-83]. Our port should either cut those classes or
actually produce them (e.g. TREE via behavior/tileset tests) — see §8.

### 3.2 Rule order (both branches) — first match wins

The procedure is a strict **ordered if-chain**; earlier rules shadow later ones.
Overall precedence:

1. out-of-instance → VOID
2. (indoor only) explicit metatile-id / behavior special cases (§3.3)
3. (outdoor only) behavior predicates: water → WATER; tall grass → FLAT (deliberately
   not extruded "for now"); any of the four directional jump-ledge behaviors → LEDGE
   [world.c:265-270]
4. the collision-neighborhood extrusion heuristic (§3.4)
5. (indoor only) the alcove rule (§3.4)
6. default → FLAT

### 3.3 Indoor / metatile-id special cases

The indoor branch front-loads a hand-curated exception list [world.c:220-237]. Two
kinds of test are mixed: **behavior-byte predicates** (portable across tilesets) and
**raw metatile-id equality** (tied to Emerald's specific indoor secondary tileset —
all the ids fall in the secondary range, low 500s to low 600s).

> Legal note: the ids below are facts about *Emerald's* tileset (which id is a chair),
> independently re-derivable from pret's metatile labels — they are functional game
> facts, not the reference author's creative data. Re-verify each against pret before
> use; treat the *pattern* (behavior first, id fallback) as the idea.

Ordered rules:

| # | Test | → Shape | Meaning / role | Provenance |
|---|---|---|---|---|
| 1 | metatile id 622 | VOID | the black filler tile used outside indoor room bounds — don't mesh it at all | [world.c:221] |
| 2 | behavior = PC, or behavior = television, or id 570 | FURNITURE | player-facing appliances get a mid-height box | [world.c:224] |
| 3 | behavior ∈ {the four bookshelf/shop-shelf behaviors}, or id 533/534 | WALL | full-height shelving reads as wall | [world.c:225] |
| 4 | behavior = counter | COUNTER | waist-high counter slab | [world.c:226] |
| 5 | id ∈ {576, 577, 584, 585, 586} | TABLE | desks/tables | [world.c:229] |
| 6 | id ∈ {565, 558, 566} | FURNITURE | chairs | [world.c:230] |
| 7 | id 578 | SIGN | thin vertical decal (sign / potted plant) | [world.c:231] |
| 8 | id 589 | WALL | indoor stairwell drawn as a vertical wall (STAIRS class unused) | [world.c:232] |
| 9 | behavior is a warp-door or door | WALL | doors stay embedded in the facade plane | [world.c:233] |
| 10 | id ∈ {514, 515, 516, 517} | DECAL | floor mats — flat art that must not z-fight the floor | [world.c:234] |
| 11 | id ∈ {567, 568, 575, 576} | BED | beds drawn low (their art is top-down) | [world.c:237] |

**Observed defect:** id 576 appears in both rule 5 (TABLE) and rule 11 (BED); rule 5
wins, so the BED mapping for 576 is dead. First-match-wins over overlapping literal
id lists is exactly the fragility to avoid (§8) — our port should make the table
one-id-one-entry and assert uniqueness.

After the exception list, the indoor branch falls through to §3.4, then FLAT
[world.c:239-261].

### 3.4 The collision-neighborhood extrusion heuristic (+ edge cases)

**The load-bearing idea of the whole renderer** — identical logic in both branches
[world.c:240-250 indoor, 273-286 outdoor]:

For a tile whose collision ≠ 0 (impassable), look only at the collision of the
**vertical screen neighbors**:

```
above = collision(x, y-1)     # north / up-screen
below = collision(x, y+1)     # south / down-screen
if above solid:        WALL   # facade / cliff face / mid-stack row
elif below solid:      ROOF   # topmost row of a solid stack
else:                  LOW    # isolated solid: fence, rock, sign, stump
```

Why it works: GBA maps draw 3D objects in ¾ view, stacked in screen rows — a
house's *top* rows are its roof art and its *bottom* rows its facade art. A solid
tile with more solid above it must be a lower row of such a stack (facade → WALL);
the stack's top row (nothing solid above, solid below) is the roof surface (ROOF);
a solid singleton has no vertical extent worth extruding (LOW). This one rule turns
a flat collision bitmap into believable massing with zero hand data.

Edge cases and properties to preserve:

- **Priority: `above` is tested first.** A middle row (solid above *and* below) is
  WALL, not ROOF. A 2-row structure = ROOF row on top + WALL row below. A 1-row solid
  strip is entirely LOW.
- **Horizontal neighbors are ignored** here — a long east-west fence stays LOW
  regardless of length (correct: fences have no roof art).
- **Neighbor lookups cross map-instance boundaries** transparently because they go
  through the world-space accessor; off-world lookups return metatile 0/collision 0
  [world.c:117-142] — so a solid tile at a map's north edge with a solid tile in the
  connected map above still classifies WALL. Without connections loaded, edge tiles
  silently become ROOF/LOW (a visible seam artifact — see §6).
- **Ledge/water/etc. shadow this rule** (outdoor rules #3 run first), so a surfable
  cliff-adjacent water tile never becomes WALL.
- **Indoor-only alcove rule** [world.c:251-259]: a *passable* tile whose north, east,
  AND west neighbors are all solid is classified WALL anyway — it's a niche cut into
  the room's back wall (e.g. a doorway recess); drawing it as floor would punch a hole
  in the wall plane. South neighbor deliberately not required (the opening faces the
  camera).
- Screen-Y polarity is the classic port bug: if your map reader's y grows the other
  way, WALL and ROOF swap and every building renders upside-down-massed.

## 4. Structure grouping (flood fill)

### 4.1 Seed conditions and fill rules

After classification, contiguous solid regions are grouped into **structures** so a
house extrudes as one volume [structure.c:102-130]:

- Scan every tile of every map instance in row-major order.
- If unvisited and classified WALL / BUILDING / ROOF → seed a **building** fill.
  If unvisited and classified TREE → seed a **tree** fill (dead in practice, §3.1).
- Fill = plain BFS over the **4-neighborhood** (N/S/E/W, no diagonals)
  [structure.c:66-96]. A neighbor joins a building fill iff its class ∈
  {WALL, BUILDING, ROOF} (TREE explicitly excluded); joins a tree fill iff TREE.
- Each structure records its tile list and bounding box; a global visited bitmap
  prevents re-seeding [structure.c:14-28, 57-64].

### 4.2 Limits (roles + magnitudes, all arbitrary caps)

- Max structures per world: 256 [structure.h:11].
- Max tiles per structure: 1024 — also the BFS queue capacity, so an oversized blob is
  silently truncated mid-fill [structure.h:12; structure.c:52, 89].
- Visited bitmap: a fixed 2048×2048 boolean grid centered by a +1024 offset; world
  coords outside ±1024 are treated as "already visited" (skipped) [structure.c:14-28].
  4 MB of .bss for what is at most a few-hundred-tile world — replace with a
  per-instance bitmap sized to real map extents in our port.
- Membership queries are linear scans over tile lists with a bbox pre-test
  [structure.c:132-152] — O(structures × tiles); fine at this scale, but our port can
  store a per-tile structure-id grid instead.

### 4.3 What grouping visually fixes vs per-tile extrusion

Per-tile extrusion (classify each tile, extrude each independently) produces:

- **Sawtooth roofs:** each facade column extrudes to its own height, so a house whose
  collision rows vary (door gaps, porch tiles) becomes a comb of pillars.
- **Interior walls:** faces are drawn between adjacent solid tiles *inside* the blob —
  wasted quads and z-fighting seams visible at grazing angles.
- **Incoherent cutaway:** you cannot "lower the building the player is behind" if the
  building isn't an object.

Grouping fixes all three:

- **One height per structure:** the structure's height = the max over member tiles of
  a per-column upward walk (count consecutive WALL/ROOF rows going north from the
  member, stopping after a ROOF is included) [structure.c:168-185]. The whole volume
  extrudes to that single height → flat coherent roof plane.
- **Exterior-only faces:** a wall quad is emitted for a member tile's side only when
  the neighbor on that side is *not* wall/building/roof-classified — interior faces
  vanish [structure.c:232-240].
- **Whole-structure cutaway:** if the structure's bbox extends south of the player
  (maxY > player Z + 0.5), the entire structure renders at height 1 with roof and
  north wall suppressed, keeping the player visible behind it
  [structure.c:188-196, 242, 261]. Crude (bbox test, binary height drop) but the hook
  exists at the right granularity.

## 5. Per-shape mesh recipes

Three geometry passes share the frame (order matters — §7.2): (a) the **wall-column
pass** for solid verticals not owned by a structure, (b) the **structure pass** (§4),
(c) the **per-tile pass** for everything else. One world unit = one tile; heights in
the same unit ("1.0 = one storey ≈ one 16 px tile of art").

### 5.1 Texturing model (all passes)

Faces are textured from a per-map **metatile atlas**: each metatile's 16×16 px art
composed into a 512×512 RGBA texture, 32 per row, slot = metatile id; UV of id *m* =
((m mod 32), (m div 32)) × 16/512 [mesh.c:52-53, 114-117, 401-406]. Atlas
construction (renderer-side, once per map layout) [renderer.c:31-159]:

- decompress both tilesets' 4bpp tile graphics (LZ77 when flagged compressed);
- mark only metatile ids actually used by the map grid (plus the 4 border entries)
  and compose only those;
- each metatile = 2 layers × 4 sub-tiles; each sub-tile entry packs tile id (10 bits),
  X/Y flip (bits 10/11), palette (top 4 bits); layer 1 overdraws layer 0 only where
  its color index ≠ 0; layer-0 color-0 = the primary tileset's backdrop color;
  palette indices below 6 resolve in the primary tileset's palettes, the rest in the
  secondary's (the Gen-3 palette split);
- BGR555 → RGBA8, nearest-neighbor filtering, fully opaque.

This "flatten both metatile layers into one opaque sprite" is a deliberate
simplification: no per-layer separation, no transparency in the atlas (transparency
was force-disabled) [renderer.c:143-145].

### 5.2 Wall-column pass (WALL tiles outside structures)

For each map tile not yet consumed and not in a structure, if classified WALL
[mesh.c:57-68; seeds nominally also BUILDING/TREE but those never occur, §3.1]:

1. **Column height**: walk north (y−1, y−2, …) while the class stays WALL, counting;
   if a ROOF is met, count it and stop; any other class stops the walk. Minimum 1
   [mesh.c:69-79]. So a facade row pulls its whole stack including the roof row into
   one column.
2. **Consume** every row of the column in a global "consumed" grid so the roof rows
   aren't re-extruded, and so the per-tile pass later draws those cells as plain
   ground instead (there must be floor beneath/behind a wall) [mesh.c:101-103,
   392-396]. The consumed grid mirrors the visited grid's shape: fixed 2048²,
   +1024 offset, cleared each frame at instance 0 [mesh.c:14-45].
3. **Face selection**: sample neighbor classes at south (x, y+1), north **beyond the
   column top** (x, y−height), east (x+1, y) and west (x−1, y) — east/west only at
   the *base* row. Emit a face unless that neighbor is WALL, ROOF, or VOID. If all
   four would be culled, force the south face on [mesh.c:81-91].
4. **Cutaway**: if the column would show its south face AND lies south of the player
   (y > playerZ + 0.5), draw only the bottom level and skip the top face — buildings
   between camera and player collapse so the player stays visible [mesh.c:99, 123,
   134].
5. **Geometry**: top face at height = column height, textured with the column's *top*
   row's metatile; each vertical level i (0-based from ground) is a 1×1 quad ring
   textured with the metatile of row (y − i) — the screen-stacked facade/roof art
   wraps naturally onto the vertical faces [mesh.c:105-117, 122-169]. Faces are
   full-bright white (no per-face shading in this pass).
6. Cross-map texturing caveat: when the column top row lands in a *different* map
   instance, its metatile id is used against the current instance's atlas — a known
   wrong-texture case at connection seams, acknowledged in the reference
   [mesh.c:107-113].

### 5.3 Structure pass (grouped buildings)

Per structure (§4): one shared height (max column walk over members,
[structure.c:168-185]); if cutaway (bbox south of player) height clamps to 1, roof
and north faces skipped [structure.c:188-196, 242, 261].

- **Roof**: one top quad per member tile at the shared height, textured with the
  metatile found `height − 1` rows north of that member (the map's roof art)
  [structure.c:197-221].
- **Walls**: for each member, a face per side whose neighbor is not
  wall/building/roof-classified (exterior-only); each vertical level textured with
  the metatile of the correspondingly-north row [structure.c:225-283].
- **Directional shading**: textured faces tinted per direction — top 1.00, north
  0.90, south 0.85, east 0.82, west 0.75 (a baked fake sun; roles: top brightest,
  west darkest) [structure.c:212, 255-276].
- **Tree recipe** (dead code in practice, §3.1, but the intended idea): fixed height
  2; canopy top textured from one row north; at ground level the box insets to the
  central 0.2..0.8 sub-square (trunk), full tile at canopy level; faces culled
  against same-type neighbors [structure.c:285-375].

### 5.4 Per-tile pass (everything else)

VOID draws nothing; consumed cells are demoted to FLAT (§5.2.2). Per-shape recipe
[mesh.c:388-433]:

| Shape | Recipe | Height/param roles | Provenance |
|---|---|---|---|
| FLAT | one textured ground quad at 0 | — | [mesh.c:414] |
| DECAL | ground quad lifted a hair (~0.02) | z-fight guard for rugs/mats over the floor | [mesh.c:415] |
| WATER | ground quad recessed slightly (~−0.1) | reads as water below bank level | [mesh.c:416] |
| LOW | extruded box, top ~0.4 | fences/rocks/signs | [mesh.c:417] |
| LEDGE | same box as LOW (~0.4) | jumpable ledge = half-height step | [mesh.c:418] |
| COUNTER | box, top ~0.7 | waist height | [mesh.c:419] |
| SIGN | single upright textured quad, 1.0 tall, standing at the tile's mid-depth (z + 0.5) | thin vertical "cardboard cutout", not extruded | [mesh.c:420, 440-451] |
| STAIRS | box ~0.2 (unreachable class) | — | [mesh.c:421] |
| WALL/ROOF/TREE/BUILDING | nothing here (handled by §5.2/§5.3) | — | [mesh.c:422-425] |
| TABLE/FURNITURE/BED | furniture sub-recipes below | — | [mesh.c:427-431] |

**Extruded-box detail (LOW/LEDGE/COUNTER/STAIRS):** textured top face; sides are
*untextured* solid grays with fixed per-side values (front ~0.6, back ~0.5, left
~0.65, right ~0.55 — same fake-sun role as §5.3's tints) [mesh.c:498-555]. A side is
culled when the neighbor is VOID **or has the same shape** — so a run of fence tiles
fuses into one continuous visual block with no interior seams [mesh.c:479-496].

**Furniture sub-recipes** [mesh.c:175-385] all use same-shape neighbor tests to fuse
multi-tile pieces:

- **TABLE**: textured top slab at ~0.5 with a thin (~0.1) dark apron on non-connected
  sides; small square legs (~0.1 wide) only at corners where two non-connected edges
  meet — a 2×1 desk gets 4 outer legs, not 8 [mesh.c:186-262].
- **FURNITURE** (styled as a TV/appliance): dark base block ~0.3 tall (full tile);
  textured upright "screen" quad from 0.3 to 1.0 standing near the tile's south edge
  (z + 0.8); solid dark body slab behind it (z + 0.3..0.8) with top and side faces;
  sides culled against same-shape neighbors [mesh.c:266-324].
- **BED**: **has no recipe at all — classified BED tiles render nothing** (defect;
  §8). [mesh.c:427-431 routes it to the furniture drawer, which has no BED branch.]
- Two further branches are unreachable: a duplicate FURNITURE test (a chair recipe:
  inset textured seat at ~0.4 + backrest slab on the north edge rising ~0.4 more)
  and a ~1.6-tall textured-front wall recipe — dead but the *chair idea* is worth
  keeping [mesh.c:325-384].

### 5.5 Sprite billboards (player + NPCs)

Not part of massing but part of the scene contract:

- Every active object event (up to 16) becomes an upright textured quad, base on the
  ground plane, anchored at its tile center (+0.5, +0.5) [renderer.c:707-724, 555-557].
- Scale rule: 16 sprite pixels = 1.0 world unit, quad centered horizontally
  [mesh.c:576-579].
- Alpha-cutout at 0.5 so transparent texels don't write depth [mesh.c:582-583].
- **Not camera-facing**: the quad is axis-aligned facing south; passable only because
  the orbit camera always looks roughly north (§7). A real port should yaw the quad
  to the camera [mesh.c:559-563 ignores the camera args].
- Sprite pixels are decoded from OAM state (shape/size → dimensions; tile number +
  palette from OAM; H/V flip read from the non-affine OAM flip bits) out of object
  VRAM + object palette RAM, color-0 transparent — i.e. the game's own current
  animation frame, re-decoded to a texture [renderer.c:164-268].
- **Sub-tile motion**: world position = lerp(previousTile, currentTile, t) with
  t = stepTimer / stepDuration, where stepDuration comes from the game's speed-class
  table (roles: speed classes 0..4 map to step lengths of 16/8/6/4/2 frames);
  idle → t = 1 [renderer.c:284-334]. The reference warns the sprite's *screen*
  coords must never be used for world position (they embed the game camera pan)
  [renderer.c:279-281].

## 6. Map-connection instancing (the seamless world)

Purpose: tiles just across a map connection must exist so (a) the neighbor's terrain
is visible, and (b) classification neighborhoods don't fabricate ROOF/LOW at map
edges (§3.4).

Rules [world.c:45-90]:

- Instance 0 = the current map at world origin (0,0), read from the **live** grid;
  its identity recorded from the save-file location.
- For each entry in the current map's connection list, up to a cap of 16 instances:
  - **skip DIVE and EMERGE connections** (they're vertical world switches, not
    spatial neighbors);
  - skip if the target header/layout can't be resolved;
  - place the neighbor by direction, using the connection's lateral `offset`:
    - north: origin = (offset, −neighborHeight)
    - south: origin = (offset, currentHeight)
    - west: origin = (−neighborWidth, offset)
    - east: origin = (currentWidth, offset)
- Only the **first ring** is instanced — connections of connections are not followed.
- Connected instances read their **static ROM** grids (no live copy exists for them)
  [world.c:134-139]; diagonal gaps at corners simply classify VOID.
- World→instance lookup: instance 0's bounds first (fast path), then linear scan;
  outside all instances → VOID / metatile 0 / collision 0 [world.c:92-142].

**Continuity across a warpless border crossing** [renderer.c:339-387]: when the
current map changes (layout id or group/num differs from last frame), search the
*old* instance table for the new map; if found (i.e. the player walked into a
neighbor), shift the camera position and follow-target by minus that instance's old
origin — the world rebases around the new map with **no visible camera jump**. If
not found (a warp/door), snap the camera to the player instead. This rebase trick is
essential and easy to miss.

Rebuild cadence in the reference: instances are rebuilt **every frame**
[renderer.c:499] — harmless on PC, wasteful for us (§8); map-change-only suffices,
except that instance 0's *grid contents* must stay live (door animations).

## 7. Camera model

### 7.1 Orbit follow camera (default)

State: position, follow-target, pitch, yaw, distance, fov [camera.h:7-14]. Defaults
and roles [camera.c:11-23]:

- pitch ≈ 40° downward (authoring note in the reference: pleasing range is roughly
  35–50°), yaw 0 (due north view), fov ≈ 35° (tight, telephoto-ish — flattens
  perspective toward the diorama look), base distance ≈ 9.
- **Distance adapts to map size**: distance = 8 + 0.2 × max(mapW, mapH), the adaptive
  term clamped to +5 (so ~8..13 tiles) — small rooms frame closer, towns further
  [camera.c:47-53].
- **Placement**: the camera sits **south of and above** the target, looking north:
  x = targetX + sin(yaw)·distance, z = targetZ + cos(yaw)·distance,
  height y = tan(pitch)·distance, look-at at (targetX, 0, targetZ), world-up +Y
  [camera.c:55-62]. (Because billboards face south, §5.5, yaw ≈ 0 is load-bearing.)
- **Follow smoothing**: each frame, target += (player − target) × 0.15 — a simple
  exponential chase, per-axis X/Z; the vertical target is pinned to the ground plane
  [camera.c:27-28, 62]. On map entry / warp the target snaps (§6).
- Player anchor is the interpolated (§5.5) tile-center position [renderer.c:555-565].
- Projection: standard perspective, near ~0.1, far ~200 [camera.c:65-71].

### 7.2 Frame pipeline order (renderer)

The full per-frame order the reference runs — a port should keep the *order*, not
the per-frame cadence [renderer.c:403-787]:

1. If the overworld isn't running (§1 gate): draw the game's normal 2D frame
   fullscreen instead, and arm a camera snap for the return [renderer.c:416-487].
2. Detect map change → camera rebase-or-snap (§6).
3. Rebuild map instances; build/lookup one metatile atlas per distinct layout
   (cached; the whole cache flushes when the main layout changes)
   [renderer.c:499-544].
4. Update camera (smooth follow or first-person).
5. GL state: depth test on, backface culling **off** (quad winding is inconsistent),
   alpha-test cutout at 0.5, sky-blue clear color [renderer.c:607-616].
6. Structure extraction (§4) — in the reference, every frame.
7. Wall-column pass per instance (§5.2), then structure pass (§5.3), then per-tile
   pass (§5.4) per instance with that instance's atlas bound [renderer.c:628-698].
8. Object-event billboards (§5.5), depth-tested against the world
   [renderer.c:702-724].
9. **2D UI overlay**: re-render the game's 2D frame with the world BG layers and OBJ
   temporarily disabled through the display-control register so only the textbox/menu
   layer survives; color-key the backdrop to transparent; alpha-blend it over the 3D
   scene as a fullscreen ortho quad [renderer.c:726-784]. (Hacky but the *idea* —
   "UI layer only, keyed, composited over 3D" — is exactly what we need; our
   emulator can extract the UI BG layer cleanly instead of register-poking.)
10. A debug view (hotkey-toggled) draws the map flat with translucent per-class color
    overlays (wall/roof/low/flat in distinct colors) — invaluable while porting the
    classifier; keep this idea [renderer.c:653-689].

### 7.3 First-person / free-fly mode

A global toggle switches to free-fly: mouse-look (sensitivity ~0.2°/px, pitch
clamped to ±89°, yaw wrapped), WASD-style planar movement along the look/right
vectors + vertical keys, speed ~0.15 units/tick; the follow logic is bypassed and
the look-at target is derived from yaw/pitch [camera.c:30-45, 82-113;
renderer.c:586-597]. Positive pitch looks down; yaw 0 looks toward −Z (north).
Role: a debug/novelty mode, not gameplay — collision is not consulted.

## 8. Hand-tuned vs derived; weaknesses / anti-patterns NOT to copy

### 8.1 What is derived from data (robust, port as-is)

- All massing: the collision-neighborhood rule (§3.4), column heights, structure
  grouping and heights, exterior-face culling, cutaway trigger.
- All texturing: atlas slot = metatile id; faces textured from the actual map rows.
- Connection instancing and the camera rebase (§6).
- Sub-tile motion from the game's own step timers (§5.5).
- Map-size-adaptive camera distance (§7.1).

### 8.2 What is hand-tuned (constants with roles; retune freely)

- The indoor metatile-id exception list (§3.3) — Emerald-tileset-specific.
- Per-shape heights: LOW/LEDGE ~0.4, COUNTER ~0.7, TABLE ~0.5, furniture base ~0.3,
  screen top 1.0, DECAL lift ~0.02, WATER recess ~−0.1, tree height 2, trunk inset
  0.2..0.8.
- Fake-sun face tints (two inconsistent sets: §5.3 vs §5.4 — unify in our port).
- Camera: pitch ~40°, fov ~35°, base distance ~8 + up-to-5 adaptive, follow lerp
  0.15, cutaway threshold "player Z + 0.5".
- Billboard scale (16 px = 1 unit) and alpha threshold 0.5.
- Caps: 16 instances, 256 structures, 1024 tiles/structure, 512² atlas.

### 8.3 Weaknesses and defects observed — do NOT copy

1. **Everything is rebuilt every frame**: classification per tile (with 4+ neighbor
   classifications each, no memoization), structure flood-fill, immediate-mode quad
   submission, and per-object sprite texture re-decode [renderer.c:499, 622, 628-698,
   714]. On PICA200 this is the difference between shipping and not: classify once
   per map into a shape grid, build static VBOs on map change, re-decode sprite
   textures only when the animation frame changes.
2. **Dead classes and dead recipes**: TREE/BUILDING/STAIRS never produced (§3.1);
   BED classified but never drawn (§5.4); duplicate unreachable FURNITURE branch;
   unreachable in-furniture WALL recipe; a leftover half-written hash function and a
   permanently-false texture flag [mesh.c:18-21, 453]. Decide each class: implement
   it for real or delete it.
3. **Overlapping id lists with first-match-wins** (576 both TABLE and BED, §3.3).
   Use one table, assert uniqueness at build time.
4. **Fixed 4 MB 2048×2048 visited + consumed grids** for a world that is a few
   hundred tiles [structure.c:14; mesh.c:40]. Size to real instance extents; on 3DS
   heap this is disqualifying as-is.
5. **O(structures × tiles) membership scans** per tile per frame
   [structure.c:132-143]. Store a per-tile structure-id grid.
6. **Billboards don't face the camera** (§5.5) — fine only at yaw ≈ 0.
7. **East/west wall-face culling samples only the base row** [mesh.c:83-84]: a tall
   column beside a short one culls faces it actually needs → occasional holes at
   height differences. Cull per level, not per column.
8. **Cross-instance atlas mismatch** at connection seams (§5.2.6) — bind/sample the
   owning instance's atlas.
9. **Crude cutaway**: bbox-south test and binary collapse-to-height-1; whole
   structures pop between two states, and un-grouped columns cut independently
   (shearing). Consider distance/frustum-based fade or top-slice instead; but keep
   the *structure-granularity* hook.
10. **The alcove rule is exact-pattern** (N+E+W solid, §3.4): a recess whose east
    neighbor is passable is missed → floor hole in the wall plane. Generalize
    (e.g. "passable tile whose north neighbor is solid and which is unreachable from
    the walkable region" is a wall cell).
11. **No elevation input at all**: Gen-3 elevation nibbles (in collision/attributes)
    and object-event elevations are ignored — one ground plane, so bridges,
    multi-level cliffs, and over/under paths flatten. Note the sibling finding
    (kb: `voxel-diorama-reference.md` §Addendum): treating elevation as geometry
    naively *tears maps apart* — if we add it, add it as per-structure/area offsets,
    not per-pixel height.
12. **UI overlay via display-register poking + backdrop color-keying**
    [renderer.c:728-742]: breaks whenever a menu uses the backdrop color, and
    briefly mutates game-visible state. Our emulator can composite the UI layer(s)
    properly instead.
13. Tall grass is deliberately FLAT (§3.2 rule 3) — the reference never got to
    grass tufts; an easy visual win for us (small DECAL cross-quads) but remember
    grass is *walkable*: never give it collision-derived volume.
14. Water animation is nonexistent (static recessed quad). Fine for v1.

## 9. INPUTS OUR LAYER MUST SUPPLY (gamestate/fieldpath/touch audit)

*(to be filled)*
