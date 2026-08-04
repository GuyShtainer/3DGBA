# SPEC-avatar — Phase 15 co-op presence: the rendering, art and interaction half

Authored 2026-08-04 against commit `ae35079` (phase 14 shipped; baseline green — 10343 checks, 0
failures). Binding on the implementer of the presence **draw / art / interaction** path.
Subordinate to `docs/phase15-presence/PHASE.md` — its **Scope — OUT**, **The honest ceiling**,
**Invariants** and **Decisions already made** win any conflict with this document, and none of them
are re-opened here.

**What this spec owns:** everything from "a `PeerPresence` struct exists and is fresh" to pixels on
the screen and a prompt under the player's thumb.
**What it does NOT own** (the sibling data half — `SPEC-data.md`, M0/M1): the `GameProfile` rows
(`sb2ptr`, sub-tile scroll), the parked-window cross-read, the same-map/liveness gate, and the
`PeerPresence` producer. This spec states the seam it consumes (§A0) and nothing beyond it.

**Sources read for every claim below** (nothing here is from memory):

| What | Where |
|---|---|
| The phase contract, invariants, the rejected paths | `docs/phase15-presence/PHASE.md` |
| The screen-tile derivation, the overlay-vs-injection verdict, the interaction split | `docs/kb/coop-shared-overworld.md` §1a, §1b, §2, §4 |
| Billboard anchoring (pure translation, unscaled, y-sorted), what transfers to PICA | `docs/kb/external/gen1-render.md` findings 2, 5, 8 |
| The 48-byte overworld record, two-tier presence, "which ABI parts we get for free" | `docs/kb/external/pm-rom-abi.md` §3, §7.4, §9 |
| The tilt projection, the pass order, the raw-C3D block, chrome-stays-flat | `docs/phase14-tilt/SPEC-render.md` R1, R3.1, R3.3, R4.0, R4.0.2 |
| The shipped tilt API | `source/tilt.h` (line cites inline) |
| Every render/pass/menu/settings/HUD line cited below | `source/main.c` (line cites inline) |
| Asset pipeline, text helpers, widget kit | `source/assets.{c,h}`, `source/assets_gen.h`, `tools/build_assets.sh`, `source/ui.h` |
| Profiles, `game_read`, the fields presence consumes | `source/gamestate.{c,h}` |
| The core's read/write surface | `source/gbacore.h` |
| CSV telemetry schema + its header/row contract | `source/diag.h` §D3 |

Everything the PC cannot settle is tagged **verify-on-hw**. Every game-RAM address referenced is
either already in the verified profile or is explicitly handed to the data half — this spec adds
**zero** new RAM addresses of its own.

---

## A0. THE SEAM — exactly what this half consumes

### A0.1 — `PeerPresence`, the render half's read-only view

PHASE Invariant 3 mandates one transport-agnostic struct, shaped after Project PM's 48-byte
overworld record (`pm-rom-abi.md` §3). The data half owns its production; the draw half sees only
this, and **must compile and behave identically whether it was filled from sibling EWRAM (this
phase) or a UDS beacon (M4)**.

```c
// presence.h (data half owns the definition; reproduced here as the CONSUMED contract)
typedef struct {
    uint8_t  live;        // 0 = draw nothing at all. The ONE bit the renderer checks first.
    uint8_t  gender;      // 0 = male variant, 1 = female variant, 0xFF = unknown -> male
    uint8_t  facing;      // 1=D 2=U 3=L 4=R, 0/other = unknown -> face DOWN (A1.3, A2.x)
    uint8_t  mapGroup, mapNum;
    int16_t  tileX, tileY;    // the peer's tile, in the SAME space as our own (A0.2)
    int16_t  subX,  subY;     // sub-tile scroll, GBA px, -15..15 (A0.3)
    uint16_t tid;             // visible trainer ID (A4.3)
    uint8_t  name[8];         // raw GBA charmap, 0xFF-terminated — NOT decoded (A4.1)
    uint32_t round;           // monotonic frame/round counter; staleness + wedge signal
} PeerPresence;
```

**A0.1.1** The renderer never reads game RAM, never calls `game_read`, never calls `profile_for`,
and never touches a `GbaCore*`. It reads `PeerPresence` and our own `GameState`-derived host
position, both of which the parked window (`main.c:2224`, *"Workers are parked here -> touch RAM
access safe"*) already produced. This is what makes the whole draw path host-testable and what
keeps M4 a data-source swap.

**A0.1.2** `live == 0` ⇒ **zero** work: no projection, no texture bind, no text buffer use. The
frame must be bit-identical to a presence-off frame. Same discipline as phase 14's
`tilt_active()` early-out (`tilt.h:109`, `main.c:3018-3024`, "built only when engaged: a tilt-off
frame pays nothing").

**A0.1.3** `name[]` crosses the seam **raw**. Decoding is a presentation concern and lives here
(§A4.1), so the byte-exact 8-byte record stays the wire format for M4 and matches
`celiolink_payloads.h:35`'s `name[8]` ("GBA-charset, 0xFF-terminated").

### A0.2 — The screen formula: VERIFIED against the live code, not re-derived

PHASE "Decisions already made" says the formula is settled and orders us to *verify, not
re-derive*. Here is the verification, from three independent in-repo anchors:

1. **The player's fixed screen tile.** `POP3D_PLAYER_GX 112` / `POP3D_PLAYER_GY 64`
   (`main.c:691-692`) with the comment *"player tile (7,5) -> sprite rect (16x32, head 16px above
   the tile)"*. So the player's **cell** top-left is `(7·16, 5·16) = (112, 80)` and its **sprite**
   top-left is `(112, 80−16) = (112, 64)` — i.e. `POP3D_PLAYER_GX/GY` **is** the cell anchor minus
   a 16-px head margin. This is hardware-validated art placement (it is what the 3D standee pass
   pops), not a guess.
2. **The visible-tile → map-grid map.** `build_depth_grid` (`main.c:865`):
   `int gx = px + c, gy = py + r + 2;` over `r ∈ [0,10)`, `c ∈ [0,15)`, with `px/py` =
   `SaveBlock1.pos` (`gamestate.c:98-99`). Substituting the player's screen tile `(c,r) = (7,5)`
   gives map grid `(px+7, py+7)`.
3. **The independent cross-check.** `game_read` reads `gObjectEvents[0].currentCoords` and the
   comment states the units: *"currentCoords.x (grid, +7)"* (`gamestate.c:129-130`). So the
   player's grid tile is `(px+7, py+7)` from a *different* symbol. Anchors 2 and 3 agree.

Therefore, for host **H** and peer **P** on the same `(mapGroup, mapNum)`, both reading
`SaveBlock1.pos` identically (the MAP_OFFSET bias cancels — `coop-shared-overworld.md` §1a):

```
peer screen tile   = ( 7 + (P.tileX − H.tileX),  5 + (P.tileY − H.tileY) )
peer CELL top-left = (112 + 16·(P.tileX − H.tileX),  80 + 16·(P.tileY − H.tileY) )   [frame px]
```

**A0.2.1 (requirement).** The degenerate case is the cheapest possible regression test and must be
asserted: with `P == H` the formula must yield exactly `(112, 80)`, i.e. the sprite top-left
`(112, 64) == (POP3D_PLAYER_GX, POP3D_PLAYER_GY)`. If a future edit breaks the anchor convention,
this fires before anything is drawn (§A7, TEST P1).

**A0.2.2 (requirement).** `tileX/tileY` must be `SaveBlock1.pos` (`GameState.px/py`,
`gamestate.c:98-99`) for **both** sides — **not** `objX/objY`. Reasons, in order: (a) `px/py` is
the field `build_depth_grid` and the touch BFS already stand on, (b) `objX/objY` come from
`p->mapObjects`, whose FR/LG base `0x02036E38` is *flagged as probably wrong* (`gamestate.c:37`
row; HANDOFF Gotchas; the standing house rule is "flag, don't silently correct"), and (c) mixing
the two spaces silently offsets the avatar by exactly 7 tiles — a bug that looks like a map
problem, not a units problem. The two must differ by exactly 7 when both are valid, which §A6.5
logs as a free consistency column.

### A0.3 — Sub-tile scroll: reuse the field the codebase already verified

`coop-shared-overworld.md` §3 M2 asks for `gSpriteCoordOffsetX/Y`. **We already have an equivalent,
in the profile, in use, and hardware-exercised**: `GameProfile.fieldCamera` — *"gFieldCamera (+0x10
x, +0x14 y = sub-tile scroll) -> 3D depth scroll-align"* (`gamestate.h:68`), read with a ±15 sanity
guard at `main.c:852-855`, and consumed by `warp_grid_eye` as a fractional grid shift
(`main.c:1312`: `float cxf = (float)d->camX / 16.0f`, sampled at `main.c:1316`).

**A0.3.1** The Gen-3 camera follows the player exactly and the map border block fills beyond the
map, so the player sprite is pinned at screen tile (7,5) at all times — which is precisely the
assumption `POP3D_PLAYER_GX/GY` encodes and the 3D work validated on hardware. Consequence: **a
game's own sub-tile walk progress IS its camera sub-scroll.** One field serves both sides.

**A0.3.2 — the sign, derived and then flagged.** `warp_grid_eye` samples the depth field at
`grid + cam/16` (`main.c:1316`), i.e. the BG content shown at screen column `c` is layout column
`px + c + camX/16`. Inverting: a fixed world feature at layout column `Gx` appears at frame
`x = 16·(Gx − px) − camX`. Applying that to both sides:

```
cellX = 112 + 16·(P.tileX − H.tileX) − (H.subX − P.subX)
cellY =  80 + 16·(P.tileY − H.tileY) − (H.subY − P.subY)
```

Self-check: `P == H` ⇒ `(112, 80)` for **any** camera value ✓. Host walks right ⇒ the peer slides
left smoothly ✓. Peer walks right ⇒ `P.tileX` jumps `+1` (`+16 px`) while `P.subX` moves to
compensate, so the sum is continuous ✓ — *provided* the compensation has the sign this derivation
assumes.

**A0.3.3 (requirement).** Ship the sign as a single named constant, defaulted to the derivation
above, with the flip documented in one place:

```c
#define PRES_SUB_SIGN  (-1)   // A0.3.2, derived from warp_grid_eye's +cxf convention (main.c:1316).
                              // VERIFY-ON-HW: if a walking peer JUMPS BACK 16 px at each tile edge
                              // instead of gliding, this is +1. Nothing else changes.
```

That failure mode is unmistakable on hardware in one step of walking, and it is the single most
likely thing to be wrong in the whole draw path. It is on the §A7.2 checklist as item **H2**.

**A0.3.4** If the data half cannot supply `subX/subY` in slice 1 (or `fieldCamera == 0` for a
profile), it passes zeros; the avatar then snaps tile-to-tile. `coop-shared-overworld.md` §4 rates
that *"still usable for M1, ugly for M2"* — acceptable as a slice-1 intermediate, **not**
acceptable as the shipped M2.

### A0.4 — The degradation ladder (binding)

Every one of these is a *silent, graceful* fallback, never a garbage draw (PHASE Invariant 5):

| Condition | Behaviour |
|---|---|
| `live == 0`, or map mismatch, or stale `round` | draw nothing; frame bit-identical to presence-off |
| No profile for either game | presence permanently off (the data half never sets `live`) |
| `facing ∉ {1,2,3,4}` | draw the **DOWN standing** frame (never an out-of-range sheet cell) |
| `gender == 0xFF` | male variant |
| `name[0] == 0xFF` or all bytes undecodable | nameplate suppressed; avatar still drawn |
| Sprite texture failed to build | presence permanently off (§A1.6.2), like `tiltOk == false` |
| Peer cell fully outside the frame | culled (§A2.4) — no clamping-to-edge, no arrow |

