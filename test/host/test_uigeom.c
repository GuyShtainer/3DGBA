// test_uigeom.c — PC host unit test for the phase-17 rounded-rect geometry (source/uigeom.{c,h}).
// Covers SPEC-widgets W2.6 T1-T11: the clamp, the non-overlap + exact-cover invariants the alpha
// scrims depend on, the bounds/quad-count budget, the AREA regression that NAMES the bug (T5 is
// red on the legacy three-rect decomposition and green on the staircase), the monotone
// silhouette, the degenerate cases, the pixel-grid property, the outline decomposition and its
// area identity, and the segmented-control radius ladder.
// Pure-C dual-compile per CLAUDE.md rule #4 (uigeom.c is header-free by design).
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_uigeom.c source/uigeom.c -lm -o /tmp/tug && /tmp/tug

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../../source/uigeom.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// The PRE-FIX ui_fill decomposition, kept verbatim so T5's regression is proven, not asserted:
// "rectangle minus four SQUARE corners of side r" — a plus/cross as r -> min(w,h)/2.
static int legacy_quads(float x, float y, float w, float h, float r, UiQuad* o) {
	if (r <= 0.0f || w <= 2.0f * r || h <= 2.0f * r) {
		o[0].x = x; o[0].y = y; o[0].w = w; o[0].h = h; return 1;
	}
	o[0].x = x + r;       o[0].y = y;     o[0].w = w - 2 * r; o[0].h = h;         // centre column
	o[1].x = x;           o[1].y = y + r; o[1].w = r;         o[1].h = h - 2 * r; // left strip
	o[2].x = x + w - r;   o[2].y = y + r; o[2].w = r;         o[2].h = h - 2 * r; // right strip
	return 3;
}

static float area(const UiQuad* q, int n) {
	float a = 0.0f;
	for (int i = 0; i < n; i++) a += q[i].w * q[i].h;
	return a;
}
// True rounded-rect area: the rect minus the four circular corner bites r^2(1 - pi/4) each.
static float true_area(float w, float h, float r) {
	float R = ui_round_clamp_r(w, h, r);
	return w * h - 4.0f * R * R * (1.0f - (float)M_PI / 4.0f);
}
static int overlaps(UiQuad a, UiQuad b) {
	float ox = fminf(a.x + a.w, b.x + b.w) - fmaxf(a.x, b.x);
	float oy = fminf(a.y + a.h, b.y + b.h) - fmaxf(a.y, b.y);
	return (ox > 1e-4f && oy > 1e-4f);
}
static float overlap_area(UiQuad a, UiQuad b) {
	float ox = fminf(a.x + a.w, b.x + b.w) - fmaxf(a.x, b.x);
	float oy = fminf(a.y + a.h, b.y + b.h) - fmaxf(a.y, b.y);
	return (ox > 0.0f && oy > 0.0f) ? ox * oy : 0.0f;
}

static const float W[] = { 5, 10, 13, 14, 15, 16, 20, 22, 26, 30, 46, 84, 208, 296 };
static const float H[] = { 5, 6, 10, 13, 14, 15, 16, 20, 22, 26, 30, 34, 42 };

// ---------------------------------------------------------------------------- T1: the clamp
static void t1_clamp(void) {
	printf("TEST 1  ui_round_clamp_r never exceeds min(w,h)/2\n");
	for (unsigned i = 0; i < sizeof W / sizeof *W; i++)
		for (unsigned j = 0; j < sizeof H / sizeof *H; j++)
			for (float r = -2.0f; r <= 40.0f; r += 0.5f) {
				float R = ui_round_clamp_r(W[i], H[j], r);
				float m = (W[i] < H[j] ? W[i] : H[j]) * 0.5f;
				CHECK(R >= 0.0f && R <= m + 1e-4f, "clamp(%g,%g,%g)=%g > %g\n", W[i], H[j], r, R, m);
				if (r > 0.0f && r <= m) CHECK(fabsf(R - r) < 1e-4f, "clamp changed an in-range r (%g -> %g)\n", r, R);
			}
	CHECK(ui_round_clamp_r(20, 20, 0.0f) == 0.0f, "r=0 must stay 0\n");
	CHECK(ui_round_clamp_r(20, 20, -5.0f) == 0.0f, "negative r must clamp to 0\n");
	CHECK(ui_round_clamp_r(0, 20, 4.0f) == 0.0f, "degenerate w must clamp to 0\n");
	CHECK(ui_round_clamp_r(20, 20, NAN) == 0.0f, "NaN r must clamp to 0\n");
}

