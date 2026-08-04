// presence_art.c — Phase 15 co-op presence, slice M2: the render half's pure-C core.
// ============================================================================================
// Spec: docs/phase15-presence/SPEC-avatar.md A1 (asset), A2.4 (cull/clip), A2.6.3 (walk), A3.4
// (y-sort). Contract and every citation: presence_art.h. Host suite: test/host/test_presence.c
// TEST 25-31.
//
// NOTHING here knows about citro2d, libctru, the screen rect, the scale mode or the tilt angle.
// main.c composes:
//     presence_solve (foot anchor, frame space)
//       -> presence_walk_step   (pose)
//       -> presence_art_rect    (sprite top-left)
//       -> presence_art_clip    (visible rect + source cell offset)
//       -> tilt_project         (OPTIONAL: a pure translation of the foot anchor, A3.1)
//       -> calc_xform           (the screen fit, applied LAST — SPEC-render R1.7)
//       -> C2D_DrawImageAt
#include <string.h>

#include "presence_art.h"

// A2.6.3: the classic 4-beat walk read out of a 3-frame sheet. One beat per PRES_WALK_BEAT_PX of
// world travel, so a 16 px tile step plays exactly one full cycle whatever the movement speed is
// (1 px/f walk, 2 px/f run, 4 px/f bike all take the same 16 px). This is OUR animation policy —
// we never read the peer's OAM or animation state and make no claim to match the game's timing.
const uint8_t PRES_WALK_CYCLE[4] = {
	PRES_POSE_STEP_A, PRES_POSE_STAND, PRES_POSE_STEP_B, PRES_POSE_STAND
};

// ---- tiny pure helpers (no <math.h>, no <stdlib.h> — see presence_art.h's include rule) ------
// C truncates toward zero, so a sign-aware correction is needed for both directions.
static int ceil_i (float v) { int i = (int)v; return ((float)i < v) ? i + 1 : i; }
static int art_abs(int   v) { return v < 0 ? -v : v; }   // presence.c has its own iabs_;
                                                       // the host suite includes BOTH .c files

// ---- the sheet layout (A1.2.1: the ONE place a facing code becomes a cell) -------------------
// Rows are DOWN / UP / LEFT; RIGHT is LEFT drawn with a negated horizontal scale (A1.2). Gen-3
// overworld characters are authored as South/North/West sets with East produced by a horizontal
// flip, and a mirrored East halves the authored art, halves the texture, and makes it impossible
// for the two horizontal facings to drift apart — the same "one source of truth" discipline
// gen1recomp uses for its 3D poses (gen1-render.md finding 2, SpriteRenderer.lua:72-77).
//
// Index by PRES_DIR_* (0..4). Index 0 (PRES_DIR_NONE) resolves to the DOWN row, and the caller
// below additionally forces col 0, so an unknown facing draws the DOWN STANDING frame and never an
// out-of-range sheet cell (A0.4).
static const uint8_t ART_DIR_ROW[5]    = { 0, 0, 1, 2, 2 };   // NONE, SOUTH, NORTH, WEST, EAST
static const uint8_t ART_DIR_MIRROR[5] = { 0, 0, 0, 0, 1 };   // only EAST mirrors

void presence_art_cell(int gender, int dir, int pose, PresArtCell* out) {
	if (!out) return;
	int valid = (dir >= PRES_DIR_SOUTH && dir <= PRES_DIR_EAST);
	int row   = valid ? (int)ART_DIR_ROW[dir]    : 0;
	int mir   = valid ? (int)ART_DIR_MIRROR[dir] : 0;
	// A0.4 / A1.2.1: an unknown facing selects row 0 COL 0 — the pose is forced to STAND too, so a
	// garbage facing can never present as a walking peer.
	int col   = (valid && pose >= 0 && pose < PRES_POSE_COUNT) ? pose : PRES_POSE_STAND;
	int var   = (gender == 1) ? 1 : 0;                       // anything else -> male (A0.4)
	out->x      = var * PRES_VAR_STRIDE + col * PRES_CELL_W;
	out->y      = row * PRES_CELL_H;
	out->mirror = mir;
}

// ---- geometry (A1.1, A2.4) -------------------------------------------------------------------

void presence_art_rect(float footX, float footY, float* sprX, float* sprY) {
	if (sprX) *sprX = footX - (float)PRES_FOOT_DX;
	if (sprY) *sprY = footY - (float)PRES_FOOT_DY;
}

