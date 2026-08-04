// tilt.h — Phase 14 slice T1: the HD-2D "diorama" tilt math + tween state.
// ============================================================================================
// PURE C (CLAUDE.md rule #4 / PHASE.md invariant 6 / SPEC-render R6.1): <stdint.h>/<math.h>
// only — no libctru, no citro3d, no C3D_*/u32/float24, no globals besides the const angle
// ladder — so tilt.c dual-compiles on the PC host harness (test/host/test_tilt.c) and this
// header is safely includable from main.c AND the host tests. Everything GPU-shaped (matrices,
// VBOs, TEV) is built in main.c FROM this module's outputs (SPEC-render R6.7): the module's
// entire contract with the GPU is (x, y, iq, u, v, rgba) floats.
//
// THE MODEL (SPEC-render R1.1, source of truth gen1recomp Tilt.groundPoint, src/render/
// Tilt.lua:120-130, cited via docs/kb/external/gen1-render.md finding 1). The GBA frame is a
// ground plane rotated about the horizontal axis through the viewport centre by angle a and
// viewed through a pinhole at distance d = FOCAL*vh (FOCAL = 1.0, Tilt.lua:33):
//
//     u = cx - vw/2 ;  w = cy - vh/2            (w > 0 = toward the viewer)
//     q = d / (d - w*sin a)                     ("scale" in their source; strictly > 0, R1.2)
//     fx = vw/2 + k*u*q                         (k = cover scale, R2.4/R2.5.1)
//     fy = yc    + k*w*cos a * q                (yc = bottom-anchored centre row, R1.6)
//
// A plane under a pinhole IS a planar homography, so u*q, w*q and q are affine in screen space.
// gen1recomp has to smuggle the projective divide through a FRAGMENT shader (Renderer.lua:
// 456-471) because LOVE is a 2D API; the PICA200 has no fragment shader at all (3ds/gpu/shbin.h:
// 11-12 enumerates only VERTEX_SHDR and GEOMETRY_SHDR — docs/kb/hd2d-octopath-3d.md §0.1) and
// does not need one: emit clip-space w = 1/q and the fixed-function rasterizer reconstructs the
// exact same map (SPEC-render R1.3 — emit the DEPTH 1/q, NEVER q; getting that inverted is a
// visibly-wrong-but-plausible 31 px error at 15 deg).
//
// AT ANGLE 0 THE MATH IS THE IDENTITY, BITWISE: sin=0, cos=1, k=1, yc=vh/2, q=1 => fx=cx,
// fy=cy, iq=1. That is the algebraic form of PHASE.md invariant 1 (flat path byte-identical
// when tilt is off) and it is what lets the tween pass through zero without a discontinuity.
#pragma once

#include "fieldgate.h"   // the shared "is this game in a meaningful field state" predicate
                         // (G5-G8 == presence P-G6). Phase-15 SPEC-data D4.6: factor, do not copy.

// ---- shared constants ----------------------------------------------------------------------

#define TILT_FOCAL      1.0f    // d = TILT_FOCAL * vh   (gen1recomp Tilt.lua:33; SPEC-render R1.1)
#define TILT_COVER_MIX  0.0f    // SPEC-render R2.5.6: 0 = fit-to-height (shipped), 1 = zero void
#define TILT_LEVELS     4       // OFF / Soft / Med / Deep (SPEC-render R2.5.5, SPEC-integration I4.1)
// == GCTX_OVERWORLD (gamestate.h:12); _Static_assert'd in main.c:1130. Now an ALIAS of
// FIELD_CTX_OVERWORLD (fieldgate.h) so the one assert at the call site covers both users and the
// two gates can never drift apart (SPEC-data D4.6).
#define TILT_CTX_FIELD  FIELD_CTX_OVERWORLD
#define TILT_DEG2RAD    0.01745329251994329577f

// The shipped angle ladder, DEGREES (SPEC-render R2.5.5): OFF / 10 / 15 / 20, default 15 (level
// 2) when enabled. gen1's own ladder is OFF/15/35/50 (gen1-render.md finding 4) and is NOT
// defensible for us above its first rung: they render up to 2.56x more world to fill the tilted
// frustum and we have a fixed 240x160 composited frame, so at 35 deg we would lose 43.6 source
// px per side at the near row (SPEC-render R2.4/R2.5.5). Retained frame: 97.4 / 95.9 / 94.1 %.
// SPEC-integration §0.3 hands this table to the gate/tween half.
extern const float TILT_ANGLE_DEG[TILT_LEVELS];