// ------------------------------------------------- T2/T3/T4/T6: the invariants over the matrix
static void t2346_matrix(void) {
	printf("TEST 2/3/4/6  non-overlap, exact vertical cover, bounds+budget, monotone silhouette\n");
	int cases = 0;
	for (unsigned i = 0; i < sizeof W / sizeof *W; i++)
		for (unsigned j = 0; j < sizeof H / sizeof *H; j++) {
			float rs[10] = { 0, 1, 2, 3, 4, 5, 6, 8, H[j] / 2.0f, W[i] / 2.0f };
			for (int k = 0; k < 10; k++) {
				UiQuad q[UI_ROUND_MAX_QUADS];
				int n = ui_round_rect_quads(10.0f, 20.0f, W[i], H[j], rs[k], q, UI_ROUND_MAX_QUADS);
				cases++;
				// T4 — budget + validity
				if (n > UI_ROUND_MAX_QUADS) { CHECK(0, "quad count %d over budget (%g x %g r%g)\n", n, W[i], H[j], rs[k]); continue; }
				for (int a = 0; a < n; a++) {
					if (!(q[a].w > 0.0f && q[a].h > 0.0f)) { CHECK(0, "empty quad %d (%g x %g r%g)\n", a, W[i], H[j], rs[k]); break; }
					if (!(q[a].x >= 10.0f - 1e-4f && q[a].x + q[a].w <= 10.0f + W[i] + 1e-4f &&
					      q[a].y >= 20.0f - 1e-4f && q[a].y + q[a].h <= 20.0f + H[j] + 1e-4f)) {
						CHECK(0, "quad %d out of bounds (%g x %g r%g)\n", a, W[i], H[j], rs[k]); break;
					}
				}
				// T2 — no two quads overlap
				int bad = 0;
				for (int a = 0; a < n && !bad; a++)
					for (int b = a + 1; b < n && !bad; b++)
						if (overlaps(q[a], q[b])) bad = 1;
				if (bad) CHECK(0, "overlapping quads (%g x %g r%g)\n", W[i], H[j], rs[k]);
				// T3 — the union's y-extent is exactly [y, y+h]
				float ymin = 1e9f, ymax = -1e9f;
				for (int a = 0; a < n; a++) { if (q[a].y < ymin) ymin = q[a].y; if (q[a].y + q[a].h > ymax) ymax = q[a].y + q[a].h; }
				if (n > 0 && (fabsf(ymin - 20.0f) > 1e-3f || fabsf(ymax - (20.0f + H[j])) > 1e-3f))
					CHECK(0, "y-extent [%g,%g] != [20,%g] (%g x %g r%g)\n", ymin, ymax, 20.0f + H[j], W[i], H[j], rs[k]);
				// T6 — the staircase never widens going inward (per-cap insets strictly decreasing)
				float R = ui_round_clamp_r(W[i], H[j], rs[k]);
				if (R >= 0.5f) {
					int N = ui_round_steps(R);
					float prev = 1e9f;
					for (int a = 0; a < n && a < 2 * N; a += 2) {
						float in = q[a].x - 10.0f;
						if (!(in < prev - 1e-5f) || !(in < R + 1e-4f)) { CHECK(0, "inset not strictly decreasing (%g x %g r%g band %d)\n", W[i], H[j], rs[k], a / 2); break; }
						prev = in;
					}
				}
			}
		}
	CHECK(cases > 1500, "matrix too small (%d cases)\n", cases);
	printf("  ...%d shapes checked\n", cases);
}

// ------------------------------------------------ T5: the regression that names the plus/cross
static void t5_area_regression(void) {
	printf("TEST 5  emitted area within 10%% of a true rounded rect (LEGACY must FAIL, new must PASS)\n");
	struct { float w, h, r; const char* what; } C[] = {
		{ 5,  5,  2, "ui_dot HUD identity mark" },
		{ 30, 15, 5, "pause feature pill" },
		{ 22, 20, 4, "gamepad colour swatch" },
		{ 10, 10, 4, "toggle knob" },
	};
	for (unsigned i = 0; i < sizeof C / sizeof *C; i++) {
		float want = true_area(C[i].w, C[i].h, C[i].r) * 0.90f;
		UiQuad q[UI_ROUND_MAX_QUADS];
		int n = ui_round_rect_quads(0, 0, C[i].w, C[i].h, C[i].r, q, UI_ROUND_MAX_QUADS);
		float a = area(q, n);
		CHECK(a >= want, "%s: new area %.2f < %.2f\n", C[i].what, a, want);
		UiQuad l[4];
		float la = area(l, legacy_quads(0, 0, C[i].w, C[i].h, C[i].r, l));
		CHECK(la < want, "%s: LEGACY area %.2f should be short of %.2f (the regression is gone?)\n",
		      C[i].what, la, want);
		printf("  %-28s legacy %.2f | new %.2f | true %.2f\n", C[i].what, la, a, true_area(C[i].w, C[i].h, C[i].r));
	}
}

