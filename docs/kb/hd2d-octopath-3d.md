# Octopath HD-2D look for the GBA overworld — feasibility & plan

_Verified design study (2026-06-10). PICA200/cost-checked._

I have everything I need. All findings are verified against live code and headers, the existing stereoscopic-depth KB (`emerald-3d-depth.md`) establishes the foundation (sprite-pop + layer-type warp on the single composited texture, no extra emulation pass), and this document layers HD-2D post-FX on top of that. I'll now produce the architecture document as my return value.

---

# HD-2D for the dual-GBA overworld — feasibility verdict, AI-depth verdict, rendering recipe & milestone plan

Author: dual-gba architect. Grounded in three verified+adversarially-corrected studies (`hd2d-gap`, `ai-depth-pipeline`, `pica200-runtime`) and confirmed against live source (`projects/dual-gba/source/main.c`, anchors at 214-366 and 823-908), the existing depth KB (`docs/kb/emerald-3d-depth.md` §0/§1), and the real PICA200 headers (`/opt/devkitpro/libctru/include/c3d/{texenv.h,light.h,fog.h,lightlut.h,proctex.h}`, `3ds/gpu/enums.h`). This builds **on top of** the already-shipped stereo layer (`pop_eye`/`warp_scenery_eye` + `build_depth_grid`/`tdepth[10][15]`), it does not replace that verdict.

---

## 0. The two facts that bound every answer

