// test_peersprite.c — PC host unit test for the phase-20 peer-sprite module (source/peersprite.c).
// The 14th app host suite.
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_peersprite.c \
//         source/peersprite.c source/presence_art.c source/presence.c -o /tmp/tps && /tmp/tps
//
// (presence_art.c comes along for T10/T11 — the _wh generalisations belong to the phase-15 art
//  module — and it in turn needs presence_sub from presence.c. Both are already header-free pure C.)
//
// A NEW FILE rather than growth in test_presence.c, for two reasons: phase 19 has concurrent edits
// in the working tree and two suites cannot collide if they are two files; and peersprite.c is
// header-free pure C with no dependency on presence.c at all, so linking it separately is what
// PROVES that (SPEC I5 / CLAUDE.md rule #4).
//
//   T1  BGR555 -> RGBA8 over the whole 5-bit ramp ......... SPEC S3.2
//   T2  4bpp nibble order (low nibble is LEFT) ............ SPEC S3.1
//   T3  tile ordering, 1D ................................. SPEC S1.6
//   T4  tile ordering, 2D + the "tile soup" regression .... SPEC S1.6
//   T5  flips (h / v / both / neither), 16x32 AND 32x32 ... SPEC S1.5, S3.3
//   T6  transparency + the edge bleed ..................... SPEC S3.4
//   T7  the change key .................................... SPEC S3.5
//   T8  THE FROZEN-FRAME REGRESSION ....................... SPEC S3.5
//   T9  the resolve ladder, all 15 clauses + every name ... SPEC S4.1, S4.2
//   T10 the anchor identity (16x32) and the 32x32 bottom .. SPEC S5.1
//   T11 the generalised clip .............................. SPEC S5.2
//   T12 the tiled encode, vs an INDEPENDENT decoder ....... SPEC S3.6
//   T13 the S3.7 proof harness can actually go red ........ SPEC S3.7
//   T14 bounds ............................................ SPEC S4.1 cl.7/13/15, S4.4
//
// MUTATION GATE (phase-18 discipline: a green suite proves nothing until it can go red). Each of
// these must produce >= 1 failure, and each was RUN:
//   * drop the hFlip term from pspr_decode                  -> T5
//   * swap the 1D tile formula for the 2D one               -> T3, T4, T9(map1d), T14
//   * swap the nibble order in pspr_tile_index              -> T2, T5
//   * drop animCmdIndex from pspr_hdr_changed               -> T7, T8
//   * write R,G,B,A instead of A,B,G,R in pspr_blit_tiled   -> T12
//   * invert the block-row order in pspr_tex_offset         -> T12, T13
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "peersprite.h"
#include "presence_art.h"   // T10/T11: the _wh generalisations live with the phase-15 art module

static int g_checks = 0, g_fails = 0;
static void check(int ok, const char* msg) {
	g_checks++;
	if (!ok) { g_fails++; printf("  [FAIL] %s\n", msg); }
}
#define CHECK(c, msg) check((c) ? 1 : 0, msg)
#define EQI(a, b, msg) do { long _a = (long)(a), _b = (long)(b); \
	g_checks++; if (_a != _b) { g_fails++; \
	printf("  [FAIL] %s: got %ld want %ld\n", msg, _a, _b); } } while (0)
#define EQU32(a, b, msg) do { uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
	g_checks++; if (_a != _b) { g_fails++; \
	printf("  [FAIL] %s: got 0x%08X want 0x%08X\n", msg, _a, _b); } } while (0)

// ============================================================================================
// A fake GBA bus: a sparse byte map, so a test writes only the handful of bytes the assertion is
// about. Every unwritten address reads 0 — which is also the honest model (an address the app
// names but the game has not populated reads as zeroes on a real core too).
// ============================================================================================
#define FAKE_SLOTS 8192
typedef struct {
	uint32_t addr[FAKE_SLOTS];
	uint8_t  val [FAKE_SLOTS];
	int      n;
	int      n8, n16, n32;    // read counters — the COST claims in SPEC S1.8 are asserted, not stated
} FakeBus;

static void fb_reset(FakeBus* m) { memset(m, 0, sizeof *m); }
static void fb_w8(FakeBus* m, uint32_t a, uint8_t v) {
	for (int i = 0; i < m->n; i++) if (m->addr[i] == a) { m->val[i] = v; return; }
	if (m->n < FAKE_SLOTS) { m->addr[m->n] = a; m->val[m->n] = v; m->n++; }
}
static void fb_w16(FakeBus* m, uint32_t a, uint16_t v) {
	fb_w8(m, a, (uint8_t)(v & 0xFF)); fb_w8(m, a + 1, (uint8_t)(v >> 8));
}
static void fb_w32(FakeBus* m, uint32_t a, uint32_t v) {
	fb_w16(m, a, (uint16_t)(v & 0xFFFF)); fb_w16(m, a + 2, (uint16_t)(v >> 16));
}
static uint8_t fb_r8raw(FakeBus* m, uint32_t a) {
	for (int i = 0; i < m->n; i++) if (m->addr[i] == a) return m->val[i];
	return 0;
}
static uint8_t  bus8 (void* c, uint32_t a) { FakeBus* m = (FakeBus*)c; m->n8++;  return fb_r8raw(m, a); }
static uint16_t bus16(void* c, uint32_t a) { FakeBus* m = (FakeBus*)c; m->n16++;
	return (uint16_t)(fb_r8raw(m, a) | ((uint16_t)fb_r8raw(m, a + 1) << 8)); }
static uint32_t bus32(void* c, uint32_t a) { FakeBus* m = (FakeBus*)c; m->n32++;
	return (uint32_t)fb_r8raw(m, a)              | ((uint32_t)fb_r8raw(m, a + 1) <<  8)
	     | ((uint32_t)fb_r8raw(m, a + 2) << 16)  | ((uint32_t)fb_r8raw(m, a + 3) << 24); }
static PsprBus mk_bus(FakeBus* m) { PsprBus b; b.rd8 = bus8; b.rd16 = bus16; b.rd32 = bus32; b.ctx = m; return b; }

// A synthetic 4bpp tile whose every pixel carries palette index `idx`.
static void tile_fill(uint8_t* t, int idx) {
	uint8_t b = (uint8_t)((idx & 0x0F) | ((idx & 0x0F) << 4));
	memset(t, b, PSPR_TILE_BYTES);
}
// A tile whose pixel (x, y) carries index f(x, y).
static void tile_fn(uint8_t* t, int (*f)(int, int)) {
	memset(t, 0, PSPR_TILE_BYTES);
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++) {
			int v = f(x, y) & 0x0F;
			t[y * 4 + (x >> 1)] |= (uint8_t)((x & 1) ? (v << 4) : v);
		}
}
static int fn_diag(int x, int y) { return ((x + 2 * y) % 15) + 1; }   // never 0 -> fully opaque

// An identity palette so a decoded pixel's colour identifies its INDEX unambiguously.
static void pal_ident(uint16_t* p) { for (int i = 0; i < 16; i++) p[i] = (uint16_t)(i * 0x0421u); }

