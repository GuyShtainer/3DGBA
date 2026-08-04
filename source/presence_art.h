// presence_art.h — Phase 15 co-op presence, slice M2: the RENDER half's pure-C core.
// ============================================================================================
// Spec: docs/phase15-presence/SPEC-avatar.md — A1 (the sprite asset), A2.4 (culling + clipping),
// A2.6.3 (the walk phase), A3.4 (the y-sort). PHASE.md invariant 8 ("the screen-tile math,
// staleness/liveness rules, interpolation and the same-map predicate live in a header-free module
// with golden tests") applies to this half too: EVERY decision the avatar draw makes that is not a
// citro2d call lives here, so test/host/test_presence.c can prove it on the PC.
//
// PURE C (CLAUDE.md rule #4): <stdint.h> and "presence.h" are the only includes — no <3ds.h>, no
// citro*, no <math.h>, no globals. main.c owns the C2D_DrawImageAt, the GX transfer and the
// tilt_project call; this module owns the sheet layout, the placeholder pixels, the sprite rect,
// the clip and the walk cycle.
//
// WHAT THIS MODULE DELIBERATELY DOES NOT DO (SPEC-data D0.2 / BUILDLOG M0 deviation 10):
//   * It never re-derives a tile->screen mapping and never re-applies a sub-tile term. The data
//     half already returns a FOOT ANCHOR in GBA frame space (PresenceOut.footX/footY) with the
//     engine's own SUB() term folded in for BOTH cameras, so the sub-tile BG-scroll tracking and
//     the tile-to-tile interpolation this slice is asked for are ALREADY EXACT at the seam.
//     Re-applying them here would double them. presence_art_rect() is the whole conversion.
//   * It never imports the tilt geometry. tilt_project is a pure translation of the foot anchor
//     applied by the caller AFTER the clip (SPEC-avatar A3.1/A3.2.1); nothing here knows an angle
//     exists, which is why the clip is one function with two callers and one test.
#pragma once
#include <stdint.h>

#include "presence.h"   // PeerPresence, presence_sub, PRES_DIR_*, PRES_FRAME_W/H, PRES_MAX_PEERS

// ---- the sheet (SPEC-avatar A1.1, A1.2, A1.3) ------------------------------------------------
// One frame cell is 16x32 GBA frame px: the BOTTOM 16 px are the character's tile, the TOP 16 px
// are the head, which overhangs the tile above. That is not a guess — main.c:693 states exactly
// this rect for the player's own sprite ("player tile (7,5) -> sprite rect (16x32, head 16px above
// the tile)") and it is the placement the 3D standee pass was hardware-validated with.
#define PRES_CELL_W        16
#define PRES_CELL_H        32
#define PRES_SHEET_DIM     128            // POT, RGBA8 => 64 KB (A1.3)
#define PRES_VAR_STRIDE    64             // x offset of the female variant block (A1.3)
#define PRES_SHEET_BYTES   (PRES_SHEET_DIM * PRES_SHEET_DIM * 4)
#define PRES_ART_COLS      3              // STAND / STEP_A / STEP_B
#define PRES_ART_ROWS      3              // DOWN / UP / LEFT   (RIGHT = LEFT mirrored, A1.2)
#define PRES_VAR_W         (PRES_ART_COLS * PRES_CELL_W)   // 48 — the used extent of one variant
#define PRES_VAR_H         (PRES_ART_ROWS * PRES_CELL_H)   // 96

// Poses. Our own animation vocabulary — we never read the peer's OAM/animation state and make no
// claim to reproduce the game's timing (A1.2, A2.6.3).
#define PRES_POSE_STAND    0
#define PRES_POSE_STEP_A   1
#define PRES_POSE_STEP_B   2
#define PRES_POSE_COUNT    3

// The foot anchor sits at the cell's BOTTOM CENTRE (A1.1): (cellX + 8, cellY + 16) where
// (cellX, cellY) is the TILE cell top-left and the art is drawn at (cellX, cellY - 16). Composing,
// the sprite top-left is (footX - 8, footY - 32) — which is the only conversion this module does.
#define PRES_FOOT_DX       8
#define PRES_FOOT_DY       32

// ---- the placeholder's colours (A1.5.2) ------------------------------------------------------
// Colour words are 0xRRGGBBAA. That convention is pinned by SPEC-avatar A1.5.2's own literal for
// the magenta tell, 0xFF00FFFF (R=FF G=00 B=FF A=FF = magenta; read as 0xAABBGGRR the same word
// would be yellow), and it is deliberately NOT C2D_Color32's packing — this module must not know
// citro2d exists. presence_art_build() emits the GPU's byte order; see its comment.
#define PRES_ART_MAGENTA   0xFF00FFFFu
#define PRES_ART_BLACK     0x000000FFu

// ---- walk animation (A2.6.3) -----------------------------------------------------------------
#define PRES_IDLE_FRAMES   6      // no world travel for this many frames -> STAND, accumulator := 0
#define PRES_WALK_BEAT_PX  4      // one animation beat per 4 world px => a 16 px tile step plays a
                                  //   full 4-beat cycle, the natural cadence for ~8 frames/tile
