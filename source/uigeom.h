// uigeom.h — PURE C rounded-rect geometry for the citro2d widget kit (phase 17, SPEC-widgets W2).
//
// Why this file exists: `ui_fill` used to approximate a rounded rect as "rectangle minus four
// SQUARE corners of side r" (a full-height centre column + two shortened side strips). That is
// fine while r << min(w,h)/2 and degenerates into a literal PLUS/CROSS as r -> min(w,h)/2 —
// which is exactly what the phase-16 sweep captured on the pause chip row, the in-game identity
// dot and the gamepad colour swatches (report D6).
//
// The DECISION (which quads make the shape) lives here, header-free, so the PC host harness can
// unit-test it (CLAUDE.md rule #4, the pattern tilt.{c,h} / control.{c,h} already follow).
// `ui.c` keeps only the blitting.
//
// Contract, relied on by callers that draw with alpha (touch.c's scrims are black at a=0x96):
// the emitted quads are NON-OVERLAPPING and cover exactly the y-range [y, y+h].
//
// Host test: test/host/test_uigeom.c
#pragma once
#include <stdint.h>

typedef struct { float x, y, w, h; } UiQuad;

#define UI_ROUND_STEPS_MAX   6                            /* staircase bands per cap          */
#define UI_ROUND_MAX_QUADS   (1 + 2 * UI_ROUND_STEPS_MAX) /* = 13, the fill worst case        */
/* Outline worst case = 30: the outer staircase's 2*MAX+1 strips plus the two extra boundaries at
   y+t / y+h-t (so no strip straddles the inner shape's edge), two quads each. */
#define UI_OUTLINE_MAX_QUADS (2 * (2 * UI_ROUND_STEPS_MAX + 3))

// Clamp a requested corner radius into [0, min(w,h)/2]. NaN / negative / degenerate -> 0.
float ui_round_clamp_r(float w, float h, float r);
// Staircase bands per cap: clamp(ceil(r), 1, UI_ROUND_STEPS_MAX). For integral r <= MAX the band
// height is exactly 1.0, so the steps land on device-pixel boundaries.
int   ui_round_steps(float r);

// Emit a non-overlapping quad decomposition of the rounded rect (x,y,w,h,r).
// Returns the quad count (0 if degenerate). `cap` should be >= UI_ROUND_MAX_QUADS; a smaller cap
// falls back to the plain rectangle rather than emitting a partial shape.
int ui_round_rect_quads(float x, float y, float w, float h, float r, UiQuad* out, int cap);

// The same shape as a `t`-thick INSIDE outline (transparent interior), run-length merged.
// `cap` should be >= UI_OUTLINE_MAX_QUADS. t >= min(w,h)/2 degrades to the solid fill.
int ui_round_outline_quads(float x, float y, float w, float h, float r, float t,
                           UiQuad* out, int cap);

// Radius ladder for the segmented control, derived from the shipped art: seg-track 168x30 -> r 8,
// seg-active 74x25 -> r 6 (ratio ~0.26), clamped to the range the art actually uses.
float ui_seg_radius(float h);