// ============================================================================================
// T1 — BGR555 -> RGBA8
// ============================================================================================
static void t1(void) {
	printf("T1  BGR555 -> RGBA8 (SPEC S3.2)\n");
	EQU32(pspr_bgr555_to_rgba8(0x0000), 0x000000FFu, "black");
	EQU32(pspr_bgr555_to_rgba8(0x7FFF), 0xFFFFFFFFu, "white");
	EQU32(pspr_bgr555_to_rgba8(0x001F), 0xFF0000FFu, "pure red   (BGR555 bits 0-4 are RED)");
	EQU32(pspr_bgr555_to_rgba8(0x03E0), 0x00FF00FFu, "pure green");
	EQU32(pspr_bgr555_to_rgba8(0x7C00), 0x0000FFFFu, "pure blue  (bits 10-14 are BLUE)");
	// The whole ramp, in all three channels, against the properties bit replication actually has.
	//
	// *** DEVIATION FROM SPEC S7 T1, which asks for `v8 == (v5*255 + 15) / 31`. THAT IS FALSE, and
	// this test found it: bit replication computes floor(v * 33/4) = floor(8.25 v), while the
	// round-to-nearest exact expansion is round(v * 255/31) = round(8.2258 v). They disagree at
	// v = 3, 7, 24, 28 (24 vs 25, 57 vs 58, 198 vs 197, 231 vs 230). Replication is still the right
	// choice — it is what every GBA/DS/3DS pipeline uses, it is branch-free, and it is EXACT at both
	// endpoints, which is the property that matters (a palette's white must come out 0xFFFFFF, not
	// 0xF8F8F8). So the golden is the honest set of properties rather than a formula that is not
	// what the code computes. ***
	uint32_t prevR = 0;
	for (int v = 0; v < 32; v++) {
		uint32_t got   = pspr_bgr555_to_rgba8((uint16_t)v) >> 24;
		uint32_t exact = (uint32_t)((v * 255 + 15) / 31);
		EQU32(got, (uint32_t)((v << 3) | (v >> 2)), "red == the bit-replication expansion");
		CHECK(got + 1 >= exact && exact + 1 >= got, "red is within 1 LSB of the exact expansion");
		CHECK(v == 0 || got > prevR, "the ramp is strictly increasing");
		prevR = got;
		// The same word, in the other two channels — a channel swap would show up here.
		EQU32((pspr_bgr555_to_rgba8((uint16_t)(v << 5))  >> 16) & 0xFF, got, "green ramp");
		EQU32((pspr_bgr555_to_rgba8((uint16_t)(v << 10)) >>  8) & 0xFF, got, "blue ramp");
		// Alpha is ALWAYS 0xFF here: transparency is decided by the INDEX (index 0 in pspr_decode),
		// never by the colour, so a palette entry can never make a pixel see-through.
		EQU32(pspr_bgr555_to_rgba8((uint16_t)(v | (v << 5) | (v << 10))) & 0xFFu, 0xFFu, "alpha");
	}
	EQU32(pspr_bgr555_to_rgba8(0) >> 24, 0u,    "endpoint 0 is EXACT");
	EQU32(pspr_bgr555_to_rgba8(31) >> 24, 255u, "endpoint 31 is EXACT (a palette white stays white)");
	// Bit 15 is unused on the GBA and must be ignored, not folded into blue.
	EQU32(pspr_bgr555_to_rgba8(0xFFFF), pspr_bgr555_to_rgba8(0x7FFF), "bit 15 is ignored");
}

// ============================================================================================
// T2 — 4bpp nibble order. THE LOW NIBBLE IS THE LEFT PIXEL. Reverse it and every sprite comes out
// mirrored inside each 8-px tile: a defect that reads as bad art, not as a bug.
// ============================================================================================
static void t2(void) {
	printf("T2  4bpp nibble order (SPEC S3.1)\n");
	uint8_t t[PSPR_TILE_BYTES];
	memset(t, 0, sizeof t);
	// Row 0 = 0x21, 0x43, 0x65, 0x87 -> indices 1,2,3,4,5,6,7,8 left to right.
	t[0] = 0x21; t[1] = 0x43; t[2] = 0x65; t[3] = 0x87;
	for (int x = 0; x < 8; x++) EQI(pspr_tile_index(t, x, 0), x + 1, "row 0 left-to-right");
	// Every other row of that tile is zero.
	for (int y = 1; y < 8; y++)
		for (int x = 0; x < 8; x++) EQI(pspr_tile_index(t, x, y), 0, "untouched rows are index 0");
	// Row addressing: 4 bytes per row.
	memset(t, 0, sizeof t);
	t[5 * 4 + 2] = 0x0A;
	EQI(pspr_tile_index(t, 4, 5), 0x0A, "row stride is 4 bytes");
	EQI(pspr_tile_index(t, 5, 5), 0x00, "high nibble of that byte is the RIGHT pixel");
	// Out of range is 0, never a wild read.
	EQI(pspr_tile_index(t, -1, 0), 0, "x < 0");
	EQI(pspr_tile_index(t,  8, 0), 0, "x > 7");
	EQI(pspr_tile_index(NULL, 0, 0), 0, "NULL tile");
}

// ============================================================================================
// T3 / T4 — tile ordering. 1D is a linear run of the sprite's own tiles; 2D indexes a 32-tile-wide
// grid. Both are driven END TO END through pspr_gather_tiles over a fake VRAM, so the assertion is
// about the addresses that would really be formed, not about a formula in isolation.
// ============================================================================================
static void gather_case(FakeBus* m, PsprHdr* h, uint16_t tileNum, int map1d, int w, int hh) {
	fb_reset(m);
	memset(h, 0, sizeof *h);
	h->ok = 1; h->w = (uint8_t)w; h->h = (uint8_t)hh;
	h->tileNum = tileNum; h->map1d = (uint8_t)map1d;
	// Paint tile (tx, ty) of the sprite with a distinct index k+1, at the address the mapping mode
	// says it lives at.
	int tw = w / 8, th = hh / 8;
	for (int ty = 0; ty < th; ty++)
		for (int tx = 0; tx < tw; tx++) {
			uint8_t t[PSPR_TILE_BYTES];
			tile_fill(t, ty * tw + tx + 1);
			uint32_t idx = (uint32_t)(tileNum + (map1d ? (ty * tw + tx) : (ty * 32 + tx)));
			for (int b = 0; b < PSPR_TILE_BYTES; b++)
				fb_w8(m, PSPR_OBJ_VRAM + PSPR_TILE_BYTES * idx + (uint32_t)b, t[b]);
		}
}

static void t3(void) {
	printf("T3  tile ordering, 1D (SPEC S1.6)\n");
	EQI(pspr_tile_offset(0, 0, 2, 1), 0, "1D (0,0)");
	EQI(pspr_tile_offset(1, 0, 2, 1), 1, "1D (1,0)");
	EQI(pspr_tile_offset(0, 1, 2, 1), 2, "1D (0,1)");
	EQI(pspr_tile_offset(1, 3, 2, 1), 7, "1D (1,3) = the last tile of a 16x32");
	EQI(pspr_max_tile_offset(16, 32, 1), 7,  "1D 16x32 spans 8 tiles");
	EQI(pspr_max_tile_offset(32, 32, 1), 15, "1D 32x32 spans 16 tiles");

	FakeBus m; PsprHdr h;
	gather_case(&m, &h, 0x0148, 1, 16, 32);
	uint8_t buf[PSPR_MAX_TILE_BYTES];
	PsprBus bus = mk_bus(&m);
	EQI(pspr_gather_tiles(&bus, &h, buf, (int)sizeof buf), 8 * PSPR_TILE_BYTES, "1D byte count");
	EQI(m.n32, 8 * 8, "1D 16x32 costs exactly 64 read32 (SPEC S1.8's burst figure)");
	// Decode and check tile k landed at (8*(k&1), 8*(k>>1)) carrying index k+1.
	uint16_t pal[16]; pal_ident(pal);
	uint32_t px[PSPR_MAX_PIXELS];
	EQI(pspr_decode(buf, 8 * PSPR_TILE_BYTES, pal, &h, px, PSPR_MAX_PIXELS), 16 * 32, "1D decode size");
	for (int k = 0; k < 8; k++) {
		int ox = 8 * (k & 1), oy = 8 * (k >> 1);
		EQU32(px[(oy + 3) * 16 + ox + 3], pspr_bgr555_to_rgba8(pal[k + 1]), "1D tile k at (8*(k&1), 8*(k>>1))");
	}
}