// -------------------------------------------------------------------------- T7: degenerate in
static void t7_degenerate(void) {
	printf("TEST 7  degenerate inputs\n");
	UiQuad q[UI_ROUND_MAX_QUADS];
	CHECK(ui_round_rect_quads(0, 0, 0, 10, 3, q, UI_ROUND_MAX_QUADS) == 0, "w<=0 must emit nothing\n");
	CHECK(ui_round_rect_quads(0, 0, 10, -1, 3, q, UI_ROUND_MAX_QUADS) == 0, "h<=0 must emit nothing\n");
	int n = ui_round_rect_quads(4, 6, 20, 10, 0.4f, q, UI_ROUND_MAX_QUADS);
	CHECK(n == 1 && q[0].x == 4 && q[0].y == 6 && q[0].w == 20 && q[0].h == 10, "r<0.5 must be the plain rect (n=%d)\n", n);
	n = ui_round_rect_quads(0, 0, 12, 12, 6, q, UI_ROUND_MAX_QUADS);           // a circle
	CHECK(n == 2 * ui_round_steps(6.0f), "circle: expected %d quads, got %d\n", 2 * ui_round_steps(6.0f), n);
	float a = area(q, n), disc = (float)M_PI * 36.0f;
	CHECK(fabsf(a - disc) / disc < 0.10f, "circle area %.2f vs pi*r^2 %.2f (>10%%)\n", a, disc);
	n = ui_round_rect_quads(0, 0, 40, 6, 3, q, UI_ROUND_MAX_QUADS);            // the volume bar: a stadium
	CHECK(n > 1, "a stadium (h=6, r=3) must not fall back to a square bar (n=%d)\n", n);
}

// ---------------------------------------------------------------------------- T8: pixel grid
static void t8_pixel_grid(void) {
	printf("TEST 8  integral r (<= UI_ROUND_STEPS_MAX) puts every band boundary on a pixel\n");
	for (int r = 1; r <= UI_ROUND_STEPS_MAX; r++) {
		UiQuad q[UI_ROUND_MAX_QUADS];
		int n = ui_round_rect_quads(0, 0, 60, 40, (float)r, q, UI_ROUND_MAX_QUADS);
		for (int i = 0; i < n; i++) {
			float ry = q[i].y - floorf(q[i].y + 0.5f), rh = q[i].h - floorf(q[i].h + 0.5f);
			CHECK(fabsf(ry) < 1e-4f && fabsf(rh) < 1e-4f, "r=%d band %d not pixel-aligned (y=%g h=%g)\n", r, i, q[i].y, q[i].h);
		}
	}
}

