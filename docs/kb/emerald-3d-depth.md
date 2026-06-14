# Emerald stereoscopic-3D depth — feasibility & plan

_Verified design study (2026-06-10). Source-checked vs mGBA renderer + Emerald decomp._

I now have full ground-truth. All three studies are mutually consistent and adversarially corrected, and I've verified every code anchor they cite. Here is the architecture document.

---

# Stereoscopic 3D depth for Pokémon Emerald (3DGBA) — architecture verdict

Author: 3DGBA architect. Grounded in the three verified studies (layer-model, tile-semantics, render-approach) and confirmed against live source: `main.c:214-258` (`render_game`), `main.c:704-709` (per-eye hook), `touch.c:131-144` (`map_read`/`walkable`), `gamestate.c:10-30` (the `GameProfile` table) and `gamestate.h:23-63` (the struct), `gbacore.h:72-74` (bus reads).

---

## 0. The one fact that decides everything

mGBA's GBA renderer rasterizes **only** while the PPU advances during emulation — `drawScanline` has a single call site, `_startHblank` (`video.c:214`); `getPixels` (`core.c:558`) just hands back the already-composited `outputBuffer` (= our `e->fb`). There is **no "re-raster this frame with a different layer mask" entry point**, and `enableVideoLayer`'s `disableBG[]` only takes effect on *subsequently emulated* scanlines (`software-private.h:210-215`). So **every layer-separated image costs one full extra emulation+render pass**, and that pass is a *different frame*, not the same one re-composited.

Our two software cores are already saturated: game A worker on core 0, game B on core 2 @ 804 MHz (`main.c:334-335`), audio on the core-1 slice (`main.c:349`); cores 1/3 are system/audio. The roadmap rates two cores at full speed GREEN-but-tight, with unfocused-frameskip as the only headroom lever. **There is no budget for a second emulation pass on the top screen at 60 fps.**