static void t4(void) {
	printf("T4  tile ordering, 2D + the tile-soup regression (SPEC S1.6)\n");
	EQI(pspr_tile_offset(0, 1, 2, 0), 32, "2D (0,1) steps a whole 32-tile row");
	EQI(pspr_tile_offset(1, 3, 2, 0), 97, "2D (1,3)");
	EQI(pspr_max_tile_offset(16, 32, 0), 97, "2D 16x32 max offset");

	FakeBus m; PsprHdr h;
	uint8_t buf[PSPR_MAX_TILE_BYTES];
	uint16_t pal[16]; pal_ident(pal);
	uint32_t px2d[PSPR_MAX_PIXELS], px1d[PSPR_MAX_PIXELS];

	gather_case(&m, &h, 0x0080, 0, 16, 32);
	PsprBus bus = mk_bus(&m);
	EQI(pspr_gather_tiles(&bus, &h, buf, (int)sizeof buf), 8 * PSPR_TILE_BYTES, "2D byte count");
	EQI(pspr_decode(buf, 8 * PSPR_TILE_BYTES, pal, &h, px2d, PSPR_MAX_PIXELS), 16 * 32, "2D decode size");
	for (int k = 0; k < 8; k++) {
		int ox = 8 * (k & 1), oy = 8 * (k >> 1);
		EQU32(px2d[(oy + 3) * 16 + ox + 3], pspr_bgr555_to_rgba8(pal[k + 1]), "2D tile k lands raster");
	}
	// THE TILE SOUP. Reading a 2D-mapped image with the 1D rule (and vice versa) must NOT produce
	// the same picture — that is the whole failure mode, and it is silent without this assertion.
	PsprHdr wrong = h; wrong.map1d = 1;
	EQI(pspr_gather_tiles(&bus, &wrong, buf, (int)sizeof buf), 8 * PSPR_TILE_BYTES, "wrong-rule gather");
	EQI(pspr_decode(buf, 8 * PSPR_TILE_BYTES, pal, &wrong, px1d, PSPR_MAX_PIXELS), 16 * 32, "wrong-rule decode");
	CHECK(memcmp(px1d, px2d, sizeof px2d) != 0, "1D rule applied to a 2D image must differ (tile soup)");
}

// ============================================================================================
// T5 — flips. Gen-3 authors South/North/West and produces EAST BY MIRRORING WEST; ignoring attr1
// bit 12 gives a trainer who faces east and walks backwards.
// ============================================================================================
static void decode_wh(int w, int hh, int hF, int vF, uint32_t* out) {
	PsprHdr h; memset(&h, 0, sizeof h);
	h.ok = 1; h.w = (uint8_t)w; h.h = (uint8_t)hh; h.hFlip = (uint8_t)hF; h.vFlip = (uint8_t)vF;
	h.map1d = 1;
	int tw = w / 8, th = hh / 8;
	uint8_t tiles[PSPR_MAX_TILE_BYTES];
	for (int k = 0; k < tw * th; k++) tile_fn(tiles + k * PSPR_TILE_BYTES, fn_diag);
	uint16_t pal[16]; pal_ident(pal);
	pspr_decode(tiles, tw * th * PSPR_TILE_BYTES, pal, &h, out, PSPR_MAX_PIXELS);
}

static void t5(void) {
	printf("T5  flips, 16x32 and 32x32 (SPEC S1.5, S3.3)\n");
	static const int W[2] = { 16, 32 }, H[2] = { 32, 32 };
	for (int c = 0; c < 2; c++) {
		int w = W[c], hh = H[c];
		uint32_t id[PSPR_MAX_PIXELS], hf[PSPR_MAX_PIXELS], vf[PSPR_MAX_PIXELS], bf[PSPR_MAX_PIXELS];
		decode_wh(w, hh, 0, 0, id);
		decode_wh(w, hh, 1, 0, hf);
		decode_wh(w, hh, 0, 1, vf);
		decode_wh(w, hh, 1, 1, bf);
		int asym = 0;
		for (int y = 0; y < hh; y++)
			for (int x = 0; x < w; x++) {
				if (id[y * w + x] != id[y * w + (w - 1 - x)]) asym = 1;
				EQU32(hf[y * w + x], id[y * w + (w - 1 - x)],      "hFlip == column-reversed");
				EQU32(vf[y * w + x], id[(hh - 1 - y) * w + x],     "vFlip == row-reversed");
				EQU32(bf[y * w + x], id[(hh - 1 - y) * w + (w - 1 - x)], "both == 180 degrees");
			}
		CHECK(asym, "the golden must be horizontally ASYMMETRIC or the hFlip test proves nothing");
	}
}

// ============================================================================================
// T6 — transparency + the edge bleed. Alpha is NEVER modified: that is what makes the bleed
// incapable of painting a halo, and it is the property that has to be pinned in both directions.
// ============================================================================================
static void t6(void) {
	printf("T6  transparency + edge bleed (SPEC S3.4)\n");
	PsprHdr h; memset(&h, 0, sizeof h);
	h.ok = 1; h.w = 16; h.h = 32; h.map1d = 1;
	uint8_t tiles[PSPR_MAX_TILE_BYTES];
	memset(tiles, 0, sizeof tiles);                       // every index 0 -> fully transparent
	uint16_t pal[16]; pal_ident(pal);
	pal[0] = 0x7FFF;                                       // colour 0 is WHITE — and must never show
	uint32_t px[PSPR_MAX_PIXELS];
	EQI(pspr_decode(tiles, 8 * PSPR_TILE_BYTES, pal, &h, px, PSPR_MAX_PIXELS), 512, "all-index-0 decode");
	for (int i = 0; i < 16 * 32; i++) EQU32(px[i], 0x00000000u, "index 0 => 0x00000000, never colour 0");

	// A fully transparent cell is a FIXED POINT of the bleed.
	uint32_t before[PSPR_MAX_PIXELS];
	memcpy(before, px, sizeof px);
	pspr_bleed_edges(px, 16, 32);
	CHECK(memcmp(before, px, (size_t)16 * 32 * 4) == 0, "all-transparent cell is a fixed point");

	// A fully OPAQUE cell is a fixed point too.
	for (int k = 0; k < 8; k++) tile_fill(tiles + k * PSPR_TILE_BYTES, 5);
	EQI(pspr_decode(tiles, 8 * PSPR_TILE_BYTES, pal, &h, px, PSPR_MAX_PIXELS), 512, "opaque decode");
	memcpy(before, px, sizeof px);
	pspr_bleed_edges(px, 16, 32);
	CHECK(memcmp(before, px, (size_t)16 * 32 * 4) == 0, "all-opaque cell is a fixed point");

	// The real case: one opaque texel in a transparent field. Its four neighbours gain RGB and KEEP
	// alpha 0; everything else is untouched.
	memset(px, 0, sizeof px);
	uint32_t ink = 0x11223344u;   // alpha 0x44 = opaque enough
	px[10 * 16 + 8] = ink;
	memcpy(before, px, sizeof px);
	pspr_bleed_edges(px, 16, 32);
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 16; x++) {
			int i = y * 16 + x;
			int nb = (x == 7 && y == 10) || (x == 9 && y == 10) || (x == 8 && y == 9) || (x == 8 && y == 11);
			if (x == 8 && y == 10)      EQU32(px[i], ink, "the opaque texel itself is untouched");
			else if (nb) { EQU32(px[i] & 0xFFu, 0u, "a bled texel keeps ALPHA == 0");
			               EQU32(px[i] >> 8, ink >> 8, "a bled texel takes the neighbour's RGB"); }
			else                         EQU32(px[i], before[i], "a non-neighbour is untouched");
		}
	// And no texel ANYWHERE gained alpha.
	for (int i = 0; i < 16 * 32; i++)
		CHECK((px[i] & 0xFFu) == (before[i] & 0xFFu), "the bleed never modifies alpha");
	pspr_bleed_edges(NULL, 16, 32);       // NULL-safe
	pspr_bleed_edges(px, 999, 999);       // out-of-range-safe
}