// ---------------------------------------------------------------- T9/T10: the outline variant
static void t910_outline(void) {
	printf("TEST 9/10  outline: non-overlap, inside-outer, outside-inner, area identity, budget\n");
	struct { float w, h, r, t; } C[] = {
		{ 30, 15, 5, 1 }, { 46, 15, 5, 1 }, { 22, 13, 3, 1 }, { 84, 16, 5, 1 },
		{ 208, 30, 8, 1 }, { 36, 14, 3, 1 }, { 20, 20, 6, 2 }, { 60, 40, 6, 1 },
	};
	for (unsigned i = 0; i < sizeof C / sizeof *C; i++) {
		UiQuad q[UI_OUTLINE_MAX_QUADS];
		int n = ui_round_outline_quads(0, 0, C[i].w, C[i].h, C[i].r, C[i].t, q, UI_OUTLINE_MAX_QUADS);
		CHECK(n > 0 && n <= UI_OUTLINE_MAX_QUADS, "%gx%g r%g t%g: bad count %d\n", C[i].w, C[i].h, C[i].r, C[i].t, n);
		int bad = 0;
		for (int a = 0; a < n && !bad; a++) {
			if (!(q[a].w > 0.0f && q[a].h > 0.0f)) bad = 1;
			if (!(q[a].x >= -1e-4f && q[a].x + q[a].w <= C[i].w + 1e-4f &&
			      q[a].y >= -1e-4f && q[a].y + q[a].h <= C[i].h + 1e-4f)) bad = 1;
			for (int b = a + 1; b < n && !bad; b++) if (overlaps(q[a], q[b])) bad = 1;
		}
		CHECK(!bad, "%gx%g r%g t%g: overlapping / out-of-bounds / empty outline quad\n", C[i].w, C[i].h, C[i].r, C[i].t);
		// the interior stays transparent: no outline quad may overlap the INNER shape's fill
		UiQuad fo[UI_ROUND_MAX_QUADS], fi[UI_ROUND_MAX_QUADS];
		float ri = C[i].r - C[i].t; if (ri < 0.0f) ri = 0.0f;
		int ni = ui_round_rect_quads(C[i].t, C[i].t, C[i].w - 2 * C[i].t, C[i].h - 2 * C[i].t, ri, fi, UI_ROUND_MAX_QUADS);
		float intrude = 0.0f;
		for (int a = 0; a < n; a++)
			for (int b = 0; b < ni; b++)
				intrude += overlap_area(q[a], fi[b]);
		// Zero when the two shapes share a band grid (every real call site). When the outer radius
		// is capped (r=8 -> 6 bands of 1.33px) the outer and inner staircases differ, leaving
		// sub-pixel slivers in the four corners only (measured: 6 px^2 on a 208x30 r8 t1 ring).
		int aligned0 = (C[i].r <= (float)UI_ROUND_STEPS_MAX) && (C[i].r == floorf(C[i].r)) && (C[i].t == floorf(C[i].t));
		CHECK(intrude <= (aligned0 ? 1e-3f : 0.02f * area(q, n)),
		      "%gx%g r%g t%g: outline intrudes %.4f px^2 into the transparent interior\n",
		      C[i].w, C[i].h, C[i].r, C[i].t, intrude);
		// area identity: outline + inner fill == outer fill. EXACT (<=1%) whenever the two shapes
		// share a band grid — integral r <= UI_ROUND_STEPS_MAX and integral t, which is every real
		// call site. When the outer radius is capped (r=8 -> 6 bands of 1.33 px) the inner shape
		// samples a different grid and the two quadratures differ by ~2%; allow 3% there.
		float ao = area(fo, ui_round_rect_quads(0, 0, C[i].w, C[i].h, C[i].r, fo, UI_ROUND_MAX_QUADS));
		float ai = area(fi, ni), an = area(q, n);
		int aligned = (C[i].r <= (float)UI_ROUND_STEPS_MAX) && (C[i].r == floorf(C[i].r)) && (C[i].t == floorf(C[i].t));
		float tol = aligned ? 0.01f : 0.03f;
		CHECK(fabsf((an + ai) - ao) <= tol * ao, "%gx%g r%g t%g: outline %.2f + inner %.2f != outer %.2f (tol %g%%)\n",
		      C[i].w, C[i].h, C[i].r, C[i].t, an, ai, ao, tol * 100.0f);
	}
	// T10 — a frame thicker than the shape degrades to the solid fill
	UiQuad q[UI_OUTLINE_MAX_QUADS];
	int n = ui_round_outline_quads(0, 0, 20, 10, 4, 6, q, UI_OUTLINE_MAX_QUADS);
	float fa = true_area(20, 10, 4);
	CHECK(n > 0 && fabsf(area(q, n) - fa) / fa < 0.10f, "t >= min(w,h)/2 must degrade to the solid fill (n=%d area=%.2f want ~%.2f)\n", n, area(q, n), fa);
	// square frame path (r < 0.5) must equal the classic four strips
	n = ui_round_outline_quads(0, 0, 40, 20, 0, 1.5f, q, UI_OUTLINE_MAX_QUADS);
	CHECK(n == 4 && fabsf(area(q, n) - (2 * 40 * 1.5f + 2 * 1.5f * (20 - 3))) < 1e-3f, "square frame path wrong (n=%d)\n", n);
}