extern const uint8_t PRES_WALK_CYCLE[4];   // { STEP_A, STAND, STEP_B, STAND } (A2.6.3)

// Per-peer walk state. Consumer-side, NOT a wire type. Stepped exactly ONCE per frame per peer, on
// the render thread, next to presence_solve — never inside the draw, because the top screen draws
// twice (left eye + right eye) and a per-draw accumulator would animate at double rate and desync
// the two eyes' poses, which on a parallax-barrier panel is visible as flicker rather than as a
// fast walk.
typedef struct {
	uint8_t  have;          // a previous sample exists
	int8_t   mapG, mapN;    // the map the last sample was on (a map change resets, A2.6.4)
	int32_t  wx, wy;        // last peer WORLD pixel position (16*tile - SUB(phase))
	uint32_t round;         // the record's producer round at the last sample (staleness, A2.6.4)
	int32_t  travel;        // accumulated |delta| world px since the last idle reset
	uint16_t idle;          // consecutive frames with zero travel
	uint8_t  pose;          // PRES_POSE_* — the current frame selection
} PresWalk;

void presence_walk_reset(PresWalk* w);

// The peer's OWN world pixel position, i.e. the peer half of the D5.3 anchor expression
// (16*px - SUB(phase)), which is monotone at 1 px per frame while they walk and jumps by nothing
// at the tile boundary. Using THIS rather than the foot anchor is load-bearing: the foot anchor is
// a DIFFERENCE and slides whenever the HOST walks, so a host-driven accumulator would animate a
// standing peer every time the player moves (and freeze them when both walk in step).
void presence_art_world_px(const PeerPresence* pe, int* wx, int* wy);

// Advance the walk phase by one frame and return the pose (PRES_POSE_*). `pe` is the peer's record
// as published. A discontinuity (map change, a stale/rewound producer round, or the caller's own
// "we did not draw last frame") restarts the cycle at STAND so a peer who walks off-screen and
// back does not resume mid-stride from a stale accumulator (A2.6.4).
int  presence_walk_step(PresWalk* w, const PeerPresence* pe);

// ---- geometry: sprite rect, cull and clip (A1.1, A2.4) ---------------------------------------

// Foot anchor (frame space) -> sprite top-left (frame space). The ONE conversion, A1.1.
void presence_art_rect(float footX, float footY, float* sprX, float* sprY);

// The clipped draw, all in GBA FRAME space. `w`/`h` are the VISIBLE extent in source px (the cell
// is drawn 1:1 in frame space; the screen fit is applied afterwards by the caller, never baked in
// — SPEC-render R1.7, SPEC-avatar A2.2), and `cx`/`cy` are the offset INSIDE the 16x32 cell of the
// visible part, so the visible pixels stay registered exactly where they were.
typedef struct {
	float x, y;     // frame-space top-left of the VISIBLE part
	int   w, h;     // visible size, source px (0 < w <= 16, 0 < h <= 32)
	int   cx, cy;   // source offset inside the cell of that visible part
} PresArtDraw;

// Trim the sprite rect to the 240x160 frame IN SOURCE SPACE — the same operation draw_pop_tex
// performs for the stereo pops (main.c:735-742), and for the same reason its comment gives: the
// destination is clipped to the on-screen frame box so a shifted copy "never bleeds into the
// letterbox (per-eye rivalry on the border)". An avatar visible in one eye and clipped in the
// other AT the frame border is binocular rivalry, which is uncomfortable rather than merely wrong.
//
// `mirror` matters: a mirrored draw maps the destination's LEFT edge to the source's RIGHT edge,
// so a cut taken off the destination left must be taken off the source RIGHT. Getting that
// backwards is invisible in the middle of the screen and produces a sprite that appears to slide
// inside its own box at the edges — TEST 28 pins both orientations.
//
// `margin` widens the clip box to [-margin, 240+margin] x [-margin, 160+margin], in frame px.
//
// FIX PASS (review finding 4). The FLAT path passes 0 and is byte-identical to before. The TILT
// path passes the view's own SPILL — how far the projected image reaches outside the flat frame —
// and that is what makes A3.2.1's decided order ("clip the unprojected rect, THEN translate")
// correct instead of merely cheap.
//
// THE DEFECT IT FIXES IS AMPUTATION. The trim used to be taken at the flat frame edge and the
// TRIMMED rect was then translated somewhere else entirely, so near the frame edges a sprite lost a
// vertical slice at a boundary it no longer touches. Worked case, 20 deg: a peer whose foot is at
// frame x = 4 has its cell at x = -4, so the flat box cuts 4 SOURCE COLUMNS — a quarter of the
// character — and the surviving 12 are then drawn at x = -25.5..-13.5, where the tilted image's
// near row spills to -26.5. The result is a complete-looking sprite standing on perfectly visible
// ground with a slice missing out of its side, which reads as a rendering bug and is nowhere near
// any edge the viewer can see. With the spill box the whole 16 columns draw, and at most ~3 px of
// them overhang the ground — exactly as the base image's own trapezoid corners overhang the frame
// box. Widening rather than clipping-in-projected-space is the SAME argument phase 14 used to ship
// TILT_SCISSOR 0 (main.c:1067-1074): under tilt the base image already spills, stereo is mutually
// exclusive with tilt (rule G10) so the letterbox-rivalry reason for a hard boundary is vacuous
// exactly when this is nonzero, and the avatar should be allowed to leave the rect as much as the
// ground it stands on does. Under NO tilt the boundary stays hard, which is what A2.4.2's rivalry
// argument requires.
//
// WHAT IT DELIBERATELY DOES NOT DO is rescue a peer the DATA half already culled. The horizontal
// map at any row is x -> 120 + s*(x - 120) with s > 0, so a flat x outside [0, 240] lands outside
// that row's projected extent EXACTLY (and likewise in y): a peer whose flat art rect misses the
// flat frame misses the tilted image too, and presence_solve's cull (which knows nothing about
// tilt, D5.7.1) is therefore the right authority for "is this peer on the picture at all". This
// margin only ever affects a PARTIALLY visible sprite. Residual: the box is a rectangle and the
// image is a trapezoid, so the over-spill above is real, bounded by half a cell, lands in the
// letterbox, and stays verify-on-hw under A7.2 H5.
//
// Returns 0 when the rect is fully outside that box (== culled; A2.4.1: no edge-clamping, no
// off-screen arrow, no "peer is nearby" indicator — the honest ceiling is that you see them when
// they are on your screen).
int presence_art_clip(float sprX, float sprY, int mirror, float margin, PresArtDraw* out);