// ============================================================================================
// T7 / T8 — the change key.
// ============================================================================================
static PsprHdr base_hdr(void) {
	PsprHdr h; memset(&h, 0, sizeof h);
	h.ok = 1; h.spriteId = 12; h.graphicsId = 0x00; h.tileNum = 0x148;
	h.pal = 5; h.w = 16; h.h = 32; h.animNum = 1; h.animCmdIndex = 2; h.map1d = 1; h.subTbl = 3;
	return h;
}

static void t7(void) {
	printf("T7  the change key (SPEC S3.5)\n");
	PsprHdr a = base_hdr(), b;
	CHECK(!pspr_hdr_changed(&a, &a), "identical structs do not fire");
	b = a; b.graphicsId++;   CHECK(pspr_hdr_changed(&a, &b), "graphicsId fires");
	b = a; b.tileNum++;      CHECK(pspr_hdr_changed(&a, &b), "tileNum fires");
	b = a; b.pal++;          CHECK(pspr_hdr_changed(&a, &b), "pal fires");
	b = a; b.hFlip = 1;      CHECK(pspr_hdr_changed(&a, &b), "hFlip fires");
	b = a; b.vFlip = 1;      CHECK(pspr_hdr_changed(&a, &b), "vFlip fires");
	b = a; b.w = 32;         CHECK(pspr_hdr_changed(&a, &b), "w fires");
	b = a; b.h = 8;          CHECK(pspr_hdr_changed(&a, &b), "h fires");
	b = a; b.animNum++;      CHECK(pspr_hdr_changed(&a, &b), "animNum fires");
	b = a; b.animCmdIndex++; CHECK(pspr_hdr_changed(&a, &b), "animCmdIndex fires");
	b = a; b.map1d = 0;      CHECK(pspr_hdr_changed(&a, &b), "map1d fires");
	b = a; b.spriteId++;     CHECK(pspr_hdr_changed(&a, &b), "spriteId fires");
	b = a; b.ok = 0;         CHECK(pspr_hdr_changed(&a, &b), "ok fires");
	// subTbl is DIAGNOSTICS ONLY: it tracks elevation and long grass, which change no pixel. If it
	// were in the key, walking into tall grass would fire a 90-read burst every step.
	b = a; b.subTbl = 63;    CHECK(!pspr_hdr_changed(&a, &b), "subTbl alone does NOT fire");
	// ...and the reason field is not in the key either (it is an output, not an identity).
	b = a; b.reason = PSPR_R_HIDDEN; CHECK(!pspr_hdr_changed(&a, &b), "reason alone does NOT fire");
	CHECK(pspr_hdr_changed(NULL, &a), "NULL is always CHANGED (never silently 'same')");

	uint16_t p1[16], p2[16];
	pal_ident(p1); memcpy(p2, p1, sizeof p1);
	CHECK(!pspr_pal_changed(p1, p2), "identical palettes do not fire");
	for (int i = 0; i < 16; i++) {
		memcpy(p2, p1, sizeof p1);
		p2[i] ^= 1u;
		CHECK(pspr_pal_changed(p1, p2), "every one of the 16 colours fires");
	}
	CHECK(pspr_pal_changed(NULL, p1), "NULL palette is always CHANGED");
}

static void t8(void) {
	printf("T8  THE FROZEN-FRAME REGRESSION (SPEC S3.5)\n");
	// The exact bug the key exists to prevent. For the overworld's non-sheet sprites `tileNum` is
	// CONSTANT across the whole walk cycle (src/sprite.c:936-939 takes the
	// RequestSpriteFrameImageCopy branch, which DMAs new pixels to the SAME tileNum), so a key that
	// used tileNum alone would freeze a walking peer on frame 1 forever.
	PsprHdr a = base_hdr(), b = a;
	b.animNum = 1; b.animCmdIndex = 3;
	EQI(a.tileNum, b.tileNum, "the two frames share a tileNum — that IS the trap");
	CHECK(pspr_hdr_changed(&a, &b), "an animCmdIndex step alone MUST be reported CHANGED");
	b = a; b.animNum = 2; b.animCmdIndex = 0;
	CHECK(pspr_hdr_changed(&a, &b), "an animNum step alone MUST be reported CHANGED");
}

// ============================================================================================
// T9 — the resolve ladder, clause by clause.
// ============================================================================================
static PsprRaw good_raw(void) {
	PsprRaw r; memset(&r, 0, sizeof r);
	r.gateDraw = r.haveProf = r.surfOk = r.texOk = r.ctxOk = r.objActive = 1;
	r.spriteId = 12; r.graphicsId = 0;
	r.haveAvatar = 1; r.avatarSpriteId = 12;
	r.attr0 = (uint16_t)(2u << 14);                    // shape 2 VERTICAL, affineMode 0, bpp 0
	r.attr1 = (uint16_t)((2u << 14) | (1u << 12));     // size 2 -> 16x32, hFlip set
	r.attr2 = (uint16_t)(0x148u | (5u << 12));         // tileNum 0x148, palette bank 5
	r.anim  = (uint16_t)(1u | (2u << 8));              // animNum 1, animCmdIndex 2
	r.sflags = 0x0001u;                                 // inUse, not invisible
	r.subTbl = 3;
	r.dispcntOk = 1; r.dispcnt = 0x1040u;              // mode 0, OBJ enabled (bit 12), 1D (bit 6)
	return r;
}
static int reason_of(PsprRaw r) { PsprHdr h; pspr_resolve(&r, &h); return h.reason; }