int presence_art_clip(float sprX, float sprY, int mirror, float margin, PresArtDraw* out) {
	if (!out) return 0;
	// Reject NaN and absurd magnitudes before any float->int cast. The anchor is bounded by the
	// data half's own cull (|dTile| <= 9 tiles), so this can only fire on a corrupted record — and
	// a cast of NaN to int is undefined behaviour, which is not an acceptable way to find out.
	if (!(sprX > -1.0e4f && sprX < 1.0e4f) || !(sprY > -1.0e4f && sprY < 1.0e4f)) return 0;
	// The margin is a SPILL, never a shrink, and it is bounded by the same NaN/absurdity rule. A
	// caller that hands over garbage gets the flat box, not a box computed from garbage.
	if (!(margin > 0.0f && margin < 1.0e4f)) margin = 0.0f;

	// Cuts are taken in whole SOURCE columns/rows and always ROUND OUTWARD, so a sub-pixel sliver
	// is dropped rather than allowed to bleed past the frame box. main.c:731-732 states why that is
	// the right direction: "the destination is clipped to the on-screen frame box, so a shifted pop
	// never bleeds into the letterbox (per-eye rivalry on the border)".
	float lo = -margin, hiX = (float)PRES_FRAME_W + margin, hiY = (float)PRES_FRAME_H + margin;
	int cutL = (sprX < lo) ? ceil_i(lo - sprX) : 0;
	int cutT = (sprY < lo) ? ceil_i(lo - sprY) : 0;
	float rx = sprX + (float)PRES_CELL_W, by = sprY + (float)PRES_CELL_H;
	int cutR = (rx > hiX) ? ceil_i(rx - hiX) : 0;
	int cutB = (by > hiY) ? ceil_i(by - hiY) : 0;

	int w = PRES_CELL_W - cutL - cutR;
	int h = PRES_CELL_H - cutT - cutB;
	if (w <= 0 || h <= 0) return 0;                     // fully clipped == culled (A2.4.1)

	out->x  = sprX + (float)cutL;
	out->y  = sprY + (float)cutT;
	out->w  = w;
	out->h  = h;
	// A mirrored draw maps the destination's LEFT edge to the source's RIGHT edge, so the cut taken
	// off the destination left comes off the source RIGHT. Getting this backwards is invisible mid
	// screen and makes the sprite appear to slide inside its own box at the frame edges.
	out->cx = mirror ? cutR : cutL;
	out->cy = cutT;
	return 1;
}

// ---- walk animation (A2.6.3, A2.6.4) ---------------------------------------------------------

void presence_walk_reset(PresWalk* w) {
	if (w) memset(w, 0, sizeof *w);
}

void presence_art_world_px(const PeerPresence* pe, int* wx, int* wy) {
	int cx = 0, cy = 0, px = 0, py = 0;
	if (pe) {
		px = (int)pe->px;
		py = (int)pe->py;
		if (pe->flags & PRES_F_CAM) {
			// The same +-15 guard presence.c's cam_phase and main.c:854-855 apply: out of range is
			// treated as 0, never clamped to the edge (D1.3/D1.9).
			int a = (int)pe->subX, b = (int)pe->subY;
			if (a >= -15 && a <= 15) cx = a;
			if (b >= -15 && b <= 15) cy = b;
		}
	}
	if (wx) *wx = 16 * px - presence_sub(cx);
	if (wy) *wy = 16 * py - presence_sub(cy);
}

// A single frame can legitimately move a peer 4 world px (bike). Anything past two whole tiles is
// a warp, a torn read or a re-appearance after a gap, and letting it into the accumulator would
// spin the cycle to an arbitrary phase. Treated as a discontinuity, i.e. exactly what A2.6.4 asks
// for ("does not resume mid-stride from a stale accumulator") applied to the position channel as
// well as to the round/map ones.
#define PRES_WALK_JUMP_PX  32

