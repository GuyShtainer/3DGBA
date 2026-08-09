// uihit.c — pure-C hit-testing + gesture state (phase-17 SPEC-input §0.1). See uihit.h for why.
#include "uihit.h"

int uihit_in(UiRect r, int px, int py) {
	if (r.w <= 0 || r.h <= 0) return 0;                 // an empty rect is un-hittable by design
	return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) ? 1 : 0;
}

int uihit_seg(UiRect r, int nseg, int px) {
	if (nseg <= 0) return -1;
	if (px < r.x || px >= r.x + r.w) return -1;
	int cw = r.w / nseg;                                 // same cell arithmetic as ui_seg_hit
	if (cw <= 0) return 0;
	int i = (px - r.x) / cw;
	if (i < 0) i = 0;
	if (i > nseg - 1) i = nseg - 1;                      // the remainder px belong to the last cell
	return i;
}

int uihit_index(const UiRect* tbl, int n, int px, int py) {
	if (!tbl) return -1;
	for (int i = 0; i < n; i++) if (uihit_in(tbl[i], px, py)) return i;
	return -1;
}

// ---- gesture ------------------------------------------------------------------------------------
UiGestEv uihit_gesture_step(UiGesture* g, int down, int held, int up, int px, int py) {
	if (!g) return GEST_NONE;
	if (down) {                                          // press: the point IS valid here
		g->active = 1; g->dragged = 0;
		g->x0 = g->x = px; g->y0 = g->y = py;
		return GEST_DOWN;
	}
	if (held && g->active) {                             // held: still valid — latch it
		g->x = px; g->y = py;
		int dx = px - g->x0, dy = py - g->y0;
		if (dx < 0) dx = -dx;
		if (dy < 0) dy = -dy;
		if (dx > UIHIT_DRAG_PX || dy > UIHIT_DRAG_PX) g->dragged = 1;   // one-way latch (§I2.1.2)
		return g->dragged ? GEST_DRAG : GEST_NONE;
	}
	if (up) {                                            // release: px,py are (0,0) — NEVER read
		UiGestEv ev = (g->active && !g->dragged) ? GEST_TAP : GEST_NONE;
		g->active = 0; g->dragged = 0;                   // full reset (§I2.1.4)
		return ev;                                       // x,y keep the last valid sample
	}
	return GEST_NONE;
}

// ---- picker tables ------------------------------------------------------------------------------
// dual: manifest select-dual-bot `dynamic` rects + the plate-measured card bodies.
static const UiRect PICK_DUAL[PICK_COUNT] = {
	{  12,  12, 296, 30 },   // PICK_MODE      seg: 1 Game / 2 Games
	{  12,  51, 296, 52 },   // PICK_SLOT_A    card body (rows 51-102)
	{  12, 111, 296, 52 },   // PICK_SLOT_B    card body (rows 111-162)
	{  12, 174, 124, 37 },   // PICK_START
	{ 144, 174, 164, 37 },   // PICK_LINKED    (x136-143 is a DEAD gutter, §I1.5.7)
	{   6, 223,  88, 16 },   // PICK_SETTINGS  SPEC-layout L2.3 (draw == hit; the lifted hint ends at 221)
};
// single: no slot B (the info panel there is not interactive), taller A card, shifted buttons.
static const UiRect PICK_SINGLE[PICK_COUNT] = {
	{  12,  12, 296, 30 },
	{  12,  51, 296, 59 },
	{   0,   0,   0,  0 },   // PICK_SLOT_B absent
	{  12, 175, 130, 37 },
	{ 150, 175, 158, 37 },   // dead gutter x142-149
	{   6, 223,  88, 16 },
};

const UiRect* uihit_pick_rects(int single, int* n) {
	if (n) *n = PICK_COUNT;
	return single ? PICK_SINGLE : PICK_DUAL;
}

UiRect uihit_pick_rect(int single, int target) {
	UiRect z = { 0, 0, 0, 0 };
	if (target < 0 || target >= PICK_COUNT) return z;
	return (single ? PICK_SINGLE : PICK_DUAL)[target];
}

int uihit_pick(int single, int px, int py) {
	int n = 0;
	const UiRect* t = uihit_pick_rects(single, &n);
	int i = uihit_index(t, n, px, py);
	return (i < 0) ? PICK_NONE : i;
}