// ---------------------------------------------------------------------- T11: the radius ladder
static void t11_seg_ladder(void) {
	printf("TEST 11  ui_seg_radius ladder vs the measured art radii (SPEC-widgets 0.1)\n");
	struct { float h, want; } L[] = { { 30, 7.8f }, { 26, 6.8f }, { 25, 6.5f }, { 14, 3.6f }, { 10, 3.0f } };
	for (unsigned i = 0; i < sizeof L / sizeof *L; i++) {
		float r = ui_seg_radius(L[i].h);
		CHECK(fabsf(r - L[i].want) < 0.35f, "seg_r(%g)=%g want ~%g\n", L[i].h, r, L[i].want);
	}
	// art: seg-track 168x30 -> r 8, seg-active 74x25 -> r 6; both within a pixel
	CHECK(fabsf(ui_seg_radius(30.0f) - 8.0f) <= 1.0f, "track h30 must land within 1px of the art's r=8\n");
	CHECK(fabsf(ui_seg_radius(25.0f) - 6.0f) <= 1.0f, "active h25 must land within 1px of the art's r=6\n");
	CHECK(ui_seg_radius(4.0f) >= 3.0f && ui_seg_radius(200.0f) <= 8.0f, "ladder must stay clamped to [3,8]\n");
}

// FIX PASS (review finding 10). The Gamepad overlay is the one piece of chrome drawn EVERY frame
// of a session with two saturated GBA workers, and W2 routed its 9 key frames through
// ui_border_round — which is inherently expensive (a staircase band is 2 quads per side). The
// reviewer counted 63 -> 351 rect draws per frame at the shipped default padEdge=Round.
//
// The finding is a HEADS-UP, not a defect (~0.1-0.2 ms of main-thread work at 0.3-0.6 us per
// C2D_DrawRectSolid, about 1 % of a 16.7 ms frame), and every cheaper draw changes what the user
// sees: the keys are TRANSLUCENT, so "rounded fill in `line` + inset fill on top" would double-blend
// the interior. So instead of changing the look, the cost is PINNED here: the exact call sites, a
// per-mode budget, and a ceiling that a future radius or step-count change cannot cross silently.
// Azahar cannot price a frame (CLAUDE.md #6) — this is a quad COUNT, not a timing claim.
static void t12_pad_quad_budget(void) {
	printf("TEST 12  gamepad overlay quad budget per frame (fix pass, finding 10)\n");
	// touch.c pad_overlay(), verbatim: 4 d-pad zones + A/B + START + L/R.
	static const struct { float w, h; } KEY[9] = {
		{ 44, 30 }, { 44, 30 }, { 32, 36 }, { 32, 36 },     // UP DOWN LEFT RIGHT
		{ 60, 60 }, { 50, 44 },                             // A B
		{ 64, 22 },                                         // START
		{ 52, 22 }, { 52, 22 },                             // L R
	};
	static const float EDGE_R[3] = { 6.0f, 3.0f, 1.0f };    // Round / Soft / Sharp
	static const int   BUDGET[3] = { 360, 200, 100 };       // ceilings, not measurements
	UiQuad q[UI_OUTLINE_MAX_QUADS];
	for (int e = 0; e < 3; e++) {
		int total = 0;
		for (int k = 0; k < 9; k++) {
			int f = ui_round_rect_quads(0, 0, KEY[k].w, KEY[k].h, EDGE_R[e], q, UI_ROUND_MAX_QUADS);
			int o = ui_round_outline_quads(0, 0, KEY[k].w, KEY[k].h, EDGE_R[e], 1.5f, q, UI_OUTLINE_MAX_QUADS);
			CHECK(f > 0 && o > 0, "edge %d key %d emitted nothing (fill %d outline %d)\n", e, k, f, o);
			total += f + o;
		}
		printf("    padEdge %d (r=%g): %d rect draws per frame for the 9 keys\n", e, EDGE_R[e], total);
		CHECK(total <= BUDGET[e], "padEdge %d costs %d quads, over the %d budget\n", e, total, BUDGET[e]);
	}
	// The per-shape ceilings the budget rests on, asserted directly so the two cannot drift.
	CHECK(UI_ROUND_MAX_QUADS == 13 && UI_OUTLINE_MAX_QUADS == 30, "worst-case quad caps moved\n");
}

int main(void) {
	printf("=== test_uigeom: phase-17 rounded-rect geometry (SPEC-widgets W2) ===\n");
	t1_clamp();
	t2346_matrix();
	t5_area_regression();
	t7_degenerate();
	t8_pixel_grid();
	t910_outline();
	t11_seg_ladder();
	t12_pad_quad_budget();
	printf("=== %d checks, %d failures -> %s ===\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
	return g_fail ? 1 : 0;
}