---

## A1. THE SPRITE ASSET

PHASE Invariant 7: **our own art, never ripped from the peer's VRAM.**
`coop-shared-overworld.md` §4 judged VRAM ripping *"not worth chasing"* (swapped-VRAM window + CPU
frame DMA) and it is also the cleaner IP posture per `~/.claude/ip-publishing-policy.md`.

### A1.1 — Cell size: 16×32, verified against an existing constant

`main.c:691` says the player's on-screen sprite rect is **16×32 with the head 16 px above the
tile**. That is the in-repo, hardware-exercised measurement of what a Gen-3 overworld character
occupies — the coop doc's 16×32 figure (§3 M2) is confirmed by it rather than merely repeated.

**A1.1 (requirement).** One frame cell = **16 wide × 32 tall**, GBA frame pixels. The bottom 16 px
of the cell are the character's tile; the top 16 px are the head, which overhangs the tile above.
Foot anchor = **cell bottom-centre**, i.e. `(cellX + 8, cellY + 16)` where `(cellX, cellY)` is the
*tile* cell top-left from §A0.2 and the sprite is drawn at `(cellX, cellY − 16)`.

### A1.2 — Frame count and sheet layout: 9 frames, East mirrored

**Decision: 3 poses × 3 facings = 9 authored frames; EAST is WEST drawn horizontally mirrored.**

Justification:

- Gen-3 overworld characters are authored as South / North / West sets with East produced by
  horizontal flip — the decomp's own picture tables carry no separate east frames. *(Stated from
  the decomp's published sheet organisation; there is no pret checkout on this machine today —
  `/tmp/pret` is absent — so this is **not** an address claim and nothing depends on it being
  byte-exact. It is an art-authoring choice we own.)*
- A mirrored East halves the authored art, halves the texture, and guarantees the two horizontal
  facings can never drift apart — the same "single source of truth so a 3D pose can never drift
  from the 2D one" discipline gen1recomp uses (`gen1-render.md` finding 2, last bullet,
  `SpriteRenderer.lua:72-77`).
- Three poses per facing (STAND, STEP_A, STEP_B) is the minimum that reads as walking. Our
  animation *timing* is our own policy (§A2.6.3), explicitly **not** a reproduction of the game's
  animation state — we never read the peer's OAM/animation, and matching it exactly is out of
  scope for this phase.

Sheet layout (one variant):

```
        col 0        col 1        col 2
row 0   S:STAND      S:STEP_A     S:STEP_B      <- facing DOWN  (facing code 1)
row 1   N:STAND      N:STEP_A     N:STEP_B      <- facing UP    (facing code 2)
row 2   W:STAND      W:STEP_A     W:STEP_B      <- facing LEFT  (facing code 3)
                                                   facing RIGHT (code 4) = row 2, mirrored
used extent: 48 x 96
```

**A1.2.1 (requirement).** The facing→row and pose→col tables live in **one** pure-C place
(`presence_art.c`), are `static const`, and are the only thing that maps a facing code to a cell.
`facing` values outside 1..4 select row 0 col 0 (§A0.4).

**A1.2.2** East is drawn by negating the horizontal draw scale, not by a second sheet. With
citro2d that is `C2D_DrawImageAt(img, x + w, y, depth, tint, -sx, sy)` — the negative-X-scale form.
**verify-on-hw / verify-in-Azahar**: confirm citro2d's negative scale mirrors about the draw origin
as assumed; the sanctioned fallback if it does not is to author 3 extra East frames (sheet goes
4 cols) — a pure art change, no code change beyond the col table.

### A1.3 — Gender variant

Two variants (male / female) side by side in one texture, variant stride **64 px**:

```
texture 128 x 128, RGBA8
  variant 0 (male)   at x =  0 .. 47,  y = 0 .. 95
  variant 1 (female) at x = 64 .. 111, y = 0 .. 95
  everything else fully transparent
```

`PeerPresence.gender` (SaveBlock2 +0x08, per `coop-shared-overworld.md` §1b) picks the variant;
`0xFF`/unknown ⇒ variant 0. Size: 128·128·4 = **64 KB**. That is affordable next to phase 14's
~100 KB vertex arena (`SPEC-render` R3.5) and one-eighth of a game texture pair. **A1.3.1**
`GPU_RGBA5551` would halve it and is a legitimate later micro-optimisation, but RGBA8 matches the
existing widget bake (`tools/build_assets.sh:46` uses `tex3ds -f rgba8`) so the placeholder and the
real art share one format.

### A1.4 — The build path for REAL art: **no script change needed**

This is the important finding. `tools/build_assets.sh:52-57` globs every PNG in
`design_handoff_3dgba_ui/assets_3ds/widgets/$THEME/` and, for each, (a) bakes a single-image `.t3x`
into `data/wg_<name>.bin` and (b) appends an `X("<name>", wg_<name>)` line to the generated
`source/assets_gen.h` `ASSET_WIDGETS` list. `assets.c:9-28` turns that X-macro into `extern`
symbols and a load table; `assets_wgt("<name>")` then returns the `C2D_Image` (`assets.c:74`).
The Makefile embeds `data/*.*` automatically (`Makefile:104` `BINFILES`, `:113`/`:115`), so **no
Makefile edit either**.

**A1.4.1 (requirement).** Real art ships as **`peer-walk-m.png`** and **`peer-walk-f.png`**,
128×128 RGBA, dropped into `assets_3ds/widgets/<theme>/`. They are then reachable as
`assets_wgt("peer-walk-m")` with **zero** changes to `build_assets.sh`, `assets_gen.h` (it is
regenerated), `assets.c` or the `Makefile`. Re-run `tools/build_assets.sh` and rebuild.

**A1.4.2 — the one caveat the pipeline imposes.** Widgets are baked **per theme**
(`widgets/{indigo,oled,retro,daylight,duo,custom}/`, `build_assets.sh:14,53`). The avatar is world
content, not chrome, so it must **not** be theme-tinted: ship the *identical* PNG in every theme
directory (a copy step in the art drop, not a code path), or accept per-theme recolours as a
deliberate cosmetic feature. Recommended: identical in all themes. **A1.4.3** Sub-cells are cut
from the single-image sheet with the subtexture arithmetic `assets.c:115-125` (`img_subrect`)
already implements for 9-slice; promote it to a small public helper
`assets_wgt_cell(id, col, row, cw, ch, C2D_Image* out, Tex3DS_SubTexture* st)` rather than writing a
second copy of that math.

### A1.5 — Slice-1 art: a procedurally-generated, UNMISTAKABLE placeholder

Producing finished pixel art is out of scope for an implementing agent, and shipping mediocre art
that *looks* finished is worse than shipping an obvious placeholder. So slice 1 generates the
sheet at runtime, and it is designed so **nobody can mistake it for the final asset**:

**A1.5.1 — the generator.** `source/presence_art.c` / `.h`, **pure C** (`<stdint.h>`/`<string.h>`
only, CLAUDE.md #4 / PHASE Invariant 8), one entry point:

```c
// Fills a LINEAR RGBA8 buffer, 128*128*4 bytes, row-major, with the placeholder walker sheet.
// Pure C: no libctru, no citro3d — host-testable pixel-for-pixel (test_presence TEST P2).
void presence_art_build(uint8_t* rgba8_128x128, uint32_t accentRGBA, uint32_t inkRGBA);
```

**A1.5.2 — what it draws, per cell** (16×32, origin at cell top-left):

| Element | Rect (cell-local) | Colour |
|---|---|---|
| Head | `(5,4)-(11,11)` | `accent` |
| Body | `(4,12)-(12,26)` | `accent`, 60 % value |
| Legs (STAND) | `(5,26)-(7,32)`, `(9,26)-(11,32)` | `ink` |
| Legs (STEP_A) | left leg shifted `−2` in x, right `+1` | `ink` |
| Legs (STEP_B) | mirrored STEP_A | `ink` |
| Facing pip | 3×3 at the facing side of the head (`S` = bottom, `N` = top, `W` = left) | `ink` |
| **Placeholder tell 1** | 1-px outline around the whole silhouette | **magenta `0xFF00FFFF`** |
| **Placeholder tell 2** | 2×2 magenta/black checker in the cell's top-left 4×4 | magenta / black |

The two magenta tells are the point: `#FF00FF` appears nowhere in the theme system
(`source/theme.c`) and nowhere in a Gen-3 overworld palette, so a screenshot instantly says
"placeholder". **A1.5.3 (requirement).** The tells are **not** conditionally compiled and **not**
behind a flag. They disappear only when real art lands, by the art landing.

**A1.5.4 — the drop-in switch.** One line, one place:

```c
// presence_draw.c init:
C2D_Image sheet = assets_ready() ? assets_wgt("peer-walk-m") : (C2D_Image){0};
s_useBakedArt = (sheet.tex != NULL);      // real art present -> use it, placeholder otherwise
```

so dropping the PNGs in (§A1.4) switches the source with no code edit at all. Until then
`s_useBakedArt` is false and the generated texture is used. Both paths hand the *same* cell
geometry (16×32, 3×3 grid, variant stride 64) to the same draw code — the sheet layout in §A1.2 is
the contract, and the host test asserts the cell-rect arithmetic once for both (§A7.1 TEST P3).

### A1.6 — Upload

**A1.6.1** Follow `upload_frame`'s proven recipe (`main.c:614-622`): allocate a `linearAlloc`
staging buffer, fill it with `presence_art_build`, `GSPGPU_FlushDataCache` it, then
`C3D_SyncDisplayTransfer` into a tiled texture created with
`C3D_TexInit(&s_peerTex, 128, 128, GPU_RGBA8)` (the `C3D_TexInit` precedent is `main.c:587`), with
`GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(0)`, then `linearFree` the staging buffer. This
runs **once**, at session init, off the per-frame path. The `FLIP_VERT(0)` + `v = 1 − y/H` UV
convention is inherited verbatim from `upload_frame`/`main.c:1180-1181` so the two agree.

**A1.6.2** Any failure (`linearAlloc` NULL, `C3D_TexInit` false) leaves `s_presenceOk = false` and
presence is off **permanently** for the session — exactly the `warpOk`/`tiltOk` discipline
(`main.c:1010`, `main.c:1136`, `SPEC-render` R3.5.3). Never a partial draw.

**A1.6.3** `C3D_TexSetFilter(&s_peerTex, GPU_NEAREST, GPU_NEAREST)` in the flat case and
`GPU_LINEAR` under tilt, mirroring the project's existing split (`main.c:1244-1248`,
`gen1-render.md` finding 1: warped LINEAR / flat NEAREST). Set it at each draw, because the tilt
block re-sets filters on shared textures (`main.c:1248`).

### A1.7 — Explicitly forbidden

No reading of the peer core's OAM, VRAM, or palette RAM for avatar pixels; no CPU frame DMA of
peer sprite tiles; no re-use of the peer's frame texture as a sprite source. PHASE Invariant 7 and
`coop-shared-overworld.md` §4. (Reading the peer's *OAM rectangles* for our own 3D depth is a
different, already-shipped thing — `main.c:2311-2327` — and is not affected.)

---

## A2. THE DRAW — FLAT CASE

### A2.1 — Where it goes in `main.c`'s per-screen sequence

Today's shipped top-screen (left eye) order, read off `main.c:3065-3076`:

```
3065  render_game(topG, top, ..., tiltTop ? &tiltTL : NULL)      base image (or the tilt mesh)
3067  if (popPass)  warp_grid_eye / warp_scenery_eye ; pop_eye   stereo warp + per-sprite standees
3072  if (dofPass)  dof_bands                                    tilt-shift bands OVER the pops
3073  if (bloomPass) bloom_add                                   additive glow
3074  if (uipop)    ui_pop_eye                                   BG0 panels, hardest pop
3075  if (litPass)  light_pass                                   time-of-day MULTIPLY, LAST
3076  if (!menuOpen) { HUD bar / chips / toast }                 chrome
```

**A2.1 (requirement).** The avatar draws **after `ui_pop_eye` and immediately BEFORE
`light_pass`** — i.e. a new line between `main.c:3074` and `main.c:3075`, and the mirror position
between `main.c:3147` and `main.c:3148` for the right eye.

Three reasons, each mechanical:

1. **After `pop_eye`/`ui_pop_eye`.** Those passes re-draw sub-rects *of the game texture* over the
   frame (`draw_pop_tex`, `main.c:731-747`). Anything drawn before them is overpainted by
   background pixels. Non-negotiable.
2. **After `dof_bands`.** The DoF bands are alpha-ramped blurred copies of the *game* texture
   (`main.c:1383-1404`). Drawn over the avatar they would composite background over it — the
   avatar would ghost out in the blur band rather than blur. Drawn under them nothing is lost,
   because the avatar simply stays sharp there (documented cosmetic limit, §A7.2 item H6).
3. **Before `light_pass`.** `light_pass` is a MULTIPLY grade over the whole frame box
   (`main.c:975`, `main.c:980`) driven by the *terrain* depth field. The avatar is world content,
   so it must receive the time-of-day grade — otherwise a bright avatar floats over a dusk map.
   This is the direct analogue of gen1recomp's per-billboard zone-palette lookup at the foot
   anchor (`gen1-render.md` finding 2, `4113-4128`), obtained for free by ordering instead of by a
   second colour path.

**A2.1.1** All four of `popPass` / `dofPass` / `bloomPass` / `litPass` are already suppressed while
tilt is up (`&& !tiltTop`, `main.c:3031-3044`), so under tilt the avatar naturally lands
immediately after `render_game` with nothing in between. One insertion point serves both cases.

**A2.1.2** The avatar is **never** drawn while `menuOpen` — the pause menu replaces the top screen
with `draw_paused_summary` (`main.c:3132-3134`, `main.c:3149`) and the bottom with the tab plate.
Gate the call on `!menuOpen`, matching the HUD (`main.c:3076`).

### A2.2 — The transform: the SAME one the game image uses

`calc_xform(mode, sW, sH, &ox, &oy, &sx, &sy)` (`main.c:720-726`) is the single screen-fit used by
every world pass — `pop_eye:766`, `ui_pop_eye:1465`, `light_pass:981`, `warp_grid_eye:1310`,
`tilt_draw_image:1210` — and it is what `render_game` reproduces inline for the flat blit
(`main.c:1564-1569`, identical arithmetic).

**A2.2 (requirement).** The avatar draw calls `calc_xform` with the **same** `mode` and screen size
that `render_game` was called with for that screen (`scaleMode[0]` + 400×240 for the top eyes,
`scaleMode[1]` + 320×240 for the bottom), and composes:

```c
float ox, oy, sx, sy; calc_xform(mode, screenW, screenH, &ox, &oy, &sx, &sy);
// frame space (§A0.2 / §A0.3)
float cellX = 112.0f + 16.0f*(P.tileX - H.tileX) + PRES_SUB_SIGN*(float)(H.subX - P.subX);
float cellY =  80.0f + 16.0f*(P.tileY - H.tileY) + PRES_SUB_SIGN*(float)(H.subY - P.subY);
float sprX  = cellX;                 // sprite top-left = cell top-left ...
float sprY  = cellY - 16.0f;         // ... minus the 16 px head margin (A1.1)
// screen space
float X = ox + sprX * sx,  Y = oy + sprY * sy;
```