int uihit_pick_mode_seg(int single, int px, int py) {
	UiRect r = uihit_pick_rect(single, PICK_MODE);
	if (!uihit_in(r, px, py)) return -1;
	return uihit_seg(r, 2, px);
}

// ---- scrolling ----------------------------------------------------------------------------------
int uihit_scroll_max(int n, int visRows) {
	int m = n - visRows;
	return (m > 0) ? m : 0;
}

int uihit_scroll_clamp(int base, int dyPx, int rowPx, int n, int visRows) {
	if (rowPx <= 0) rowPx = 1;
	int top = base + dyPx / rowPx;                       // C truncation: symmetric about 0
	int max = uihit_scroll_max(n, visRows);
	if (top < 0)   top = 0;
	if (top > max) top = max;
	return top;
}

int uihit_clamp_sel(int sel, int topRow, int visRows, int n) {
	if (n <= 0) return 0;
	if (sel < topRow) sel = topRow;
	if (sel > topRow + visRows - 1) sel = topRow + visRows - 1;
	if (sel > n - 1) sel = n - 1;
	if (sel < 0) sel = 0;
	return sel;
}

int uihit_follow_sel(int topRow, int sel, int visRows, int n) {
	if (sel < topRow) topRow = sel;
	if (sel >= topRow + visRows) topRow = sel - visRows + 1;
	int max = uihit_scroll_max(n, visRows);
	if (topRow > max) topRow = max;
	if (topRow < 0) topRow = 0;
	return topRow;
}

// ---- pause / settings content scroll (§I2.4) -----------------------------------------------------
int uihit_content_h(const UiRect* tbl, int n) {
	int h = UIHIT_SCREEN_H;                              // the plate is always at least a screen tall
	if (!tbl) return h;
	for (int i = 0; i < n; i++) {
		if (tbl[i].w <= 0 || tbl[i].h <= 0) continue;    // an absent control contributes nothing
		int b = tbl[i].y + tbl[i].h;
		if (b > h) h = b;
	}
	return h;
}

int uihit_max_scroll(int contentH) {
	// A tab that FITS must not scroll at all — otherwise every tab would gain 12 px of pointless
	// travel (SCREEN_H - VIEW_H) and the honest scrollbar would appear on tabs whose plate bakes
	// no track. A tab that overflows scrolls until its last row clears the hint line.
	return (contentH > UIHIT_SCREEN_H) ? contentH - UIHIT_MENU_VIEW_H : 0;
}

int uihit_scroll_px(int base, int dyPx, int maxScroll) {
	if (maxScroll < 0) maxScroll = 0;
	int s = base + dyPx;                                 // 1:1: finger and content share a screen
	if (s < 0) s = 0;                                    // §I2.2.4 no rubber-band, it just stops
	if (s > maxScroll) s = maxScroll;
	return s;
}

int uihit_follow_rect(int scroll, UiRect r, int maxScroll) {
	if (r.w <= 0 || r.h <= 0) return scroll;
	// LEAD: every row's caption is baked ~11 px ABOVE its control, so parking the control's top
	// edge exactly at the viewport top would scroll its own label out of sight — a focus ring on
	// an unlabelled widget. Pull that band in too, and treat "nearly at the top" as the top.
	if (r.y - UIHIT_MENU_LEAD < scroll) scroll = r.y - UIHIT_MENU_LEAD;      // above the viewport
	if (r.y + r.h > scroll + UIHIT_MENU_VIEW_H) scroll = r.y + r.h - UIHIT_MENU_VIEW_H;  // below it
	if (scroll < UIHIT_MENU_LEAD) scroll = 0;
	if (maxScroll < 0) maxScroll = 0;
	if (scroll > maxScroll) scroll = maxScroll;
	if (scroll < 0) scroll = 0;
	return scroll;
}

int uihit_in_scrolled(UiRect r, int scroll, int px, int py) {
	if (py >= UIHIT_MENU_VIEW_H) return 0;               // the hint band is chrome, not content
	if (r.w <= 0 || r.h <= 0) return 0;
	int y = r.y - scroll;                                // int, not int16: y can go negative
	return (px >= r.x && px < r.x + r.w && py >= y && py < y + r.h) ? 1 : 0;
}

