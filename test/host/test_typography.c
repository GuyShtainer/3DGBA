// test_typography.c — PC host unit test for the type ladder (source/typography.h) and the
// five baked faces in data/ (phase 18's crispness law; phase 19's sizes).
//
// This is the regression barrier for SPEC-crisp R1: "every text draw is at texel scale EXACTLY
// 1.0 with an integer origin". The blur the user reported on hardware was not a subtle
// mis-tuning — every one of the 16 (face, px) pairs the app used was off 1.0, between 0.577x
// and 1.049x — and it survived two phases because the check that was supposed to catch it
// (measure the face with C2D_TextGetDimensions and divide) measures citro2d's NORMALISED
// 30-px-based height, which is ~the same for every font and so always "confirms" the scale.
//
// So this suite refuses to measure anything through citro2d. It parses the shipped .bcfnt bytes
// directly, recomputes the scale with the SAME pure-C helpers the app calls (typography.h, no
// second copy of the arithmetic — CLAUDE.md rule #4), and asserts exact equality. A tolerance is
// deliberately NOT used: the ladder is built so 1.0 is reachable, and a tolerance is how a
// ladder rots.
//
// T1  every role's face exists in data/ and parses as a CFNT
// T2  FINF.lineFeed == the role's draw px, EXACTLY (the crispness law, bake side)
// T3  typo_texel_scale(...) == 1.0f exactly, for every role (the crispness law, draw side)
// T4  the OLD ceil()-based s_native does NOT give 1.0 — the bug is reproduced, not assumed
// T5  the phase-17 four-face pack is gone from data/ (a stale .bin would be silently embedded)
// T6  the non-ASCII codepoints the UI prints have real glyphs, per family (no tofu)
// T6b every non-ASCII codepoint in any UI string literal exists in at least one family
// T7  role table hygiene: unique syms, sane pt, px == lineFeed for all, no duplicate rungs
// T8  sheet geometry is self-consistent (A4, POT, every glyph addressable)
// T9  source lint: not one reference to the phase-17 face enum survives
// T10 every re-fitted label still fits the real box it is drawn into
// T11 text drawn under the menu content clears the fixed chrome repaint  FIX PASS finding 3
// T12 the system-font budget: no NEW draw bypasses the role ladder     FIX PASS
// T13 the bake is stem-snapped: solid ink, not grey mush   PHASE 18 second cause
// T14 the touch explainer wraps inside its rect, in all three copies         PHASE 19 / G2
// T15 the alias contract: 7 roles, 5 faces, and data/ holds exactly those  PHASE 19
// T16 vertical fit: every fixed-offset row keeps its INK inside its box     PHASE 19 / G2
// T17 cap height per rung — the "it is actually bigger" claim, from the bytes  PHASE 19
// T18 the declared ink box (typography.h) matches the shipped bytes         PHASE 19 / G2
// T19 one module per band: the bottom HUD bar vs the touch mode chip     PHASE 19 FIX PASS
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_typography.c source/uihit.c \
//         -lm -o /tmp/tty && /tmp/tty
//   (run from the project root — T1/T5/T6 read data/, T6b/T9 read source/. source/uihit.c is
//    linked as of phase 19: T14 drives the SHIPPED uihit_wrap rather than a copy of it.)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include "../../source/typography.h"
#include "../../source/uihit.h"       // FIX PASS: T11 grades a real viewport constant
#include "../../source/presence_ui.h" // PHASE 19: T10 grades the presence card's own copy

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] "); printf(__VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

// ---------------------------------------------------------------- a minimal BCFNT reader
// Offsets verified against the shipped data/fnt_*.bin (tools/fontlab/bcfnt.py cross-checks the
// same numbers and renders legible glyphs from them):
//   FINF+8 fontType, +9 lineFeed, +16 tglpOff, +20 cwdhOff, +24 cmapOff (offsets are ABSOLUTE)
//   TGLP+8 cellW, +9 cellH, +10 baseline, +11 maxCharW, +12 sheetSize, +16 nSheets, +18 fmt,
//        +20 nColumns, +22 nRows, +24 sheetW, +26 sheetH, +28 sheetDataOffset (absolute)
//   CWDH+8 start, +10 end, +12 nextOff, +16 (left i8, glyphWidth u8, charWidth u8)*
//   CMAP+8 codeBegin, +10 codeEnd, +12 method, +16 nextOff, +20 payload

typedef struct {
	unsigned char* b;
	long           n;
	int lineFeed, cellW, cellH, baseline, nSheets, fmt, nCols, nRows, sheetW, sheetH;
	unsigned sheetSize, sheetOff;
	long finf, tglp;
} Font;

static unsigned rd16(const unsigned char* p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned rd32(const unsigned char* p) {
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static long find_blk(const unsigned char* b, long n, const char* tag, long from) {
	for (long i = from; i + 4 <= n; i++)
		if (b[i] == (unsigned char)tag[0] && !memcmp(b + i, tag, 4)) return i;
	return -1;
}

static int font_open(Font* f, const char* path) {
	memset(f, 0, sizeof *f);
	FILE* fp = fopen(path, "rb");
	if (!fp) return 0;
	fseek(fp, 0, SEEK_END); f->n = ftell(fp); fseek(fp, 0, SEEK_SET);
	f->b = (unsigned char*)malloc((size_t)f->n);
	if (!f->b || fread(f->b, 1, (size_t)f->n, fp) != (size_t)f->n) { fclose(fp); return 0; }
	fclose(fp);
	if (f->n < 64 || memcmp(f->b, "CFNT", 4)) return 0;
	f->finf = find_blk(f->b, f->n, "FINF", 0);
	f->tglp = find_blk(f->b, f->n, "TGLP", 0);
	if (f->finf < 0 || f->tglp < 0) return 0;
	f->lineFeed  = f->b[f->finf + 9];
	f->cellW     = f->b[f->tglp + 8];
	f->cellH     = f->b[f->tglp + 9];
	f->baseline  = f->b[f->tglp + 10];
	f->sheetSize = rd32(f->b + f->tglp + 12);
	f->nSheets   = (int)rd16(f->b + f->tglp + 16);
	f->fmt       = (int)rd16(f->b + f->tglp + 18);
	f->nCols     = (int)rd16(f->b + f->tglp + 20);
	f->nRows     = (int)rd16(f->b + f->tglp + 22);
	f->sheetW    = (int)rd16(f->b + f->tglp + 24);
	f->sheetH    = (int)rd16(f->b + f->tglp + 26);
	f->sheetOff  = rd32(f->b + f->tglp + 28);
	return 1;
}
static void font_close(Font* f) { free(f->b); f->b = NULL; }

// codepoint -> glyph index, walking the CMAP chain (all three mapping methods).
static int font_glyph(const Font* f, unsigned cp) {
	long off = find_blk(f->b, f->n, "CMAP", 0);
	int guard = 0;
	while (off > 0 && guard++ < 64) {
		unsigned lo = rd16(f->b + off + 8), hi = rd16(f->b + off + 10);
		unsigned method = rd16(f->b + off + 12), nxt = rd32(f->b + off + 16);
		const unsigned char* p = f->b + off + 20;
		if (cp >= lo && cp <= hi) {
			if (method == 0) { unsigned base = rd16(p); unsigned gi = base + (cp - lo);
			                   return gi == 0xFFFF ? -1 : (int)gi; }
			if (method == 1) { unsigned gi = rd16(p + 2 * (cp - lo)); return gi == 0xFFFF ? -1 : (int)gi; }
		}
		if (method == 2) {
			unsigned cnt = rd16(p);
			for (unsigned i = 0; i < cnt; i++)
				if (rd16(p + 2 + 4 * i) == cp) { unsigned gi = rd16(p + 4 + 4 * i);
				                                 return gi == 0xFFFF ? -1 : (int)gi; }
		}
		off = nxt ? (long)nxt - 8 : 0;
	}
	return -1;
}

// glyph index -> advance width (0 => an empty cell, i.e. tofu or a space)
static int font_char_w(const Font* f, int gi) {
	long off = find_blk(f->b, f->n, "CWDH", 0);
	int guard = 0;
	while (off > 0 && guard++ < 64) {
		int start = (int)rd16(f->b + off + 8), end = (int)rd16(f->b + off + 10);
		unsigned nxt = rd32(f->b + off + 12);
		if (gi >= start && gi <= end) return f->b[off + 16 + 3 * (gi - start) + 2];
		off = nxt ? (long)nxt - 8 : 0;
	}
	return -1;
}

// ---- glyph alpha (T13, the stem-snap floor) ----
//
// A4 sheets are stored in 8x8 Morton-swizzled tiles, two 4-bit alphas per byte. Decoding a
// glyph's own cell (rather than counting the whole sheet) matters: the sheet is 1024x1024 and
// mostly empty, and jbm's 1326 glyphs include CJK whose ink distribution says nothing about
// running Latin text. This measures exactly what tools/fontlab/sharpen.py reports at bake time.
static int morton8(int x, int y) {
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
	     | ((x & 4) << 2) | ((y & 4) << 3);
}

// -> alpha nibble 0..15 at (x, y) of sheet 0, or -1 out of range.
static int sheet_nib(const Font* f, int x, int y) {
	if (x < 0 || y < 0 || x >= f->sheetW || y >= f->sheetH) return -1;
	int tw = f->sheetW / 8;
	long tbase = (long)((y / 8) * tw + (x / 8)) * 32;
	int m = morton8(x & 7, y & 7);
	long idx = (long)f->sheetOff + tbase + (m >> 1);
	if (idx < 0 || idx >= f->n) return -1;
	unsigned char b = f->b[idx];
	return (m & 1) ? (b >> 4) : (b & 0x0F);
}

// Ink row extent of a glyph inside its cell -> cap height in device px (PHASE 19 / T17).
// Returns the number of rows that carry ANY ink, or -1 if the cell is not decodable. Measured on
// 'H' this is the cap height ISO 9241-303 means by "character height", i.e. the quantity the whole
// legibility argument in SPEC-legible L1 is denominated in — read from the bytes that ship, so
// "the text got bigger" is an assertion here and not a screenshot impression.
static int glyph_ink_rows(const Font* f, int gi, int* top) {
	int per = f->nCols * f->nRows;
	if (per <= 0 || gi < 0 || gi >= per) return -1;
	int line = gi / f->nCols, col = gi % f->nCols;
	int x0 = col * (f->cellW + 1) + 1, y0 = line * (f->cellH + 1) + 1;
	int lo = -1, hi = -1;
	for (int y = 0; y < f->cellH; y++)
		for (int x = 0; x < f->cellW; x++) {
			int nib = sheet_nib(f, x0 + x, y0 + y);
			if (nib < 0) return -1;
			if (nib) { if (lo < 0) lo = y; hi = y; break; }
		}
	if (lo < 0) return 0;
	if (top) *top = lo;
	return hi - lo + 1;
}

// PHASE 19 / SPEC-legible L3.2 — the ink extent of a whole RUN inside its line box.
// Vertical fit is not a cap-height question: a row overflows when its DESCENDERS hit the thing
// below it, and it collides upward when its ASCENDERS do. So the quantity every §L3.2 number is
// denominated in is the ink box over "AHgpy1:9" (caps, ascender, descenders, digits, colon) —
// the tallest and deepest thing any UI string can contain. Measured here from the shipped bytes
// and cross-checked against typography.h's declared table by T18.
static int str_ink_extent(const Font* f, const char* s, int* top, int* bottom) {
	int lo = -1, hi = -1;
	for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
		int gi = font_glyph(f, *p);
		if (gi < 0) continue;
		int gt = -1, rows = glyph_ink_rows(f, gi, &gt);
		if (rows <= 0 || gt < 0) continue;
		if (lo < 0 || gt < lo) lo = gt;
		if (gt + rows - 1 > hi) hi = gt + rows - 1;
	}
	if (lo < 0) return 0;
	if (top) *top = lo;
	if (bottom) *bottom = hi;
	return hi - lo + 1;
}

// Ink histogram (16 buckets) over a glyph's cell, accumulated into h[].
static int glyph_hist(const Font* f, int gi, long* h) {
	int per = f->nCols * f->nRows;
	if (per <= 0 || gi < 0 || gi >= per) return 0;   /* sheet 0 only; ASCII always lands there */
	int line = gi / f->nCols, col = gi % f->nCols;
	int x0 = col * (f->cellW + 1) + 1, y0 = line * (f->cellH + 1) + 1;
	for (int y = 0; y < f->cellH; y++)
		for (int x = 0; x < f->cellW; x++) {
			int nib = sheet_nib(f, x0 + x, y0 + y);
			if (nib < 0) return 0;
			h[nib]++;
		}
	return 1;
}

// ---------------------------------------------------------------- helpers

// T14 drives the SHIPPED uihit_wrap, which measures through a callback so the arithmetic and the
// metrics stay independent (uihit.h's own note). On the device the callback is assets_text_w; here
// it is the same sum of charWidths the rest of this suite uses, over the same face.
static const Font* g_measFont = NULL;
static int run_width(const Font* f, const char* s);
static int meas_cb(const char* str, void* ctx) {
	(void)ctx;
	return g_measFont ? run_width(g_measFont, str) : 0;
}

static const char* role_name(TxtRole r) {
	switch (r) {
	case TXT_TITLE: return "TXT_TITLE"; case TXT_BUTTON: return "TXT_BUTTON";
	case TXT_BODY: return "TXT_BODY";   case TXT_SEG: return "TXT_SEG";
	case TXT_SECTION: return "TXT_SECTION"; case TXT_CHIP: return "TXT_CHIP";
	case TXT_VALUE: return "TXT_VALUE"; default: return "?";
	}
}

// The exact ceil() citro2d's C2D_TextGetDimensions applies — the phase-17 s_native.
static float shipped_ceil_s_native(int lineFeed, int cellH) {
	return ceilf((float)lineFeed * 30.0f / (float)cellH);
}

// PHASE 18 FIX PASS. Count whole-identifier occurrences of `name` in a C file, ignoring comments
// and string literals — T12's instrument. Comment-blind grepping would count this file's own prose
// (and the app's, which now explains at every such call site why it is one), so the scan has to
// parse. `ui_text_w` is a MEASUREMENT, not a draw, and identifier matching keeps it out for free.
static int count_ident(const char* path, const char* name) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return -1;
	static char buf[1 << 21];
	size_t len = fread(buf, 1, sizeof buf - 1, fp);
	fclose(fp);
	buf[len] = 0;
	size_t nl = strlen(name);
	int hits = 0, in_str = 0, in_ch = 0, in_line = 0, in_blk = 0;
	for (size_t i = 0; i < len; i++) {
		char c = buf[i];
		if (in_line) { if (c == '\n') in_line = 0; continue; }
		if (in_blk)  { if (c == '*' && buf[i+1] == '/') { in_blk = 0; i++; } continue; }
		if (in_str)  { if (c == '\\') i++; else if (c == '"')  in_str = 0; continue; }
		if (in_ch)   { if (c == '\\') i++; else if (c == '\'') in_ch  = 0; continue; }
		if (c == '/' && buf[i+1] == '/') { in_line = 1; i++; continue; }
		if (c == '/' && buf[i+1] == '*') { in_blk  = 1; i++; continue; }
		if (c == '"')  { in_str = 1; continue; }
		if (c == '\'') { in_ch  = 1; continue; }
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
			size_t j = i;
			while (j < len && (((buf[j] >= 'A') && (buf[j] <= 'Z')) || ((buf[j] >= 'a') && (buf[j] <= 'z')) ||
			                   ((buf[j] >= '0') && (buf[j] <= '9')) || buf[j] == '_')) j++;
			if (j - i == nl && !memcmp(buf + i, name, nl)) hits++;
			i = j - 1;
		}
	}
	return hits;
}