and draws the 16×32 cell at scale `(sx, sy)` — the *screen fit* scale, never any other. Frame space
first, screen fit afterwards, exactly as `SPEC-render` R1.7 mandates for tilt ("Tilt is computed in
frame space and composed with `calc_xform` afterwards. Never bake the screen rect into the tilt
math"). Same rule, same reason: it keeps the math per-screen-agnostic and host-testable.

**A2.2.1** All three scale modes (`SCALE_1X` / `SCALE_FIT` / `SCALE_STRETCH`, `main.c:625`) work
unmodified because `calc_xform` handles them; `SCALE_STRETCH` gives `sx ≠ sy` and the avatar
stretches with the game image, which is correct — it must share the image's distortion.

### A2.3 — The citro2d call

```c
C2D_SceneBegin(tgt);                              // defensive, matching pop_eye:767 / light_pass:985
C3D_TexSetFilter(&s_peerTex, GPU_NEAREST, GPU_NEAREST);      // A1.6.3 (LINEAR under tilt)
C2D_Image cell; Tex3DS_SubTexture st;
presence_cell_image(&cell, &st, variant, row, col, clip);    // A1.4.3 / A2.4
if (facingRight) C2D_DrawImageAt(cell, X + 16.0f*sx, Y, 0.0f, tint, -sx, sy);   // A1.2.2 mirror
else             C2D_DrawImageAt(cell, X,            Y, 0.0f, tint,  sx, sy);
```

`C2D_DrawImageAt` with a `C2D_ImageTint*` is the same call `render_game` uses for the flat blit
(`main.c:1598`, `main.c:1611`), so the avatar and the game image go through one code path in the
GPU. No raw-C3D escape, no new shader, no new render target — **zero** new GPU state (PHASE
Invariant 6, and `SPEC-render` R3.4.1's "zero new render targets, zero new VRAM").

### A2.4 — Culling and clipping: the avatar never leaves the game rect

**A2.4.1 — cull.** In frame space, reject when the sprite rect `[sprX, sprX+16) × [sprY, sprY+32)`
does not intersect `[0,240) × [0,160)`. A culled peer costs one comparison. No edge-clamping, no
off-screen arrow, no "peer is nearby" indicator in the game rect — the honest ceiling is that you
see them when they are on your screen.

**A2.4.2 — clip.** Partial overlap is trimmed **in source (frame) space** by shrinking the
subtexture, which is exactly what `draw_pop_tex` already does for the stereo pops
(`main.c:733-740`):

```c
// mirrors main.c:733-736 verbatim in structure
if (sprX < 0)              { cw += sprX; su += -sprX; sprX = 0; }
if (sprY < 0)              { ch += sprY; sv += -sprY; sprY = 0; }
if (sprX + cw > GBA_W)     cw = GBA_W - sprX;
if (sprY + ch > GBA_H)     ch = GBA_H - sprY;
if (cw <= 0 || ch <= 0) return;     // fully clipped == culled
```

where `su/sv` advance the source cell origin so the *visible* part of the sprite stays registered.
This guarantees the avatar can never bleed into the letterbox — the same property `draw_pop_tex`'s
comment calls out as mandatory (`main.c:729-730`: *"the destination is clipped to the on-screen
frame box, so a shifted pop never bleeds into the letterbox (per-eye rivalry on the border)"*), and
for the same stereo reason: an avatar visible in one eye and clipped in the other at the frame
border is binocular rivalry.

**A2.4.3** The clip runs identically in the tilt case (§A3.2) — one function, two callers, one host
test.

### A2.5 — Z / draw order versus HUD, menus and overlays

**A2.5.1** The avatar is **world content**: it draws before all chrome. Chrome — HUD bar and chips
(`main.c:3077-3130`), toast (`main.c:3131`), pause summary (`main.c:3133`), gamepad overlay
(`main.c:3194-3196`), and every `ui.c`/`theme.c`/`assets.c` draw — stays **flat, untilted and on
top**, per `SPEC-render` R4.0.2 (*"They are our chrome, not the game world"*). Placing the avatar
at `main.c:3074/3075` puts it under all of it automatically.

**A2.5.2** The nameplate (§A4.4) is chrome attached to world content. It draws **immediately after
the avatar, in the same call**, so it is over the avatar and under the HUD. It is drawn in *screen*
space at a fixed pixel size (not scaled by `sx`), because it must stay legible at `SCALE_1X`; its
anchor is the projected head point, so it rides the world.

**A2.5.3** citro2d has no depth sorting here — everything in this sequence relies on submission
order and `C3D_DepthTest(false, ...)` (`main.c:1257`). Order *is* the z. Do not add a depth value;
`C2D_DrawImageAt`'s `depth` argument stays `0.0f` like every other call in this file.

### A2.6 — Tint, focus, and the walk phase

**A2.6.1 — dim tint.** The unfocused screen's game image is dimmed (`dimTint`, `main.c:2946-2949`).
The avatar must share it or it will glow on the dimmed screen: pass the **same** `topTint`/`botTint`
pointer the matching `render_game` call received (`main.c:3065`, `main.c:3165`). Under tilt
`render_game` carries the dim as a *vertex colour* instead (`TiltDraw.mod`, `main.c:3050`,
`main.c:3163`), but the avatar is a citro2d draw either way, so it keeps using the `C2D_ImageTint`
in both cases — the two produce the same 0.5-toward-black result and the avatar is the only thing
that needs them to agree.

**A2.6.2** The right eye passes `NULL` for the tint (`main.c:3138`) — the avatar must mirror that
exactly, or the two eyes differ in brightness and the parallax barrier shows it.

**A2.6.3 — the walk phase (pure C, ours).** Frame selection is driven by **accumulated world
travel**, not by any peer animation state:

```
travel += |Δ world px| this frame        (world px = 16*tile + sub, §A0.3.2)
if travel unchanged for PRES_IDLE_FRAMES (=6) -> pose = STAND, travel := 0
else pose = PRES_WALK_CYCLE[(int)(travel / 4) & 3]      // 4 px per beat
PRES_WALK_CYCLE[4] = { STEP_A, STAND, STEP_B, STAND }   // classic 4-beat from a 3-frame sheet
```

Rationale: a 16-px tile step therefore plays a full 4-beat cycle, which is the natural cadence for
a one-tile-per-~8-frame walk, and it degrades correctly when `sub` is unavailable (§A0.3.4) —
travel then jumps 16 px at once and the pose advances a full cycle, reading as a stiff step rather
than a freeze. The tables live in one `static const` place (§A1.2.1). This is **our** animation
policy; we never claim it reproduces the game's timing.

**A2.6.4** The phase state is per peer slot and lives in the presence module, not in `main.c`.
It resets on `live` 0→1, on map change, and on a `round` gap (staleness) so a peer that walks
off-screen and back does not resume mid-stride from a stale accumulator.

### A2.7 — Bottom screen

The bottom screen's sequence is `render_game` (`main.c:3165`) → HUD (`main.c:3167`) → touch overlay
(`main.c:3194`). None of the pop/DoF/bloom/light passes run there. **A2.7 (requirement).** The
avatar draws immediately after `main.c:3166` (the `render_game` call's closing line) and before the
`if (!menuOpen)` HUD block, with `mode = scaleMode[1]`, `screenW = 320`, `screenH = 240`.

**A2.7.1** On the bottom screen the *peer* is whichever game is on the **top** screen — the roles
invert. The data half supplies one `PeerPresence` per screen (index = SCREEN, 0 = top, 1 = bottom),
mirroring exactly how `TiltSnap tiltSnap[2]` is indexed (`main.c:1125`, `main.c:2375-2384`, I1.8's
"per SCREEN, never game slot, because `swapped` maps games to screens"). The renderer never
resolves `swapped` itself.

**A2.7.2** Smart touch draws its own diagnostics on the bottom screen (`main.c:3197+`). No
interaction: those are chrome, drawn after. But note the avatar sits *under* a tapped location —
tapping "on" the peer avatar does nothing and must do nothing (smart touch pathfinds on the real
game UI; the avatar is not in the game's world). Documented, not fixed (§A7.2 item H8).

### A2.8 — Stereo: the avatar gets NO disparity of its own in this phase

The top screen renders three game images (left eye `main.c:3065`, right eye `main.c:3138`, plus the
bottom). Today the stereo effect comes from `pop_eye`'s per-sprite standee shifts, which are
computed from the *game's own OAM* (`main.c:2311-2327`) — the avatar is not in that set and cannot
be, because it is not in the game's frame.

**A2.8 (requirement).** Slice 1 draws the avatar at the **same frame-space position in both eyes**
(zero disparity), i.e. it sits exactly on the screen plane. This is deliberate:

- It is safe. `POP_DISP_MAX = 6.5f` (`main.c:705`) is a *hardware-validated comfort ceiling*, and
  `SPEC-render` R4.1.1 is emphatic that it "does not get quietly relaxed by a rendering change".
- It is honest. A zero-disparity avatar reads as a flat overlay, which is what it is.
- The upgrade is fully specified and one line: give the avatar the same disparity the game's own
  sprites get at its feet — `clamp_disp(RAMP_AT(fy) + floor_at(&depth3d, cx, fy))` with the
  standee lean `POP3D_STANDUP` toward the head, i.e. literally `pop_eye`'s formula
  (`main.c:777-784`), applied as `±slider3d * disp / sx` in frame space. Under tilt it must
  additionally be re-clamped by `tilt_disp_scale(v, cy)` (`tilt.h:151`, R4.1.1).
- It is deferred because the first hardware question is whether the avatar reads *at all*
  (§A7.2 H1), and adding an unproven disparity to an unproven overlay makes a bad photo
  un-diagnosable.

**A2.8.1** Because tilt and stereo are mutually exclusive by gate rule G10 (`main.c:2997`,
SPEC-integration I5.6), the tilted avatar never needs a disparity story at all in the shipped
configuration.

---

## A3. THE DRAW — TILTED CASE

This is the phase-14 composition, and it is `gen1-render.md` finding 2 applied verbatim.

### A3.1 — The rule: project the FOOT ANCHOR, translate the upright sprite, never scale it

`gen1-render.md` finding 2 (`OverworldController.lua:4144-4157`):

```
sx, sy = Tilt.groundPoint(fx, fy, vw, vh)
translate(sx - fx, sy - fy); drawFn()          -- PURE TRANSLATION
```

with *"the sprite is UPRIGHT and UNSCALED — 'depthScale is deliberately ignored for sizing...
pixel-identical to flat mode'. Only the anchor moves."* And the foot anchor is the *baseline centre
of the cell* with the art drawn 16 px above it — which is exactly §A1.1's anchor convention, and
exactly what `POP3D_PLAYER_GX/GY` already encodes (§A0.2 anchor 1). Two independent sources, same
geometry.

**A3.1 (requirement).** When `tilt_active()` is true for that screen (`main.c:3018-3019`):

```c
// 1. foot anchor in FRAME space (flat)
float ax = cellX + 8.0f, ay = cellY + 16.0f;            // A1.1
// 2. project it through the SHIPPED tilt view — tilt.h:134, no new math anywhere
float fax, fay, q;
tilt_project(tv, ax, ay, &fax, &fay, &q);               // tv = &tiltVw (top) / &tiltVwB (bottom)
// 3. PURE TRANSLATION of the whole upright sprite (q is DISCARDED for sizing)
float dx = fax - ax, dy = fay - ay;
float X = ox + (sprX + dx) * sx,  Y = oy + (sprY + dy) * sy;
// 4. draw exactly as in the flat case, at the SAME (sx, sy)
```

**A3.1.1** `q` is read and thrown away. Do not scale by `q`, by `k·q`, or by `tilt_disp_scale`.
`gen1-render.md` finding 2 states the reason ("pixel-identical to flat mode"); the practical one is
that any non-integer scale on 16×32 pixel art turns it to mush, which is the single most visible
way to make this look broken.

**A3.1.2** The screen fit `(ox, oy, sx, sy)` is applied **after** the translation and is the same
`calc_xform` result as the flat case — gen1's zoom-composition rule (`gen1-render.md` finding 2,
*"zoom is a plain post scale/translate applied identically to the ground mesh and the upright canvas
blit — so anchors compose for free"*) and `SPEC-render` R1.7.

**A3.1.3** The function this spec names is **`tilt_project`** (`source/tilt.h:134`). It is the only
tilt entry point presence calls. `presence_draw.c` must not `#include <math.h>` for trig, must not
reference `TILT_FOCAL`, `tilt_cover_*`, `tilt_view_init` or the angle ladder, and must not compute
`sin`/`cos` of anything. PHASE Invariant 4: *"Read phase 14's `tilt.h` and use it; do not duplicate
the math."* A grep for `sinf|cosf|TILT_FOCAL` in `presence_*.c` returning nothing is the check.

### A3.2 — Exact call sequence, and the clip under tilt

```c
// presence_draw_screen(tgt, screen, mode, screenW, screenH, tv /*NULL = flat*/, tint, ...)
if (!s_presenceOk || !peer->live) return;
float ox, oy, sx, sy; calc_xform(mode, screenW, screenH, &ox, &oy, &sx, &sy);
float cellX, cellY;  presence_cell_frame(host, peer, &cellX, &cellY);       // pure C, A0.2/A0.3
float sprX = cellX, sprY = cellY - 16.0f;
PresClip c; if (!presence_clip(&sprX, &sprY, &c)) return;                   // pure C, A2.4 (SOURCE space)
float dx = 0.0f, dy = 0.0f;
if (tv) { float ax = cellX + 8.0f, ay = cellY + 16.0f, fax, fay, q;
          tilt_project(tv, ax, ay, &fax, &fay, &q); dx = fax - ax; dy = fay - ay; }
C2D_SceneBegin(tgt);
C3D_TexSetFilter(&s_peerTex, tv ? GPU_LINEAR : GPU_NEAREST, tv ? GPU_LINEAR : GPU_NEAREST);
/* ... build C2D_Image from (variant,row,col,c) ... */
C2D_DrawImageAt(cell, ox + (sprX + dx)*sx, oy + (sprY + dy)*sy, 0.0f, tint,
                mirror ? -sx : sx, sy);
presence_nameplate(...);                                                    // A4.4, screen space
```

**A3.2.1 — clipping under tilt (decided, with its cost stated).** The clip runs on the
**unprojected** source rect (§A2.4.2) and the *trimmed* rect is then translated. Because the
transform is a pure translation, a trimmed rectangle stays a rectangle and citro2d can draw it —
that is the whole reason this composition needs no scissor and no raw-C3D. **Cost:** near the frame
edges the avatar's clip boundary is the *flat* frame rect drawn at a translated position, so it can
be trimmed slightly inside or outside the tilted image's own edge. That is acceptable because the
tilted image itself already spills past the frame rect: phase 14 shipped `TILT_SCISSOR 0`
(`main.c:1076-1078`, the sanctioned R3.6.2 "spill" fallback), so there is no rectangular boundary
to be faithful to. **verify-on-hw** (§A7.2 item H5).

**A3.2.2 — the tween.** `tv` is `&tiltVw` / `&tiltVwB`, which `main.c:3021-3024` rebuilds every
frame from `tiltTw[sc].ang` — the **live tween angle**, not the target. Passing that pointer is
therefore sufficient and correct by construction; the avatar's anchor follows the 250 ms tween
because the `TiltView` it reads *is* the tween. **A3.2.3 (requirement).** Do not cache a
`TiltView`, do not recompute one, and do not read `tiltTw[].angTo`. A cached view would make the
avatar slide against the ground during every tween — the exact defect PHASE Invariant 4 is about.

**A3.2.4** `tv == NULL` ⇔ `tilt_active()` false for that screen ⇔ `dx = dy = 0` ⇔ the flat path,
byte-identical. Phase 14's Invariant 1 shape, reused.

### A3.3 — citro2d, not raw C3D — and why that is not laziness

**Decision: the avatar stays a citro2d `C2D_DrawImageAt`. It does NOT join phase 14's raw-C3D
block.**

1. **A billboard needs no perspective divide.** The tilt block exists to emit a per-vertex clip
   `w = 1/q` so the fixed-function rasterizer reconstructs the projective texture map
   (`SPEC-render` R1.3, `main.c:1230-1235`). A billboard is *pure translation of an upright quad*
   (`gen1-render.md` finding 2) — its four corners all share one anchor, so `q` is constant across
   it and the divide is the identity. There is literally nothing for the tilt shader to do.
2. **Joining the block would cost more than it saves.** `SPEC-render` R3.3's whole argument is that
   *one* escape per game image beats several; the avatar entering the block means the block can no
   longer end where `render_game` ends it (`main.c:1279-1280`, `C2D_Prepare` + `C2D_SceneBegin`),
   so either `render_game` grows a presence parameter and an ordering dependency on the passes at
   `main.c:3067-3075`, or the block is re-opened later — one *extra* escape, which is the thing
   R3.3 optimises away.
3. **It keeps `render_game`'s contract intact.** R3.1.4 requires the tilt path to leave the screen
   bound so every existing HUD/menu draw is untouched; the avatar drawing as an ordinary citro2d
   sprite onto that bound scene is precisely the case that contract was written for.
4. **R4.0.1 is satisfied, not bypassed.** That rule forbids drawing a *not-tilt-aware* pass flat
   over a tilted base. The avatar **is** tilt-aware — its anchor rides the ground plane through
   `tilt_project`. It composes; it is not excluded.

**A3.3.1** If a future slice ever needs the avatar *warped* (it should not — see A3.1.1), the
escalation is to add it to the tilt block as 4 verts in the existing arena
(`TILT_SLAB_VERTS 908` already budgets headroom, `main.c:1093`), not to invent a second escape.

### A3.4 — Y-sorting (relevant the moment there is more than one peer)

`gen1-render.md` finding 2: *"ONE y-sorted list of all billboards keyed on baseline world y
('farther rows project higher/smaller, so back-to-front is just ascending baseline y')."*

**A3.4 (requirement).** The draw is structured **now** as a loop over `PeerPresence peers[PRES_MAX]`
(`PRES_MAX = 1` this phase; 3 when the flagged 3-4-player work lands — HANDOFF Next steps #3),
sorted **ascending by foot `fy`** — the *projected* foot y under tilt, the flat `ay` otherwise —
with ties broken by `tid` so the order is deterministic frame to frame (a flickering tie-break
reads as z-fighting). The comparator is pure C and host-tested (§A7.1 TEST P6) even though a
one-element sort is a no-op today: writing it later means retrofitting order into a shipped draw.

**A3.4.1** The **host player is not in the list.** The game draws its own avatar; ours is an
overlay for peers only. A host-vs-peer y-sort would require cutting the host's sprite out of the
composited frame, which is exactly the hole-leaving operation `gen1-render.md` finding 8 §5 rules
out ("cutting `DepthSnap.spr[]` rects leaves holes with no backing data"). Consequence, stated in
the honest ceiling: **a peer standing on the tile in front of you draws over your own sprite
regardless of who is in front.** Cosmetic, inherent, documented — not a bug to be fixed by tweaking.

### A3.5 — What must never happen

| Forbidden | Why |
|---|---|
| Scaling the sprite by `q`, `k·q`, or `tilt_disp_scale` | `gen1-render.md` finding 2; mushy pixel art |
| Rotating or shearing the sprite | it is upright by definition (billboard) |
| Re-deriving `sin`/`cos`/`kfit`/`yc` in `presence_*.c` | PHASE Invariant 4; `tilt.h` is the API |
| Caching a `TiltView` across frames | breaks the tween (A3.2.3) |
| Drawing the avatar when `tiltOk == false` but `tilt_active()` true | impossible by construction (`main.c:2978`), but do not add a second check that could disagree |

---

## A4. IDENTITY READOUT (M3, first half)

### A4.1 — There is no charmap decoder in this codebase; here is the one to add

Grep result (`grep -rn "charmap\|decode_name\|to_ascii" source/ test/`): the GBA character set is
*produced and copied* in three places — `celiolink_payloads.h:35` (`name[8]`, "GBA-charset,
0xFF-terminated"), `celiolink_payloads.h:98-102` (`cl_make_trainer_card` writes literal charmap
bytes), `celiolink.c:734` — but it is **never decoded**. So presence adds the first decoder.

**A4.1 (requirement).** New pure-C module `source/gbatext.c` / `.h`, header-free
(`<stdint.h>`/`<string.h>` only), host-tested:

```c
// Decode `n` GBA-charmap bytes (0xFF-terminated) into a NUL-terminated UTF-8 C string.
// Returns the number of characters written (excluding the NUL). `out` must hold >= 3*n+1 bytes
// (worst case: every byte maps to a 2-byte UTF-8 gender sign plus the NUL).
// Unknown/unmapped bytes decode to '?'. NEVER indexes outside the table. NEVER writes past cap.
int gbatext_decode(const uint8_t* src, int n, char* out, int cap);
```

It is deliberately **not** placed in `gamestate.c` (which is game-RAM-shaped and links against
`gbacore`) nor in `celiolink_payloads.h` (which is the wire-format header): it is a pure text
utility that both the presence card and any future trainer-card viewer can share.

### A4.2 — The table, and the in-repo anchor that verifies it

| Range | Meaning |
|---|---|
| `0x00` | space |
| `0xA1..0xAA` | `0`..`9` |
| `0xAB` `0xAC` `0xAD` `0xAE` | `!` `?` `.` `-` |
| `0xB4` | `'` (apostrophe) |
| `0xB5` `0xB6` | `♂` `♀` (UTF-8 `♂` / `♀`) |
| `0xB8` `0xBA` | `,` `/` |
| `0xBB..0xD4` | `A`..`Z` |
| `0xD5..0xEE` | `a`..`z` |
| `0xFF` | terminator |
| anything else | `?` |

**A4.2.1 — the verification anchor (in-repo, not from memory).** `celiolink_payloads.h:98-100` and
`:130-133` ship Celio's canned demo identity with the name bytes `0xC8 0xDD 0xE0 0xE7 0xFF`.
Decoding under the table above:

```
0xC8 - 0xBB = 13 -> 'N'
0xDD - 0xD5 =  8 -> 'i'
0xE0 - 0xD5 = 11 -> 'l'
0xE7 - 0xD5 = 18 -> 's'
0xFF             -> terminator
                 => "Nils"
```

which is Celio-Link's demo trainer name — a real, in-repo, four-character round trip that pins the
two letter ranges and the terminator simultaneously. **A7.1 TEST P7 asserts exactly this.** It is
the only part of the table with an in-repo witness.

**A4.2.2 (house rule).** The punctuation rows (`0xAB..0xBA`) have **no in-repo witness** and are
transcribed from the decomp's published `charmap.txt`, which is not readable on this machine (no
pret checkout — `/tmp/pret` is absent as of 2026-08-04). They are therefore marked
**verify-on-hw-pending** in the source comment, and the decoder's unknown-byte behaviour (`'?'`)
makes a wrong entry a cosmetic `?` in one glyph, never an out-of-range read. Letters and digits
carry the anchor above; a player name is overwhelmingly letters.

**A4.2.3** The table is a `static const char* const [256]` of short UTF-8 strings (so `♂`/`♀` and
ASCII share one lookup), sized 256 exactly so no index can escape it. No arithmetic ranges at
runtime — the table is the code.

### A4.3 — Trainer ID and gender

From `coop-shared-overworld.md` §1b, which cites the decomp directly:

| Field | Read | Provenance |
|---|---|---|
| name | `sb2 + 0x00`, 8 bytes, `0xFF`-terminated | §1b |
| gender | `read8(sb2 + 0x08)`, 0 = M, 1 = F | §1b |
| visible TID | `read16(sb2 + 0x0A)` | §1b, *"proven: `trainer_card.c:722` computes exactly this LE u16"* |

**A4.3.1** These reads belong to the **data half** (`sb2ptr` is M0's `GameProfile` addition and
does not exist today — `gamestate.h:23-89` has no `sb2ptr`). This spec consumes `PeerPresence.tid`,
`.gender` and the raw `.name[8]`, and specifies only their *presentation*. The data half must mark
`sb2ptr` verified vs verify-on-hw-pending per game code, per the standing house rule.

**A4.3.2** Display TID as `%05u` — the five-digit form a Gen-3 trainer card shows. Do **not**
display the secret ID (`sb2 + 0x0C`); it is not on the trainer card and surfacing it is a save-data
leak with no player value.

### A4.4 — Where identity surfaces: nameplate **and** HUD line **and** card

Three surfaces, three jobs, all built from existing helpers — no parallel text path
(`ui.h:17-27`, `assets.h:31-44`):

**A4.4.1 — Nameplate (over the avatar).** A filled pill via `ui_chip_fill(buf, name, x, y, bg, fg)`
(`ui.c:55`), centred on the avatar's **head** point and clamped to the screen:

```
head point (frame) = (cellX + 8, cellY - 16 + dy_tilt)      // top-centre of the sprite cell
screen             = (ox + headX*sx, oy + headY*sy)
plate              = ui_chip_fill(buf, decodedName, screenX - w/2, screenY - 13, THEME_HUD_BAR, g_ui.acc)
```

Fixed pixel size (not multiplied by `sx`) so it stays legible at `SCALE_1X`; clamped so the pill
never leaves the *screen* (unlike the sprite, which is clamped to the *frame rect* — the plate is
chrome, §A2.5.2). **A4.4.2** The plate is suppressed when the name is empty/undecodable (§A0.4) and
when `hudMode` hides that screen's HUD (`main.c:3077` / `main.c:3168`) — one user control, both
surfaces.

**A4.4.3 — HUD line (the M1 deliverable, and the thing that proves the data before art exists).**
A chip in the existing top HUD chip row: `CO-OP` (§A6.4), plus, while a peer is live, one compact
readout appended to the toast/HUD line in the existing `snprintf` style
(`main.c:3096-3105`'s right-to-left flow):

```
"PEER <name> (<x>,<y>)"      // e.g.  PEER Nils (14,9)
```

This is `coop-shared-overworld.md` §3 M1's "HUD line naming the peer + their tile", verbatim, and
it must work **before** any sprite exists — it is the slice-1 acceptance test.

**A4.4.4 — The Card panel** (raised by §A5): a top-screen overlay in the language of
`draw_paused_summary` (`main.c:1888-1926`) — `assets_fill9("fill-card-r8", ...)` for the plate
(`assets.h:46`), `assets_text` / `assets_text_c` with `FNT_SG_BOLD` for the name and `FNT_JBM_BOLD`
for the values (`assets.h:31-33`, roles per `build_assets.sh:27-30`), `ui_border` for the frame.
Contents:

```
   ┌──────────────────────────────┐
   │  NILS                    ♂   │   FNT_SG_BOLD 14px + gender glyph
   │  ID  01234                   │   FNT_JBM_BOLD 10px
   │  MAP  3-12   TILE  14,9      │   FNT_JBM_MED 8px
   │  same map · read-only        │   FNT_JBM_MED 8px, g_ui.dim
   └──────────────────────────────┘
```

**A4.4.5** The card is a **pure read** — no link, no writes, no state machine (PHASE Invariant 1;
`coop-shared-overworld.md` §1b: *"See ID / name / gender — FEASIBLE"*). That is precisely why it is
in scope while trade/battle is not (§A5.5).

### A4.5 — Text path discipline

`ui_text*` (shared system font, `ui.h:17-20`) is used for the HUD-scale readouts because the HUD
already uses it (`main.c:3098`, `main.c:3101`); `assets_text*` (baked bcfnt, `assets.h:31-34`) is
used for the Card panel because every other panel does (`main.c:3223` uses
`assets_text_r(txtBuf, FNT_JBM_MED, ...)`). Both take the **shared frame `C2D_TextBuf`** (`txtBuf`)
already threaded through `run_session`. **A4.5.1** Do not allocate a `C2D_TextBuf`, do not call
`C2D_TextBufNew` per frame, do not add a font. **A4.5.2** The decoded name is cached in the
presence module and re-decoded only when the raw 8 bytes change — decoding is cheap, but a
`C2D_TextFontParse` per frame per surface is not, and this runs on the thread that drives the
`LightEvent` handshake with two saturated workers.

---

## A5. INTERACTION TRIGGER (M3, second half)

### A5.1 — Adjacency

Pure C, in the presence module:

```c
// Tile-space adjacency in the SAME space as A0.2 (SaveBlock1.pos), same map already guaranteed.
int dx = peer->tileX - host->tileX, dy = peer->tileY - host->tileY;
int adjacent = (dx*dx + dy*dy) == 1;        // exactly one of |dx|,|dy| is 1, the other 0
```

Diagonals are **not** adjacent — Gen-3 characters cannot face diagonally, so a diagonal pair can
never satisfy §A5.2 anyway; excluding it here keeps the predicate a single expression.

### A5.2 — Facing each other

Facing codes are `1=D 2=U 3=L 4=R` (`gamestate.h:119`, `diag.h`'s `face` column comment). In tile
space, **D is +y, U is −y, L is −x, R is +x** (screen-down = increasing row, from §A0.2's
`gy = py + r + 2`).

```c
static int dir_from_delta(int dx, int dy) {      // -> 1..4, or 0 if not a unit step
    if (dx ==  1 && dy == 0) return 4;           // R
    if (dx == -1 && dy == 0) return 3;           // L
    if (dx == 0 && dy ==  1) return 1;           // D
    if (dx == 0 && dy == -1) return 2;           // U
    return 0;
}
int facingOk = adjacent
            && host->facing == dir_from_delta(dx, dy)
            && peer->facing == dir_from_delta(-dx, -dy);
```

**A5.2.1 — the honest caveat.** `GameState.facing` is read at `gamestate.c:131` with the comment
*"facingDirection — offset verify-on-hw (FR/LG base also suspect)"*, and the FR/LG `mapObjects` base
`0x02036E38` is separately flagged as probably wrong (memory: *"likely should be ~0x02037038 —
verify-on-hw, don't silently change"*). Therefore:

- **The avatar's position never depends on `facing`** (§A0.2.2 uses `SaveBlock1.pos`), so a wrong
  facing costs a wrong sprite direction, never a wrong location.
- **The meeting predicate degrades to adjacency-only** when either side's facing is outside 1..4:
  `facingOk` becomes `adjacent` and the prompt says so. Better a slightly eager prompt than a
  feature that silently never fires on FireRed.
- Whether facing is correct on each game is a **hardware** question (§A7.2 item **H4**), and it is
  cheap to answer: walk in a circle and watch the avatar's sprite direction.

### A5.3 — The A-press: presence NEVER touches input

**A5.3 (requirement, absolute).** The presence path does not consume, swallow, remap, or synthesise
a single key. It **observes** the already-assembled key state.

- The 3DS→GBA mapping is `to_gba_keys` (`main.c:599-612`); A/B/START/SELECT/L/R/D-pad/C-pad are all
  forwarded to a game. The only unmapped buttons are X (screen swap, `main.c:2247`), Y (focus /
  link experiment, `main.c:2232`), ZL (filter, `main.c:2261`) and ZR (scale, `main.c:2254`) — **all
  four are taken**. There is no free button.
- So the trigger is **A on the focused game**, read as an edge from `kDown & KEY_A`, and it is
  **also** delivered to the game exactly as today. That is safe: in Gen 3, pressing A while facing
  an empty tile does nothing, and the avatar's tile *is* empty as far as the engine is concerned
  (the honest ceiling — the engine does not know it exists). If the player happens to be facing a
  real sign or NPC, the game opens a textbox, `ctx`/`textDlg` changes, and the presence gate closes
  on its own — the correct behaviour, obtained for free.

**A5.3.1** Rationale for not intercepting: PHASE Invariant 1 is about not corrupting the games, and
swallowing a keypress is the input-side version of the same promise. It is also what makes this
feature impossible to blame for a gameplay bug.

### A5.4 — The prompt and the Card

**A5.4.1 — Passive prompt.** While `meetingOk` holds (§A5.1/§A5.2) and the avatar is on screen, a
chip appears just below the nameplate: `ui_chip(buf, "Ⓐ CARD", x, y, g_ui.acc)` (`ui.c:47`),
same visual language as the HUD chips. No keypress needed to see it — the player learns the
interaction by walking into it.

**A5.4.2 — A opens the Card** (§A4.4.4) on the **top** screen, over the game image and under the
HUD, as a `!menuOpen` overlay. It closes on: another A/B edge, `meetingOk` going false (either
player walks away or turns), `ctx` leaving the overworld, the pause menu opening, or presence being
switched off. Every one of those is already computed elsewhere in the frame; the card owns no
timer and no state machine beyond one `bool`.

**A5.4.3** The card is drawn from the *same* `PeerPresence` the avatar uses — no extra RAM read, no
second parked-window pass. `coop-shared-overworld.md` §3 M3: *"'Card' shows correct peer TID/name
immediately (read-only, safe)."*

**A5.4.4** The prompt and the card are suppressed entirely when the presence setting is off, and
when `hudMode` hides that screen's chrome, per §A4.4.2.

### A5.5 — Trade / battle: **NOT IMPLEMENTED IN THIS PHASE.** Design only.

> **STOP.** Everything in §A5.5 is a *design record for a later phase*. Nothing in it is buildable
> in phase 15, and an implementer who starts building it is violating a phase invariant, not
> taking an initiative. Read A5.5.1 before A5.5.2.

**A5.5.1 — Why it cannot be built now. Four independent blockers, any one of them sufficient:**

1. **PHASE Invariant 1 forbids the write.** *"Read-only with respect to both games. Presence never
   writes emulated RAM."* Every candidate route — setting `gSpecialVar_0x8004` so
   `TryTradeLinkup`/`TryBattleLinkup` runs, or warping both games into the Union Room — is a
   **write** to emulated RAM. The invariant is stated as absolute and as *the reason this design
   was chosen over injection*. This blocker is a rule, not a risk, and it does not have a
   workaround.
2. **A trade is a live link state machine, not a callable function.** `coop-shared-overworld.md`
   §1b, hard-corrected across all three of its source studies: it requires a live link with
   `GetLinkPlayerCount() >= 2`, populated `gLinkPlayers[]`, matching `gLinkType`, both games idle in
   the overworld, and a real block exchange over `gBlockSend/RecvBuffer`. PHASE "Decisions already
   made" repeats it: *"never a forced `callback2`"*. Poking `callback2` skips the linkup task that
   establishes `gLinkPlayers[]` ⇒ **near-certain crash**.
3. **The core cannot make the call even if we wanted to.** `gbacore.h:119-124` exposes
   `read8/16/32` and `write8/16` — **no `write32`, no ROM-call trampoline, no way to invoke a
   `static` ROM function.** Every driver function the native path needs is `static` in the decomp
   (`coop-shared-overworld.md` §2).
4. **The link path is frozen.** The wireless trade is hardware-validated and **HW run #13 is still
   pending** (memory: *"3DGBA run #12 FIXED + BUILT ... awaiting HW run #13"*). PHASE Scope—OUT
   defers even the *presence beacon* (M4) for exactly this reason. Reaching into the trade path from
   a cosmetic overlay phase is the worst possible time to touch it.

**A5.5.2 — What the later design looks like** (recorded so it is not re-derived, and so the profile
work can be costed):

- **Preferred route (path a):** warp both games into the **real Union Room** via the game's own
  specials (`RunUnionRoom` / `TryBecomeLinkLeader` / `TryJoinLinkGroup`, listed in the decomp's
  `data/specials.inc`), where every map-coordinate assumption `TryInteractWithUnionRoomMember`
  makes is already satisfied, and let the existing emulated SIO link carry the trade.
  `coop-shared-overworld.md` §2 and §4 both name this as the safe one.
- **Experimental route (path b):** set the field special's variable `gSpecialVar_0x8004` and let
  `TryTradeLinkup`/`TryBattleLinkup` run from arbitrary route coordinates. §4 calls this
  *"the fragile sub-path"* — map-state assumptions the cable-club seat normally guarantees are not
  met on a route tile.
- **Symbols that must first be added to `GameProfile`** (none exist today — checked against
  `gamestate.h:23-89`): `gSaveBlock2Ptr` (M0 adds it), `gLinkType`, `gSpecialVar_0x8004`,
  `gBlockSendBuffer`, `gBlockRecvBuffer`, `gLinkPlayers`, and the Union-Room / linkup special
  entries — **per game code, byte-verified, each marked verified vs verify-on-hw-pending**.
  (`gLinkType` is partially known already: memory records EM `0x020229c6` / FR `0x0202271a` for
  auto-mode, which is a starting point, not a verification.)
- **Battle specifically is a different tier.** Memory + `docs/kb/wireless-strategy.md`: battles are
  the **input-sync / mirrored-pair tier**, not the local-termination tier — Celio has no battle
  synthesis and the observed failure ("chose differently") is a `linkType` mismatch, i.e.
  expected-unsupported. A "Battle" button on this card would promise something the whole link
  strategy does not yet deliver.

**A5.5.3 (requirement).** The Card panel in this phase shows **`Card` only**. It must not render a
greyed-out "Trade" or "Battle" affordance, because a greyed button reads as "coming in the next
build" and this is not that. One dim line of body text is the sanctioned disclosure:

```
   trade & battle use the game's own Union Room — not from here
```

---

## A6. UI / SETTINGS / DIAGNOSTICS

### A6.1 — The setting: it goes on the **LINK** tab, because ENHANCE is measurably full

Measured against the shipped layout (`main.c:1778-1781`), the ENHANCE plate is out of room:

| Row | Widget | Rect |
|---|---|---|
| 1-5 | `ACT_3D`, `ACT_DOF`, `ACT_BLOOM`, `ACT_LIGHT`, `ACT_VIVID` toggles | `x276`, `y 51/83/112/141/170`, `34×18` |
| 6 | `ACT_TILT` segment (phase 14) | `x140 y198 170×26` → bottom edge **224** |
| — | status hint | `y231` |

Phase 14's own comment (`main.c:1771-1777`) states it took the last empty band: *"the five baked
toggles end at y=170+18=188 and the pause-bot-enhance plate bakes NOTHING below y~190 ... bottom
edge 224 (7 px above the hint)"*. There are **7 px** left. A presence row does not fit, and phase
14's open question **O6** (*"ENHANCE tab room ... a 6th row needs new plate art"*) is already
outstanding.

**A6.1 (decision).** Presence goes on the **LINK** tab (`PT_LINK`, `main.c:1782-1785`), which has a
genuinely empty band: its last widget is `ACT_LOADSAV` at `y156 h31` → bottom edge **187**, and the
hint is at `y231` — a **44 px** gap.

```c
static const PCtl PT_LINK[] = {
  {PK_TOG,ACT_LINK,0, 276,16,34,18,0},{PK_TOG,ACT_NETLINK,0, 276,44,34,18,0},
  {PK_BTN,ACT_WIRELESS,0, 93,74,216,35,0},{PK_BTN,ACT_SAVEST,0, 93,118,104,31,0},
  {PK_BTN,ACT_LOADST,0, 204,118,104,31,0},{PK_BTN,ACT_LOADSAV,0, 93,156,216,31,0},
  {PK_TOG,ACT_PRESENCE,0, 276,196,34,18,"CO-OP"} };          // phase 15 — A6.1
```

x276/w34/h18 is the exact toggle geometry the other two toggles on this tab already use, so the
column stays aligned; bottom edge 214 clears the hint by 17 px; the `ov` label `"CO-OP"` rides the
overlay mechanism that exists for precisely this (`main.c:3223`:
`assets_text_r(txtBuf, FNT_JBM_MED, c->ov, x - 6.0f, ...)` — right-aligned ending at x=270), with
precedent on three tabs (DISPLAY "Swap"/"Skip", TOUCH "EDGES", ENHANCE "TILT"). **No plate art
changes; no existing row moves.**

**A6.1.1 (requirement, the silent trap).** `PTABN[4]` must go **6 → 7** (`main.c:1793`). Phase 14's
comment names this exact failure: *"Forgetting this is SILENT — the row would never draw (the draw
loop is `for (i < nPd)`) and the touch hit-test loop would never reach it."*

**A6.1.2** Also add `PUSH(W_TOGGLE, ACT_PRESENCE, 0, 0);` to `menu_layout`'s LINK case
(`main.c:1863-1871`) for the v2 list layout, mirroring how `ACT_TILT` was added at `main.c:1861`.

**A6.1.3 — semantics.** LINK is the right home: the tab is "how this console talks to another
game", and presence is the co-op feature. It is also where M4 (the wireless version) will need to
live, so the control does not move when the transport changes — which is PHASE Invariant 3's whole
point.

**A6.1.4 — shape: `PK_TOG`, not `PK_SEG`.** Presence has no ladder (unlike tilt's four angles). A
2-state toggle needs no `nseg`, no label table, and no entry in the four `PK_SEG` switch statements
(§A6.2) whose `default:` clause is a known trap.

### A6.2 — The four dispatch sites (all of them, or the control silently misbehaves)

Phase 14 documented that a new control must be handled in **four** switch statements, two in the
pause menu and two in `run_settings`, and that the `PK_SEG` ones end in a `default:` that
*"silently re-skins the virtual gamepad"* (`main.c:2634-2637`, I4.6 "THE TRAP"). A `PK_TOG` avoids
the two `PK_SEG` switches entirely, and needs:

| # | Site | Edit |
|---|---|---|
| 1 | pause-menu toggle dispatch (the `else if (activate && act == ...)` chain from `main.c:2661`) | `case ACT_PRESENCE: presenceOn = !presenceOn; status = ...; settings_save(...)` |
| 2 | pause-menu toggle **draw** state (`main.c:3229` `switch` that resolves `on=`) | `case ACT_PRESENCE: on = presenceOn; break;` |
| 3 | `run_settings` toggle dispatch (`main.c:3438`) | same as #1 |
| 4 | `run_settings` toggle draw state (`main.c:3468`) | same as #2 |

**A6.2.1** Status line, in the phase-14 house style of naming what the row cannot show (I4.14,
`main.c:2650-2652`):

```
"Co-op: on — same map, overworld only"    /    "Co-op: off"
```

### A6.3 — Settings persistence

Append **one** `s32` after `tilt` (`main.c:1618-1642`), following phase 14's own decision exactly
(I4.10: *"NO magic bump — `SETTINGS_MAGIC` identifies the FAMILY and the length ladder does the
versioning"*, `main.c:1664-1669`):

```c
    s32 tilt;        // phase 14
    s32 presence;    // phase 15 (appended; files that end at `tilt` still load, presence stays 0)
```

Then, in `settings_load` (`main.c:1651-1700`):

```c
size_t lenTilt = offsetof(Settings, presence);   // the pre-presence full struct = every file written before this build
size_t lenNew  = sizeof s;
if ((n != lenNew && n != lenTilt && n != lenPad && ... ) || s.magic != SETTINGS_MAGIC) return;
...
if (n >= lenNew) presenceOn = (s.presence != 0);
```

**A6.3.1** Note the rename: today's `lenNew` (`main.c:1669`) becomes `lenTilt`, and the new
`lenNew` is `sizeof s`. Getting this wrong rejects every existing settings file — the ladder's
whole purpose.

**A6.3.2 — the `_Static_assert`s WILL fire, and that is the design.** `main.c:1647-1649` pins
`sizeof(Settings) == 24*sizeof(s32)` and `offsetof(tilt) == 23*sizeof(s32)`, with the comment
*"test/host/test_tilt.c TEST 6 replicates this layout ... If a field is inserted anywhere above,
these fire and the test's copy must be updated in the same edit."* Adding `presence` makes
`sizeof` 25 words: update the assert to `25 * sizeof(s32)`, add
`_Static_assert(offsetof(Settings, presence) == 24 * sizeof(s32), ...)`, and update `test_tilt`
TEST 6's mirrored struct **in the same commit**. `offsetof(tilt)` stays 23 ✓ (append-only), which
is the proof the change is backward-compatible.

**A6.3.3** `settings_save` (`main.c:1702-1713`) gains `presenceOn` as the last initialiser, after
`g_prefs.tiltLevel`. **A6.3.4** Default **off**, matching phase 14's I5.8 discipline (a new effect
ships inert so no existing user's frame changes).

**A6.3.5 — where the flag lives.** `presenceOn` is a `run_session` local like `dofOn`/`bloomOn`
(threaded through `settings_load`/`settings_save`'s parameter lists), **not** a `g_prefs` field —
`g_prefs` is the *chrome/theme* preference block (`main.c:1633-1641`) and tilt is there because it
is a render-style preference; presence is a session feature toggle, which is what the
`dofOn`-family models.

### A6.4 — HUD indicator

Add a `CO-OP` chip to the top HUD chip row, immediately to the **left** of the TILT chip
(`main.c:3116-3126`), using the identical `rx -= cw` flow idiom:

```c
if (presenceOn) {
    const char* pc = "CO-OP";
    float cw = ui_text_w(txtBuf, pc, 0.32f) + 12.0f;
    rx -= cw;
    ui_chip(txtBuf, pc, rx, 0.5f, peerDrawnThisFrame ? g_ui.acc : THEME_ON_DARK_DIM);
}
```

**A6.4.1** The colour carries the gate, exactly as phase 14's tilt chip does (`main.c:3106-3115`:
*"A photo of a tilted screen with a DIM chip is self-contradictory and localises a gate bug
immediately"*). Accent = a peer avatar was actually drawn on the top screen this frame; dim = the
setting is on but nothing was drawn (different map / not overworld / no profile / stale). **This
makes the M1 milestone photographable** and is the fastest possible triage for "why do I not see
my friend".

**A6.4.2** Top screen only, and inside the `else` branch of the `netOn || wlOn` split
(`main.c:3086-3089`) so it never competes with the net-diag readout — same reasoning phase 14 gave
(I6.3), and harmless here because M4 is out of scope, so presence and a live link do not co-occur
in this phase.

**A6.4.3** The pause summary's feature-pill row (`draw_paused_summary`, `main.c:1888-1926`) gains a
`Co-op` pill alongside the existing DoF/Bloom/Light pills, so the pause screen answers "is it on"
without opening the LINK tab.

### A6.5 — The phase-13 CSV columns

`DiagCsvRow` (`diag.h:263-311`) already carries the *own* game's `mapg`/`mapn`/`px`/`py`/`objx`/
`objy`/`face`. Append peer columns at the end of the struct, in column order, per the schema's own
rule (*"ONE FIELD PER COLUMN, in column order"*, `diag.h:259-261`):

| Column | Type | Meaning |
|---|---|---|
| `prLive` | int | `PeerPresence.live` 0/1 — the two-tier presence bit (`pm-rom-abi.md` §7.4: transport-connected ≠ game-active) |
| `prMapg`, `prMapn` | int | peer map (−1 = n/a) — a mismatch with `mapg`/`mapn` explains a missing avatar in one glance |
| `prPx`, `prPy` | int | peer tile |
| `prSubX`, `prSubY` | int | peer sub-tile scroll (the §A0.3.3 sign lives or dies here) |
| `prFace` | int | peer facing 1..4, −1 unknown |
| `prRound` | uint32 | peer `round` — frozen while `live` is the wedge signal `pm-rom-abi.md` §3 describes |
| `prDrawn` | int | 1 = the avatar actually reached `C2D_DrawImageAt` this frame (i.e. gate open **and** not culled) |
| `prObjD` | int | `objx − px` for the peer: must be exactly **7** when both are valid (§A0.2.2's free consistency check; anything else means a mis-mapped `mapObjects`) |

**A6.5.1** `diag_csv_header` (`diag.h:322`) must gain the matching names — the header/row column
counts are host-asserted (*"comma count == DiagCsvRow field count — host-test asserted so a future
column add can't desync header vs row"*), so `test_diag` (369 checks today) catches a mismatch.
**A6.5.2** The CSV only writes while a wireless session is armed (`main.c:2483`,
`s_csvFile && wlOn && ...`), so in this same-console phase the columns are mostly dormant — they
exist so that M4's first run is instrumented on day one rather than retrofitted. **A6.5.3** The
gs-log ring (`gs_log_sample`, `gamestate.h:180`) is the surface that *does* fire in this phase: add
the peer's `live/map/tile/facing` to `GsDepth` as **appended, size-tolerant** fields exactly as
phase 14 appended `tiltLvl`/`tiltAngTop`/`tiltAngBot` (`gamestate.h:147-157`), and stamp them from
the same parked-window block. LOGGING ONLY; nothing reads them back.

---

## A7. TEST PLAN + HARDWARE CHECKLIST

### A7.1 — What the host suite proves (`test/host/test_presence.c`)

Built and run like every sibling (PHASE Invariant 9), added to the standing command list:

```
clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_presence.c -o /tmp/tp && /tmp/tp
```

It `#include`s `../../source/presence.c`, `presence_art.c` and `gbatext.c` directly (the
`test_tilt.c` pattern, `test/host/test_tilt.c:41`) — all three are header-free pure C, so no mock
`<3ds.h>` is needed. Numbering continues in a fresh **P** series so it can never collide with the
tilt suite's TEST 1-19.

| # | Proves | Method |
|---|---|---|
| **P1** | **The anchor identity.** `presence_cell_frame` with peer == host yields exactly `(112, 80)` ⇒ sprite top-left `(112, 64) == (POP3D_PLAYER_GX, POP3D_PLAYER_GY)`, for every camera value in −15..15 and every scale mode | bitwise equality; §A0.2.1 |
| **P2** | **Draw-position math.** A golden table of (peer tile, host tile, sub-scroll) → frame-space sprite rect, including the ±16 px continuity across a tile step: `‖pos(tile=n, sub=15) − pos(tile=n+1, sub=−15)‖ ≤ 2 px` for both signs of `PRES_SUB_SIGN` (so the *shape* is tested and only the sign is left to hardware) | golden values, 1e−4 |
| **P3** | **Sheet cell arithmetic.** Every (variant, facing, pose) maps inside the 128×128 texture; the 9 authored cells of a variant tile `48×96` exactly; facing 4 selects row 2 with `mirror == 1`; every out-of-range facing selects row 0 col 0 | exhaustive over facing 0..255, pose 0..3, variant 0..2 |
| **P4** | **Culling and clipping.** Exhaustive sweep of the sprite rect over `[−32, 272] × [−48, 208]`: culled ⇔ no intersection with the frame rect; clipped rect is always inside `[0,240]×[0,160]`; the trimmed subtexture origin keeps the visible part registered (spot-checked against a hand-computed case per edge and per corner) | exhaustive, 1 px steps |
| **P5** | **The tilt composition is a pure translation.** For each angle in the ladder (0/10/15/20°): the projected sprite rect equals the flat rect plus a constant offset — i.e. **width and height are unchanged, bitwise**; and at angle 0 the offset is exactly `(0,0)` | uses the real `tilt_view_init`/`tilt_project` from `source/tilt.c`; this is the machine-checked form of §A3.1.1 |
| **P6** | **Y-sort.** The comparator is a strict weak ordering; ascending foot `fy`; `tid` breaks ties deterministically; sorting a 1-element list is a no-op | 3-peer permutations |
| **P7** | **Charmap decode.** `{0xC8,0xDD,0xE0,0xE7,0xFF} → "Nils"` (the in-repo Celio anchor, §A4.2.1); full `A-Z`/`a-z`/`0-9` round trip; `0xFF` terminates; unknown bytes → `?`; **no read outside the 256-entry table for any of the 256 byte values**; **no write past `cap`** for every cap in 0..32 | exhaustive over 0..255 |
| **P8** | **Adjacency + facing predicate.** Exhaustive over `dx,dy ∈ [−2,2]` × `hostFacing ∈ 0..5` × `peerFacing ∈ 0..5` (900 cases): `meetingOk` true **iff** exactly-one-step-apart and both facings are the correct opposite pair; plus the degraded mode (facing out of range ⇒ adjacency-only) | exhaustive |
| **P9** | **Walk-frame selection.** A 64-frame simulated walk produces the 4-beat cycle in order, STAND after `PRES_IDLE_FRAMES` of no travel, and a full cycle across one 16 px tile step; the accumulator resets on map change / `live` edge / `round` gap | golden sequence |
| **P10** | **Placeholder art invariants.** `presence_art_build` writes exactly `128*128*4` bytes and no more; every one of the 18 cells (2 variants × 9) is non-empty; **the magenta tells are present in every cell** (so nobody can quietly delete them); every pixel outside the two 48×96 variant blocks is fully transparent | pixel scan |
| **P11** | **The degradation ladder (§A0.4).** Each row of that table, asserted as a predicate on the draw decision — in particular `live == 0` ⇒ the draw function returns before touching any output | state table |
| **P12** | **Settings ladder.** Mirror of `main.c`'s `Settings` with `presence` appended: `offsetof(tilt)` still 23 words, `offsetof(presence) == 24`, `sizeof == 25` words; every historical file length still loads; a corrupt/negative `presence` word can only produce 0/1 | replicates `test_tilt` TEST 6's technique |

**A7.1.1** `test_tilt.c` TEST 6 must be updated in the same commit (§A6.3.2) and the two suites must
both stay green. The standing command list in PHASE/HANDOFF gains the `test_presence` line.

**A7.1.2** What the host suite **cannot** prove and must not pretend to: anything involving
`C2D_DrawImageAt`, the GX transfer, the real texture, filtering, the parallax barrier, or timing.
Those are §A7.2 in their entirety.

### A7.2 — Hardware checklist (HANDOFF style — add verbatim to `docs/HANDOFF.md`)

Real New 3DS, `.cia`, **two ROMs from the SAME map universe**, same map, presence **on**. Per
CLAUDE.md #6, none of these is "done" until it is done here.

> **CORRECTED 2026-08-04 (fix pass, review finding 3).** This line originally said "both carts
> (Emerald + FireRed rev1)" — the user's own pair — and **that pair can never draw a peer**.
> `SPEC-data` D4.3's map-universe gate (P-G5) is correct and stays: `(mapGroup, mapNum)` is only
> meaningful inside *one* game's map table, so Emerald's `(3, 12)` is not FireRed's `(3, 12)` and
> allowing the pair would put a peer on an unrelated map — a category error that presents as a
> mysteriously wrong position. Every item below therefore needs **FireRed + LeafGreen**, **FireRed +
> FireRed** or **Emerald + Emerald**. An Emerald + FireRed session is a legitimate configuration of
> the emulator; presence simply reports `universe` and draws nothing, and as of this fix the
> pause-menu row says so out loud ("Co-op: on — Hoenn vs Kanto: no peer") instead of leaving the
> CO-OP chip silently dim.

- [ ] **H1 — Does the avatar appear at the right tile at all?** Stand both players on the same map,
      walk one to a known landmark, and photograph. The `CO-OP` chip must be **accent** (§A6.4.1).
      *If the chip is dim, this is a data/gate problem, not a draw problem — stop and read the CSV
      `prLive`/`prMapg` columns before touching the renderer.* **Accept:** the avatar stands on the
      same tile the other screen shows the player standing on, to the tile.
- [ ] **H2 — The sub-tile sign (`PRES_SUB_SIGN`, §A0.3.3).** Walk the peer one tile, slowly, and
      watch the avatar. **Accept:** a smooth 16 px glide. **Fail:** a 16 px jump forward followed by
      a slide back (or the reverse) ⇒ flip the constant, rebuild, re-shoot. This is the single most
      likely defect in the phase and it is one character to fix.
- [ ] **H3 — Sprite direction.** Walk the peer in a square. **Accept:** the avatar faces the way
      the other screen's player faces, all four ways, and the East frame is the mirrored West
      (§A1.2.2 — confirms citro2d's negative-scale mirror).
- [ ] **H4 — Facing correctness per game (§A5.2.1).** Repeat H3 **on FireRed specifically**, because
      `mapObjects` for FR/LG is flagged suspect. **Accept:** correct on both. **If FR is wrong:**
      record it, leave the address alone (house rule), and confirm the degraded adjacency-only
      prompt still fires.
- [ ] **H5 — Frame-edge behaviour.** Walk the peer off each of the four edges. **Accept:** the
      avatar is trimmed cleanly at the game rect and never draws over the letterbox / never
      survives past the edge; it disappears entirely rather than clamping. Repeat **with tilt on**
      (§A3.2.1) and photograph the near edge, where the tilted image spills.
      *Updated 2026-08-04 (fix pass, finding 4):* under tilt the clip box is now widened by the
      view's own **spill**, so the accept criterion at the edges differs by mode. **Flat:** trimmed
      hard at the game rect, exactly as above. **Tilted:** the avatar stays **whole** while its foot
      is on the trapezoid and may overhang the ground by up to ~half a cell at the corners — what
      must NOT appear is a *vertical slice missing* out of an otherwise complete sprite standing on
      visible ground, which is the defect this fixed. A peer whose foot leaves the picture still
      disappears entirely (the data half's cull is unchanged and is exact).
- [ ] **H6 — Composition with the HD-2D stack.** DoF + bloom + light all on, 3D slider full.
      **Accept:** the avatar is graded by the time-of-day light (it darkens at dusk with the map —
      the §A2.1 ordering claim), is *not* erased by the DoF band, and does not flicker.
      *Known and accepted:* it stays sharp inside the blur band.
- [ ] **H7 — Tilt composition (the phase-14 half).** Tilt at 10/15/20°, walk the peer from the far
      row to the near row. **Accept:** the avatar's **feet stay planted on the ground plane** at
      every row, the sprite stays **pixel-crisp and the same size** at every row (§A3.1.1), and
      during the 250 ms tween the feet track the moving ground rather than sliding across it
      (§A3.2.3). This is the one Azahar genuinely cannot judge.
- [ ] **H8 — Touch is unaffected.** Bottom screen, smart touch on, tap on and around the avatar.
      **Accept:** tap-to-walk behaves exactly as before; the avatar is not a touch target; nothing
      about the hardware-validated touch mapping changed (memory: *"Touch: reliability over
      piling"*).
- [ ] **H9 — Frame budget.** `worstMs` with two games, presence on, peer visible, tilt 20°, 3D
      full, DoF+bloom+light on. **Accept:** ≤ 16.7 ms, and **no measurable delta vs presence off**
      (the draw is one textured quad plus at most two text runs). If it moves, the suspect is the
      per-frame text parse — check §A4.5.2's decode cache first.
- [ ] **H10 — Presence off costs nothing.** Toggle off; confirm the frame is indistinguishable from
      a pre-phase-15 build and `worstMs` returns to baseline (PHASE Invariant 6).
- [ ] **H11 — The Card.** Walk both players face to face, confirm the `Ⓐ CARD` prompt appears, press
      A. **Accept:** name / ID / gender match what each game's own trainer card shows (open it
      in-game and compare — this is the only real verification of `sb2ptr` and the charmap), and A
      reaching the game does nothing harmful (§A5.3).
- [ ] **H12 — Nothing was written.** After a session with presence on: both saves load, both games
      behave normally, and a save-and-reload round trip is clean. PHASE Invariant 1 is the property
      the whole design was chosen for, so it gets an explicit hardware check rather than an assumed
      one.
- [ ] **H13 — Placeholder honesty.** Photograph the placeholder. **Accept:** anyone looking at the
      photo can tell it is a placeholder (§A1.5.3). If they cannot, the tells are not strong enough
      and must be made stronger before the build leaves the bench.

> **BACKFILLED 2026-08-04 (final gate).** H14–H16 were authored in the **M3** BUILDLOG entry and
> H17–H18 in the **fix pass** entry, because each was discovered by the slice that armed it. They
> are reproduced here verbatim so A7.2 is the single complete checklist — the BUILDLOG stays the
> per-slice narrative, this stays the list you take to the bench.

- [ ] **H14 — the prompt fires when it should.** Walk the two players face to face on the same map.
      **Accept:** the `A - CARD` chip appears only when adjacent AND facing each other, and
      disappears the moment either turns away. **If it appears whenever merely adjacent**, the
      facing nibble is unavailable on that game (§A5.2.1's degraded mode) — that is the *designed*
      fallback, not a bug; record which game and check `f<folded>/<raw>` in the M1 readout.
- [ ] **H15 — A still reaches the game.** With the prompt up, face a real sign or NPC one tile away
      and press A. **Accept:** the game's textbox opens exactly as it always did, and the presence
      gate closes by itself while it is up (§A5.3's "obtained for free" claim).
- [ ] **H16 — the card's fonts.** Photograph the card. **Accept:** every line renders — in
      particular the two dim disclosure lines at 8 px. If a glyph is missing, the fix is the string,
      not the font. The punctuation rows `0xAB..0xBA` are the one part of the charmap with **no
      in-repo witness** and are marked verify-on-hw-pending in `gbatext.c`; H11 and this item settle
      them, and a wrong entry is one cosmetic glyph by construction — never an overrun.
- [ ] **H17 — the pill stack** *(fix pass, finding 8)*. Stand the peer 3+ tiles **above** the player
      with both the nameplate and the `A - CARD` prompt up. **Accept:** two distinct pills, 15 px
      apart, both entirely below the HUD bar. **Fail:** one pill (they collapsed onto each other),
      or text overlapping the bar's clock/FOCUS/TILT/CO-OP row.
- [ ] **H18 — the co-op toggle explains itself** *(fix pass, finding 3)*. Turn the CO-OP row on with
      an **Emerald + FireRed** pair — the pair that can never draw. **Accept:** the pause-menu status
      line names the mismatch (`Co-op: on — Hoenn vs Kanto: no peer`). This is the one case the CO-OP
      chip alone cannot distinguish from "not on the same map yet".

---

## Open Questions

**O-A1 — Does a 16×32 overlay sprite read as "a person on my map" or as "a sticker"?** The engine
does not occlude it, so it will walk *over* fences, tree bases and building corners
(`gen1-render.md` finding 7's abandoned-billboarding lesson is the general form of this problem: Gen
art has no clean seam between ground and standing scenery). The honest ceiling says we accept it.
Only a hardware photo of a peer walking behind a tree tells us whether "accept" is actually
tolerable or whether the avatar needs a soft drop shadow / partial alpha to read as an overlay
*on purpose*. **Cheapest mitigation if it reads badly:** a 1-px dark ellipse under the feet, drawn
before the sprite, which grounds it without pretending to occlude.

**O-A2 — The `PRES_SUB_SIGN` question is really two questions.** §A0.3.2 assumes the game keeps
`16·tile + cam` continuous across a step. If instead `SaveBlock1.pos` updates at the *end* of the
step (not the start), the sum is continuous with the opposite sign **and** lags by up to 16 px in
one direction only — which H2 would show as a smooth-but-offset avatar rather than a jump. If H2
shows *that*, the answer is not a sign flip but the `previousCoords → currentCoords` interpolation
`coop-shared-overworld.md` §3 M2 originally called for, which needs a `previousCoords` offset the
data half would have to verify. Recorded so H2's second-order outcome is not mistaken for the
first-order one.

**O-A3 — Should the avatar carry stereo disparity?** §A2.8 ships zero. Once H1/H7 pass, the
question is whether a flat-on-the-screen-plane avatar looks *wrong* next to game sprites that pop.
The upgrade is specified and small, but it touches `POP_DISP_MAX`, which is a hardware-validated
comfort ceiling — so it needs its own 10-minute stereo comfort session (phase 14's O1 pattern), not
a quick toggle.

**O-A4 — Nameplate always-on, or on-approach?** A permanent pill over the peer's head is
informative and also clutter, especially at `SCALE_1X` where 320×240 of screen shows a 240×160
frame. Alternatives: fade in within N tiles, or show only while the meeting predicate holds. This
is a taste call that needs to be seen at real size on a real panel; the code should make it a
one-line policy function, not a scattered condition.

**O-A5 — Which theme owns the avatar's colours?** §A1.4.2 recommends the identical PNG in every
theme directory (world content should not re-skin with the chrome). But the *placeholder* uses
`g_ui.acc`, which does. If the real art lands theme-neutral and the placeholder is theme-tinted,
the two will look different in a way that could be mistaken for a bug. Decide before the art drop:
either the placeholder also goes theme-neutral, or the art gets per-theme variants deliberately.

**O-A6 — 3-4 players (flagged in HANDOFF Next steps #3).** §A3.4's y-sorted loop and `PRES_MAX` are
built for it, but two things are not settled: how a nameplate row behaves with three overlapping
avatars, and whether the CSV peer columns should be per-slot (columns × 3) or a single
"most-recently-updated" slot. Deferred deliberately — PHASE says 2-player must be fully clean
first — but the loop shape is chosen now so it is not a rewrite later.

**O-A7 — Does the ENHANCE-vs-LINK placement (§A6.1) match where players will look for it?** The
measurement is unambiguous (ENHANCE has 7 px left, LINK has 44) and LINK is semantically right for
a co-op feature, but "co-op presence" sitting under a tab whose other rows are save-states and
wireless may read oddly. The zero-cost alternative is to leave it on LINK and *rename the tab* in
`MENU_TAB_NAMES` (`main.c:1747`) — which is one string, but touches every tab's baked plate
alignment, so it is a UX call, not an implementation one. Resolving phase 14's own **O6** (new
ENHANCE plate art with a 6th and 7th row) would make both questions moot.