1. **The PICA200 has NO programmable fragment/pixel shader.** Confirmed in headers: `3ds/gpu/shbin.h:11-12` enumerates only `VERTEX_SHDR` and `GEOMETRY_SHDR` — there is no fragment-program type anywhere in libctru/citro3d. The fragment back-end is fixed-function: **6 TEV combiner stages** (`GPUREG_TEXENV0..5`, with `GPU_INTERPOLATE=0x04`, `GPU_SUBTRACT=0x05`, `GPU_DOT3_RGB=0x06`, `GPU_MULTIPLY_ADD=0x08` all verified at `enums.h:369-373`), a **fragment-lighting unit** with hardware tangent-space normal mapping (`GPU_BUMP_AS_TANG=2` at `enums.h:423`, `C3D_LightEnvBumpMode/BumpSel` in `light.h`, 8 lights), and a **fog/gas depth-fade LUT** (`fog.h`). **Every "post-process shader" must be expressed as multi-pass quad draws + TEV/fog/light fixed units — not GLSL.** (Minor correction to the studies: the geometry shader is *also* programmable; irrelevant to post-FX, which is per-pixel, but it's a cheap lever for tessellating a vertex grid on-GPU.)

2. **No second emulation pass is affordable, ever.** `emerald-3d-depth.md` §0 is decisive: mGBA only rasterizes while the PPU advances, there is no re-raster-this-frame entry point, so every layer-separated image = a full extra emulation pass on cores already saturated (game A on core 0, game B on core 2 @804 MHz, audio on the core-1 slice). All HD-2D work must run on the **single already-composited 256×256 RGB565 texture** we upload per game, inside the existing `C3D_FrameBegin/End` on the main thread (`main.c:823-908`), where the GPU is otherwise idle but the CPU/bus are not.

These two facts mean: **we are doing HD-2D-flavored screen-space post on a flat 2D frame + a coarse depth hint, not native HD-2D geometry.** That is the honest ceiling, and it's still a real win.

---

## 1. Verdict — how close to HD-2D can we get?

**Honest answer: we can reach a convincing "lit, focus-graded 2.5D diorama," but not native HD-2D.** Octopath is real 3D geometry with billboarded sprites at true Z, so DoF/parallax/shadows/lighting all read *true depth for free*. We have one flattened frame + a coarse 15×10 tile-depth grid + OAM rects. Anything needing data *behind* the visible surface — true layer separation, occlusion-correct parallax, real per-pixel relighting — is unbridgeable on the dual flagship (it needs per-layer re-emulation; §0). Anything that needs only the visible frame + a depth hint is reachable.

HD-2D decomposed and tagged against **our** inputs (impact ranked by Square Enix's own breakdown of the look):

| # | HD-2D element | Visual impact | Reachable here? | How / cost |
|---|---|---|---|---|
| 1 | **Depth-graded DoF + tilt-shift** (sharp focal band, blurred near/far) | ★★★★★ — the single biggest "miniature diorama" tell | **YES** | Render flat frame to offscreen target, make a half-res blurred copy, composite sharp-vs-blur. Cheapest honest version = blur top/bottom **bands** (literal tilt-shift), keep mid sharp; upgrade to per-tile blend via TEV `GPU_INTERPOLATE` using `tdepth` as a tiny 15×10 lerp texture. |
| 2 | **Bloom / glow** | ★★★★ — lush, lit feel; universally flattering | **YES** | Bright-pass (TEV `GPU_SUBTRACT` a threshold const), quarter-res separable blur, additive blend back. Note: GBA frame is **RGB565/LDR** (`main.c` per-instance tex), so this is **LDR glow, not HDR bloom** — visually fine, label it honestly. |
| 3 | **Real layer separation + parallax** | ★★★★★ structurally | **NO (true) / approximation only** | Pixels behind a sprite were never rendered; sliding bg behind it uncovers data we don't have (`emerald-3d-depth.md` A2 = IMPRACTICAL). The horizontal warp we already ship (`warp_scenery_eye`) is the ceiling — fake parallax with edge gaps, kept subtle. |
| 4 | **Dynamic light on sprites (normal-mapped)** | ★★★★ | **PARTIAL — the AI question, see §2** | HW can do it (`GPU_BUMP_AS_TANG`). Blocker is the normal map. Real per-sprite version out of reach at runtime; **offline-baked per-metatile/per-sprite normals** are the only viable path, and they require a custom citro3d pass (not a C2D toggle). |
| 5 | **Soft cast shadows** | ★★★ — grounds sprites | **PARTIAL/cheap fake** | We know player + OAM rects (`main.c:217`, `DepthSnap.spr[]`). Draw a squashed, blurred, dark quad on the ground plane under each sprite. Won't conform to terrain but reads fine. |
| 6 | **Stereoscopic pop** (3DS bonus axis Octopath never had) | ★★★ | **DONE** | `pop_eye`+`warp_scenery_eye`, slider-gated (`main.c:826-848`). Keep, tune disparity down (2-3 px ceiling) to kill edge ghosting. |

**The subset that gives the biggest wow cheapest: DoF/tilt-shift (#1) + LDR bloom (#2).** Both are pure fixed-function screen-space passes on the frame we already have, need zero AI, zero art, zero extra emulation, and deliver most of the HD-2D feel per GPU cycle. Everything else is a nice-to-have or out of reach.

---

## 2. AI-depth verdict — is the offline bake worth it?

**Split decision, and the split IS the answer:**

- **AI monocular DEPTH (MiDaS / Depth-Anything-V2) → NOT worth it.** Two walls. (a) **Runtime is impossible**: these are 25M-300M+ param nets; the 804 MHz ARM11 (no NPU, fixed-function GPU) is off by orders of magnitude — and the cores are already saturated, so per-frame inference is a non-starter, not a close call. (b) **Even offline-baked, depth on 16px pixel-art tiles is garbage**: these models train at 384-518px on photographic perspective cues (vanishing lines, defocus, texture gradient) that a 16px GBA tile physically lacks; Depth-Anything tokenizes at 14px patches, so a whole sprite is ~1 patch → coherent blob, no usable structure. **And we already have a better depth signal**: `build_depth_grid`'s analytic layer-type grid (`tdepth`) encodes Emerald's *actual* occlusion semantics — it is more correct and is dynamic, which baked AI depth can never be. **Keep `build_depth_grid`; skip AI depth entirely.**

- **AI NORMAL maps → worth it, but only for sprite/tile lighting, and only as an offline art-pipeline tool.** Normal maps are *dramatically* more robust on pixel art than depth, because a normal is a *local* surface-orientation estimate (gradients, silhouette inflation, emboss) that survives at 16px where global perspective does not. A mature ecosystem exists for exactly this (Laigter, Sprite DLight, SpriteIlluminator, Sprite Lamp, open-source Unflattener). The PICA200 was *built* to consume these (`GPU_BUMP_AS_TANG` + `C3D_LightEnv`, or a single `GPU_DOT3_RGB` TEV stage for N·L). So normals feed the one HD-2D cue the hardware renders natively. **This is the genuine "lit diorama" unlock — but read §3's cost correction before budgeting it.**

**Worth it for:** dynamic directional/time-of-day light on overworld sprites and foreground tiles (the "3D-feeling shading on 2D art" cue). **Not for:** per-pixel displacement / stereo (analytic `tdepth` wins there).

**Pipeline, if pursued (M4, optional):**
1. Pull tileset + overworld sprite PNGs and `metatile_attributes.bin` from the **pret/pokeemerald decomp** (clean assets, no ROM-ripping; `porytiles` round-trips tile↔metatile-id, giving the exact runtime key).
2. Upscale each tile/sprite ~4-8× (nearest + mild edge-preserving smooth) before inference.
3. Run a normal-map generator (Laigter / Sprite DLight class, or Marigold-style normal model) offline on the PC.
4. Pack outputs as **256×256 tiled RGB8 normal atlases**, keyed by metatile id (mirrors the color tileset) and by OAM tile.
5. Ship in the `.cia` (~1-3 MB total — noise against embedded libmgba + ROM). Bind per-map; at runtime look up the normal tile for each visible metatile (id already computed in `metatile_layer`) / on-screen OAM sprite (already scanned into `DepthSnap.spr[]`).

**Honest caveats on the bake:** (a) AI normals on 16px art need a per-character QA pass — it's a *starting point*, not push-button; for many flat tiles a hand/procedural normal is as good. (b) **Emerald-specific, zero generality** — runtime VRAM tiles can be re-banked/palette-swapped/animated, so matching baked normals to live tiles is a per-game reverse-engineering project. Same "Emerald-only vs general" trap the KB flags. (c) The cheap **AI-free substitute** — a Sobel of `tdepth` driving one directional TEV light — is *much* weaker than the studies' optimistic "~70% of the feel": `tdepth` is 1 sample per 16px tile (too coarse to shade sprites) and is discrete layer-type plateaus (Sobel is mostly zero with hard step edges → blocky faceted banding). Treat it as a faint large-scale terrain cue (~30-40%), not real relighting. **The honest call: spend the AI budget only if M1-M3 land and look good; otherwise skip it — DoF+bloom deliver more HD-2D per cycle.**

---

## 3. Recommended rendering recipe on the PICA200

Building on the existing per-eye + depth-grid code. All passes run on the main thread between `C3D_FrameBegin/End` (`main.c:823`), reusing the offscreen target machinery (`preTex`/`preTgt`, `main.c:448-452`).

**Stereo displacement — vertex-grid warp vs per-tile quad shift.** The studies recommend replacing today's per-quad `warp_scenery_eye`/`pop_eye` (which shift whole 16×16 quads and **tear at edges**) with a **vertex-grid warp**: tessellate the 240×160 frame into a grid (15×10=150 quads, or 30×20=600 at 8px), and in a custom vertex shader offset each vertex X by `sign(eye)·disparity·depth(vertex)`, depth from `tdepth`. Shared verts → depth discontinuities **stretch** the texture instead of leaving a hole → soft stretch, not hard tear. This is strictly better quality at ~0 net GPU cost (vertex work is the PICA200's strong suit). **Important integration cost:** this needs a custom `.v.pica` vertex shader and steps **outside citro2d** (C2D owns its own shader + texenv and resets them on `C2D_Prepare`/`C2D_Flush`) — you draw the warp with raw C3D, then `C2D_Prepare` again for HUD. That sequencing is the real cost, not GPU time. **Verdict: a worthwhile M2, but it's a genuine new render pass alongside C2D, not a flag flip.**

**DoF/bloom — texenv multi-pass, NOT a single trick.** TEV cannot do dependent texture reads (no per-pixel UV offset), so blur = real downsample + separable passes. Cheap fakes that fit: bilinear "free blur" (downscale through a half-res target with `GPU_LINEAR`, upscale back = ~1 extra pass, reusing the `preTex`/`preTgt` machinery + a *new* half-res scratch target — you cannot blur in-place on the one target you own); tilt-shift = blur only top+bottom bands; 2-tap TEV `GPU_INTERPOLATE` "poor man's blur." Convincing depth-graded DoF wants ≥2 blur radii (near AND far) → ~4-6 passes, not 2-3. Bloom = bright-pass (`GPU_SUBTRACT` threshold) → quarter-res separable blur → additive (`C3D_AlphaBlend`) → ~3-4 passes.

**Concrete pass/frame budget at 60fps (main thread, between FrameBegin/End):**

Today (verified `main.c:823-908`): top-L, top-R (when 3D on), bottom = up to 3 game renders, each up to 2 passes (sharp-bilinear prescale) + per-eye pop/warp overdraws + HUD ≈ **~5-7 effective passes/frame**, comfortably 60 dual-game (v0.4 GREEN on real hardware).

| Effect | Passes added/frame | 60fps dual-game? |
|---|---|---|
| Vertex-grid stereo warp (replaces `pop_eye`/`warp_scenery_eye`) | ~0 net (swaps quad overdraws for one grid draw/eye) + state churn | ✅ better quality, plausible |
| TEV `GPU_DOT3_RGB` / light-env normal lighting (top, focused) | 0 (extra TEV stages in existing draw) | ✅ if drawn in one pass |
| Tilt-shift bands, **focused top screen only** | +1-2 | ✅ tight |
| LDR bloom, **focused top only** | +2-3 | ⚠️ borderline |
| Full separable depth-graded DoF, **both eyes** | +4-6 | ❌ → 30fps or single-game |
| Full DoF + bloom, all 3 game images | +10-14 | ❌ → 30fps single-game |

**The canary is the per-pass `GX_DisplayTransfer` + render-target rebind + texenv/shader reconfig overhead on the main thread — NOT pixel fill at 240×160.** That main thread also orchestrates the per-frame `LightEvent` handshake with the two saturated workers, so extra target binds/stalls eat into the 16.7 ms the workers depend on. **Stereoscopic 3D already triples game renders (top-L + top-R + bottom), which multiplies every post-FX pass** — so stack effects on the *focused top screen only*, never per-eye-per-screen.

**Recipe to build (the one I recommend):**
1. **Tilt-shift bands** on the focused top screen via one half-res `GPU_LINEAR` bounce (+1-2 passes). Skip per-pixel depth-graded DoF and skip the unfocused/bottom screen.
2. **LDR bloom**, focused-only, bright-pass+downsample+add (+2-3). Drop it first if the frame-budget canary trips.
3. **Tune the existing pop/warp** down to 2-3 px disparity (kill edge ghosting); later upgrade to the vertex-grid warp for continuous stretch.
4. **(Optional)** fake soft shadows under player/OAM rects.
5. **(Optional, last)** baked-normal lighting via a separate citro3d pass — only if budget + visuals justify a new subsystem.

**Hard rule (CLAUDE.md convention #6):** every budget number above is a reasoned estimate, **not measured**. Azahar does not model core-2 contention or the 804 MHz frame budget. Nothing here is "done" until the worst-frame timer (`worstMs`, `main.c:907`) confirms ≤16.7 ms on real New 3DS.

---

## 4. Milestone plan (each hardware-verifiable)

Ordered by impact-per-effort; each is independently shippable and abandonable. Each ends at the **`worstMs` hardware gate** (already instrumented at `main.c:907`).

**M1 — Tilt-shift DoF on the existing depth grid.** *Biggest wow, cheapest.*
- Files: `source/main.c` — add a half-res scratch target beside `preTex` (`main.c:448-452`); add a `dof_pass()` that blurs top/bottom bands of the focused top screen (reuse the `GPU_LINEAR` prescale pattern at `main.c:351`); call it after `render_game(topG,...)` (`main.c:828`) but before HUD; add a "DoF" menu toggle (the menu block at `main.c:416-428` + `Settings` at `main.c:371-381`).
- USER verifies: on real New 3DS, overworld top/bottom blur softly while the mid-band stays crisp → "diorama" read; `worstMs` HUD stays ≤16.7 ms with both games running; toggle persists across reboot.
- DROP if: ugly band seams, or `worstMs` breaches budget even focused-only.

**M2 — Vertex-grid per-pixel depth warp (replace per-tile quad shift).**
- Files: new `source/warp.v.pica` (vertex shader) + Makefile picaup; `source/main.c` — a raw-C3D grid-draw path replacing `pop_eye`/`warp_scenery_eye` (`main.c:248-256, 310-317`), re-`C2D_Prepare` after; feed `tdepth` as a per-vertex attribute.
- USER verifies: with the 3D slider up, foreground edges **stretch** instead of ghosting/tearing; player still pops; `worstMs` unchanged vs the quad version; A/B against current build shows fewer edge artifacts.
- DROP if: the C2D↔C3D state-switch churn pushes `worstMs` over budget, or stretch artifacts read worse than the current subtle quad pop. (Fallback: keep the shipped quad warp.)

**M3 — LDR bloom.**
- Files: `source/main.c` — `bloom_pass()` (bright-pass TEV `GPU_SUBTRACT` → quarter-res separable blur in a new scratch target → additive blend), focused top only; menu toggle.
- USER verifies: bright spots (water glints, lamps, white UI) glow on real hardware; `worstMs` ≤16.7 ms; visibly drops to first if M1+M3 stacked breach budget.
- DROP if: borderline budget (it's the most expendable effect) or LDR glow looks muddy on RGB565.

**M4 — (Optional) AI-baked normals for sprite/tile lighting.** *Highest effort, lowest certainty — do only if M1-M3 land well.*
- Files: new `tools/bake-normals/` (decomp PNGs → normal atlas keyed by metatile id + OAM tile, via Laigter/Sprite-DLight class tool); new normal `C3D_Tex` atlas loaded per map; `source/main.c` — a **separate citro3d lit pass** (custom vertex shader with position/texcoord/**normal**/**tangent** attrs + `C3D_LightEnv` + `GPU_BUMP_AS_TANG`, or single `GPU_DOT3_RGB` TEV stage) interleaved with the C2D passes; one key light tied to in-game time-of-day; menu toggle. No mGBA/MPL files touched.
- USER verifies: overworld sprites/tiles visibly shade with a directional light (day/night glow) on real hardware; `worstMs` ≤16.7 ms with the extra pipeline switch; per-character QA shows no broken normals.
- DROP if (likely): the new C2D↔C3D-lit↦C2D pipeline switch breaches budget; AI normals look wrong on too many sprites; Emerald-only scope isn't worth the effort. **Default expectation: this is the one to cut.**

---

## What to DROP / never attempt
- **AI monocular depth for displacement** — garbage on 16px art and dominated by the analytic `tdepth` signal already shipped. (Keep `build_depth_grid`.)
- **True layer separation / occlusion-correct parallax** — needs per-layer re-emulation; the saturated dual cores can't pay (`emerald-3d-depth.md` §0/A2). Document as the hardware wall.
- **Full per-pixel depth-graded DoF on both eyes, or any post-FX per-eye-per-screen** — crosses the 60fps line (stereo already triples renders). Focused-top-only or 30fps/single-game "cinematic" mode only.
- **Sobel-of-`tdepth` fake normal lighting sold as "real relighting"** — too coarse/blocky; at most a faint terrain cue, not a hero effect.

**Bottom line:** We can turn the flat GBA frame into a *lit, focus-graded 2.5D diorama* using only fixed-function PICA200 units + the depth grid we already build — that's HD-2D-flavored post on a 2D frame, the honest achievable win. We cannot get native HD-2D geometry, and AI is worth it *only* as an offline normal-map baker for the optional lighting milestone, never as a runtime or depth source.

**Key code anchors (all verified live):** `projects/dual-gba/source/main.c` — `render_game` (322-366), `preTex`/`preTgt` offscreen target (448-452), `build_depth_grid`/`tdepth[10][15]` (281-307), `metatile_layer` (260-278), `DepthSnap`/OAM rects (214-219), `pop_eye`/`warp_scenery_eye` (248-256, 310-317), per-eye render block (823-851), `worstMs` budget timer (907). Verdict basis: `projects/dual-gba/docs/kb/emerald-3d-depth.md` §0/§1. GPU caps: `/opt/devkitpro/libctru/include/c3d/{texenv.h,light.h,fog.h,lightlut.h,proctex.h}`, `3ds/gpu/enums.h` (TEV 369-373, `GPU_BUMP_AS_TANG` 423), `3ds/gpu/shbin.h:11-12` (no fragment shader). Note: this document was not written to disk per instructions — it is returned here for the parent agent to place (suggested home: `projects/dual-gba/docs/kb/hd2d-rendering.md`, listed in `docs/kb/00-overview.md`).