// ---- gate + tween policy (SPEC-integration §1-§2, §5) --------------------------------------

// THE GATE (SPEC-integration §1.4 / I1.10). One flat struct so the whole policy is a PURE
// FUNCTION of observable state, which is what lets the host suite enumerate it exhaustively
// (PHASE.md invariant 6; test/host/test_tilt.c TEST 1-3). main.c fills it and calls
// tilt_target_level ONCE PER SCREEN PER FRAME; no gate rule exists anywhere else (I1.11) —
// in particular the render block contains no gating logic at all.
//
// Every field is a plain int so this header stays free of GameCtx/libctru types (the pure-C
// rule). The ctx enum crosses the boundary pinned at the CALL SITE by
//     _Static_assert(GCTX_OVERWORLD == TILT_CTX_FIELD, ...)     // main.c, I1.4
// so a future insert into GameCtx (gamestate.h:10-20) is a COMPILE ERROR, not a silent mis-gate.
typedef struct {
	int userLevel;     // the SAVED preference 0..TILT_LEVELS-1 (g_prefs.tiltLevel once §4 lands)
	int screen;        // 0 = top, 1 = bottom. The gate is PER SCREEN, by the game ON it (I1.8)
	int ok;            // game_read returned true for that screen's game (i.e. a profile exists)
	int ctx;           // GameState.ctx as read, compared against TILT_CTX_FIELD
	int sb1Valid;      // GameState.sb1Valid — the save is loaded, so px/py are real
	int px;            // GameState.px (-1 = gSaveBlock1Ptr not ready)
	int textDlg;       // GameState.textDlg — a field textbox is up = a script is talking
	int menuOpen;      // OUR pause menu is up
	int wlOn, netOn;   // wireless / loopback net link live (linkOn is deliberately NOT a rule)
	int touchActive;   // tmEff != TOUCH_OFF — bottom screen only (§3, PHASE.md invariant 4)
	int stereoEngaged; // top screen only: the per-eye pop/warp passes run this frame (pop3d)
	int isN3DS;        // APT_CheckNew3DS (main.c:3312)
	int fsOn;          // frameskip: the UNFOCUSED game is being starved to free budget
	int focScreen;     // which SCREEN the focused game sits on (main.c:2832)
} TiltGateIn;

// Returns the target level, 0..TILT_LEVELS-1 — the value fed to tilt_tween_step every frame.
// Evaluation is a ladder of eleven 0-returning rules (G1-G11, SPEC-integration §1.4 table);
// first hit wins, so the result is order-independent and no rule can ever "unblock" another.
// A closed gate is NOT a snap: the caller keeps feeding this every frame and the 250 ms tween
// carries the angle down (I2.5).
int tilt_target_level(const TiltGateIn* in);

#define TILT_TWEEN_MS   250.0f  // ~0.25 s, gen1recomp's tween (PHASE.md invariant 5; SPEC-int I2.4)
#define TILT_EPS_DEG    0.001f  // "angle > 0" epsilon for tilt_active (SPEC-integration I2.6)
#define TILT_DT_MAX_MS  100.0f  // apt_hook suspends the app; clamp the resume delta (SPEC-int I2.3)

// One per SCREEN (SPEC-integration I2.8), stepped from the wall clock (osGetTime, main.c:1777)
// rather than a frame counter: the render loop is capped to 60 fps but not floored (main.c:2805
// only waits when UNDER budget), so a frame-counted tween would visibly run slow under exactly
// the load tilt adds. Wall-clock also makes the tween a pure function of dtMs — host-testable
// with no fake clock (SPEC-integration I2.2; note the divergence from dofLvl/bloomLvl's fixed
// per-frame increments at main.c:2596-2601 so nobody "fixes" it back).
typedef struct {
	float angFrom, angTo, ang;   // DEGREES; ang is what the renderer consumes
	float tMs;                   // elapsed ms inside the current segment, clamped to TILT_TWEEN_MS
	int   level;                 // last target level fed in (Tilt.active()'s "level > 0" half)
} TiltTween;