static void t9(void) {
	printf("T9  the resolve ladder, all 15 clauses (SPEC S4.1)\n");
	PsprHdr h;
	PsprRaw g = good_raw();
	CHECK(pspr_resolve(&g, &h) == 1, "the good case resolves");
	EQI(h.reason, PSPR_R_OK,  "reason OK");
	EQI(h.ok, 1,              "ok flag");
	EQI(h.w, 16, "w"); EQI(h.h, 32, "h");
	EQI(h.tileNum, 0x148, "tileNum"); EQI(h.pal, 5, "paletteNum");
	EQI(h.hFlip, 1, "hFlip == attr1 bit 12"); EQI(h.vFlip, 0, "vFlip");
	EQI(h.animNum, 1, "animNum"); EQI(h.animCmdIndex, 2, "animCmdIndex");
	EQI(h.map1d, 1, "map1d == DISPCNT bit 6"); EQI(h.subTbl, 3, "subTbl carried for diagnostics");
	EQI(h.spriteId, 12, "spriteId"); EQI(h.graphicsId, 0, "graphicsId");

	{ PsprRaw r = g; r.gateDraw = 0;        EQI(reason_of(r), PSPR_R_CTX,      "cl1 not drawing"); }
	{ PsprRaw r = g; r.remote = 1;          EQI(reason_of(r), PSPR_R_REMOTE,   "cl2 M4 peer"); }
	{ PsprRaw r = g; r.haveProf = 0;        EQI(reason_of(r), PSPR_R_NOPROF,   "cl3 no profile"); }
	{ PsprRaw r = g; r.surfOk = 0;          EQI(reason_of(r), PSPR_R_NOSURF,   "cl4a no sheet"); }
	{ PsprRaw r = g; r.texOk = 0;           EQI(reason_of(r), PSPR_R_TILEFMT,  "cl4b tiling proof red"); }
	{ PsprRaw r = g; r.ctxOk = 0;           EQI(reason_of(r), PSPR_R_CTX,      "cl5 not overworld"); }
	{ PsprRaw r = g; r.objActive = 0;       EQI(reason_of(r), PSPR_R_NOOBJ,    "cl6 object inactive"); }
	{ PsprRaw r = g; r.spriteId = 64;       EQI(reason_of(r), PSPR_R_BADID,    "cl7 spriteId 64"); }
	{ PsprRaw r = g; r.spriteId = 0xFF;     EQI(reason_of(r), PSPR_R_BADID,    "cl7 SPRITE_NONE"); }
	{ PsprRaw r = g; r.avatarSpriteId = 13; EQI(reason_of(r), PSPR_R_MISMATCH, "cl8 avatar disagrees"); }
	{ PsprRaw r = g; r.haveAvatar = 0; r.avatarSpriteId = 13;
	                                        EQI(reason_of(r), PSPR_R_OK,       "cl8 skipped when unmapped"); }
	{ PsprRaw r = g; r.sflags = 0;          EQI(reason_of(r), PSPR_R_NOTINUSE, "cl9 !inUse"); }
	{ PsprRaw r = g; r.sflags = 0x0005u;    EQI(reason_of(r), PSPR_R_HIDDEN,   "cl10 invisible"); }
	{ PsprRaw r = g; r.attr0 |= (1u << 8);  EQI(reason_of(r), PSPR_R_AFFINE,   "cl11 affineMode 1"); }
	{ PsprRaw r = g; r.attr0 |= (2u << 8);  EQI(reason_of(r), PSPR_R_AFFINE,   "cl11 affineMode 2"); }
	{ PsprRaw r = g; r.attr0 |= (1u << 13); EQI(reason_of(r), PSPR_R_BPP,      "cl12 256-colour"); }
	{ PsprRaw r = g; r.attr0 = (uint16_t)((r.attr0 & 0x3FFFu) | (3u << 14));
	                                        EQI(reason_of(r), PSPR_R_SIZE,     "cl13 shape 3"); }
	{ PsprRaw r = g; r.attr0 = (uint16_t)(r.attr0 & 0x3FFFu);   // shape 0 SQUARE
	                 r.attr1 = (uint16_t)((r.attr1 & 0x3FFFu) | (3u << 14));  // size 3 -> 64x64
	                                        EQI(reason_of(r), PSPR_R_SIZE,     "cl13 64x64 refused"); }
	{ PsprRaw r = g; r.dispcnt = 0x1043u;   EQI(reason_of(r), PSPR_R_MODE,     "cl14 bitmap mode 3"); }
	{ PsprRaw r = g; r.dispcnt = 0x0040u;   EQI(reason_of(r), PSPR_R_MODE,     "cl14 OBJ disabled"); }
	{ PsprRaw r = g; r.dispcnt = 0x1000u;   PsprHdr o; pspr_resolve(&r, &o);
	                 EQI(o.reason, PSPR_R_OK, "cl14 2D mapping is ACCEPTED"); EQI(o.map1d, 0, "map1d 0"); }
	{ PsprRaw r = g; r.dispcntOk = 0; r.dispcnt = 0;  PsprHdr o; pspr_resolve(&r, &o);
	                 EQI(o.reason, PSPR_R_OK, "unreadable DISPCNT falls back, never refuses");
	                 EQI(o.map1d, 1, "...to the pret-cited default: mode 0, 1D"); }
	{ PsprRaw r = g; r.attr2 = (uint16_t)((r.attr2 & 0xFC00u) | 1020u);
	                                        EQI(reason_of(r), PSPR_R_TILE,     "cl15 tile range leaves VRAM"); }

	// A refusal must ZERO the header, never hand back a half-parsed one.
	{ PsprRaw r = g; r.sflags = 0; PsprHdr o;
	  CHECK(pspr_resolve(&r, &o) == 0, "a refusal returns 0");
	  EQI(o.ok, 0, "refused: ok is 0"); EQI(o.w, 0, "refused: w zeroed");
	  EQI(o.tileNum, 0, "refused: tileNum zeroed"); EQI(o.spriteId, 0, "refused: spriteId zeroed"); }
	CHECK(pspr_resolve(NULL, &h) == 0, "NULL raw is refused, not dereferenced");
	{ PsprRaw r = g; CHECK(pspr_resolve(&r, NULL) == 0, "NULL out is refused"); }

	// Every reason code has a NAME, and the names are unique — the HUD prints these.
	for (int i = 0; i < PSPR_R_COUNT; i++) {
		const char* a = pspr_reason_name(i);
		CHECK(a != NULL && a[0] != '\0', "every reason code has a non-empty name");
		for (int j = i + 1; j < PSPR_R_COUNT; j++)
			CHECK(strcmp(a, pspr_reason_name(j)) != 0, "reason names are unique");
	}
	CHECK(strcmp(pspr_reason_name(-1),           "?") == 0, "out of range name");
	CHECK(strcmp(pspr_reason_name(PSPR_R_COUNT), "?") == 0, "out of range name (high)");
}

// ============================================================================================
// T10 / T11 — the phase-15 art geometry, generalised. The identities are what make phase 20 a
// PURE ADDITION to the draw path rather than a rewrite of it.
// ============================================================================================
static void t10(void) {
	printf("T10 the anchor identity (SPEC S5.1)\n");
	for (int i = -40; i <= 280; i += 7) {
		for (int j = -40; j <= 200; j += 11) {
			float fx = (float)i + 0.5f, fy = (float)j - 0.25f;
			float ax, ay, bx, by;
			presence_art_rect(fx, fy, &ax, &ay);
			presence_art_rect_wh(fx, fy, PRES_CELL_W, PRES_CELL_H, &bx, &by);
			CHECK(ax == bx && ay == by, "presence_art_rect == _wh(16, 32), bit for bit");
			// A 32x32 form hangs off the SAME foot point with the same BOTTOM edge, 8 px wider each
			// side — which is exactly pret's own subsprite convention (.x = -16, .y = -16 vs -8/-16).
			float cx, cy;
			presence_art_rect_wh(fx, fy, 32, 32, &cx, &cy);
			CHECK(cy + 32.0f == fy, "32x32: the BOTTOM edge sits on the foot y");
			CHECK(cx + 16.0f == fx, "32x32: horizontally centred on the foot x");
			CHECK(cx == ax - 8.0f,  "32x32 is 8 px wider on each side than 16x32");
		}
	}
	presence_art_rect_wh(1.0f, 2.0f, 16, 32, NULL, NULL);   // NULL-safe
}

