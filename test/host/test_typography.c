// test_typography.c — PC host unit test for the phase-18 type ladder (source/typography.h)
// and the seven baked faces in data/.
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
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_typography.c -lm -o /tmp/tty && /tmp/tty
//   (run from the project root — T1/T5/T6 read data/, T6b/T9 read source/)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../source/typography.h"
#include "../../source/uihit.h"       // FIX PASS: T11 grades a real viewport constant

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

	// ---------------- T5 — the phase-17 pack must be gone
	printf("\nT5: the four phase-17 faces are removed from data/\n");
	{
		static const char* dead[] = { "fnt_sg_bold", "fnt_sg_med", "fnt_jbm_med", "fnt_jbm_bold" };
		for (int i = 0; i < 4; i++) {
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
		for (int j = i + 1; j < TXT_COUNT; j++)
			CHECK(strcmp(a.sym, typo_face((TxtRole)j).sym) != 0,
			      "T7 %s and %s share the face %s — one of them is a wasted 525 KB",
			      role_name((TxtRole)i), role_name((TxtRole)j), a.sym);
	}
	CHECK(typo_face((TxtRole)TXT_COUNT).px == 0.0f, "T7 out-of-range role must return an empty face");
	CHECK(typo_role_px(TXT_SECTION) == 9.0f, "T7 typo_role_px disagrees with typo_face");
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
			{ TXT_SECTION, SET_LINK_DISABLED_NOTE,  219, "settings LINK disabled note" },
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
			if (w > FIT[k].box * 9 / 10)
				printf("    note: %-22s %-24s %3d/%3d px (>90%% of the box)\n",
				       FIT[k].where, FIT[k].s, w, FIT[k].box);
		}
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
		static const struct { TxtRole r; int y; const char* what; } UNDER[] = {
			{ TXT_SECTION, SET_LINK_NOTE_Y, "run_settings LINK disabled note" },
		};
		for (unsigned k = 0; k < sizeof UNDER / sizeof UNDER[0]; k++) {
			int i = (int)UNDER[k].r;
			if (!ok[i]) continue;
			// The line box a citro2d text run occupies is cellHeight tall at texel scale 1.0.
			int bottom = UNDER[k].y + f[i].cellH;
			CHECK(bottom <= UIHIT_MENU_VIEW_H,
			      "T11 %s: line box y %d..%d overlaps the chrome repaint at y >= %d",
			      UNDER[k].what, UNDER[k].y, bottom, UIHIT_MENU_VIEW_H);
			// ...and it must still sit below the tab's last control (PT_LINK's presence toggle
			// ends at y = 214), or it would print on top of the row it is explaining.
			CHECK(UNDER[k].y >= 214, "T11 %s: y %d collides with the last row", UNDER[k].what, UNDER[k].y);
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
			{ "source/main.c",       13, "assets_ready fallbacks + the dev diag readouts" },
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

	for (int i = 0; i < TXT_COUNT; i++) if (ok[i]) font_close(&f[i]);

	printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