void tilt_tween_reset(TiltTween* t);
void tilt_tween_step (TiltTween* t, int level, float angTo, float dtMs);

// THE one check PHASE.md invariant 1 hangs on: level > 0 OR angle > 0, tween included — verbatim
// gen1recomp Tilt.active() (Tilt.lua:110-114, gen1-render.md finding 3). The level half matters
// on the first frame after the user enables tilt (target set, angle still 0); the angle half on
// every frame of the tween back down. When it returns 0 the renderer MUST take today's flat
// render_game blit unchanged.
int  tilt_active(const TiltTween* t);

float tilt_angle_deg_for_level(int level);   // level -> degrees (clamped to the ladder)
float tilt_angle_for_level    (int level);   // level -> RADIANS (SPEC-render R6.4)

// ---- projection / corners / view growth (SPEC-render R1, R2, R6) ----------------------------

// Precomputed per image per frame by tilt_view_init; tilt_project does no trig (R6.4.2), just
// ~4 mul + 2 add + 1 div per vertex.
typedef struct {
	float angle;      // tween angle, RADIANS (0 = flat)
	float sinA, cosA;
	float vw, vh;     // frame extent (240, 160 = GBA_W, GBA_H)
	float d;          // focal distance = TILT_FOCAL * vh
	float k;          // cover scale (R2.4)
	float yc;         // frame-space y the source centre row projects to (R1.6, bottom-anchored)
} TiltView;

void tilt_view_init (TiltView* v, float angleRad, float vw, float vh, float coverMix);

// Frame space -> frame space. Writes q, NOT 1/q: keeping the module in q matches gen1's published
// formulas so a reader can diff them line by line (R6.4.1). main.c emits vert->iq = 1.0f/q.
// The screen fit (calc_xform, main.c:709-715) is applied AFTERWARDS by the caller and never baked
// in here (R1.7) — affine o homography is still a homography, so perspective-correct
// interpolation survives the screen fit for all three scale modes.
void tilt_project   (const TiltView* v, float cx, float cy, float* fx, float* fy, float* q);
// Exact inverse (R6.5). Not needed by the shipped configuration — tilt is top-screen only and the
// bottom screen (the touch controller) stays flat, so touch_to_gba is bit-identical to today
// (SPEC-render R4.8.1, SPEC-integration I3.1) — but host-tested here so enabling bottom tilt
// later is a one-line change and not a re-derivation.
void tilt_unproject (const TiltView* v, float fx, float fy, float* cx, float* cy);

float tilt_top_scale (float angleRad);   // q at the far edge:  1/(1 + 0.5 sin a)  (Tilt.lua:133)
float tilt_bot_scale (float angleRad);   // q at the near edge: 1/(1 - 0.5 sin a)
float tilt_cover_fit (float angleRad);   // (1 - 0.25 sin^2 a)/cos a  — bottom-anchored fit (R2.3)
float tilt_cover_full(float angleRad);   // 1 + 0.5 sin a             — zero-void cover  (R2.4)
float tilt_cover     (float angleRad, float mix);   // fit + mix*(full - fit)          (R2.5.1)

// k*q(cy): the factor by which the homography multiplies a frame-space stereo displacement made
// BEFORE projection. Every stereo pass re-clamps against POP_DISP_MAX (main.c:694) with this, so
// the hardware-validated comfort ceiling is preserved exactly instead of being quietly raised by
// 17-25 % at the near edge (SPEC-render R4.1.1). Returns exactly 1.0f at angle 0.
float tilt_disp_scale(const TiltView* v, float cy);

// Coverage diagnostics (HUD / BUILDLOG / tests), GBA px per side (SPEC-render R2.4):
//   voidFarPerSide  — DESTINATION px of the frame rect left uncovered at the far row (the wedge)
//   cropMidPerSide  — SOURCE columns lost per side at the centre row
//   cropNearPerSide — SOURCE columns lost per side at the near row (what the R3.6 scissor eats)
void tilt_coverage(const TiltView* v, float* voidFarPerSide,
                   float* cropMidPerSide, float* cropNearPerSide);