static void t11(void) {
	printf("T11 the generalised clip (SPEC S5.2)\n");
	// The identity, over a sweep that includes every edge and both mirror values.
	for (int i = -30; i <= 250; i += 3) {
		for (int j = -40; j <= 170; j += 5) {
			for (int mir = 0; mir < 2; mir++) {
				for (int mi = 0; mi < 2; mi++) {
					float mg = mi ? 26.5f : 0.0f;
					float x = (float)i, y = (float)j;
					PresArtDraw a, b;
					int ra = presence_art_clip   (x, y,           mir, mg, &a);
					int rb = presence_art_clip_wh(x, y, PRES_CELL_W, PRES_CELL_H, mir, mg, &b);
					CHECK(ra == rb, "clip == clip_wh(16, 32): same verdict");
					if (ra) CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h &&
					              a.cx == b.cx && a.cy == b.cy, "clip == clip_wh(16, 32): same rect");
				}
			}
		}
	}
	// The 32x32 cases, at all four frame edges and fully outside.
	PresArtDraw d;
	CHECK(presence_art_clip_wh(100.0f, 60.0f, 32, 32, 0, 0.0f, &d), "32x32 mid-frame draws");
	EQI(d.w, 32, "mid-frame full width"); EQI(d.h, 32, "mid-frame full height");
	EQI(d.cx, 0, "mid-frame source x"); EQI(d.cy, 0, "mid-frame source y");
	CHECK(presence_art_clip_wh(-10.0f, 60.0f, 32, 32, 0, 0.0f, &d), "32x32 off the LEFT clips");
	EQI(d.w, 22, "left cut is 10 columns"); EQI(d.cx, 10, "unmirrored: the cut comes off source LEFT");
	CHECK(presence_art_clip_wh(-10.0f, 60.0f, 32, 32, 1, 0.0f, &d), "32x32 off the LEFT, mirrored");
	EQI(d.cx, 0, "mirrored: a destination-left cut comes off the source RIGHT");
	CHECK(presence_art_clip_wh(220.0f, 60.0f, 32, 32, 0, 0.0f, &d), "32x32 off the RIGHT clips");
	EQI(d.w, 20, "right cut is 12 columns");
	CHECK(presence_art_clip_wh(100.0f, -12.0f, 32, 32, 0, 0.0f, &d), "32x32 off the TOP clips");
	EQI(d.h, 20, "top cut"); EQI(d.cy, 12, "source y follows the top cut");
	CHECK(presence_art_clip_wh(100.0f, 150.0f, 32, 32, 0, 0.0f, &d), "32x32 off the BOTTOM clips");
	EQI(d.h, 10, "bottom cut");
	CHECK(!presence_art_clip_wh(-40.0f, 60.0f, 32, 32, 0, 0.0f, &d), "fully off the left is culled");
	CHECK(!presence_art_clip_wh(300.0f, 60.0f, 32, 32, 0, 0.0f, &d), "fully off the right is culled");
	CHECK(!presence_art_clip_wh(100.0f, -40.0f, 32, 32, 0, 0.0f, &d), "fully above is culled");
	CHECK(!presence_art_clip_wh(100.0f, 200.0f, 32, 32, 0, 0.0f, &d), "fully below is culled");
	CHECK(!presence_art_clip_wh(100.0f, 60.0f, 0, 32, 0, 0.0f, &d), "a zero-width cell is refused");
	CHECK(!presence_art_clip_wh(100.0f, 60.0f, 32, 32, 0, 0.0f, NULL), "NULL out is refused");
	// The spill margin rescues a sprite that the flat box would amputate.
	CHECK(presence_art_clip_wh(-10.0f, 60.0f, 32, 32, 0, 26.5f, &d), "with spill, the left case draws");
	EQI(d.w, 32, "with spill nothing is cut"); EQI(d.cx, 0, "...and the source offset is 0");

	// The live cell selector.
	PresArtCell c;
	presence_art_live_cell(0, &c);
	EQI(c.x, 0, "live slot 0 x"); EQI(c.y, PSPR_LIVE_Y, "live slot 0 y");
	EQI(c.mirror, 0, "live cells never mirror");
	presence_art_live_cell(1, &c); EQI(c.x, 32, "live slot 1 x");
	presence_art_live_cell(3, &c); EQI(c.x, 96, "live slot 3 x");
	presence_art_live_cell(4, &c); EQI(c.x, 0,  "an out-of-range slot clamps to 0, never indexes out");
	presence_art_live_cell(-1, &c); EQI(c.x, 0, "a negative slot clamps to 0");
	presence_art_live_cell(0, NULL);   // NULL-safe
}

// ============================================================================================
// T12 — the tiled encode, checked with an INDEPENDENTLY WRITTEN decoder (Morton recomputed from
// first principles, not the shipped pspr_morton8). If both were the same function, the test would
// only prove the encoder agrees with itself.
// ============================================================================================
static long indep_offset(int x, int y, int dim) {
	// Morton interleave, written the other way round: take the low 3 bits of x and y and interleave
	// them x0 y0 x1 y1 x2 y2 (x in the EVEN bit positions).
	int m = 0;
	for (int b = 0; b < 3; b++) {
		m |= ((x >> b) & 1) << (2 * b);
		m |= ((y >> b) & 1) << (2 * b + 1);
	}
	long blocksPerRow = dim / 8;
	long block = (long)(y / 8) * blocksPerRow + (long)(x / 8);
	return block * 256 + (long)m * 4;
}

static void t12(void) {
	printf("T12 the tiled encode (SPEC S3.6)\n");
	const int DIM = PRES_SHEET_DIM;
	// The two offset functions must agree everywhere.
	for (int y = 0; y < DIM; y++)
		for (int x = 0; x < DIM; x++)
			EQI(pspr_tex_offset(x, y, DIM), indep_offset(x, y, DIM), "pspr_tex_offset vs an independent Morton");
	EQI(pspr_tex_offset(-1, 0, DIM), -1, "out of range x");
	EQI(pspr_tex_offset(0, DIM, DIM), -1, "out of range y");

	uint8_t* tex = calloc(1, (size_t)DIM * DIM * 4);
	CHECK(tex != NULL, "alloc");
	if (!tex) return;
	// A golden gradient, blitted into the live cell at (0, 96), read back through the independent
	// decoder byte for byte in A, B, G, R order.
	uint32_t px[PSPR_MAX_PIXELS];
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 32; x++)
			px[y * 32 + x] = ((uint32_t)(x * 8) << 24) | ((uint32_t)(y * 8) << 16)
			               | ((uint32_t)(x ^ y) << 8) | (uint32_t)((x + y) & 0xFF);
	pspr_blit_tiled(tex, DIM, PSPR_LIVE_X(0), PSPR_LIVE_Y, px, 32, 32);
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 32; x++) {
			long o = indep_offset(x, PSPR_LIVE_Y + y, DIM);
			uint32_t w = px[y * 32 + x];
			EQI(tex[o + 0], (w      ) & 0xFF, "byte 0 is ALPHA");
			EQI(tex[o + 1], (w >>  8) & 0xFF, "byte 1 is BLUE");
			EQI(tex[o + 2], (w >> 16) & 0xFF, "byte 2 is GREEN");
			EQI(tex[o + 3], (w >> 24) & 0xFF, "byte 3 is RED");
		}
	// THE FLUSH RANGE, proven in software: a cell at (0, 96) must touch ONLY [49152, 65536), which
	// is what licenses main.c's 16 KB GSPGPU_FlushDataCache instead of a whole-texture C3D_TexFlush.
	int outside = 0, inside = 0;
	for (long i = 0; i < (long)DIM * DIM * 4; i++) {
		if (!tex[i]) continue;
		if (i < 49152 || i >= 65536) outside++; else inside++;
	}
	EQI(outside, 0, "a live-cell blit touches NOTHING below byte 49152");
	CHECK(inside > 0, "...and it did write something (the assertion above is not vacuous)");
	// All four slots stay inside that range too — the 3-4-player ceiling costs no layout work.
	for (int s = 0; s < PSPR_LIVE_SLOTS; s++) {
		long lo = pspr_tex_offset(PSPR_LIVE_X(s), PSPR_LIVE_Y, DIM);
		long hi = pspr_tex_offset(PSPR_LIVE_X(s) + 31, PSPR_LIVE_Y + 31, DIM);
		CHECK(lo >= 49152 && hi < 65536, "every live slot lies in the last 16 KB");
	}
	// Bounds: a blit that would leave the texture is refused whole, never partially applied.
	uint8_t* copy = malloc((size_t)DIM * DIM * 4);
	CHECK(copy != NULL, "alloc 2");
	if (copy) {
		memcpy(copy, tex, (size_t)DIM * DIM * 4);
		pspr_blit_tiled(tex, DIM, DIM - 8, PSPR_LIVE_Y, px, 32, 32);
		CHECK(memcmp(copy, tex, (size_t)DIM * DIM * 4) == 0, "an overhanging blit writes NOTHING");
		pspr_blit_tiled(tex, DIM, -1, 0, px, 32, 32);
		CHECK(memcmp(copy, tex, (size_t)DIM * DIM * 4) == 0, "a negative origin writes NOTHING");
		pspr_blit_tiled(NULL, DIM, 0, 0, px, 32, 32);
		free(copy);
	}
	free(tex);
}