// ---- the sheet layout (A1.2.1: ONE place maps a facing code to a cell) ----------------------
typedef struct {
	int x, y;      // texel origin of the 16x32 cell inside the 128x128 sheet
	int mirror;    // 1 => draw with a negated horizontal scale (EAST is WEST mirrored, A1.2.2)
} PresArtCell;

// `gender` 0 = male / 1 = female (anything else -> male, A0.4). `dir` is PRES_DIR_* 1..4; ANY
// out-of-range value selects row 0 col 0 — the DOWN standing frame — and can never index outside
// the sheet (A0.4). `pose` is PRES_POSE_*; out of range -> STAND.
void presence_art_cell(int gender, int dir, int pose, PresArtCell* out);

// ---- the placeholder sheet (A1.5) ------------------------------------------------------------
// Fills a LINEAR RGBA8 buffer of exactly PRES_SHEET_BYTES with the placeholder walker sheet:
// 2 variants x 3 facings x 3 poses = 18 cells, everything outside the two 48x96 variant blocks
// fully transparent.
//
// BYTE ORDER. `accentRGBA`/`inkRGBA` are 0xRRGGBBAA words (above); the bytes WRITTEN are A,B,G,R
// ascending, which is the PICA200's GPU_RGBA8 texel layout. main.c hands the buffer straight to a
// GX_TRANSFER_FMT_RGBA8 -> GX_TRANSFER_FMT_RGBA8 display transfer, which is a pure linear->tiled
// reshuffle of 4-byte units and converts nothing, so what is written here is what the sampler
// reads. VERIFY-ON-HW (A7.2 item H13's photo settles it): if the placeholder comes out with red
// and blue swapped, the fix is the byte order in this one function's put_px and nothing else.
//
// The two MAGENTA TELLS (a 1-px outline around the silhouette and a 2x2 checker in each cell's
// top-left 4x4) are not conditional, not behind a flag, and must not be removed by anything except
// real art landing (A1.5.3). #FF00FF appears nowhere in theme.c and nowhere in a Gen-3 overworld
// palette, so a screenshot says "placeholder" instantly.
void presence_art_build(uint8_t* rgba8, uint32_t accentRGBA, uint32_t inkRGBA);

// ---- y-sort (A3.4) ---------------------------------------------------------------------------
// gen1-render.md finding 2: "ONE y-sorted list of all billboards keyed on baseline world y
// (farther rows project higher/smaller, so back-to-front is just ascending baseline y)". With
// PRES_MAX_PEERS == 1 the sort is a no-op today; it is written NOW because retrofitting order into
// a shipped draw is the expensive version of this (A3.4), and because the flagged 3-4 player work
// is a bound change rather than a redesign (presence.h PRES_MAX_PEERS).
typedef struct {
	float    fy;     // the FOOT y actually drawn at: projected under tilt, flat otherwise
	uint16_t tid;    // deterministic tie-break — a flickering tie-break reads as z-fighting
	uint8_t  slot;   // peer slot, carried through the sort
} PresBillboard;

int  presence_art_ycmp (const PresBillboard* a, const PresBillboard* b);   // <0 / 0 / >0
void presence_art_ysort(PresBillboard* b, int n);                          // ascending foot y