static int file_contains(const char* path, const char* needle) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return -1;
	static char buf[1 << 20];
	size_t n = fread(buf, 1, sizeof buf - 1, fp);
	fclose(fp);
	buf[n] = 0;
	return strstr(buf, needle) != NULL;
}


// Collect the distinct non-ASCII codepoints that appear inside "..." literals of a C source file,
// skipping // and /* */ comments (which are full of typography this test must not police).
static void scan_literal_codepoints(const char* path, unsigned* out, int cap, int* n) {
	*n = 0;
	FILE* fp = fopen(path, "rb");
	if (!fp) return;
	static unsigned char buf[1 << 21];
	size_t len = fread(buf, 1, sizeof buf, fp);
	fclose(fp);
	int in_str = 0, in_ch = 0, in_line = 0, in_blk = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = buf[i];
		if (in_line) { if (c == '\n') in_line = 0; continue; }
		if (in_blk)  { if (c == '*' && i + 1 < len && buf[i+1] == '/') { in_blk = 0; i++; } continue; }
		if (!in_str && !in_ch) {
			if (c == '/' && i + 1 < len && buf[i+1] == '/') { in_line = 1; i++; continue; }
			if (c == '/' && i + 1 < len && buf[i+1] == '*') { in_blk = 1; i++; continue; }
			if (c == '"')  { in_str = 1; continue; }
			if (c == '\'') { in_ch = 1; continue; }
			continue;
		}
		if (c == '\\') { i++; continue; }
		if (in_str && c == '"')  { in_str = 0; continue; }
		if (in_ch  && c == '\'') { in_ch = 0; continue; }
		if (c < 0x80) continue;
		// decode one UTF-8 sequence
		unsigned cp = 0; int extra = 0;
		if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
		else continue;
		for (int k = 0; k < extra && i + 1 < len; k++) cp = (cp << 6) | (buf[++i] & 0x3F);
		int seen = 0;
		for (int k = 0; k < *n; k++) if (out[k] == cp) { seen = 1; break; }
		if (!seen && *n < cap) out[(*n)++] = cp;
	}
}


// On-screen width of a UTF-8 run at texel scale 1.0 = sum of the glyphs' charWidth. (At 1.0
// citro2d's C2D_FontCalcGlyphPos multiplies each integer charWidth by exactly 1, so this is
// the same number C2D_TextGetDimensions returns — no float layout engine required.)
static int run_width(const Font* f, const char* s) {
	int w = 0;
	const unsigned char* p = (const unsigned char*)s;
	while (*p) {
		unsigned cp = *p;
		int extra = 0;
		if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; extra = 1; }
		else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; extra = 2; }
		else if ((cp & 0xF8) == 0xF0) { cp &= 0x07; extra = 3; }
		p++;
		for (int k = 0; k < extra && *p; k++) { cp = (cp << 6) | (*p & 0x3F); p++; }
		int gi = font_glyph(f, cp);
		if (gi < 0) gi = 0;
		int cw = font_char_w(f, gi);
		if (cw > 0) w += cw;
	}
	return w;
}