// ============================================================================================
// T13 — the S3.7 proof harness. The design deliberately makes the WRONG answer safe rather than
// making the right answer certain, which only works if the oracle can actually go red.
// ============================================================================================
static void t13(void) {
	printf("T13 the tiling proof harness (SPEC S3.7)\n");
	const int DIM = 32;                       // a small sheet: same code path, 4 KB instead of 64
	size_t n = (size_t)DIM * DIM * 4;
	uint8_t* lin = malloc(n); uint8_t* oracle = malloc(n); uint8_t* scratch = malloc(n);
	CHECK(lin && oracle && scratch, "alloc");
	if (!lin || !oracle || !scratch) { free(lin); free(oracle); free(scratch); return; }
	for (size_t i = 0; i < n; i++) lin[i] = (uint8_t)((i * 37u + 11u) & 0xFFu);
	pspr_encode_tiled(oracle, lin, DIM);
	CHECK(pspr_verify_tiling(lin, oracle, DIM, scratch) == 1, "a matching pair passes");
	// ...and every way of being wrong fails.
	uint8_t* bad = malloc(n);
	CHECK(bad != NULL, "alloc bad");
	if (bad) {
		memcpy(bad, oracle, n); bad[n / 3] ^= 1u;
		CHECK(pspr_verify_tiling(lin, bad, DIM, scratch) == 0, "one flipped BIT goes red");
		// one channel swapped everywhere
		memcpy(bad, oracle, n);
		for (size_t i = 0; i < n; i += 4) { uint8_t t = bad[i]; bad[i] = bad[i + 3]; bad[i + 3] = t; }
		CHECK(pspr_verify_tiling(lin, bad, DIM, scratch) == 0, "an A<->R channel swap goes red");
		// rows inverted (the GX_TRANSFER_FLIP_VERT question Open Q2 names)
		uint8_t* flip = malloc(n);
		if (flip) {
			for (int y = 0; y < DIM; y++)
				memcpy(flip + (size_t)y * DIM * 4, lin + (size_t)(DIM - 1 - y) * DIM * 4, (size_t)DIM * 4);
			pspr_encode_tiled(bad, flip, DIM);
			CHECK(pspr_verify_tiling(lin, bad, DIM, scratch) == 0, "a row-inverted oracle goes red");
			free(flip);
		}
		free(bad);
	}
	CHECK(pspr_verify_tiling(NULL, oracle, DIM, scratch) == 0, "NULL is not a match");
	free(lin); free(oracle); free(scratch);
}