int presence_walk_step(PresWalk* w, const PeerPresence* pe) {
	if (!w) return PRES_POSE_STAND;
	if (!pe) { presence_walk_reset(w); return PRES_POSE_STAND; }

	int wx, wy;
	presence_art_world_px(pe, &wx, &wy);

	int step = 0;
	int disc = 0;
	if (!w->have)                                                     disc = 1;   // first sample
	else if (w->mapG != pe->mapGroup || w->mapN != pe->mapNum)        disc = 1;   // map change
	else if (pe->round < w->round)                                    disc = 1;   // producer restarted
	else if (pe->round - w->round > PRES_STALE_FRAMES)                disc = 1;   // back from stale
	else {
		step = art_abs(wx - w->wx) + art_abs(wy - w->wy);
		if (step > PRES_WALK_JUMP_PX) { disc = 1; step = 0; }                     // warp / teleport
	}

	w->have  = 1;
	w->mapG  = pe->mapGroup;
	w->mapN  = pe->mapNum;
	w->wx    = wx;
	w->wy    = wy;
	w->round = pe->round;

	if (disc) {
		w->travel = 0; w->idle = 0; w->pose = PRES_POSE_STAND;
		return w->pose;
	}
	if (step == 0) {
		if (w->idle < 0xFFFFu) w->idle++;
		if (w->idle >= PRES_IDLE_FRAMES) {
			w->travel = 0; w->pose = PRES_POSE_STAND;
			return w->pose;
		}
		// Below the idle threshold the accumulator is untouched, so the pose simply holds — that is
		// what makes a one-frame stall (a hold frame, a dropped beacon) invisible instead of a twitch.
	} else {
		w->idle    = 0;
		w->travel += step;
		// Wrap on a multiple of the 16 px cycle length so the PHASE is preserved exactly; a plain
		// clamp or a modulo by anything else would visibly skip a beat once every few hours.
		w->travel &= 0xFFFF;
	}
	w->pose = PRES_WALK_CYCLE[(w->travel / PRES_WALK_BEAT_PX) & 3];
	return w->pose;
}

// ---- y-sort (A3.4) ---------------------------------------------------------------------------

int presence_art_ycmp(const PresBillboard* a, const PresBillboard* b) {
	if (!a || !b) return 0;
	if (a->fy < b->fy) return -1;
	if (a->fy > b->fy) return  1;
	if (a->tid < b->tid) return -1;    // deterministic tie-break: a flickering one reads as z-fight
	if (a->tid > b->tid) return  1;
	if (a->slot < b->slot) return -1;  // and slot below that, so the order is TOTAL, never arbitrary
	if (a->slot > b->slot) return  1;
	return 0;
}

void presence_art_ysort(PresBillboard* b, int n) {
	if (!b || n < 2) return;           // PRES_MAX_PEERS == 1 today: a proven no-op, not dead code
	for (int i = 1; i < n; i++) {      // insertion sort: n <= 4 forever (3-4 player is the ceiling)
		PresBillboard k = b[i];
		int j = i - 1;
		while (j >= 0 && presence_art_ycmp(&b[j], &k) > 0) { b[j + 1] = b[j]; j--; }
		b[j + 1] = k;
	}
}

// ---- the placeholder sheet (A1.5) ------------------------------------------------------------
// Deliberately unmistakable. Producing finished pixel art is out of scope for an implementing
// agent, and shipping mediocre art that LOOKS finished is worse than shipping an obvious
// placeholder — so this generates the sheet at runtime with two magenta tells that no real asset
// would ever carry (A1.5.3). They are not conditionally compiled and not behind a flag: they
// disappear when real art lands, by the art landing (A1.4/A1.5.4).

// GPU_RGBA8 texel order, bytes ascending: A, B, G, R. `rgba` is 0xRRGGBBAA (presence_art.h).
static void put_px(uint8_t* p, uint32_t rgba) {
	p[0] = (uint8_t)( rgba        & 0xFFu);   // A
	p[1] = (uint8_t)((rgba >>  8) & 0xFFu);   // B
	p[2] = (uint8_t)((rgba >> 16) & 0xFFu);   // G
	p[3] = (uint8_t)((rgba >> 24) & 0xFFu);   // R
}

static uint32_t dim60(uint32_t c) {          // 60 % value, alpha untouched (A1.5.2's body colour)
	uint32_t r = (c >> 24) & 0xFFu, g = (c >> 16) & 0xFFu, b = (c >> 8) & 0xFFu, a = c & 0xFFu;
	return ((r * 3u / 5u) << 24) | ((g * 3u / 5u) << 16) | ((b * 3u / 5u) << 8) | a;
}

// The female variant's accent: a deterministic RGB rotation (r,g,b) -> (b,r,g). A1.3 asks for two
// variants but does not say how the PLACEHOLDER should distinguish them, and adding a second colour
// parameter would change the signature A1.5.1 fixes. A rotation cannot collide with the male accent
// unless the accent is grey, and the variant also carries a SHAPE tell (below) so the difference
// survives a monochrome photo and colour-blind eyes.
static uint32_t rot_rgb(uint32_t c) {
	uint32_t r = (c >> 24) & 0xFFu, g = (c >> 16) & 0xFFu, b = (c >> 8) & 0xFFu, a = c & 0xFFu;
	return (b << 24) | (r << 16) | (g << 8) | a;
}