int main(void) {
	printf("test_typography — SPEC-crisp R1 (px == lineFeed, texel scale 1.0)\n");

	Font f[TXT_COUNT];
	int  ok[TXT_COUNT];

	// ---------------- T1 / T2 / T3 / T4 / T8
	printf("\nT1-T4,T8: the seven ladder rungs\n");
	printf("  %-12s %-14s %5s %5s %8s %10s %10s\n",
	       "role", "face", "lf", "cellH", "px", "scale", "was(ceil)");
	for (int i = 0; i < TXT_COUNT; i++) {
		TxtFace fc = typo_face((TxtRole)i);
		char path[256];
		snprintf(path, sizeof path, "data/fnt_%s.bin", fc.sym);
		ok[i] = font_open(&f[i], path);
		CHECK(ok[i], "T1 %s: cannot open/parse %s", role_name((TxtRole)i), path);
		if (!ok[i]) continue;

		// T2 — the bake side of the law.
		CHECK((float)f[i].lineFeed == fc.px,
		      "T2 %s (%s): lineFeed=%d but the ladder draws it at %.1f px — the bake and "
		      "source/typography.h disagree; re-run tools/build_assets.sh",
		      role_name((TxtRole)i), fc.sym, f[i].lineFeed, (double)fc.px);

		// T3 — the draw side. EXACT equality, no epsilon.
		float texel = typo_texel_scale(fc.px, f[i].lineFeed, f[i].cellH);
		CHECK(texel == 1.0f,
		      "T3 %s (%s): texel scale %.6f != 1.0 — every glyph resamples",
		      role_name((TxtRole)i), fc.sym, (double)texel);

		// T4 — reproduce the defect: the phase-17 ceil()'d s_native must NOT come out at 1.0
		// wherever the ceil actually rounds (it is a no-op only when lineFeed*30 divides cellH).
		float ceilNat = shipped_ceil_s_native(f[i].lineFeed, f[i].cellH);
		float ceilTex = (fc.px / ceilNat) * (30.0f / (float)f[i].cellH);
		if (ceilNat != typo_s_native_citro(f[i].lineFeed, f[i].cellH))
			CHECK(ceilTex < 1.0f,
			      "T4 %s: the ceil()'d s_native was expected to shrink text, got %.6f",
			      role_name((TxtRole)i), (double)ceilTex);

		// T8 — sheet geometry.
		CHECK(f[i].fmt == 11, "T8 %s: sheet format %d, expected 11 (GPU_A4)", role_name((TxtRole)i), f[i].fmt);
		CHECK(f[i].sheetW > 0 && (f[i].sheetW & (f[i].sheetW - 1)) == 0 &&
		      f[i].sheetH > 0 && (f[i].sheetH & (f[i].sheetH - 1)) == 0,
		      "T8 %s: sheet %dx%d is not power-of-two", role_name((TxtRole)i), f[i].sheetW, f[i].sheetH);
		CHECK((unsigned)f[i].sheetSize == (unsigned)(f[i].sheetW * f[i].sheetH / 2),
		      "T8 %s: sheetSize %u != w*h/2 for A4", role_name((TxtRole)i), f[i].sheetSize);
		CHECK(f[i].nCols * (f[i].cellW + 1) <= f[i].sheetW && f[i].nRows * (f[i].cellH + 1) <= f[i].sheetH,
		      "T8 %s: %dx%d cells of %dx%d do not fit the %dx%d sheet", role_name((TxtRole)i),
		      f[i].nCols, f[i].nRows, f[i].cellW, f[i].cellH, f[i].sheetW, f[i].sheetH);
		CHECK((long)f[i].sheetOff + (long)f[i].sheetSize * f[i].nSheets <= f[i].n,
		      "T8 %s: sheet data runs past EOF", role_name((TxtRole)i));
		// the cell must be able to hold a line: baseline inside the cell, cell >= lineFeed-ish
		CHECK(f[i].baseline > 0 && f[i].baseline <= f[i].cellH,
		      "T8 %s: baseline %d outside cell height %d", role_name((TxtRole)i), f[i].baseline, f[i].cellH);

		printf("  %-12s %-14s %5d %5d %8.1f %10.4f %10.4f\n",
		       role_name((TxtRole)i), fc.sym, f[i].lineFeed, f[i].cellH,
		       (double)fc.px, (double)texel, (double)ceilTex);
	}

	// ---------------- T5 — every superseded pack must be gone
	// Two vintages now: the phase-17 four (no call site could draw them at 1.0) and the phase-18
	// seven (baked 0.78x too small — SPEC-legible L1.4). A leftover .bin is ~527 KB that bin2s
	// embeds into the ELF whether or not anything references it, and it is also how a half-run of
	// build_assets.sh hides a stale ladder.
	printf("\nT5: superseded faces are removed from data/\n");
	{
		static const char* dead[] = {
			"fnt_sg_bold", "fnt_sg_med", "fnt_jbm_med", "fnt_jbm_bold",           /* phase 17 */
			"fnt_sg_bold_17", "fnt_sg_bold_12", "fnt_sg_med_12", "fnt_sg_med_10",  /* phase 18 */
			"fnt_jbm_med_9", "fnt_jbm_med_7", "fnt_jbm_bold_11",
		};
		for (unsigned i = 0; i < sizeof dead / sizeof dead[0]; i++) {
			char path[256];
			snprintf(path, sizeof path, "data/%s.bin", dead[i]);
			FILE* fp = fopen(path, "rb");
			CHECK(fp == NULL,
			      "T5 %s still exists — it can be drawn at no size at 1.0 and bin2s would embed "
			      "another 525 KB; tools/build_assets.sh removes it", path);
			if (fp) fclose(fp);
		}
	}

	// ---------------- T6 — the codepoints the UI actually prints
	printf("\nT6: every codepoint the UI prints has a glyph (no tofu)\n");
	{
		// Extracted from the string literals in source/*.c by T6b below; split by family because
		// Space Grotesk genuinely has no geometric-shape block and does not need one — the strings
		// that print those live on the mono roles. Asserting the whole set on every face would be
		// a false invariant that only a bigger, wrong font could satisfy.
		static const struct { unsigned cp; const char* what; } UNIVERSAL[] = {
			{ 0x00B7, "MIDDLE DOT ·" },   { 0x2014, "EM DASH —" },
			{ 0x00AB, "LEFT GUILLEMET «" }, { 0x00D7, "MULTIPLY ×" },
			{ 0x00E9, "LATIN e-acute (ROM filenames)" }, { 0x00FC, "LATIN u-diaeresis" },
		};
		static const struct { unsigned cp; const char* what; } MONO_ONLY[] = {
			{ 0x25CF, "BLACK CIRCLE ● (the HUD focus chip)" },
			{ 0x25CB, "WHITE CIRCLE ○ (the wireless match marker)" },
		};
		for (int i = 0; i < TXT_COUNT; i++) {
			if (!ok[i]) continue;
			const char* sym = typo_face((TxtRole)i).sym;
			int mono = (strncmp(sym, "jbm", 3) == 0);
			for (unsigned k = 0; k < sizeof UNIVERSAL / sizeof UNIVERSAL[0]; k++) {
				int gi = font_glyph(&f[i], UNIVERSAL[k].cp);
				CHECK(gi > 0, "T6 %s: U+%04X %s missing — it would draw as tofu",
				      sym, UNIVERSAL[k].cp, UNIVERSAL[k].what);
				if (gi > 0)
					CHECK(font_char_w(&f[i], gi) > 0, "T6 %s: U+%04X %s has zero advance",
					      sym, UNIVERSAL[k].cp, UNIVERSAL[k].what);
			}
			if (mono)
				for (unsigned k = 0; k < sizeof MONO_ONLY / sizeof MONO_ONLY[0]; k++)
					CHECK(font_glyph(&f[i], MONO_ONLY[k].cp) > 0,
					      "T6 %s: U+%04X %s missing", sym, MONO_ONLY[k].cp, MONO_ONLY[k].what);
			// ASCII must be complete or a label silently loses letters.
			for (unsigned cp = 0x21; cp <= 0x7E; cp++)
				CHECK(font_glyph(&f[i], cp) > 0, "T6 %s: ASCII U+%04X missing", sym, cp);
		}
	}

	// ---------------- T6b — nothing in the source prints a codepoint NO face has
	// The learn skill's invariant 5 (tofu) made mechanical: scan every UI string literal, decode
	// UTF-8, and fail on a codepoint that is missing from BOTH families. This is what catches the
	// next person pasting a ⚡ or a → into a label.
	printf("\nT6b: no source string literal prints a codepoint no face has\n");
	{
		static const char* files[] = { "source/main.c", "source/rompicker.c", "source/wireless.c",
		                              "source/touch.c", "source/ui.c", "source/assets.c" };
		int sg = -1, jb = -1;
		for (int i = 0; i < TXT_COUNT; i++)
			if (ok[i]) { if (strncmp(typo_face((TxtRole)i).sym, "jbm", 3) == 0) jb = i; else sg = i; }
		CHECK(sg >= 0 && jb >= 0, "T6b needs one loaded face per family");
		for (unsigned fi = 0; fi < sizeof files / sizeof files[0] && sg >= 0 && jb >= 0; fi++) {
			unsigned cps[256]; int ncp = 0;
			scan_literal_codepoints(files[fi], cps, 256, &ncp);
			for (int k = 0; k < ncp; k++) {
				int a2 = font_glyph(&f[sg], cps[k]), b2 = font_glyph(&f[jb], cps[k]);
				CHECK(a2 > 0 || b2 > 0,
				      "T6b %s: U+%04X appears in a string literal but NEITHER family has a glyph "
				      "for it — it draws as tofu on screen", files[fi], cps[k]);
			}
		}
	}

	// ---------------- T7 — role table hygiene
	printf("\nT7: role table hygiene\n");
	for (int i = 0; i < TXT_COUNT; i++) {
		TxtFace a = typo_face((TxtRole)i);
		CHECK(a.sym && a.sym[0], "T7 role %d has no face symbol", i);
		CHECK(a.px >= 6.0f && a.px <= 32.0f, "T7 %s: px %.1f outside a sane UI range",
		      role_name((TxtRole)i), (double)a.px);
		CHECK(a.px == floorf(a.px), "T7 %s: px %.2f is fractional — lineFeed is an integer, so a "
		      "fractional draw size can never be 1.0", role_name((TxtRole)i), (double)a.px);
		CHECK(a.pt > 0 && a.pt < 40, "T7 %s: implausible bake pt %d", role_name((TxtRole)i), a.pt);
		// PHASE 19: two roles MAY share a face (SEG=BODY, CHIP=SECTION — L2.3) and that is the
		// point, not a leak. What must never happen is a *disagreeing* share: the same .bin
		// claimed at two different draw sizes is the phase-17 defect with a new name, because only
		// one of them can be lineFeed. So: same sym => identical rung, in every field.
		for (int j = i + 1; j < TXT_COUNT; j++) {
			TxtFace b = typo_face((TxtRole)j);
			if (strcmp(a.sym, b.sym) != 0) continue;
			CHECK(a.px == b.px && a.pt == b.pt && strcmp(a.ttf, b.ttf) == 0,
			      "T7 %s and %s share the face %s but disagree (%.1f px/pt %d vs %.1f px/pt %d) — "
			      "one .bin has one lineFeed, so one of them cannot be at texel scale 1.0",
			      role_name((TxtRole)i), role_name((TxtRole)j), a.sym,
			      (double)a.px, a.pt, (double)b.px, b.pt);
		}
	}
	CHECK(typo_face((TxtRole)TXT_COUNT).px == 0.0f, "T7 out-of-range role must return an empty face");
	CHECK(typo_role_px(TXT_SECTION) == 12.0f, "T7 typo_role_px disagrees with typo_face");
	// the arithmetic helpers themselves
	CHECK(typo_s_native_citro(12, 15) == 24.0f, "T7 typo_s_native_citro(12,15) should be 24.0");
	CHECK(typo_texel_scale(12.0f, 12, 15) == 1.0f, "T7 texel scale identity");
	CHECK(typo_texel_scale(9.0f, 12, 15) == 0.75f, "T7 the off-ladder case the app used to ship");
	CHECK(typo_draw_scale(9.0f, 0, 0) == 1.0f && typo_texel_scale(9.0f, 0, 0) == 1.0f,
	      "T7 the degenerate guards must not divide by zero");

	// ---------------- T9 — source lint
	printf("\nT9: no phase-17 face enum survives in source/\n");
	{
		static const char* files[] = { "source/main.c", "source/rompicker.c", "source/wireless.c",
		                              "source/touch.c", "source/ui.c", "source/assets.c",
		                              "source/assets.h", "source/ui.h" };
		static const char* dead[] = { "FNT_SG_BOLD", "FNT_SG_MED", "FNT_JBM_MED", "FNT_JBM_BOLD" };
		for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++) {
			int seen = file_contains(files[i], "assets_text");
			CHECK(seen >= 0, "T9 cannot read %s (run the suite from the project root)", files[i]);
			for (unsigned k = 0; k < 4 && seen >= 0; k++)
				CHECK(file_contains(files[i], dead[k]) == 0,
				      "T9 %s still names %s — the ladder leaked back in", files[i], dead[k]);
		}
	}

	// ---------------- T10 — nothing overflows its box after the re-fit
	// Every rung draws at scale 1.0, so a run's on-screen width is exactly the sum of its
	// glyphs' charWidth values — no float layout engine needed, and this is the SAME quantity
	// C2D_TextGetDimensions returns. Boxes are the real rects from source/uihit.c and the draw
	// sites. Sizes moved by up to -15% and +13% in this phase, so this is the guard that a
	// re-fit did not push a label out of its 9-slice or a chip out of its bar.
	printf("\nT10: every re-fitted label still fits its box\n");
	{
		static const struct { TxtRole r; const char* s; int box; const char* where; } FIT[] = {
			// rompicker resume prompt — BTN[3] = 280 wide (rompicker.c:173)
			{ TXT_BUTTON, "Resume this pairing",   280, "rompicker BTN[0]" },
			{ TXT_BUTTON, "Pick new games",        280, "rompicker BTN[1]" },
			{ TXT_BODY,   "Use defaults",          280, "rompicker BTN[2]" },
			// picker actions — PICK_START 124, PICK_LINKED 164 (uihit.c:56-57)
			{ TXT_BUTTON, "START",                 124, "PICK_START" },
			{ TXT_BUTTON, "LINK A FRIEND",         164, "PICK_LINKED" },
			{ TXT_BUTTON, "START \xE2\x80\x94 LINKED",  164, "PICK_LINKED linked" },
			{ TXT_BUTTON, "Rescan",                124, "empty-state Rescan" },
			{ TXT_BODY,   "Start without a game",  164, "empty-state go" },
			// PICK_MODE is a 296-wide 2-cell segmented control (uihit.c:53)
			{ TXT_SEG,    "1 Game",                148, "PICK_MODE cell" },
			{ TXT_SEG,    "2 Games",               148, "PICK_MODE cell" },
			// the settings chip: PICK_SETTINGS 88 wide, label inset 6 px (rompicker.c:304)
			{ TXT_CHIP,   "settings \xC2\xB7 ZR",     82, "PICK_SETTINGS chip" },
			// splash hero button 175 wide (main.c:4807)
			{ TXT_TITLE,  "TAP TO START",          175, "splash CTA" },
			// settings Done pill 72 wide (main.c:4631)
			{ TXT_BUTTON, "Done",                   72, "settings DONE" },
			// wireless bottom buttons are 293 wide (wireless.c:235-260)
			{ TXT_BUTTON, "Host a session",        293, "wireless host" },
			{ TXT_BUTTON, "Scan for lobbies",      293, "wireless scan" },
			{ TXT_BODY,   "Connect online (soon)", 293, "wireless online" },
			{ TXT_BUTTON, "Start linked trade",    293, "wireless start" },
			{ TXT_BODY,   "Leave",                 293, "wireless leave" },
			{ TXT_BODY,   "\xC2\xAB back",            293, "wireless back" },
			// wireless seat card: name column is 150 px (sx+22 .. sx+172), code column 162
			{ TXT_BODY,   "GUYA-PLAYER",           150, "wireless seat name" },
			{ TXT_CHIP,   "\xC3\x97 different game",   272, "wireless lobby mismatch" },
			// presence card is 214 wide, rows inset 10 px each side (main.c:1690)
			{ TXT_BUTTON, "NILS",                  194, "presence name" },
			{ TXT_CHIP,   "MAP 3-12   TILE 14,9",  194, "presence loc" },
			{ TXT_CHIP,   "same map \xE2\x80\x94 read-only", 194, "presence note" },
			// the HUD's right-to-left run on the 320-wide bottom screen: clock + fps + gaps
			{ TXT_SECTION, "88:88",                 60, "HUD clock" },
			{ TXT_SECTION, "60fps",                 60, "HUD fps" },
			// pause-menu section labels sit in the 208-px content column (main.c TEXPL_W)
			{ TXT_SECTION, "SCALE \xC2\xB7 TOP",       208, "pause section label" },
			{ TXT_SECTION, "GAMEPAD \xC2\xB7 EDGES",   208, "pause section label" },
			// ---- PHASE 18 FIX PASS: everything this pass moved onto the ladder ----
			// the pre-game LINK tab's note: content column x=93 .. the scrollbar at x=312
			{ TXT_BODY, SET_LINK_DISABLED_NOTE,     151, "settings LINK disabled note" },
			// the HUD bar's game name: x=15, and the right-hand cluster (clock+fps+up to three
			// chips) starts around x=230 on the 400-px top bar; the FOCUS chip follows the name.
			{ TXT_BODY,   "Pokemon LeafGreen",     150, "HUD top game name" },
			{ TXT_BODY,   "CONTROLLER",            150, "HUD bottom game name (single mode)" },
			// the co-op toast: x=8 on the 400-px top screen, no clamp (main.c pres_pair_toast)
			{ TXT_BODY,   "Co-op: Hoenn RS vs Hoenn RS \xE2\x80\x94 no peer possible", 384, "co-op pair toast" },
			{ TXT_BODY,   "Co-op: this pair has no profile", 384, "co-op no-profile toast" },
			{ TXT_BODY,   "Layout: B top / A bottom", 384, "longest ordinary toast" },
			// the .sav picker (savpicker_run): title x=8, rows x=12 in a 388-px band, help x=8
			{ TXT_TITLE,  "Pick a .sav to load",   392, "sav picker title" },
			{ TXT_SECTION,"Up/Down: move   A: load   B: cancel", 312, "sav picker help" },
			{ TXT_BODY,   "pokemon_emerald_backup_2026.sav", 388, "sav picker row" },
			// the virtual gamepad's key glyphs, in their own key rects (touch.c pad_overlay)
			{ TXT_BUTTON, "START",                  64, "pad START key" },
			{ TXT_BUTTON, "A",                      60, "pad A key" },
			{ TXT_BUTTON, "L",                      52, "pad L key" },
			// ---- PHASE 19 / SPEC-legible L3.1: the ladder moved up 1-2 rungs, so every box in
			// the app is now measured against a WIDER string. These are the pause tabs' own
			// controls (source/main.c PT_* tables), whose geometry phase 19 does NOT change — a
			// seg cell is w/nseg and a PK_BTN label is centred in w. This is the half of the
			// corpus that must fit as-is; the boxes that DO move are graded after G2 re-fits them.
			{ TXT_SEG,    "1:1",                    69, "PT_DISPLAY scale seg (208/3)" },
			{ TXT_SEG,    "Aspect-fit",             69, "PT_DISPLAY scale seg (208/3)" },
			{ TXT_SEG,    "Stretch",                69, "PT_DISPLAY scale seg (208/3)" },
			{ TXT_SEG,    "Sharp",                 104, "PT_DISPLAY filter seg (208/2)" },
			{ TXT_SEG,    "Smooth",                104, "PT_DISPLAY filter seg (208/2)" },
			{ TXT_SEG,    "off",                    52, "PT_DISPLAY hud seg (208/4)" },
			{ TXT_SEG,    "bottom",                 52, "PT_DISPLAY hud seg (208/4)" },
			{ TXT_SEG,    "Solo",                   72, "PT_AUDIO mode seg (216/3)" },
			{ TXT_SEG,    "Mixed",                  72, "PT_AUDIO mode seg (216/3)" },
			{ TXT_SEG,    "Split",                  72, "PT_AUDIO mode seg (216/3)" },
			{ TXT_SEG,    "Off",                    69, "PT_TOUCH mode seg (208/3)" },
			{ TXT_SEG,    "Gamepad",                69, "PT_TOUCH mode seg (208/3)" },
			{ TXT_SEG,    "Smart",                  69, "PT_TOUCH mode seg (208/3)" },
			{ TXT_SEG,    "Round",                  69, "PT_TOUCH padEdge seg (208/3)" },
			{ TXT_SEG,    "Max",                    42, "PT_ENHANCE tilt seg (170/4)" },
			{ TXT_BUTTON, "Resume",                216, "PT_SESSION resume" },
			{ TXT_BUTTON, "Change games",          216, "PT_SESSION change" },
			{ TXT_BUTTON, "Quit",                  216, "PT_SESSION quit" },
			{ TXT_BUTTON, "Wireless lobby...",     216, "PT_LINK wireless" },
			{ TXT_BUTTON, "Wireless: ON",          216, "PT_LINK wireless on" },
			{ TXT_BUTTON, "Save state",            104, "PT_LINK save state" },
			{ TXT_BUTTON, "Load state",            104, "PT_LINK load state" },
			{ TXT_BUTTON, "Load .sav",             216, "PT_LINK load sav" },
			{ TXT_BUTTON, "Preview Smart",         101, "PT_TOUCH preview smart" },
			{ TXT_SECTION,"DIORAMA \xC2\xB7 TILT",    170, "PT_ENHANCE tilt caption" },
			{ TXT_SECTION,"Co-op presence",        219, "PT_LINK presence OV_ROW label" },
			{ TXT_SECTION,"L/R tab  A select  B resume (or tap)", 312, "pause status hint" },
			{ TXT_SECTION,"L / R  switch tab",     300, "run_settings hint" },
			{ TXT_SECTION,"B  done",               300, "run_settings hint" },

			// ================= PHASE 19 / SLICE G2 — THE RE-FIT'S OWN CORPUS =================
			// L5.2.1 asks for >= 93 boxes: every literal that reaches assets_text* / assets_button /
			// assets_seg, against its SHIPPED box. The rows above are the half that did not move;
			// these are the boxes G2 changed, the strings G2 re-roled (L3.3.3), and the
			// table/macro-driven labels the spec's own extraction reached only in prose. Anything
			// that is drawn but not listed here is a hole in the gate, not a passing test.

			// --- L3.1.3: the ONE horizontal overflow, now graded against the box it SHIPS with.
			{ TXT_BUTTON, "Preview Gamepad",       106, "PT_TOUCH preview pad (box grew 101->106)" },
			{ TXT_BUTTON, "Preview Smart",         106, "PT_TOUCH preview smart" },

			// (The touch explainer's LINES are deliberately NOT here. They are wrapper OUTPUT, and
			//  a greedy wrapper's job is to fill the column — every last line it emits is within
			//  one word of the box by construction, so the >98% ceiling below would fire on a
			//  correctly wrapped paragraph forever. T14 owns them: it re-wraps the shipped copy at
			//  the shipped width and asserts both the per-line fit and the line COUNT, which is
			//  the invariant that actually protects the Preview buttons underneath.)

			// --- L3.3.3: the strings that moved from the mono rung to TXT_BODY. Each is a full
			//     sentence, which FONTS.md never assigned to JetBrains Mono — the app did.
			// The box is the DONE chip's left edge (244) minus the content column's x (93). The
			// full 219 px column is NOT available on this row: run_settings draws the Done pill
			// over it, last, as fixed chrome.
			{ TXT_BODY,   SET_LINK_DISABLED_NOTE,  151, "settings LINK note (vs the Done chip at x244)" },
			{ TXT_BODY,   "configure before you pick a game",   300, "run_settings subtitle" },
			{ TXT_BODY,   "Tilt is set, but this is an Old 3DS - it stays flat.", 300, "run_settings O3DS warning" },
			{ TXT_BODY,   "put .gba files in /3ds/dual-gba/",   400, "picker empty state (top)" },
			{ TXT_BODY,   "put .gba files in /3ds/dual-gba/, then Rescan", 320, "picker empty state (bottom)" },
			{ TXT_BODY,   "not a valid .gba - pause menu, Change games", 320, "dead-core panel line 2" },
			{ TXT_BUTTON, "This game could not be loaded",      320, "dead-core panel line 1" },
			{ TXT_BODY,   "Wireless unavailable \xE2\x80\x94 install + run the .CIA", 300, "wireless unavailable" },

			// --- L3.2.4: the presence card's rows, incl. the union note G2 had to WRAP.
			{ TXT_VALUE,  "01234",                  80, "presence id" },
			{ TXT_VALUE,  "M",                      30, "presence gender" },
			{ TXT_CHIP,   PRES_CARD_READONLY_NOTE, 194, "presence read-only note" },
			{ TXT_CHIP,   PRES_CARD_UNION_NOTE_1,  194, "presence union note L1" },
			{ TXT_CHIP,   PRES_CARD_UNION_NOTE_2,  194, "presence union note L2" },
			{ TXT_CHIP,   PRES_PROMPT_TEXT,        194, "presence prompt pill" },

			// --- L3.2.8 / L4.6: the chips, in the boxes uihit.h now owns.
			{ TXT_CHIP,   "menu",                   36, "touch menu chip (48 - 8 bars - 4 gap)" },
			{ TXT_CHIP,   "TOUCH \xC2\xB7 GAMEPAD",       300, "touch mode chip" },
			{ TXT_CHIP,   "TOUCH \xC2\xB7 SMART POINTER", 300, "touch mode chip" },
			{ TXT_BUTTON, "B",                       60, "pad B key" },
			{ TXT_BUTTON, "R",                       52, "pad R key" },

			// --- L4.1: the remaining seg cells and stepper glyphs on the six pause tabs.
			{ TXT_SEG,    "Smooth",                104, "PT_DISPLAY filter seg" },
			{ TXT_SEG,    "top",                    52, "PT_DISPLAY hud seg" },
			{ TXT_SEG,    "both",                   52, "PT_DISPLAY hud seg" },
			{ TXT_SEG,    "Soft",                   69, "PT_TOUCH padEdge seg" },
			{ TXT_SEG,    "Sharp",                  69, "PT_TOUCH padEdge seg" },
			{ TXT_SEG,    "Off",                    42, "PT_ENHANCE tilt seg (170/4)" },
			{ TXT_SEG,    "Low",                    42, "PT_ENHANCE tilt seg" },
			{ TXT_SEG,    "Mid",                    42, "PT_ENHANCE tilt seg" },
			{ TXT_BUTTON, "-",                      20, "PK_STEP minus pad" },
			{ TXT_BUTTON, "+",                      20, "PK_STEP plus pad" },
			{ TXT_VALUE,  "100",                    30, "PK_STEP volume value" },
			{ TXT_BUTTON, "Wireless link",         216, "run_settings LINK wireless" },
			{ TXT_BODY,   "Swap screens",          175, "PT_DISPLAY OV_ROW (x93 .. toggle x268)" },
			{ TXT_BODY,   "Frameskip",             175, "PT_DISPLAY OV_ROW" },
			{ TXT_BODY,   "Co-op presence",        183, "PT_LINK OV_ROW (x93 .. toggle x276)" },

			// --- L4.3 / L4.5: the pickers and the lobby, worst-case content.
			{ TXT_TITLE,  "No games found",        369, "picker empty title" },
			{ TXT_BODY,   "pick a game (d-pad + A)", 250, "picker slot A placeholder (single)" },
			{ TXT_BODY,   "pick game A",           250, "picker slot A placeholder (dual)" },
			{ TXT_BODY,   "pick game B",           250, "picker slot B placeholder" },
			{ TXT_BODY,   "copy it: game1.gba / game2.gba", 250, "picker duplicate-file refusal" },
			{ TXT_SECTION,"SAME FILE IN BOTH SLOTS", 300, "picker state tag (longest)" },
			{ TXT_SECTION,"PICK A GAME FIRST",     300, "picker state tag" },
			{ TXT_SECTION,"B \xC2\xB7 BOTTOM",          300, "picker state tag" },
			{ TXT_BODY,   "You",                   150, "wireless seat (self)" },
			{ TXT_SECTION,"NEARBY SESSIONS",       290, "wireless scan caption" },
			{ TXT_SECTION,"UDS \xC2\xB7 local \xC2\xB7 2 seats linked", 300, "wireless transport line" },
			{ TXT_SECTION,"then open the in-game Cable Club to trade", 300, "wireless conn hint" },
			{ TXT_SECTION,"RTT 999 ms \xC2\xB7 loss 100 %", 300, "wireless RTT line" },
			{ TXT_CHIP,   "link-surface DIFF: gameCode(BPEE!=BPRE),gameRev(0!=1)", 380, "wireless fp verdict" },
			{ TXT_CHIP,   "Link closed (left for HOME). Re-host or re-join.", 380, "wireless status (longest)" },

			// --- L3.2.2 / L3.2.9: the hint band, with the strings the app really formats.
			{ TXT_SECTION,"L/R tab  A select  B resume  (or tap)", 228, "pause status hint (shipped copy)" },
			{ TXT_SECTION,"3D on top \xC2\xB7 Gamepad \xC2\xB7 START+SELECT = menu", 320, "in-game hint (single)" },
			{ TXT_SECTION,"START+SELECT \xC2\xB7 pause menu", 320, "in-game hint (dual)" },
		};
		for (unsigned k = 0; k < sizeof FIT / sizeof FIT[0]; k++) {
			int i = (int)FIT[k].r;
			if (!ok[i]) continue;
			int w = run_width(&f[i], FIT[k].s);
			CHECK(w <= FIT[k].box,
			      "T10 %s: \"%s\" is %d px wide at %s (%.0f px) but its box is %d px",
			      FIT[k].where, FIT[k].s, w, typo_face(FIT[k].r).sym,
			      (double)typo_role_px(FIT[k].r), FIT[k].box);
			if (w > FIT[k].box)
				continue;
			// L5.2.2 — a HARD FAIL at >98%. "It fits" and "it fits by one pixel" are different
			// states: the second one is broken by the next copy edit, the next ladder move, or a
			// font update, and it breaks on the user's hardware rather than here. Below 98% but
			// above 90% is a printed note, so a tightening trend is visible run over run.
			CHECK(w * 100 <= FIT[k].box * 98,
			      "T10 %s: \"%s\" is %d px in a %d px box (%.1f%%) — over the 98%% ceiling. Grow "
			      "the box or shorten the copy; a label this tight is one edit from overflowing.",
			      FIT[k].where, FIT[k].s, w, FIT[k].box, 100.0 * (double)w / (double)FIT[k].box);
			if (w > FIT[k].box * 9 / 10)
				printf("    note: %-34s %-38s %3d/%3d px (>90%% of the box)\n",
				       FIT[k].where, FIT[k].s, w, FIT[k].box);
		}
		printf("    %u boxes measured\n", (unsigned)(sizeof FIT / sizeof FIT[0]));

		// (T10b is retired. It graded ONE pending overflow — "Preview Gamepad" at 103 px in a
		// 101 px button — against the box SPEC-legible L3.1.3 promised it. G2 shipped that box,
		// so the row is an ordinary T10 entry now and the phase has no pending overflow left.)
	}

	// ---------------- T11 — the menu viewport's fixed chrome does not eat a glyph
	// FIX PASS (review finding 3). menu_draw_chrome repaints the plate background over
	// x >= UIHIT_MENU_RAIL_W, y >= UIHIT_MENU_VIEW_H, AFTER the content. A string drawn below the
	// control table is therefore invisible to uihit_content_h and unprotected by maxScroll — the
	// pre-game LINK tab's "why are these faded" note was drawn at y = 220 with a 13-row cell, so
	// the repaint took its baseline row and the 'g' tails, on the one line added to stop the user
	// guessing the app is broken. This grades the SHIPPED constant against the SHIPPED font.
	printf("\nT11: text under the menu content clears the fixed chrome band\n");
	{
		// PHASE 19 / SPEC-legible L3.3.3: the note is PROSE, so it draws at TXT_BODY now, whose
		// line box is 18 px rather than the mono rung's 15. Both legs of the squeeze moved with it
		// (uihit.h): the note to 208 and PT_LINK's presence toggle to y190 (bottom edge 208).
		// The role is part of what is graded — reading it from a table here means a future
		// re-roling of the note fails this test instead of clipping on hardware.
		enum { PT_LINK_LAST_BOTTOM = 208 };   /* ACT_PRESENCE y190 h18 (main.c PT_LINK) */
		static const struct { TxtRole r; int y; const char* what; } UNDER[] = {
			{ TXT_BODY, SET_LINK_NOTE_Y, "run_settings LINK disabled note" },
		};
		for (unsigned k = 0; k < sizeof UNDER / sizeof UNDER[0]; k++) {
			int i = (int)UNDER[k].r;
			if (!ok[i]) continue;
			// The line box a citro2d text run occupies is cellHeight tall at texel scale 1.0.
			int bottom = UNDER[k].y + f[i].cellH;
			CHECK(bottom <= UIHIT_MENU_VIEW_H,
			      "T11 %s: line box y %d..%d overlaps the chrome repaint at y >= %d",
			      UNDER[k].what, UNDER[k].y, bottom, UIHIT_MENU_VIEW_H);
			// ...and it must still sit below the tab's last control (PT_LINK's presence toggle,
			// y190 h18 since SPEC-legible L3.2.2 + L3.3.3, ends at y = 208), or it would print on
			// top of the row it is explaining. Both legs move together or the note is squeezed out.
			CHECK(UNDER[k].y >= PT_LINK_LAST_BOTTOM, "T11 %s: y %d collides with the last row (bottom %d)",
			      UNDER[k].what, UNDER[k].y, PT_LINK_LAST_BOTTOM);
		}
	}

	// ---------------- T12 — the system-font budget
	// FIX PASS. R1 is enforced INSIDE assets_text (the role carries the size, so no call site can
	// pick a wrong one), which means the only way to put blurry text back on screen is to bypass it
	// — ui_text* or a raw C2D_DrawText, both of which use the 3DS system font at whatever fractional
	// scale the caller types. Phase 18's headline claim ("every draw at texel scale 1.0") was in
	// fact false for the HUD game names, the .sav picker, the gamepad key glyphs and the co-op
	// toast; those are converted now, and this test pins what is LEFT so the next one is deliberate.
	// A file at 0 must STAY at 0.
	printf("\nT12: no new system-font text draws (the R1 bypass budget)\n");
	{
		static const char* const DRAW[] = { "ui_text", "ui_text_c", "ui_text_r", "C2D_DrawText" };
		static const struct { const char* path; int budget; const char* why; } BUDGET[] = {
			// main.c's 13: SEVEN ui_text + one ui_text_r are the presence card's !assets_ready()
			// fallback (no baked font exists in that build, so there is nothing to fall back TO);
			// two ui_text_c are the load-error and PAUSED !assets_ready() fallbacks; three
			// C2D_DrawText are the two net-diag stat lines and the #if'd gamestate probe — dev
			// readouts that cram ~72 characters into 400 px, which no 12 px face holds.
			// PHASE 20 raises this 13 -> 14, with the reason this test demands. The new draw is the
			// peer-SPRITE readout (`spr:OK 16x32 t0148 p5 F-1 u37` / `spr:AFFINE g00`), the second
			// line of the co-op debug pair at y = 216/226. It is the SAME KIND of surface as the
			// CO-OP line directly under it — a dev diagnostic that must stay terse and must match
			// its sibling's face — and it is the only thing that answers "why is my friend still
			// magenta?" from a photograph. Routing it through assets_text(role) would make the two
			// halves of one readout disagree in font and size, and the baked faces are ~2x wider at
			// their smallest, which is what pushed these lines off the 400 px screen in the first
			// place. If the co-op debug pair is ever promoted to real chrome, both lines go baked
			// together and this budget drops to 12.
			{ "source/main.c",       14, "assets_ready fallbacks + the dev diag readouts" },
			{ "source/rompicker.c",   0, "picker, resume prompt, empty state and .sav picker are all baked" },
			{ "source/wireless.c",    0, "the whole lobby is baked" },
			{ "source/touch.c",       0, "pad key glyphs and both chips are baked" },
			{ "source/presence_ui.c", 0, "presence chrome is baked" },
		};
		for (unsigned i = 0; i < sizeof BUDGET / sizeof BUDGET[0]; i++) {
			int tot = 0, bad = 0;
			for (unsigned k = 0; k < sizeof DRAW / sizeof DRAW[0]; k++) {
				int c = count_ident(BUDGET[i].path, DRAW[k]);
				if (c < 0) { bad = 1; break; }
				tot += c;
			}
			CHECK(!bad, "T12 cannot read %s (run the suite from the project root)", BUDGET[i].path);
			if (bad) continue;
			CHECK(tot == BUDGET[i].budget,
			      "T12 %s has %d system-font text draws, budget %d (%s). A NEW one is a new blurry "
			      "label — route it through assets_text(role); if it genuinely cannot be, raise the "
			      "budget here WITH the reason.", BUDGET[i].path, tot, BUDGET[i].budget, BUDGET[i].why);
		}
		// The measurement helper must not be counted as a draw (it is how the app lays text out).
		CHECK(count_ident("source/ui.h", "ui_text_w") > 0, "T12 ui_text_w should still exist");
	}

	// T13 — THE STEM-SNAP FLOOR. The second cause of the user's "mostly very blurry".
	//
	// R1 (T2/T3) only guarantees the app does not RESAMPLE the bitmap. It says nothing about
	// the bitmap. mkbcfnt v2.3.0 rasterises unhinted, with no gamma or contrast switch, so at
	// 7-12 px a Medium stem straddles two pixel columns: the faces phase 18 first shipped had
	// jbm_med_7 at 0.0% fully-opaque texels and 87.7% of its ink stranded between 25% and 75%
	// alpha, i.e. grey mush reproduced perfectly. tools/fontlab/sharpen.py (run by
	// build_assets.sh) applies a monotone 16-entry alpha LUT per face; this pins the OUTCOME in
	// the shipped bytes, so a bake that forgets the pass fails here rather than on the user's
	// hardware. Measured over letters+digits, as the tool does.
	printf("\nT13: the bake is stem-snapped, not grey mush (solidity floor)\n");
	{
		static const char ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		                            "abcdefghijklmnopqrstuvwxyz0123456789";
		for (int i = 0; i < TXT_COUNT; i++) {
			if (!ok[i]) continue;
			long h[16] = { 0 };
			int glyphs = 0;
			for (const char* s = ALPHA; *s; s++) {
				int gi = font_glyph(&f[i], (unsigned char)*s);
				if (gi >= 0 && glyph_hist(&f[i], gi, h)) glyphs++;
			}
			CHECK(glyphs >= 50, "T13 %s: only %d of %d alphabet glyphs decoded",
			      role_name((TxtRole)i), glyphs, (int)(sizeof ALPHA - 1));
			long lit = 0, solid = 0, opaque = 0, mid = 0;
			for (int n = 1; n < 16; n++) {
				lit += h[n];
				if (n == 15) opaque += h[n];
				if (n * 100 >= 15 * 75) solid += h[n];         /* >= 75% alpha */
				else if (n * 100 >= 15 * 25) mid += h[n];      /* stranded 25..75% */
			}
			CHECK(lit > 0, "T13 %s: no ink at all", role_name((TxtRole)i));
			if (lit <= 0) continue;
			double solidPct = 100.0 * (double)solid / (double)lit;
			double opaquePct = 100.0 * (double)opaque / (double)lit;
			double midPct = 100.0 * (double)mid / (double)lit;
			// The specific defect: two faces shipped with LITERALLY no opaque texel.
			CHECK(opaquePct > 5.0,
			      "T13 %s (%s): only %.1f%% of its ink reaches full opacity. An unhinted bake at "
			      "this size has no solid stem — run tools/fontlab/sharpen.py (build_assets.sh "
			      "does it automatically after mkbcfnt).",
			      role_name((TxtRole)i), typo_face((TxtRole)i).sym, opaquePct);
			// The floor sharpen.py itself enforces (--min-solid), restated against the bytes.
			CHECK(solidPct >= 30.0,
			      "T13 %s (%s): %.1f%% of its ink is >= 75%% alpha, floor 30%%. The stem-snap pass "
			      "did not run, or mkbcfnt changed under it.",
			      role_name((TxtRole)i), typo_face((TxtRole)i).sym, solidPct);
			// And the mush itself: no face may leave most of its ink in the mid-tones.
			CHECK(midPct < 50.0,
			      "T13 %s (%s): %.1f%% of its ink is stranded between 25%% and 75%% alpha",
			      role_name((TxtRole)i), typo_face((TxtRole)i).sym, midPct);
			printf("     %-12s %-12s opaque %5.1f%%  solid %5.1f%%  stranded %5.1f%%\n",
			       role_name((TxtRole)i), typo_face((TxtRole)i).sym, opaquePct, solidPct, midPct);
		}
	}

	// ---------------- T15 — THE ALIAS CONTRACT (PHASE 19 / SPEC-legible L5.2.4)
	// Merging TXT_SEG into TXT_BODY and TXT_CHIP into TXT_SECTION is what pays for the growth:
	// a .bcfnt is a fixed 1024x1024 A4 sheet (~527 KB) whatever its point size, so seven faces
	// were 3.53 MB in the ELF *and* 3.53 MB of linear heap at runtime (C2D_FontLoadFromMem
	// linearAllocs + memcpys the whole file). Five faces are 2.52 MB of each. This test pins all
	// three halves of that: the aliases resolve to the same sym, the ladder resolves to exactly
	// TXT_DISTINCT_FACES syms, and data/ holds exactly those and nothing else — a stale sixth
	// .bin is silently embedded by bin2s and would give back the megabyte.
	printf("\nT15: the alias contract — %d roles resolve to %d faces\n", TXT_COUNT, TXT_DISTINCT_FACES);
	{
		CHECK(strcmp(typo_face(TXT_SEG).sym, typo_face(TXT_BODY).sym) == 0,
		      "T15 TXT_SEG (%s) must alias TXT_BODY (%s)",
		      typo_face(TXT_SEG).sym, typo_face(TXT_BODY).sym);
		CHECK(strcmp(typo_face(TXT_CHIP).sym, typo_face(TXT_SECTION).sym) == 0,
		      "T15 TXT_CHIP (%s) must alias TXT_SECTION (%s)",
		      typo_face(TXT_CHIP).sym, typo_face(TXT_SECTION).sym);

		const char* distinct[TXT_COUNT]; int nd = 0;
		for (int i = 0; i < TXT_COUNT; i++) {
			const char* s = typo_face((TxtRole)i).sym;
			int seen = 0;
			for (int k = 0; k < nd; k++) if (!strcmp(distinct[k], s)) { seen = 1; break; }
			if (!seen) distinct[nd++] = s;
		}
		CHECK(nd == TXT_DISTINCT_FACES,
		      "T15 the ladder resolves to %d distinct faces, TXT_DISTINCT_FACES says %d — every "
		      "extra face is ~527 KB in the ELF AND ~527 KB of linear heap", nd, TXT_DISTINCT_FACES);

		// data/ must contain exactly the distinct faces. (The T5 idiom, but exhaustive: T5 names
		// the vintages we know about, this catches a face nobody remembered to name.)
		DIR* d = opendir("data");
		CHECK(d != NULL, "T15 cannot open data/ (run the suite from the project root)");
		if (d) {
			int nfound = 0;
			struct dirent* e;
			while ((e = readdir(d))) {
				if (strncmp(e->d_name, "fnt_", 4) || !strstr(e->d_name, ".bin")) continue;
				nfound++;
				char stem[128];
				snprintf(stem, sizeof stem, "%s", e->d_name + 4);
				char* dot = strstr(stem, ".bin"); if (dot) *dot = 0;
				int wanted = 0;
				for (int k = 0; k < nd; k++) if (!strcmp(distinct[k], stem)) { wanted = 1; break; }
				CHECK(wanted, "T15 data/%s is not on the ladder — a stale bake bin2s still embeds",
				      e->d_name);
			}
			closedir(d);
			CHECK(nfound == nd, "T15 data/ holds %d fnt_*.bin, the ladder needs %d", nfound, nd);
		}
	}

	// ---------------- T17 — THE RUNG ACTUALLY GREW (PHASE 19, the size claim itself)
	// SPEC-legible L1 argues in CAP HEIGHT: ISO 9241-303 / ANSI-HFES want >= 16 arcmin of
	// character height, which at 0.1905 mm/px (133 ppi bottom panel) and 30 cm is 7.33 device px,
	// and the game text the user reads happily on the same panel is cap 12. Phase 18 shipped cap
	// 4-10; phase 19 targets 11/9/9/9/7/7/7. Screenshots cannot settle this (Azahar upscales
	// 320x240 by 2.25x), so it is settled here, in the shipped font bytes: the ink rows of 'H'.
	printf("\nT17: cap height per rung — the size claim, measured in the shipped bytes\n");
	{
		static const int WANT_CAP[TXT_COUNT] = { 11, 9, 9, 9, 7, 7, 7 };
		static const int WAS_CAP[TXT_COUNT]  = { 10, 7, 7, 6, 5, 4, 7 };   /* phase 18, for the log */
		for (int i = 0; i < TXT_COUNT; i++) {
			if (!ok[i]) continue;
			int gi = font_glyph(&f[i], 'H'), top = -1;
			int cap = (gi >= 0) ? glyph_ink_rows(&f[i], gi, &top) : -1;
			CHECK(cap == WANT_CAP[i],
			      "T17 %s (%s): cap height %d px, the ladder is designed for %d — the rung moved "
			      "without SPEC-legible L1.5.2 moving with it",
			      role_name((TxtRole)i), typo_face((TxtRole)i).sym, cap, WANT_CAP[i]);
			if (cap <= 0) continue;
			// The floor the whole phase exists to clear, restated at the point of measurement.
			double arcmin = (double)cap * 0.1905 / 300.0 * 3437.75;
			CHECK(cap >= 7, "T17 %s: cap %d px = %.1f arcmin at 30 cm — under the 16' floor by more "
			      "than the one rung SPEC-legible L1.5.5 knowingly accepts",
			      role_name((TxtRole)i), cap, arcmin);
			printf("     %-12s %-12s cap %2d px (was %2d)  ink top %2d of cell %2d  %5.1f arcmin@30cm\n",
			       role_name((TxtRole)i), typo_face((TxtRole)i).sym, cap, WAS_CAP[i], top,
			       f[i].cellH, arcmin);
		}
	}

	// ---------------- T18 — THE INK TABLE IS THE FONT'S, NOT A GUESS (PHASE 19 / L3.2.7)
	// typography.h now DECLARES cellH / inkTop / inkH per role, because typo_center_y and every
	// §L3.2 vertical number are computed from them. A declared metric that the bake can move is
	// exactly the failure phase 18 shipped (a ladder whose numbers were read off a design table
	// instead of the produced file), so the same guard applies: re-measure the shipped bytes and
	// assert exact equality. Measured over "AHgpy1:9" — cap, ascender, descenders, digits, colon.
	printf("\nT18: the declared ink box matches the shipped bytes (per rung)\n");
	{
		static const char PROBE[] = "AHgpy1:9";
		for (int i = 0; i < TXT_COUNT; i++) {
			if (!ok[i]) continue;
			TxtFace tf = typo_face((TxtRole)i);
			CHECK(f[i].cellH == tf.cellH,
			      "T18 %s (%s): TGLP.cellHeight is %d, typography.h declares %d — typo_center_y "
			      "and every L3.2 offset are computed from that number",
			      role_name((TxtRole)i), tf.sym, f[i].cellH, tf.cellH);
			int top = -1, bot = -1;
			int h = str_ink_extent(&f[i], PROBE, &top, &bot);
			CHECK(h > 0, "T18 %s: no ink measured over \"%s\"", role_name((TxtRole)i), PROBE);
			if (h <= 0) continue;
			CHECK(top == tf.inkTop,
			      "T18 %s (%s): ink starts on row %d of the line box, typography.h declares %d",
			      role_name((TxtRole)i), tf.sym, top, tf.inkTop);
			CHECK(h == tf.inkH,
			      "T18 %s (%s): ink is %d rows, typography.h declares %d",
			      role_name((TxtRole)i), tf.sym, h, tf.inkH);
			CHECK(typo_ink_bottom((TxtRole)i) <= tf.cellH,
			      "T18 %s: the ink box (%d..%d) leaves the %d px line box",
			      role_name((TxtRole)i), tf.inkTop, typo_ink_bottom((TxtRole)i), tf.cellH);
			printf("     %-12s %-12s cell %2d  ink rows %2d..%-2d (h %2d)  padding %d above / %d below\n",
			       role_name((TxtRole)i), tf.sym, tf.cellH, top, bot, h, top, tf.cellH - 1 - bot);
		}
		// ...and the centring rule itself, on the boxes the app actually uses. This is arithmetic,
		// not a font read, but it is the arithmetic five widgets depend on: assert the ink lands
		// INSIDE the box and no more than one row off dead centre (0.5 px is unreachable on an
		// integer grid, which is the whole reason the helper rounds).
		static const struct { TxtRole r; int boxH; const char* what; } CTR[] = {
			{ TXT_BUTTON, 40, "PT_SESSION Resume button" },
			{ TXT_BUTTON, 43, "PT_SESSION Quit button" },
			{ TXT_BUTTON, 44, "PT_TOUCH preview button" },
			{ TXT_BUTTON, 22, "pad START key" },
			{ TXT_BUTTON, 42, "wireless / splash button" },
			{ TXT_SEG,    30, "PT_DISPLAY seg cell" },
			{ TXT_SEG,    26, "PT_ENHANCE tilt seg" },
			{ TXT_BODY,   18, "OV_ROW toggle row" },
			{ TXT_CHIP,   UIHIT_CHIP_H,       "ui.c pill chip" },
			{ TXT_CHIP,   UIHIT_TOUCH_CHIP_H, "touch mode chip" },
			{ TXT_CHIP,   UIHIT_MCHIP_H,      "menu affordance chip" },
			{ TXT_CHIP,   UIHIT_PILL_H,       "pause-top feature pill" },
			// PHASE 19 FIX PASS (verify findings V1/C2 + V2/C3): the two boxes that were still
			// placed by a hand-typed literal after L3.2.7 converted the rest. Both are hand-rolled
			// ui_fill + assets_text sites rather than assets_button/ui_chip calls, which is exactly
			// why the sweep missed them; grading their BOX HEIGHTS here is what makes the miss
			// impossible to repeat. run_settings DONE = {244,224,72,14}; PICK_SETTINGS = {6,223,88,16}.
			{ TXT_BUTTON, 14, "run_settings Done pill" },
			{ TXT_CHIP,   16, "ROM picker settings-ZR chip" },
		};
		for (unsigned k = 0; k < sizeof CTR / sizeof CTR[0]; k++) {
			TxtRole r = CTR[k].r;
			float y = typo_center_y(r, 0.0f, (float)CTR[k].boxH);
			CHECK(y == (float)(int)y, "T18 %s: typo_center_y returned %.2f, not an integer row",
			      CTR[k].what, (double)y);
			int inkTop = (int)y + typo_ink_top(r), inkBot = (int)y + typo_ink_bottom(r);
			CHECK(inkTop >= 0 && inkBot <= CTR[k].boxH,
			      "T18 %s: ink %d..%d leaves the %d px box", CTR[k].what, inkTop, inkBot, CTR[k].boxH);
			int slackTop = inkTop, slackBot = CTR[k].boxH - inkBot;
			CHECK(slackTop - slackBot <= 1 && slackBot - slackTop <= 1,
			      "T18 %s: %d px above the ink, %d below — that is not centred",
			      CTR[k].what, slackTop, slackBot);
		}
	}

	// ---------------- T14 — THE WRAPPED PARAGRAPH FITS ITS RECT (PHASE 19 / L5.2.3)
	// The TOUCH tab's explainer is the only text in the app whose LAYOUT is computed rather than
	// placed, and it is computed against a real font at runtime. Phase 19 moved it to a bigger
	// face (TXT_BODY, cell 18) in a slightly wider column and REWROTE one copy because the old
	// one contained a 19-character unbreakable token. Any of those three could silently give back
	// a fourth line — and a fourth line lands on the Preview buttons at y=109, over a plate that
	// BAKES its own caption and cannot move. So this runs the SHIPPED uihit_wrap over the SHIPPED
	// copy with the SHIPPED font metrics and grades the result geometrically.
	printf("\nT14: the touch explainer wraps inside its rect, in all three copies\n");
	{
		enum { TEXPL_W = 216, TEXPL_Y = 62, TEXPL_LEAD = 15, TEXPL_LINES = 3, TEXPL_CAP = 72,
		       PREVIEW_ROW_Y = 109 };
		// Verbatim from main.c's TOUCH_EXPLAIN[] — including L3.2.3's rewrite of copy [2].
		static const char* const EXPLAIN[3] = {
			"Off \xE2\x80\x94 a touch opens the pause menu. No game input from the touch screen.",
			"Gamepad \xE2\x80\x94 a translucent virtual controller (D-pad, A/B, L/R, START) over game B.",
			"Smart \xE2\x80\x94 point at the real game UI: tap to walk, tap menus. Tap yourself = START, hold = SELECT.",
		};
		if (ok[TXT_BODY]) {
			g_measFont = &f[TXT_BODY];
			for (int m = 0; m < 3; m++) {
				char lines[TEXPL_LINES + 1][TEXPL_CAP];
				memset(lines, 0, sizeof lines);
				int n = uihit_wrap(EXPLAIN[m], TEXPL_W, TEXPL_LINES, &lines[0][0], TEXPL_CAP,
				                   meas_cb, NULL);
				CHECK(n > 0 && n <= TEXPL_LINES,
				      "T14 copy %d wraps to %d lines, TEXPL_LINES is %d", m, n, TEXPL_LINES);
				int truncated = 0;
				for (int i = 0; i < n; i++) {
					int w = run_width(&f[TXT_BODY], lines[i]);
					CHECK(w <= TEXPL_W, "T14 copy %d line %d (\"%s\") is %d px in a %d px column",
					      m, i, lines[i], w, TEXPL_W);
					size_t L = strlen(lines[i]);
					if (L >= 3 && !strcmp(lines[i] + L - 3, "...")) truncated = 1;
				}
				// uihit_wrap ELLIPSISES rather than overflowing when the text does not fit, so a
				// too-small TEXPL_LINES would pass the width check while silently eating the end of
				// the sentence. That is a copy defect, not a layout one, and it is invisible on a
				// screenshot unless you already know the full string.
				CHECK(!truncated,
				      "T14 copy %d was ELLIPSISED to fit %d lines — the explainer is losing words, "
				      "which is worse than the overflow it is avoiding", m, TEXPL_LINES);
				// ...and the geometric leg: the last line's INK must clear the Preview row.
				int lastY = TEXPL_Y + (n - 1) * TEXPL_LEAD;
				int inkBot = lastY + typo_ink_bottom(TXT_BODY);
				CHECK(inkBot <= PREVIEW_ROW_Y,
				      "T14 copy %d: last line at y=%d puts ink through row %d, and PT_TOUCH's "
				      "Preview buttons start at y=%d", m, lastY, inkBot - 1, PREVIEW_ROW_Y);
				printf("     copy %d: %d lines, last ink row %d (buttons at %d)\n",
				       m, n, inkBot - 1, PREVIEW_ROW_Y);
			}
			g_measFont = NULL;
		}
	}

	// ---------------- T16 — VERTICAL FIT (PHASE 19 / L5.2.5)
	// T10 is the horizontal gate; this is the vertical one, and it is the gate the phase actually
	// needed. Every rung's LINE BOX grew 1-6 px, so every hard-coded `y` in the app changed meaning
	// on the same day — the status hint fell off the bottom of the screen, the presence card's last
	// three rows landed on each other, and a 16 px .sav row could no longer hold an 18 px line.
	// §L3.2 is a table of numbers precisely so this can be a table of numbers: for each fixed-offset
	// row, assert the INK (read from the shipped bytes, not the line box) lies inside the box it is
	// drawn in. A test that used cellH instead of ink would demand padding the design does not have
	// and would fail correct layouts; a test that used cap height would miss every descender.
	printf("\nT16: every fixed-offset row keeps its ink inside its box\n");
	{
		static const struct { TxtRole r; int y, top, bottom; const char* what; } FITV[] = {
			// --- L3.2.1 the in-game HUD bar (both screens are identical)
			{ TXT_BODY,    UIHIT_HUD_NAME_Y,    0, UIHIT_HUD_BAR_H, "HUD game name" },
			{ TXT_SECTION, UIHIT_HUD_READOUT_Y, 0, UIHIT_HUD_BAR_H, "HUD clock / fps" },
			// --- L3.2.2 the status-hint band + the LINK note (the three-way squeeze)
			{ TXT_SECTION, UIHIT_MENU_VIEW_H,   UIHIT_MENU_VIEW_H, UIHIT_SCREEN_H, "pause status hint" },
			{ TXT_SECTION, UIHIT_MENU_VIEW_H,   UIHIT_MENU_VIEW_H, UIHIT_SCREEN_H, "in-game touch-off hint" },
			{ TXT_BODY,    SET_LINK_NOTE_Y,     208, UIHIT_MENU_VIEW_H, "settings LINK disabled note" },
			// --- L3.2.4 the presence card (214x108, rows hard-offset from its own top)
			{ TXT_BUTTON,   6, 0, 108, "presence card: name" },
			{ TXT_VALUE,    8, 0, 108, "presence card: gender" },
			{ TXT_CHIP,    30, 0, 108, "presence card: ID label" },
			{ TXT_VALUE,   30, 0, 108, "presence card: id value" },
			{ TXT_CHIP,    48, 0, 108, "presence card: location" },
			{ TXT_CHIP,    64, 0, 108, "presence card: read-only note" },
			{ TXT_CHIP,    78, 0, 108, "presence card: union note L1" },
			{ TXT_CHIP,    92, 0, 108, "presence card: union note L2" },
			// --- L3.2.5 the .sav picker: title, first row, last visible row, help line
			{ TXT_TITLE,    6,  0,  30, "sav picker: title (above row 0 at y=30)" },
			{ TXT_BODY,    30, 30,  50, "sav picker: row 0 in its 20 px pitch" },
			{ TXT_BODY,   190, 30, 214, "sav picker: last visible row (30 + 8*20)" },
			{ TXT_SECTION,214, 214, UIHIT_SCREEN_H, "sav picker: help line" },
			// --- L3.2.9 the standalone settings screen's chrome
			{ TXT_BODY,    48,  40,  71, "run_settings: subtitle (must clear the O3DS warning ink at 71)" },
			{ TXT_BODY,    66,  66,  90, "run_settings: Old-3DS warning" },
			{ TXT_SECTION,206, 200, 222, "run_settings: L/R hint (above the B hint)" },
			{ TXT_SECTION,222, 222, UIHIT_SCREEN_H, "run_settings: B done hint" },
			// --- L3.2.10 / L4.5 the wireless lobby
			{ TXT_CHIP,   206, 200, 224, "wireless: fingerprint verdict (above status)" },
			{ TXT_CHIP,   224, 224, UIHIT_SCREEN_H, "wireless: status line" },
			{ TXT_BODY,     6,  0,  30, "wireless seat card: name (52 px card)" },
			{ TXT_CHIP,     8,  0,  30, "wireless seat card: HOST/SEAT tag" },
			{ TXT_CHIP,    30, 30,  52, "wireless seat card: game code row" },
			{ TXT_BODY,     2,  0,  22, "wireless scan card: host line (34 px card)" },
			{ TXT_CHIP,    21, 21,  34, "wireless scan card: match chip" },
			{ TXT_VALUE,  184, 178, 206, "wireless: RTT / LOSS tile value" },
			// --- L4.3 the ROM picker's list rows (ROWH 23.5, card y-1 .. y+20.5)
			{ TXT_BODY,     3,  0,  21, "ROM picker: row name in its card" },
			{ TXT_CHIP,     4,  0,  21, "ROM picker: row game-code chip" },
			// --- L3.2.6 the code-drawn OV_SECTION captions, 17 px above their control
			// The upper bound is the plate's OWN baked ink, measured off the shipped
			// pause-bot-enhance.png: "Vivid mode" occupies rows 175..183, so the caption may not
			// start before 184. That is why the tilt row is at y=200 and not the manifest's 198.
			// PHASE 19 FIX PASS (verify finding C4): the tilt caption uses OV_SECTION_TIGHT (13 px,
			// not 17) — see the OV_* enum in main.c. 13 is the FLOOR: typo_ink_bottom(TXT_SECTION)
			// is 13, so this row asserts the tightest legal binding, and 12 would put a
			// descender on the seg's first row.
			{ TXT_SECTION, 200 - 13, 184, 200, "OV_SECTION_TIGHT caption above the tilt seg (y200)" },
			{ TXT_SECTION, 253 - 17, 220, 253, "OV_SECTION caption above the pad-edges seg (y253)" },
			// --- L4.1 the AUDIO tab's volume value, 18 px above its row
			{ TXT_SEG,     200,    200, 226, "PT_ENHANCE tilt seg label in its 26 px row" },
			{ TXT_VALUE,   82 - 18, 56, 82, "PK_STEP volume value above the y82 row" },
			{ TXT_VALUE,  132 - 18, 106, 132, "PK_STEP volume value above the y132 row" },
			// --- the dead-core panel
			{ TXT_BUTTON,   8,  0, 27, "dead-core panel: title" },
			{ TXT_BODY,    27, 27, 56, "dead-core panel: explanation" },
			// --- the pause-top summary: the two game names, then the pill row at y129
			{ TXT_BODY,    96, 80, UIHIT_PILL_Y, "paused summary: game name" },
		};
		for (unsigned k = 0; k < sizeof FITV / sizeof FITV[0]; k++) {
			int i = (int)FITV[k].r;
			if (!ok[i]) continue;
			int inkTop = FITV[k].y + typo_ink_top(FITV[k].r);
			int inkBot = FITV[k].y + typo_ink_bottom(FITV[k].r);   /* exclusive */
			CHECK(inkTop >= FITV[k].top,
			      "T16 %s: ink starts at row %d, above its box top %d", FITV[k].what, inkTop, FITV[k].top);
			CHECK(inkBot <= FITV[k].bottom,
			      "T16 %s: %s at y=%d puts ink through row %d, and its box ends at %d — this is the "
			      "clipping the phase-19 ladder introduces wherever a `y` was tuned for the old cell",
			      FITV[k].what, role_name(FITV[k].r), FITV[k].y, inkBot - 1, FITV[k].bottom);
		}

		// ---- the two rows that are CENTRED rather than offset: the pause-top pill row and the
		// pad key glyphs. Same rule, but the y comes out of typo_center_y instead of a table.
		{
			float py = typo_center_y(TXT_CHIP, (float)UIHIT_PILL_Y, (float)UIHIT_PILL_H);
			int t = (int)py + typo_ink_top(TXT_CHIP), b = (int)py + typo_ink_bottom(TXT_CHIP);
			CHECK(t >= UIHIT_PILL_Y && b <= UIHIT_PILL_Y + UIHIT_PILL_H,
			      "T16 pause-top pill: ink %d..%d leaves the pill box %d..%d",
			      t, b, UIHIT_PILL_Y, UIHIT_PILL_Y + UIHIT_PILL_H);
		}
		{	// the virtual gamepad's tallest key row: START at (128,214,64,22) — L4.6 keeps the
			// geometry and only the glyphs grow, so this is the check that says "and it still fits".
			float ky = typo_center_y(TXT_BUTTON, 214.0f, 22.0f);
			int t = (int)ky + typo_ink_top(TXT_BUTTON), b = (int)ky + typo_ink_bottom(TXT_BUTTON);
			CHECK(t >= 214 && b <= 236,
			      "T16 pad START key: ink %d..%d leaves the key box 214..236", t, b);
			CHECK(b <= UIHIT_SCREEN_H, "T16 pad START key: ink runs off the screen at row %d", b - 1);
		}
		{	// the "menu" affordance: a HIT TARGET as well as art, so its box must stay on-screen.
			CHECK(UIHIT_MCHIP_Y + UIHIT_MCHIP_H <= UIHIT_SCREEN_H,
			      "T16 menu chip: box %d..%d runs off the %d px screen",
			      UIHIT_MCHIP_Y, UIHIT_MCHIP_Y + UIHIT_MCHIP_H, UIHIT_SCREEN_H);
			float my = typo_center_y(TXT_CHIP, (float)UIHIT_MCHIP_Y, (float)UIHIT_MCHIP_H);
			int b = (int)my + typo_ink_bottom(TXT_CHIP);
			CHECK(b <= UIHIT_MCHIP_Y + UIHIT_MCHIP_H, "T16 menu chip label: ink through %d, box ends %d",
			      b - 1, UIHIT_MCHIP_Y + UIHIT_MCHIP_H);
		}
		// ---- and the ROW-PITCH rule the .sav picker broke: consecutive rows must not have their
		// ink boxes overlap. This is what a 16 px pitch under an 18 px cell actually costs.
		{
			enum { SAV_ROW_H = 20, SAV_VIS = 9, SAV_Y0 = 30, SAV_HELP_Y = 214 };
			CHECK(SAV_ROW_H >= typo_ink_h(TXT_BODY) + 2,
			      "T16 sav picker: a %d px row pitch cannot separate a %d px ink box",
			      SAV_ROW_H, typo_ink_h(TXT_BODY));
			int lastTop = SAV_Y0 + (SAV_VIS - 1) * SAV_ROW_H;
			CHECK(lastTop + typo_ink_bottom(TXT_BODY) <= SAV_HELP_Y + typo_ink_top(TXT_SECTION),
			      "T16 sav picker: the last of %d rows (y=%d) collides with the help line at y=%d",
			      SAV_VIS, lastTop, SAV_HELP_Y);
		}
		// ---- the HUD bar's horizontal cluster rule (L3.2.1's other half): the left cluster
		// (dot + name + FOCUS chip [+ LINK chip]) must not reach the right cluster's leftmost
		// element. Both are laid out from MEASURED widths, so this is arithmetic on the shipped
		// font rather than a box in a table.
		if (ok[TXT_BODY] && ok[TXT_SECTION] && ok[TXT_CHIP]) {
			const int CHIP_PAD = 12;   /* ui_chip_measure: label + 12 */
			struct { const char* name; int screenW; int withLink; } BAR[2] = {
				{ "Pokemon LeafGreen", 400, 0 }, { "Pokemon LeafGreen", 320, 1 },
			};
			for (int b = 0; b < 2; b++) {
				int left = 21 + run_width(&f[TXT_BODY], BAR[b].name);
				left += run_width(&f[TXT_CHIP], "\xE2\x97\x8F" "FOCUS") + CHIP_PAD;   /* the FOCUS chip */
				if (BAR[b].withLink) left += 5 + run_width(&f[TXT_CHIP], "LINK") + CHIP_PAD;
				int rx = BAR[b].screenW - 6;
				if (!BAR[b].withLink) rx -= 19;                                    /* battery glyph */
				rx -= run_width(&f[TXT_SECTION], "88:88") + 7;
				rx -= run_width(&f[TXT_SECTION], "60fps") + 8;
				if (!BAR[b].withLink) {                                            /* top bar only */
					rx -= run_width(&f[TXT_CHIP], "3D")     + CHIP_PAD + 6;
					rx -= run_width(&f[TXT_CHIP], "TILT3")  + CHIP_PAD;
					rx -= run_width(&f[TXT_CHIP], "CO-OP x")+ CHIP_PAD + 6;
				}
				CHECK(left <= rx,
				      "T16 HUD bar (%d px): the left cluster ends at x=%d and the right cluster "
				      "starts at x=%d — at the phase-19 widths they overlap", BAR[b].screenW, left, rx);
				printf("     HUD %3d px bar: left cluster ends %3d, right cluster starts %3d (%d px clear)\n",
				       BAR[b].screenW, left, rx, rx - left);
			}
		}
		// ---- the pause-top pill row, summed on the shipped face at its WORST case (nine pills,
		// longest variant of every dynamic label). L3.1.4 spends padding rather than type here.
		if (ok[TXT_CHIP]) {
			static const char* const PILLS[9] = { "3D", "DoF", "Bloom", "Light", "Tilt", "Vivid",
			                                      "Touch Off", "Co-op", "Wireless" };
			int tw = 0;
			for (int k = 0; k < 9; k++) tw += run_width(&f[TXT_CHIP], PILLS[k]) + UIHIT_PILL_PAD + UIHIT_PILL_GAP;
			CHECK(tw <= UIHIT_PILL_ROW_W,
			      "T16 pause-top pill row: nine pills sum to %d px on a %d px screen", tw, UIHIT_PILL_ROW_W);
			CHECK(tw <= UIHIT_PILL_ROW_W - 20,
			      "T16 pause-top pill row: %d px leaves only %d px of margin — the row needs to read "
			      "as centred chrome, not as a strip jammed edge to edge", tw, UIHIT_PILL_ROW_W - tw);
			printf("     pause-top pills: 9 worst-case pills = %d px on %d (%d px margins)\n",
			       tw, UIHIT_PILL_ROW_W, (UIHIT_PILL_ROW_W - tw) / 2);
			// PHASE 19 FIX PASS (verify finding C5). The finding's FRAMING is refuted and its
			// measurement is banked here instead. C5 said "the plate dims native x 80..320 and the row
			// now overhangs that band". The shipped pause-top.png does no such thing: every row of it is
			// a UNIFORM full-screen scrim — row 137 is RGBA (9,7,13,189) for all 400 columns — so there
			// is no 80..320 band in the ART. The 240 px band the capture shows is the GAME IMAGE at
			// SCALE_1X (240x160 centred on 400x240), i.e. a user setting; the pills are outlined chrome
			// over a scrim and read at least as well over plain letterbox as over dimmed game pixels.
			// What IS true, measured: the design's own widget rect for this row is pause-top
			// "active-feature pills" = x 56 y 129 w 288, and at the phase-19 chip the row exceeds 288 px
			// for most real states (8 pills with the longest dynamic labels = 313 px; 9 = 352 px). That
			// is a deviation from a design HINT with no visual consequence on this plate — nothing is
			// baked outside x 56..344 to collide with — so the gates that mean something are asserted:
			// the row stays on the screen with real margins (above), and it stays CENTRED on the
			// manifest rect's own centre, which is what makes it read as chrome belonging to that band.
			{
				enum { MANIFEST_X = 56, MANIFEST_W = 288 };
				static const char* const PILLS8[8] = { "3D", "DoF", "Bloom", "Light", "Tilt",
				                                       "Touch Off", "Co-op", "Wireless" };
				int t8 = 0;
				for (int k = 0; k < 8; k++)
					t8 += run_width(&f[TXT_CHIP], PILLS8[k]) + UIHIT_PILL_PAD + UIHIT_PILL_GAP;
				int row8 = t8 - UIHIT_PILL_GAP;            /* no trailing gap is drawn */
				int left8 = (UIHIT_PILL_ROW_W - t8) / 2;   /* main.c: x = (400 - tw) / 2 */
				CHECK(2 * left8 + row8 >= UIHIT_PILL_ROW_W - 6 &&
				      2 * left8 + row8 <= UIHIT_PILL_ROW_W + 6,
				      "T16 pause-top pill row: 8 pills span x %d..%d, whose centre is %d — the manifest "
				      "rect's centre (and the screen's) is %d", left8, left8 + row8,
				      left8 + row8 / 2, MANIFEST_X + MANIFEST_W / 2);
				CHECK(left8 >= 20 && left8 + row8 <= UIHIT_PILL_ROW_W - 20,
				      "T16 pause-top pill row: 8 pills span x %d..%d on a %d px screen — under 20 px of "
				      "margin it stops reading as centred chrome", left8, left8 + row8, UIHIT_PILL_ROW_W);
				printf("     pause-top pills: 8-pill worst row = %d px at x %d..%d "
				       "(design widget rect %d..%d, centre %d)\n",
				       row8, left8, left8 + row8, MANIFEST_X, MANIFEST_X + MANIFEST_W,
				       MANIFEST_X + MANIFEST_W / 2);
			}
		}
	}

	// ---------------- T19 — ONE MODULE PER BAND (PHASE 19 FIX PASS, verify finding C1)
	// The blocker this fix pass answered was not a string overflowing its box — T10 and T16 both
	// passed while it shipped — it was TWO MODULES drawing into the SAME 20 px band with no shared
	// constant between them. main.c's bottom HUD bar (game name + ●FOCUS/LINK chips + fps/clock)
	// and touch.c's mode chip ("TOUCH · SMART POINTER") both own the top of the 320 px screen, and
	// the phase-19 rungs made them collide: the name was cut mid-word and the FOCUS chip vanished
	// underneath the mode chip. A per-string test can never see that, so this one is written from
	// the other end: PROVE the two are geometrically incompatible, then assert main.c gates the bar
	// so that they can never both be drawn.
	printf("\nT19: the bottom screen's top band has exactly one owner per touch mode\n");
	if (ok[TXT_BODY] && ok[TXT_CHIP]) {
		const int CHIP_PAD = 12;              /* ui_chip_measure: label + 12 */
		/* touch.c: ui_fill(cx - w/2, y, w, TCHIP_H) with w = ui_chip_measure(s) + 2 */
		struct { const char* s; } MODE[2] = { { "TOUCH \xC2\xB7 SMART POINTER" },
		                                      { "TOUCH \xC2\xB7 GAMEPAD" } };
		for (int m = 0; m < 2; m++) {
			int cw    = run_width(&f[TXT_CHIP], MODE[m].s) + CHIP_PAD + 2;
			int chipL = 160 - cw / 2, chipR = chipL + cw;
			/* the bar's left cluster for the longest real game name, exactly as main.c lays it out */
			int nameL = 15, nameR = nameL + run_width(&f[TXT_BODY], "Pokemon Emerald");
			int focL  = 21 + run_width(&f[TXT_BODY], "Pokemon Emerald");
			int focR  = focL + run_width(&f[TXT_CHIP], "\xE2\x97\x8F" "FOCUS") + CHIP_PAD;
			/* vertical: the bar owns 0..HUD_BAR_H-1 plus a 2 px focus rule; the chip owns its own box */
			int barBot  = UIHIT_HUD_BAR_H + 2;                       /* exclusive */
			int chipTop = UIHIT_TOUCH_CHIP_Y;
			int yOverlap = (chipTop < barBot);
			int xOverlap = (chipL < focR && chipR > nameL);
			CHECK(yOverlap && xOverlap,
			      "T19 %s: the mode chip (x %d..%d, y from %d) and the HUD bar's left cluster "
			      "(name %d..%d, FOCUS %d..%d, bar+rule rows 0..%d) do NOT overlap — if that is really "
			      "true the gate below is over-strict and the bar could come back",
			      MODE[m].s, chipL, chipR, chipTop, nameL, nameR, focL, focR, barBot - 1);
			printf("     %-24s chip x %3d..%3d vs name %3d..%3d / FOCUS %3d..%3d -> %s\n",
			       MODE[m].s, chipL, chipR, nameL, nameR, focL, focR,
			       (yOverlap && xOverlap) ? "COLLIDES (so only one may draw)" : "clear");
		}
		/* ...and main.c must gate the bottom bar on TOUCH_OFF, not on "not the pad". The source text
		   is the only place this rule lives; count_ident is comment- and string-blind, so TOUCH_PAD
		   appearing here would be a REAL gate, not prose. The bar block is the only site that names
		   the mode alongside `hudMode`, and phase 19 replaced its `tmEff != TOUCH_PAD` with
		   `tmEff == TOUCH_OFF`; every other TOUCH_PAD use is input routing or the pad's own draw. */
		{
			int nOff = count_ident("source/main.c", "TOUCH_OFF");
			CHECK(nOff >= 5,
			      "T19: source/main.c names TOUCH_OFF %d times; the bottom HUD bar's gate is one of "
			      "them (`(hudMode & 2) && tmEff == TOUCH_OFF`) and it must not be rewritten back to a "
			      "\"not the pad\" test — Smart mode owns the same band", nOff);
		}
	}

	for (int i = 0; i < TXT_COUNT; i++) if (ok[i]) font_close(&f[i]);

	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