// ============================================================================================
// T14 — bounds, and the whole read driven end to end through pspr_capture.
// ============================================================================================
static void t14(void) {
	printf("T14 bounds + the end-to-end capture (SPEC S4.1, S4.4, S1.8)\n");
	// tileNum 1023 with a 32x32 sprite would need tiles 1023..1038 -> refused.
	{ PsprRaw r = good_raw();
	  r.attr0 = (uint16_t)(0u << 14); r.attr1 = (uint16_t)(2u << 14);      // shape 0 size 2 -> 32x32
	  r.attr2 = (uint16_t)(1023u | (5u << 12));
	  EQI(reason_of(r), PSPR_R_TILE, "tileNum 1023 + 32x32 is refused"); }
	// tileNum 1023 with an 8x8 sprite is exactly the last tile -> accepted.
	{ PsprRaw r = good_raw();
	  r.attr0 = (uint16_t)(0u << 14); r.attr1 = (uint16_t)(0u << 14);      // shape 0 size 0 -> 8x8
	  r.attr2 = (uint16_t)(1023u | (5u << 12));
	  PsprHdr h; CHECK(pspr_resolve(&r, &h) == 1, "tileNum 1023 + 8x8 is accepted");
	  EQI(h.w, 8, "8x8 w"); EQI(h.h, 8, "8x8 h"); }
	// pal 15 addresses plttUnfaded + 512 + 480.
	{ FakeBus m; fb_reset(&m);
	  const uint32_t PLTT = 0x02037714u;
	  for (int i = 0; i < 16; i++) fb_w16(&m, PLTT + 512u + 32u * 15u + 2u * (uint32_t)i, (uint16_t)(0x1000 + i));
	  uint16_t pal[16];
	  PsprBus b = mk_bus(&m);
	  pspr_gather_palette(&b, PLTT, 15, pal);
	  for (int i = 0; i < 16; i++) EQI(pal[i], 0x1000 + i, "pal bank 15 addressing");
	  EQI(m.n16, 16, "the palette costs exactly 16 read16 (SPEC S1.8)");
	  // plttUnfaded == 0 -> the hardware PLTT fallback, NOT a read at address 512.
	  fb_reset(&m);
	  for (int i = 0; i < 16; i++) fb_w16(&m, PSPR_PLTT_OBJ + 32u * 3u + 2u * (uint32_t)i, (uint16_t)(0x2000 + i));
	  b = mk_bus(&m);
	  pspr_gather_palette(&b, 0, 3, pal);
	  for (int i = 0; i < 16; i++) EQI(pal[i], 0x2000 + i, "the hardware PLTT fallback");
	  pspr_gather_palette(&b, PLTT, 16, pal);
	  for (int i = 0; i < 16; i++) EQI(pal[i], 0, "an out-of-range bank reads nothing, never out of bounds"); }

	// ---- the end-to-end capture, over a fake Emerald ----
	const uint32_t OBJS = 0x02037350u, SPRS = 0x02020630u, PLTT = 0x02037714u, PAV = 0x02037590u;
	FakeBus m; fb_reset(&m);
	fb_w16(&m, PSPR_REG_DISPCNT, 0x1040u);            // mode 0, OBJ on, 1D
	fb_w32(&m, OBJS + 0x00u, 1u);                     // active
	fb_w8 (&m, OBJS + 0x04u, 12u);                    // spriteId
	fb_w8 (&m, OBJS + 0x05u, 0u);                     // graphicsId (Brendan normal)
	fb_w8 (&m, PAV  + 0x04u, 12u);                    // gPlayerAvatar.spriteId agrees
	uint32_t sp = SPRS + PSPR_SPRITE_STRIDE * 12u;
	fb_w16(&m, sp + 0x00u, (uint16_t)(2u << 14));                       // shape 2
	fb_w16(&m, sp + 0x02u, (uint16_t)(2u << 14));                       // size 2 -> 16x32, no flip
	fb_w16(&m, sp + 0x04u, (uint16_t)(0x0148u | (5u << 12)));           // tileNum, pal 5
	fb_w16(&m, sp + 0x2Au, (uint16_t)(1u | (2u << 8)));                 // animNum 1, cmd 2
	fb_w16(&m, sp + 0x3Eu, 0x0001u);                                    // inUse
	fb_w8 (&m, sp + 0x42u, 3u);
	for (int i = 0; i < 16; i++) fb_w16(&m, PLTT + 512u + 32u * 5u + 2u * (uint32_t)i, (uint16_t)(i * 0x0421u));
	for (int k = 0; k < 8; k++) {
		uint8_t t[PSPR_TILE_BYTES]; tile_fill(t, k + 1);
		for (int b = 0; b < PSPR_TILE_BYTES; b++)
			fb_w8(&m, PSPR_OBJ_VRAM + PSPR_TILE_BYTES * (0x0148u + (uint32_t)k) + (uint32_t)b, t[b]);
	}
	PsprCaptureIn in; memset(&in, 0, sizeof in);
	in.mapObjects = OBJS; in.sprites = SPRS; in.plttUnfaded = PLTT; in.playerAvatar = PAV;
	in.gateDraw = in.surfOk = in.texOk = in.ctxOk = 1;
	PsprCapture cap; memset(&cap, 0, sizeof cap);
	PsprBus b = mk_bus(&m);

	m.n8 = m.n16 = m.n32 = 0;
	CHECK(pspr_capture(&b, &in, &cap) == 1, "the first capture resolves");
	EQI(cap.hdr.reason, PSPR_R_OK, "reason OK");
	EQI(cap.nTileBytes, 8 * PSPR_TILE_BYTES, "a 16x32 gather is 256 bytes");
	EQI(cap.havePixels, 1, "pixels landed");
	EQI(cap.seq, 1, "seq bumped");
	EQI(cap.gathers, 1, "one gather");
	EQI(cap.diagFlags, 0, "DISPCNT was readable");
	// The header costs ONE read32 (gObjectEvents[0].active); everything above that is pixels.
	EQI(m.n32, 1 + 8 * 8, "the FIRST frame's burst is 64 read32 of tiles (+1 header read32)");
	// SPEC S1.8's steady-state claim, asserted rather than stated: 12 header reads + 16 palette
	// reads = 28, and ZERO pixel reads while nothing moves.
	m.n8 = m.n16 = m.n32 = 0;
	CHECK(pspr_capture(&b, &in, &cap) == 1, "the second capture resolves");
	EQI(cap.seq, 1, "an unchanged frame does NOT re-gather");
	EQI(cap.gathers, 1, "...and does not count a gather");
	EQI(m.n32, 1, "a standing peer costs ZERO pixel reads (the 1 read32 is the `active` bit)");
	EQI(m.n8,   5, "5 rd8:  spriteId, graphicsId, avatarFlags, avatarSpriteId, subTbl");
	EQI(m.n16, 22, "22 rd16: DISPCNT + attr0/1/2 + anim + sflags (6), then the 16 palette colours");
	EQI(m.n8 + m.n16 + m.n32, 12 + 16, "steady state is exactly 28 emulated-bus reads (SPEC S1.8)");

	// A walk step: animCmdIndex moves, tileNum does NOT. This is the T8 trap, end to end.
	fb_w16(&m, sp + 0x2Au, (uint16_t)(1u | (3u << 8)));
	m.n32 = 0;
	CHECK(pspr_capture(&b, &in, &cap) == 1, "the walk step resolves");
	EQI(cap.seq, 2, "a walk step DOES re-gather (the frozen-frame regression, end to end)");
	EQI(m.n32, 1 + 64, "...at a cost of 64 read32");

	// A refusal must not destroy the cached pixels — a peer who opens a menu and closes it again
	// resumes without re-reading 512 bytes of unchanged VRAM.
	fb_w16(&m, sp + 0x3Eu, 0x0005u);        // invisible
	CHECK(pspr_capture(&b, &in, &cap) == 0, "an invisible peer is refused");
	EQI(cap.hdr.reason, PSPR_R_HIDDEN, "...with the right reason");
	EQI(cap.havePixels, 1, "...and the cached pixels SURVIVE");
	EQI(cap.seq, 2, "...and no gather happened");
	fb_w16(&m, sp + 0x3Eu, 0x0001u);
	m.n32 = 0;
	CHECK(pspr_capture(&b, &in, &cap) == 1, "and it resumes");
	EQI(cap.seq, 2, "...without re-gathering unchanged pixels");
	EQI(m.n32, 1, "...at zero PIXEL cost (only the `active` header read32)");

	// The gate closed: not one bus read may happen.
	{ PsprCaptureIn off = in; off.gateDraw = 0;
	  m.n8 = m.n16 = m.n32 = 0;
	  CHECK(pspr_capture(&b, &off, &cap) == 0, "gateDraw 0 refuses");
	  EQI(m.n8 + m.n16 + m.n32, 0, "a closed gate costs ZERO bus reads"); }
	{ PsprCaptureIn np = in; np.sprites = 0;
	  m.n8 = m.n16 = m.n32 = 0;
	  CHECK(pspr_capture(&b, &np, &cap) == 0, "sprites == 0 refuses");
	  EQI(cap.hdr.reason, PSPR_R_NOPROF, "...as NOPROF");
	  EQI(m.n8 + m.n16 + m.n32, 0, "an unmapped game costs ZERO bus reads"); }

	// The unwired-IO path (Open Q1): DISPCNT reads back 0x0000 -> PSPR_D_NOIO + the 1D default.
	fb_w16(&m, PSPR_REG_DISPCNT, 0x0000u);
	CHECK(pspr_capture(&b, &in, &cap) == 1, "an unreadable DISPCNT still resolves");
	EQI(cap.diagFlags & PSPR_D_NOIO, PSPR_D_NOIO, "...and RAISES the diagnostic bit");
	EQI(cap.hdr.map1d, 1, "...falling back to the pret-cited mode 0 / 1D default");
	fb_w16(&m, PSPR_REG_DISPCNT, 0xFFFFu);
	CHECK(pspr_capture(&b, &in, &cap) == 1, "0xFFFF is the same signature");
	EQI(cap.diagFlags & PSPR_D_NOIO, PSPR_D_NOIO, "...and raises the bit too");

	CHECK(pspr_capture(NULL, &in, &cap) == 0, "a NULL bus is refused, not dereferenced");
	CHECK(pspr_capture(&b, NULL, &cap) == 0, "a NULL input is refused");
	CHECK(pspr_capture(&b, &in, NULL) == 0, "a NULL capture is refused");
}

int main(void) {
	printf("== test_peersprite (phase 20: the peer's genuine trainer frame) ==\n");
	t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10(); t11(); t12(); t13(); t14();
	printf("\n%d checks, %d failures\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
