// uigeom.c — see uigeom.h. Pure C: <math.h> only, no libctru, no citro2d, no globals.
#include "uigeom.h"
#include <math.h>

float ui_round_clamp_r(float w, float h, float r) {
	if (!(r > 0.0f)) return 0.0f;                    // also swallows NaN
	float m = (w < h ? w : h) * 0.5f;
	if (!(m > 0.0f)) return 0.0f;
	return r > m ? m : r;
}

int ui_round_steps(float r) {
	if (!(r > 0.0f)) return 1;
	int n = (int)ceilf(r);
	if (n < 1) n = 1;
	if (n > UI_ROUND_STEPS_MAX) n = UI_ROUND_STEPS_MAX;
	return n;
}

// Horizontal inset of a cap of radius R, sampled `d` px in from the shape's top/bottom edge.
// d >= R is the straight middle (inset 0); d <= 0 is the very corner (inset R).
static float inset_at(float R, float d) {
	if (!(R > 0.0f)) return 0.0f;
	if (d >= R)      return 0.0f;
	if (d <= 0.0f)   return R;
	float dy = R - d;
	float s  = R * R - dy * dy;
	if (s <= 0.0f) return R;
	return R - sqrtf(s);
}

int ui_round_rect_quads(float x, float y, float w, float h, float r, UiQuad* out, int cap) {
	if (!out || cap <= 0) return 0;
	if (!(w > 0.0f) || !(h > 0.0f)) return 0;
	float R = ui_round_clamp_r(w, h, r);
	if (R < 0.5f) {                                   // sub-pixel radius: the old fast path
		out[0].x = x; out[0].y = y; out[0].w = w; out[0].h = h;
		return 1;
	}
	int N = ui_round_steps(R);
	if (cap < 2 * N + 1) {                            // never emit a partial silhouette
		out[0].x = x; out[0].y = y; out[0].w = w; out[0].h = h;
		return 1;
	}
	float s = R / (float)N;
	int n = 0;
	for (int k = 0; k < N; k++) {                     // top + bottom cap bands, midpoint-sampled
		float in = inset_at(R, (k + 0.5f) * s);
		float qw = w - 2.0f * in;
		if (!(qw > 0.0f)) continue;
		out[n].x = x + in; out[n].y = y + (float)k * s;             out[n].w = qw; out[n].h = s; n++;
		out[n].x = x + in; out[n].y = y + h - (float)(k + 1) * s;   out[n].w = qw; out[n].h = s; n++;
	}
	float bodyH = h - 2.0f * R;
	if (bodyH > 0.0f) { out[n].x = x; out[n].y = y + R; out[n].w = w; out[n].h = bodyH; n++; }
	return n;
}

int ui_round_outline_quads(float x, float y, float w, float h, float r, float t,
                           UiQuad* out, int cap) {
	if (!out || cap <= 0) return 0;
	if (!(w > 0.0f) || !(h > 0.0f) || !(t > 0.0f)) return 0;
	if (2.0f * t >= w || 2.0f * t >= h)               // the frame swallows the interior
		return ui_round_rect_quads(x, y, w, h, r, out, cap);

	float R = ui_round_clamp_r(w, h, r);
	if (R < 0.5f) {                                   // square frame: the classic four strips
		if (cap < 4) return 0;
		out[0].x = x;         out[0].y = y;           out[0].w = w; out[0].h = t;
		out[1].x = x;         out[1].y = y + h - t;   out[1].w = w; out[1].h = t;
		out[2].x = x;         out[2].y = y + t;       out[2].w = t; out[2].h = h - 2.0f * t;
		out[3].x = x + w - t; out[3].y = y + t;       out[3].w = t; out[3].h = h - 2.0f * t;
		return 4;
	}
	int N = ui_round_steps(R);
	float s  = R / (float)N;
	float Ri = R - t; if (Ri < 0.0f) Ri = 0.0f;       // the inset shape's own radius

	// Strip boundaries: the outer cap staircase, plus y+t / y+h-t so no strip ever STRADDLES the
	// inner shape's top/bottom edge (it would then be emitted full-width over the interior).
	float bd[2 * (UI_ROUND_STEPS_MAX + 1) + 2];
	int nb = 0;
	for (int k = 0; k <= N; k++) bd[nb++] = y + (float)k * s;
	for (int k = 0; k <= N; k++) bd[nb++] = y + h - R + (float)k * s;
	bd[nb++] = y + t; bd[nb++] = y + h - t;
	for (int a = 1; a < nb; a++) {                    // insertion sort (nb <= 16)
		float v = bd[a]; int b = a - 1;
		while (b >= 0 && bd[b] > v) { bd[b + 1] = bd[b]; b--; }
		bd[b + 1] = v;
	}
	float sy[2 * UI_ROUND_STEPS_MAX + 3][2];
	int ns = 0;
	for (int a = 0; a + 1 < nb; a++) {
		float ya = bd[a], yb = bd[a + 1];
		if (ya < y - 1e-4f) ya = y;
		if (yb > y + h + 1e-4f) yb = y + h;
		if (yb - ya <= 1e-4f) continue;               // duplicate / out-of-range boundary
		if (ns >= 2 * UI_ROUND_STEPS_MAX + 3) break;
		sy[ns][0] = ya; sy[ns][1] = yb; ns++;
	}
	if (cap < 2 * ns) return 0;

	int n = 0;
	float pO = 0.0f, pI = 0.0f; int pHasInner = 0, haveRun = 0;   // run-length merge state
	float runY0 = 0.0f, runY1 = 0.0f;
	for (int i = 0; i <= ns; i++) {
		float o = 0.0f, ii = 0.0f; int hasInner = 0;
		if (i < ns) {
			float ymid = 0.5f * (sy[i][0] + sy[i][1]);
			float dTop = ymid - y, dBot = (y + h) - ymid;
			o = inset_at(R, dTop < dBot ? dTop : dBot);
			float dti = ymid - (y + t), dbi = (y + h - t) - ymid;
			if (dti > 0.0f && dbi > 0.0f) { hasInner = 1; ii = t + inset_at(Ri, dti < dbi ? dti : dbi); }
		}
		int same = haveRun && i < ns && hasInner == pHasInner &&
		           fabsf(o - pO) < 1e-4f && fabsf(ii - pI) < 1e-4f;
		if (same) { runY1 = sy[i][1]; continue; }
		if (haveRun) {                                 // flush the accumulated run
			float hh = runY1 - runY0;
			if (hh > 0.0f) {
				if (!pHasInner) {
					float qw = w - 2.0f * pO;
					if (qw > 0.0f) { out[n].x = x + pO; out[n].y = runY0; out[n].w = qw; out[n].h = hh; n++; }
				} else {
					float lw = pI - pO;
					if (lw > 0.0f) { out[n].x = x + pO;      out[n].y = runY0; out[n].w = lw; out[n].h = hh; n++; }
					if (lw > 0.0f) { out[n].x = x + w - pI;  out[n].y = runY0; out[n].w = lw; out[n].h = hh; n++; }
				}
			}
			haveRun = 0;
		}
		if (i < ns) { pO = o; pI = ii; pHasInner = hasInner; runY0 = sy[i][0]; runY1 = sy[i][1]; haveRun = 1; }
	}
	return n;
}

float ui_seg_radius(float h) {
	float r = 0.26f * h;
	if (r < 3.0f) r = 3.0f;
	if (r > 8.0f) r = 8.0f;
	return r;
}