static void cell_rect(uint32_t* c, int x0, int y0, int x1, int y1, uint32_t col) {
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > PRES_CELL_W) x1 = PRES_CELL_W;
	if (y1 > PRES_CELL_H) y1 = PRES_CELL_H;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++) c[y * PRES_CELL_W + x] = col;
}

// One 16x32 cell, cell-local coordinates, per A1.5.2's table. Rects are [x0,x1) x [y0,y1).
static void build_cell(uint32_t* c, int gender, int row, int col, uint32_t accent, uint32_t ink) {
	memset(c, 0, (size_t)PRES_CELL_W * PRES_CELL_H * sizeof *c);
	uint32_t body = dim60(accent);

	cell_rect(c, 5,  4, 11, 11, accent);   // head
	cell_rect(c, 4, 12, 12, 26, body);     // body
	if (gender) cell_rect(c, 3, 22, 13, 26, body);   // the female variant's SHAPE tell (a flare)

	// Legs. STEP_A shifts the left leg -2 and the right +1; STEP_B is its mirror (+1 / -2), which
	// reads as the opposite stride and keeps the two step frames exactly as distinguishable as the
	// stand frame is from either.
	int offL = 0, offR = 0;
	if      (col == PRES_POSE_STEP_A) { offL = -2; offR = +1; }
	else if (col == PRES_POSE_STEP_B) { offL = +1; offR = -2; }
	cell_rect(c, 5 + offL, 26,  7 + offL, 32, ink);
	cell_rect(c, 9 + offR, 26, 11 + offR, 32, ink);

	// Facing pip, 3x3 at the facing side of the head. EAST needs none: it is WEST mirrored.
	if      (row == 0) cell_rect(c, 6,  8, 9, 11, ink);   // S — bottom of the head
	else if (row == 1) cell_rect(c, 6,  4, 9,  7, ink);   // N — top
	else               cell_rect(c, 5,  6, 8,  9, ink);   // W — left

	// PLACEHOLDER TELL 1: a 1-px magenta outline around the whole silhouette. Computed from a
	// snapshot of the opaque mask so the outline never grows into itself.
	{
		uint8_t solid[PRES_CELL_W * PRES_CELL_H];
		for (int i = 0; i < PRES_CELL_W * PRES_CELL_H; i++) solid[i] = c[i] ? 1u : 0u;
		for (int y = 0; y < PRES_CELL_H; y++) {
			for (int x = 0; x < PRES_CELL_W; x++) {
				int i = y * PRES_CELL_W + x;
				if (solid[i]) continue;
				int n = (x > 0                 && solid[i - 1])
				     || (x + 1 < PRES_CELL_W   && solid[i + 1])
				     || (y > 0                 && solid[i - PRES_CELL_W])
				     || (y + 1 < PRES_CELL_H   && solid[i + PRES_CELL_W]);
				if (n) c[i] = PRES_ART_MAGENTA;
			}
		}
	}
	// PLACEHOLDER TELL 2: a 2x2 magenta/black checker in the cell's top-left 4x4. Written LAST and
	// unconditionally, so it is present in every one of the 18 cells no matter what the silhouette
	// did (test_presence TEST 30 asserts exactly that, so nobody can quietly delete it).
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			c[y * PRES_CELL_W + x] = (((x >> 1) + (y >> 1)) & 1) ? PRES_ART_BLACK : PRES_ART_MAGENTA;
}

void presence_art_build(uint8_t* rgba8, uint32_t accentRGBA, uint32_t inkRGBA) {
	if (!rgba8) return;
	memset(rgba8, 0, (size_t)PRES_SHEET_BYTES);   // everything outside the two variant blocks stays
	                                              // fully transparent (A1.3)
	uint32_t cell[PRES_CELL_W * PRES_CELL_H];
	for (int v = 0; v < 2; v++) {
		uint32_t acc = v ? rot_rgb(accentRGBA) : accentRGBA;
		for (int row = 0; row < PRES_ART_ROWS; row++) {
			for (int col = 0; col < PRES_ART_COLS; col++) {
				build_cell(cell, v, row, col, acc, inkRGBA);
				int ox = v * PRES_VAR_STRIDE + col * PRES_CELL_W;
				int oy = row * PRES_CELL_H;
				for (int y = 0; y < PRES_CELL_H; y++) {
					for (int x = 0; x < PRES_CELL_W; x++) {
						uint32_t px = cell[y * PRES_CELL_W + x];
						if (!px) continue;        // transparent: leave the memset's zero
						put_px(rgba8 + (((size_t)(oy + y) * PRES_SHEET_DIM + (size_t)(ox + x)) * 4u), px);
					}
				}
			}
		}
	}
}