int uihit_index_scrolled(const UiRect* tbl, int n, int scroll, int px, int py) {
	if (!tbl) return -1;
	for (int i = 0; i < n; i++) if (uihit_in_scrolled(tbl[i], scroll, px, py)) return i;
	return -1;
}

int uihit_thumb_h(int trackH, int viewH, int contentH) {
	if (trackH <= 0) return 0;
	if (contentH <= 0 || contentH <= viewH) return trackH;   // nothing to scroll: a full-length thumb
	int h = viewH * trackH / contentH;                       // the visible fraction of the content
	if (h < UIHIT_THUMB_MIN) h = UIHIT_THUMB_MIN;
	if (h > trackH) h = trackH;
	return h;
}

int uihit_thumb_y(int trackY, int trackH, int thumbH, int scroll, int maxScroll) {
	int span = trackH - thumbH;
	if (span <= 0 || maxScroll <= 0) return trackY;      // at scroll-top the thumb is at the TOP
	if (scroll < 0) scroll = 0;
	if (scroll > maxScroll) scroll = maxScroll;
	return trackY + scroll * span / maxScroll;
}

// ---- word wrap (§ SPEC-layout L5.2) --------------------------------------------------------------
// Pure arithmetic over a measurement callback; see uihit.h for why it lives here and not in main.c.
static int wrap_cont(unsigned char c) { return (c & 0xC0) == 0x80; }   // UTF-8 continuation byte

// Length of the UTF-8 sequence starting at s[0] (>=1, never past the NUL).
static int wrap_ch(const char* s) {
	int n = 1;
	while (s[n] && wrap_cont((unsigned char)s[n])) n++;
	return n;
}

int uihit_wrap(const char* s, int maxW, int maxLines, char* out, int lineCap,
               int (*meas)(const char*, void*), void* ctx) {
	if (!s || !out || !meas || maxLines <= 0 || lineCap < 8 || maxW <= 0) return 0;
	int nl = 0;
	const char* p = s;
	while (*p == ' ') p++;
	while (*p && nl < maxLines) {
		char* dst = out + nl * lineCap;
		int fit = 0, nextStart = 0, q = 0;
		while (p[q]) {                                   // grow the line one WORD at a time
			int w = q;
			while (p[w] && p[w] != ' ') w++;
			if (w > lineCap - 1) break;                  // would not fit the line buffer
			for (int i = 0; i < w; i++) dst[i] = p[i];
			dst[w] = '\0';
			if (meas(dst, ctx) > maxW) break;            // this word overflows -> the line ends here
			fit = w;
			q = w;
			while (p[q] == ' ') q++;
			nextStart = q;
			if (!p[q]) break;
		}
		if (fit == 0) {                                  // ONE word wider than the whole line
			int cand = wrap_ch(p);                       // never split a UTF-8 sequence
			for (int t = cand; p[t] && t < lineCap - 1; ) {
				int nx = t + wrap_ch(p + t);
				if (nx > lineCap - 1) break;
				for (int i = 0; i < nx; i++) dst[i] = p[i];
				dst[nx] = '\0';
				if (meas(dst, ctx) > maxW) break;
				cand = nx; t = nx;
			}
			fit = cand; nextStart = cand;
		}
		for (int i = 0; i < fit; i++) dst[i] = p[i];
		dst[fit] = '\0';
		nl++;
		p += nextStart;
		while (*p == ' ') p++;
	}
	if (*p && nl > 0) {                                  // truncated: say so, do not overflow the rect
		char* dst = out + (nl - 1) * lineCap;
		int L = 0; while (dst[L]) L++;
		for (;;) {
			if (L + 3 < lineCap) {
				dst[L] = '.'; dst[L + 1] = '.'; dst[L + 2] = '.'; dst[L + 3] = '\0';
				if (meas(dst, ctx) <= maxW) break;
				dst[L] = '\0';
			}
			if (L == 0) break;
			L--;
			while (L > 0 && wrap_cont((unsigned char)dst[L])) L--;
			dst[L] = '\0';
		}
	}
	return nl;
}