Corollary: any "true layered" approach that re-emulates is dead for the dual flagship. Every feasible approach must run on the **single already-composited 256×256 texture** we already upload (`main.c:174-181`) with **zero extra mGBA passes**. All GPU work happens on the main render thread between `C3D_FrameBegin/End` (convention #2; today's loop at `main.c:687`).

---

## 1. Verdict per approach

| Approach | What it is | Verdict | Reason |
|---|---|---|---|
| **A1 — mGBA layer split** (re-emulate 2-3× with `disableBG[]` masks, composite per-eye at per-layer parallax) | True occlusion-correct layered parallax | **IMPRACTICAL** for the flagship | Each masked image = a full extra emulation pass (§0). 2-3× emulator cost on cores already at 1×. Blows 16.7 ms. Only viable single-game or at 30 fps top-screen. *Visually the best, wrong place to spend our scarcest resource.* |
| **A2 — semantic re-composite** (extract foreground pixels from the flat texture using the layer mask) | Cut foreground out of the one image | **IMPRACTICAL** | The 256×256 tex is already flattened; you can't extract foreground pixels you don't have art for. Not viable without tile art. |
| **B — depth-map warp** (one composited tex + per-tile depth grid; shift tiles/columns horizontally per eye) | 2.5D off-axis warp | **PARTIAL** | Cheap (~300 quads/eye, no extra pass). But Emerald's elevation is *discrete plateau semantics*, not a height field, so the better depth signal is **layer-type** — which needs a new `gMapHeader` symbol + a 5-hop ROM walk. Occlusion gaps tear exactly at the most-looked-at foreground edges. Survives only at *low* disparity, where the effect is faint. Good as a subtle environmental layer, not a hero effect. |
| **C — sprite/character pop** (re-draw player + NPC rects from the flat tex, shifted forward per eye) | Pop characters off the map plane | **FEASIBLE** | Cheapest of all. Player is at fixed screen-tile (7,5) (`touch.c:194,202`) — zero reads; NPCs from OAM at `0x07000000` via existing `gbacore_read16` — no new API. ~a handful of quads + a dozen reads. No extra pass. One artifact: the baked-in sprite ghost at the original position (small/feather-able). Classic, convincing 3DS depth cue. |
| **Hybrid C + subtle B** | C as hero, B as gentle environment | **FEASIBLE (recommended)** | Both run on the single composited tex, no extra pass — exactly what the budget allows. |

**Headline verdict:** A *convincing, occlusion-correct* layered 3D (the "tree truly in front of the player with parallax behind it") is **IMPRACTICAL** on the dual flagship — it's a hardware-budget wall, not an effort wall (same shape as the DS dual-GBA verdict). A **convincing-enough** effect — characters popping off the map, foreground scenery (tree/roof/fence tops) nudged toward the viewer, ground pinned at screen plane, distance receding — is **FEASIBLE** and is the realistic win. Be honest with the user: this is *sprite-pop-led 2.5D*, not true volumetric depth.

---

## 2. Recommended approach — **Hybrid: C primary (sprite pop) + subtle B (layer-type warp), slider-scaled**

### 2.1 Depth signal it uses

Two signals, in priority order:

1. **C (hero) — sprite positions.** Player: fixed screen-tile (7,5) → GBA px ≈ (112, 80), free. NPCs: scan OAM at `0x07000000`, 8 bytes/entry × 128. attr0 Y=bits 0-7, attr1 X=bits 0-8, attr2 priority=bits 10-11. **Must honor** attr0 bit 9 = Disable (non-affine) / DoubleSize (affine, attr0 bit 8 = Transformed) — skip disabled/garbage entries (corrected in render-approach verify). All characters pop forward by `POP·slider`; the player (always present, always the focus) carries the effect.

2. **B (subtle) — metatile LAYER-TYPE, not raw elevation.** This is the load-bearing correction across all three studies. The depth classifier is **layer-type** (`NORMAL`/`COVERED`/`SPLIT`), because it *is* Emerald's own occlusion model (`DrawMetatile`, `field_camera.c:244-307`):
   - `NORMAL` → top half drawn to **BG1**, the layer that *"covers object event sprites"* (verbatim comment, `field_camera.c:300`) = standing scenery (tree canopy, wall, roof, fence, cliff) → **pop forward**.
   - `COVERED` → BG1 transparent, never occludes the player = ground/floor → **screen plane (depth 0), the anchor**.
   - `SPLIT` → ledge tops / low fences → **in-between**.
   - Modulate with **elevation** (`0xF000` of the map grid) only as a *secondary* nudge for stacked levels (parity of `sElevationToPriority = {2,2,2,2,1,2,1,2,1,2,1,2,1,0,0,2}`, even≥4 = in front). **Do not** drive B from raw elevation alone — outdoors it's almost all `ELEVATION_DEFAULT=3` with sentinel spikes (`TRANSITION=0`, `SURF=1`, `MULTI_LEVEL=15`); it reads mostly-flat.
   - Behavior allowlist (`0x00FF` of the attribute) only for two cases a flat `COVERED` tile still wants offset: tall/long grass (`MB_TALL_GRASS=0x02`, `MB_LONG_GRASS=0x03`, `MB_ASHGRASS=0x24` — **0x24, not 0x25**, corrected in tile-semantics verify) → slight pull-in; water (`MB_POND_WATER=0x10`..`MB_OCEAN_WATER=0x15`) → push back.
   - Off-map (id `0x03FF`): **do not** blanket-recede. The game draws the 2×2 border metatile (`MapLayout+0x08`); classify by the border metatile's own layer-type, or the substituted id `MapGridGetMetatileIdAt` already yields (corrected in tile-semantics verify). On routes the border is trees/grass — slamming it to -1.0 sinks visible scenery.

   B is *correct but expensive in attribute reads and gap-prone*; it must stay low-disparity and is **optional/deferred** behind C.

### 2.2 How the depth is read from RAM — the pointer chains

**Grid + elevation (free, already done):** reuse `map_read`/`walkable` (`touch.c:131-143`). `gBackupMapLayout` = profile field `mapLayout = 0x03005DC0` for BPEE (`gamestate.c:17`), struct `{s32 w@0, s32 h@4, u16* map@8}`. World tile (wx,wy) → grid index `(wx+7) + w*(wy+7)`; per-tile u16: id `&0x03FF`, collision `&0x0C00`, elevation `&0xF000`.

**Layer-type (new — needs a profile symbol):** the `0xF000` that holds layer-type is **not** in the grid word — it's in the metatile-attribute table, a *different* structure (the central correction in both layer-model and tile-semantics verifies). `gMapHeader = 0x02037318` (BPEE, from `/tmp/EMERALD.sym`) is **not in our `GameProfile`** and must be added. Then per metatile id:

```c
// new profile field: uint32_t mapHeader;  // gMapHeader (BPEE 0x02037318)
uint32_t layoutP = gbacore_read32(c, p->mapHeader + 0x00);            // MapLayout*
uint32_t ts      = gbacore_read32(c, layoutP + (id < 512 ? 0x10 : 0x14)); // primary/secondary Tileset*
uint32_t attrP   = gbacore_read32(c, ts + 0x10);                      // metatileAttributes (ROM)
uint16_t attr    = gbacore_read16(c, attrP + 2u * (id < 512 ? id : id - 512));
uint8_t  layer   = (attr & 0xF000) >> 12;   // 0 NORMAL / 1 COVERED / 2 SPLIT
uint8_t  behav   =  attr & 0x00FF;
// guard: id >= 1024 -> treat as ground (MB_INVALID)
```

(Offsets verified field-by-field: `MapHeader.mapLayout +0x00`, `MapLayout.primaryTileset +0x10`/`secondaryTileset +0x14`/`border +0x08`, `Tileset.metatileAttributes +0x10`; `NUM_METATILES_IN_PRIMARY=512`. `attrP` lives in ROM `0x08…`, covered by `gbacore_read16`.)

**Cost + caching:** ~150 tiles/screen. Cache the two `metatileAttributes` bases per map (invalidate by **re-reading and comparing the base pointers themselves**, not just `mapLayoutId` — secondary tileset can swap indoors without `mapLayoutId` changing). Memoize attr by id (`u16 attrCache[1024]`, cleared on map change). Drops to ~150 grid + ≤150 first-touch attr reads/frame. Note `gbacore_read16` is the full GBA bus loader (`busRead16` → `GBALoad16`, a function-pointer + region switch + waitstate branch — *tens* of ns, not "a few," corrected in tile-semantics verify), but at a few hundred reads/frame it's still negligible vs the frame budget.

**Thread-safety (must respect):** these reads hit a core a worker thread owns. `gbacore.c` documents bus reads as safe only same-thread or while the worker is **parked** — never during a link free-run. In **linked** sessions the cores free-run, so all stereo RAM/OAM reads must happen at the parked per-frame handshake point, the same place `map_read` already runs — not arbitrarily in the render thread.

### 2.3 Rendered per eye (citro2d sketch, slider-scaled)

Repurpose the per-eye hook at `main.c:704-709`. **Scope correction the user must accept:** today the right eye shows the *other game* (`rightG = (slider3d>0.03f) ? botG : topG`, `main.c:708`) — a per-eye dual-game gimmick, **not** stereo of one game. Real single-game depth **replaces** that: both eyes render the *same* (top) game, the right eye with horizontal per-element shifts. And `render_game` draws the whole frame with one `C2D_DrawImageAt` (`main.c:246/256`) — a per-tile/per-sprite shift is **net-new strip/quad drawing**, not a parameter tweak. The slider must now choose: dual-game-3D (current) **or** single-game-depth (new). Recommend a settings toggle (`Settings` already persists per-screen modes, `gamestate-adjacent` block at `main.c:263-273`).

```c
float s3d = osGet3DSliderState();                 // main.c:707
if (s3d < 0.03f) {
    render_game(topG, top, ...); render_game(topG, topR, ...);   // flat: both eyes same game
} else {
    const float POP = 4.0f * s3d;                 // sprite pop px @ full slider (HERO)
    const float ENV = 2.0f * s3d;                 // optional layer-type warp (SUBTLE), clamp small
    DepthSnap d;                                  // captured at the parked handshake, not here
    for (int eye = 0; eye < 2; eye++) {
        C3D_RenderTarget* tgt = eye ? topR : top;
        float sgn = eye ? +1.0f : -1.0f;          // opposite disparity per eye
        // 1) base flat frame (existing render_game transform: ox,oy,sx,sy)
        render_game_base(topG, tgt, ...);         // leaves tgt bound
        // 2) (B, optional) re-draw NORMAL/SPLIT tiles shifted forward; ground stays at 0;
        //    CLAMP_TO_EDGE on each 16x16 sub-tex to smear-fill the uncovered gap.
        //    for each visible tile with depth>0:
        //      C2D_DrawImageAt(tileImg, ox + col*16*sx + sgn*ENV*depth, oy + row*16*sy, ...);
        // 3) (C, hero) re-draw player rect (fixed 7,5) + on-screen OAM sprites shifted forward:
        //      C2D_DrawImageAt(spriteImg, ox + spx*sx + sgn*POP, oy + spy*sy, ...);
    }
}
```

Keep `POP`/`ENV` small (2-4 px); CLAMP_TO_EDGE hides gap smears; depth data captured at the handshake so the render thread never touches a free-running core. Bad/zero data → zero shift, never a broken frame.

---

## 3. Milestone plan (each independently hardware-verifiable — 3D is hardware-final)

3D fusion, comfort, and the gap artifacts **cannot** be judged in Azahar (it doesn't model the parallax-barrier eye separation or the 804 MHz/core-2 budget). Every milestone ends on real New 3DS hardware. Each milestone is shippable on its own.

**M0 — Slider-mode toggle + flat regression guard.** Add a setting so the slider drives either today's dual-game-3D or the new single-game-depth mode; in single-game mode with slider *down*, both eyes render the same top game (zero regression). *Files:* `main.c:704-709` (per-eye block), `main.c:263-273` (`Settings`). *USER verifies:* slider down = identical to flat; toggling the setting switches right-eye content; no perf change.

**M1 — Crude 2-plane pop (prove the per-eye pipeline).** No RAM. Re-draw the whole frame in the right eye at a single constant horizontal offset `sgn·k·slider` (everything pops uniformly = a flat card floating forward). Proves per-eye disparity, sign correctness, and slider scaling fuse comfortably. *Files:* `main.c` per-eye block; a `render_game_base` split out of `render_game`. *USER verifies on hardware:* slider up → the whole top game floats forward as one plane, no double-vision, comfortable; slider controls depth magnitude; both top-screen games still run full-speed (check the HUD fps / unfocused-frameskip lever didn't trip).

**M2 — Sprite pop = C hero (player only).** Pop just the player rect (fixed tile 7,5) forward while the map stays flat. Re-draw the base, then the player's 16×16 sub-tex shifted by `POP·slider`. *Files:* `main.c` per-eye block (new player-rect sub-draw). *USER verifies:* the character visibly stands *off* the map; ground stays at screen plane; the baked-in ghost behind the player is faint/acceptable; full-speed maintained. **This is the milestone that proves the hero effect is convincing.** If it isn't convincing here, B won't save it.

**M3 — NPC pop via OAM = C complete.** Scan OAM (`0x07000000`) at the parked handshake; capture on-screen sprite rects (honor Disable/DoubleSize/Transformed bits); pop each forward (smaller `POP` for distant NPCs if desired). *Files:* new `depth_oam_snap()` (read OAM into a `DepthSnap`, called where `map_read` is called); `main.c` per-eye block consumes it. *USER verifies:* NPCs in town also pop; no garbage/hidden sprites popped; works in motion; full-speed; **link session** doesn't crash (reads only at handshake).

**M4 — Layer-type depth classes = B subtle (deferred, optional).** Add `mapHeader = 0x02037318` to the BPEE `GameProfile`; build a 15×10 layer-type depth grid (NORMAL=forward, COVERED=0, SPLIT=mid) with per-map attr-base caching + id memoization; warp those tiles in the right eye at *low* `ENV`, CLAMP_TO_EDGE gap-fill. *Files:* `gamestate.h:23-63` (+`mapHeader` field), `gamestate.c:15` (BPEE row), new `depth_tile_snap()` reusing the `touch.c:131-143` grid pattern, `main.c` per-eye block. *USER verifies on hardware:* tree/roof/fence tops sit nearer than the path; ground is the anchor; gap-tears at foreground edges are tolerable at the chosen low disparity; **the FR/LG profiles (no `mapHeader`) silently fall back to C-only** (no crash); full-speed. *Honest gate:* if the gaps are ugly at any disparity that produces visible depth, **ship M3 (C-only) and drop B** — that's the realistic win.

**M5 — Polish + comfort.** Tune `POP`/`ENV` against the slider; ghost-feathering; ensure dual-game-3D mode (M0) still works; confirm on Old 3DS it degrades gracefully (best-effort). *USER verifies:* extended-play comfort, no eye strain, both games full-speed across map transitions and battles (battle has no overworld map — B must no-op outside `GCTX_OVERWORLD`; reuse `game_read` ctx).

---

## 4. Risks (honest)

- **Occlusion gaps (B's fatal flaw).** Shifting a foreground tile uncovers screen the flat texture never captured → smear/duplicate. Emerald's elevation is *discrete plateaus*, so tears land at the most-looked-at boundaries (ledges, cliff edges). Sub-tile camera scroll (BG offset regs) means the tile grid ≠ pixel grid, worsening seams. Mitigation: foreground-forward only (never push ground back), clamp disparity to ~2-3 px, CLAMP_TO_EDGE fill — but every mitigation trades depth for cleanliness. **This is why B is subtle/optional and C is the hero.**

- **Per-frame cost vs the two-core budget.** The recommended path adds **zero** emulation passes — only ~a few hundred GPU quads/eye + a few hundred bus reads/frame at the handshake. That's comfortably inside the PICA200 and the frame budget. The thing that would blow the budget — A1's 2-3× re-emulation — is explicitly rejected. Watch the HUD/frameskip lever on hardware as the canary (roadmap's "tight" rating).

- **Attribute-table reliability (B).** The 5-hop chain is real and verified, but `gMapHeader` is a new symbol we don't yet map; secondary-tileset swaps can invalidate the cache without `mapLayoutId` changing (key the cache on the attr-base pointers themselves). Off-map tiles must classify via the border metatile, not blanket-recede. C (sprite pop) has none of this fragility — player position is engine-fixed, OAM is universal.

- **Emerald-only vs general.** C is the **most general**: OAM is universal across all GBA games; the player-fixed-tile trick is Emerald-specific but the OAM scan is not. B is **Emerald-specific** (needs `gMapHeader` + the layer-type model + per-game profile). FR/LG share Emerald's RAM-map shape but the `gMapHeader` address differs and isn't in our profiles — they get C-only until added. Non-Pokémon games get C (OAM pop) for free, no B.

- **The honest bottom line.** The *convincing volumetric* version — real foreground/ground/background separation with correct parallax and no gaps — is **IMPRACTICAL** on the dual-GBA flagship: it requires per-layer images, which require re-emulation, which the two saturated cores can't afford. The **realistic win** is the cheaper subtle version: **character pop (C) as the hero, optional gentle layer-type warp (B) underneath**, both on the single composited frame, slider-scaled, captured at the handshake. It reads as genuine 3DS depth without ever paying for a second emulation pass — and it degrades safely to flat on bad data, unknown games, or the Old 3DS.

**Key files to touch:** `projects/3DGBA/source/main.c` (`render_game` 214-258 → split out `render_game_base`; per-eye block 704-709 → the depth render); `projects/3DGBA/source/gamestate.h:23-63` + `gamestate.c:15` (add `mapHeader` for M4); a new `depth.c`/`depth.h` for `depth_oam_snap()` (M3) and `depth_tile_snap()` (M4), reusing the `touch.c:131-143` grid pattern and `gbacore_read16/32` (`gbacore.h:72-74`); `main.c:263-273` `Settings` (M0 toggle). No mGBA/MPL files are modified — `enableVideoLayer` is deliberately *not* used (it would force the rejected re-emulation path).