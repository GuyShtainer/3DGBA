// peersprite.c — Phase 20: the peer's genuine overworld trainer frame. Contract, citations and
// the cost bill: peersprite.h. Host suite: test/host/test_peersprite.c (T1-T14).
//
// PURE C. <stdint.h> + <string.h> only, no floats, no allocation, no globals. The ONE way this
// file reaches emulated memory is the caller-supplied PsprBus, which has three READS and no write.
#include <string.h>

#include "peersprite.h"

// ============================================================================================
// OAM decode
// ============================================================================================

// The hardware OBJ size table. Indexed [shape][size]; shape 3 is prohibited and yields 0x0, which
// pspr_resolve turns into PSPR_R_SIZE.
static const uint8_t PSPR_OBJ_W[4][4] = {
	{  8, 16, 32, 64 },   // 0 SQUARE
	{ 16, 32, 32, 64 },   // 1 HORIZONTAL
	{  8,  8, 16, 32 },   // 2 VERTICAL
	{  0,  0,  0,  0 }    // 3 PROHIBITED
};
static const uint8_t PSPR_OBJ_H[4][4] = {
	{  8, 16, 32, 64 },
	{  8,  8, 16, 32 },
	{ 16, 32, 32, 64 },
	{  0,  0,  0,  0 }
};

void pspr_oam_size(int shape, int size, int* w, int* h) {
	int ww = 0, hh = 0;
	if (shape >= 0 && shape < 4 && size >= 0 && size < 4) {
		ww = (int)PSPR_OBJ_W[shape][size];
		hh = (int)PSPR_OBJ_H[shape][size];
	}
	if (w) *w = ww;
	if (h) *h = hh;
}

static const char* const PSPR_REASON_NAME[PSPR_R_COUNT] = {
	"OK", "REMOTE", "NOPROF", "NOSURF", "TILEFMT", "CTX", "NOOBJ", "BADID", "MISMATCH",
	"NOTINUSE", "HIDDEN", "AFFINE", "BPP", "SIZE", "MODE", "TILE", "PENDING"
};

const char* pspr_reason_name(int reason) {
	if (reason < 0 || reason >= PSPR_R_COUNT) return "?";
	return PSPR_REASON_NAME[reason];
}

// ============================================================================================
// The resolve ladder (SPEC S4.1). Clause order is the spec's, and it is load-bearing: the cheap
// caller-side refusals come first so a peer who is not being drawn at all costs nothing, and every
// clause that DEREFERENCES something is downstream of the clause that proved it safe.
// ============================================================================================
int pspr_resolve(const PsprRaw* in, PsprHdr* out) {
	PsprHdr h;
	memset(&h, 0, sizeof h);
	if (!out) return 0;
	if (!in) { h.reason = PSPR_R_CTX; *out = h; return 0; }

	#define PSPR_REFUSE(r) do { memset(&h, 0, sizeof h); h.reason = (uint8_t)(r); \
	                            *out = h; return 0; } while (0)

	// Clause 1 has no reason code of its own in S4.2, and it does not need one: when presence has
	// already refused to draw this peer, `presence_off_reason` on the same HUD line is the answer
	// and this field is noise. CTX is the honest umbrella ("not in a state we can read a peer from").
	if (!in->gateDraw)  PSPR_REFUSE(PSPR_R_CTX);       // 1: presence itself is not drawing this peer
	if (in->remote)     PSPR_REFUSE(PSPR_R_REMOTE);    // 2: their VRAM is on another console (S2.5)
	if (!in->haveProf)  PSPR_REFUSE(PSPR_R_NOPROF);    // 3
	if (!in->surfOk)    PSPR_REFUSE(PSPR_R_NOSURF);    // 4a: the sheet was never built
	if (!in->texOk)     PSPR_REFUSE(PSPR_R_TILEFMT);   // 4b: the S3.7 init proof went red
	if (!in->ctxOk)     PSPR_REFUSE(PSPR_R_CTX);       // 5: not a readable overworld state
	if (!in->objActive) PSPR_REFUSE(PSPR_R_NOOBJ);     // 6

	// 7. A spriteId that is out of range or the SPRITE_NONE sentinel would index gSprites past its
	//    own 65-entry extent. This is the one bound that stands between us and a wrong-struct read.
	if (in->spriteId == PSPR_SPRITE_NONE || in->spriteId >= PSPR_MAX_SPRITES)
		PSPR_REFUSE(PSPR_R_BADID);

	// 8. The corroborating cross-check, at a cost of one rd8: gPlayerAvatar.spriteId must agree
	//    with gObjectEvents[0].spriteId. "Slot 0 is the player" is true in practice and our whole
	//    presence stack already depends on it; this turns that into a per-frame assertion. Skipped
	//    when the profile has no gPlayerAvatar column (0 = not mapped, never a guess).
	if (in->haveAvatar && in->avatarSpriteId != in->spriteId) PSPR_REFUSE(PSPR_R_MISMATCH);

	// 9/10. Liveness. `inUse` is bit 0 and `invisible` bit 2 of the u16 at +0x3E. A peer who has
	//    entered a battle is caught here as well as by clause 5: ResetSpriteData frees the field
	//    sprites and the slot is reused, so refusing on !inUse is a second independent net.
	if (!(in->sflags & 0x0001u)) PSPR_REFUSE(PSPR_R_NOTINUSE);
	if (  in->sflags & 0x0004u ) PSPR_REFUSE(PSPR_R_HIDDEN);

	// 11. affineMode is attr0 bits 8-9. Nonzero means attr1[9:13] is a real matrix index and the
	//     flip bits DO NOT EXIST. An overworld trainer is never affine; if we ever see one, refuse
	//     rather than read a matrix index as a pair of flips.
	if (((in->attr0 >> 8) & 3u) != 0u) PSPR_REFUSE(PSPR_R_AFFINE);
	// 12. bpp is attr0 bit 13. Set == 256-colour OBJ, whose tiles are 64 bytes and whose palette is
	//     not a 16-entry bank — a different decoder, not a variant of this one.
	if ((in->attr0 >> 13) & 1u) PSPR_REFUSE(PSPR_R_BPP);

	// 13. Shape/size -> extent. shape 3 gives 0x0 from the table; anything past 32x32 is refused
	//     because the live cell block is 32x32 (S3.6) and because a 64-px OBJ is not a trainer.
	{
		int shape = (int)((in->attr0 >> 14) & 3u);
		int size  = (int)((in->attr1 >> 14) & 3u);
		int w = 0, hgt = 0;
		pspr_oam_size(shape, size, &w, &hgt);
		if (w <= 0 || hgt <= 0 || w > PSPR_MAX_W || hgt > PSPR_MAX_H) PSPR_REFUSE(PSPR_R_SIZE);
		h.w = (uint8_t)w;
		h.h = (uint8_t)hgt;
	}

	// 14. DISPCNT. mode 0-2 only: in bitmap modes 3-5 the OBJ character base moves to 0x06014000
	//     and tiles 0-511 are unusable, so PSPR_OBJ_VRAM would be the wrong base entirely. OBJ
	//     drawing must also be enabled (bit 12), because a peer whose game is not drawing sprites
	//     has nothing for us to read.
	//     When the IO read looked unwired (dispcntOk == 0) we assume the S1.6 defaults — mode 0,
	//     1D mapping, OBJ on — which is a DOCUMENTED PROPERTY of these five games, not a guess, and
	//     the caller has already raised PSPR_D_NOIO so the run says it took that path.
	if (in->dispcntOk) {
		if ((in->dispcnt & 7u) > 2u)      PSPR_REFUSE(PSPR_R_MODE);
		if (!(in->dispcnt & 0x1000u))     PSPR_REFUSE(PSPR_R_MODE);
		h.map1d = (uint8_t)((in->dispcnt >> 6) & 1u);
	} else {
		h.map1d = 1;   // Gen-3 runs the overworld in BG mode 0 with DISPCNT_OBJ_1D_MAP
	}

	h.tileNum = (uint16_t)(in->attr2 & 0x03FFu);
	h.pal     = (uint8_t)((in->attr2 >> 12) & 0x0Fu);
	h.hFlip   = (uint8_t)((in->attr1 >> 12) & 1u);   // matrixNum bit 3 (SPEC S1.5)
	h.vFlip   = (uint8_t)((in->attr1 >> 13) & 1u);   // matrixNum bit 4

	// 15. Bounds. The last tile we touch must satisfy tileNum + maxTileOffset <= 1023; the resulting
	//     address range ends at 0x06010000 + 32*1023 + 31 = 0x06017FFF, the last byte of VRAM, so a
	//     passing check makes an out-of-range read impossible BY CONSTRUCTION.
	{
		int maxOff = pspr_max_tile_offset((int)h.w, (int)h.h, (int)h.map1d);
		if ((int)h.tileNum + maxOff > PSPR_MAX_TILENUM) PSPR_REFUSE(PSPR_R_TILE);
	}

	h.spriteId     = in->spriteId;
	h.graphicsId   = in->graphicsId;
	h.animNum      = (uint8_t)( in->anim       & 0xFFu);
	h.animCmdIndex = (uint8_t)((in->anim >> 8) & 0xFFu);
	h.subTbl       = in->subTbl;
	h.reason       = PSPR_R_OK;
	h.ok           = 1;
	*out = h;
	return 1;
	#undef PSPR_REFUSE
}

// ============================================================================================
// Tile addressing (SPEC S1.6)
// ============================================================================================
int pspr_tile_offset(int tx, int ty, int tilesW, int map1d) {
	if (tx < 0 || ty < 0 || tilesW <= 0) return 0;
	return map1d ? (ty * tilesW + tx)
	             : (ty * 32     + tx);   // OBJ VRAM is a 32-tile-wide grid in 2D mapping
}

int pspr_max_tile_offset(int w, int h, int map1d) {
	int tw = w / 8, th = h / 8;
	if (tw <= 0 || th <= 0) return 0;
	return pspr_tile_offset(tw - 1, th - 1, tw, map1d);
}

// ============================================================================================
// Conversion (SPEC S3.1-S3.4)
// ============================================================================================
uint32_t pspr_bgr555_to_rgba8(uint16_t c) {
	uint32_t r5 = (uint32_t)( c        & 0x1Fu);
	uint32_t g5 = (uint32_t)((c >>  5) & 0x1Fu);
	uint32_t b5 = (uint32_t)((c >> 10) & 0x1Fu);
	// 5 -> 8 with the high bits replicated into the low ones: 0 -> 0 and 31 -> 255 exactly, which
	// is the same expansion `(v*255 + 15) / 31` gives for every one of the 32 inputs (T1 asserts
	// the identity rather than trusting the folklore).
	uint32_t r8 = (r5 << 3) | (r5 >> 2);
	uint32_t g8 = (g5 << 3) | (g5 >> 2);
	uint32_t b8 = (b5 << 3) | (b5 >> 2);
	return (r8 << 24) | (g8 << 16) | (b8 << 8) | 0xFFu;
}

int pspr_tile_index(const uint8_t* tile32, int x, int y) {
	if (!tile32 || x < 0 || x > 7 || y < 0 || y > 7) return 0;
	uint8_t b = tile32[y * 4 + (x >> 1)];
	// THE LOW NIBBLE IS THE LEFT PIXEL. Reverse it and every sprite comes out mirrored inside each
	// 8-px tile — a defect that looks like bad art rather than like a bug (T2).
	return (x & 1) ? ((b >> 4) & 0x0F) : (b & 0x0F);
}

int pspr_decode(const uint8_t* tiles, int nTileBytes, const uint16_t* pal,
                const PsprHdr* h, uint32_t* out, int outCap) {
	if (!tiles || !pal || !h || !out) return 0;
	int w = (int)h->w, hh = (int)h->h;
	if (w <= 0 || hh <= 0 || w > PSPR_MAX_W || hh > PSPR_MAX_H) return 0;
	if ((w & 7) || (hh & 7)) return 0;
	int tw = w / 8, th = hh / 8;
	if (nTileBytes < tw * th * PSPR_TILE_BYTES) return 0;
	if (outCap < w * hh) return 0;

	for (int ty = 0; ty < th; ty++) {
		for (int tx = 0; tx < tw; tx++) {
			// `tiles` is RASTER tile order — pspr_gather_tiles already applied the 1D/2D mapping
			// when it fetched them, so the mapping mode appears exactly ONCE in this module and
			// never in the inner loop.
			const uint8_t* tile = tiles + (size_t)(ty * tw + tx) * PSPR_TILE_BYTES;
			for (int py = 0; py < 8; py++) {
				for (int px = 0; px < 8; px++) {
					int i  = pspr_tile_index(tile, px, py);
					int sx = tx * 8 + px, sy = ty * 8 + py;
					// The flip is applied to the DESTINATION coordinate: one pass, and the draw path
					// stays flip-free (SPEC S1.5).
					if (h->hFlip) sx = w  - 1 - sx;
					if (h->vFlip) sy = hh - 1 - sy;
					// Index 0 is TRANSPARENT and must never be painted with colour 0's RGB.
					out[(size_t)sy * (size_t)w + (size_t)sx] =
						i ? pspr_bgr555_to_rgba8(pal[i]) : 0x00000000u;
				}
			}
		}
	}
	return w * hh;
}

void pspr_bleed_edges(uint32_t* px, int w, int h) {
	if (!px || w <= 0 || h <= 0 || w > PSPR_MAX_W || h > PSPR_MAX_H) return;
	// A SNAPSHOT of the source, so a texel that has just been bled cannot itself become a source
	// and grow the fringe outward by more than one texel per call.
	uint32_t src[PSPR_MAX_PIXELS];
	memcpy(src, px, (size_t)(w * h) * sizeof *src);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int i = y * w + x;
			if (src[i] & 0xFFu) continue;                 // already opaque (any nonzero alpha)
			uint32_t n = 0;
			if (x > 0     && (src[i - 1] & 0xFFu)) n = src[i - 1];
			else if (x + 1 < w && (src[i + 1] & 0xFFu)) n = src[i + 1];
			else if (y > 0     && (src[i - w] & 0xFFu)) n = src[i - w];
			else if (y + 1 < h && (src[i + w] & 0xFFu)) n = src[i + w];
			// RGB only. ALPHA IS NEVER MODIFIED — that is what makes this incapable of painting a
			// halo, and T6 pins it in both directions.
			if (n) px[i] = (n & 0xFFFFFF00u);
		}
	}
}

// ============================================================================================
// The change key (SPEC S3.5)
// ============================================================================================
int pspr_hdr_changed(const PsprHdr* a, const PsprHdr* b) {
	if (!a || !b) return 1;
	// Field by field, NOT memcmp: PsprHdr carries padding, and `subTbl` is DIAGNOSTICS ONLY — it
	// tracks elevation and long grass, neither of which changes a pixel, so including it would fire
	// a 90-read burst every time the peer stepped onto a different elevation plane.
	return a->ok           != b->ok
	    || a->spriteId     != b->spriteId
	    || a->graphicsId   != b->graphicsId
	    || a->tileNum      != b->tileNum
	    || a->pal          != b->pal
	    || a->hFlip        != b->hFlip
	    || a->vFlip        != b->vFlip
	    || a->w            != b->w
	    || a->h            != b->h
	    || a->animNum      != b->animNum
	    || a->animCmdIndex != b->animCmdIndex
	    || a->map1d        != b->map1d;
}

int pspr_pal_changed(const uint16_t* a, const uint16_t* b) {
	if (!a || !b) return 1;
	for (int i = 0; i < PSPR_PAL_COLORS; i++) if (a[i] != b[i]) return 1;
	return 0;
}

// ============================================================================================
// The gathers — the only functions in the module that touch emulated memory, all READ (SPEC I1)
// ============================================================================================
void pspr_gather_palette(const PsprBus* bus, uint32_t plttUnfaded, int pal, uint16_t* out) {
	if (!out) return;
	for (int i = 0; i < PSPR_PAL_COLORS; i++) out[i] = 0;
	if (!bus || !bus->rd16 || pal < 0 || pal > 15) return;
	// gPlttBufferUnfaded is a u16[512]: BG banks 0-15 then OBJ banks 0-15, so the OBJ bank starts
	// 512 BYTES in. The UNFADED buffer is the primary on purpose (SPEC S1.7): gPlttBufferFaded and
	// hardware PLTT carry the PEER's screen-wide fades, so reading them would turn the avatar on OUR
	// screen into a black silhouette while the peer walks through a door on theirs. The trade — a
	// genuine in-game palette effect on the peer's side (Flash in a cave) is not reflected — is the
	// correct call and is recorded so nobody "fixes" it later.
	uint32_t base = plttUnfaded ? (plttUnfaded + 512u + 32u * (uint32_t)pal)
	                            : (PSPR_PLTT_OBJ         + 32u * (uint32_t)pal);
	for (int i = 0; i < PSPR_PAL_COLORS; i++)
		out[i] = bus->rd16(bus->ctx, base + 2u * (uint32_t)i);
}

int pspr_gather_tiles(const PsprBus* bus, const PsprHdr* h, uint8_t* out, int outCap) {
	if (!bus || !bus->rd32 || !h || !out || !h->ok) return 0;
	int w = (int)h->w, hh = (int)h->h;
	if (w <= 0 || hh <= 0 || (w & 7) || (hh & 7)) return 0;
	int tw = w / 8, th = hh / 8;
	int need = tw * th * PSPR_TILE_BYTES;
	if (need > outCap || need > PSPR_MAX_TILE_BYTES) return 0;
	// Belt and braces on top of pspr_resolve clause 15: the bound is re-tested HERE, at the only
	// place that forms a VRAM address, so a future caller that hands over a hand-built header
	// cannot reach past OBJ VRAM either.
	if ((int)h->tileNum + pspr_max_tile_offset(w, hh, (int)h->map1d) > PSPR_MAX_TILENUM) return 0;

	uint8_t* dst = out;
	for (int ty = 0; ty < th; ty++) {
		for (int tx = 0; tx < tw; tx++) {
			uint32_t tile = (uint32_t)((int)h->tileNum + pspr_tile_offset(tx, ty, tw, (int)h->map1d));
			uint32_t addr = PSPR_OBJ_VRAM + PSPR_TILE_BYTES * tile;
			// 8 aligned rd32 per tile — the mapping mode is applied HERE, once, so everything
			// downstream (pspr_decode) sees plain raster tile order.
			for (int wi = 0; wi < 8; wi++) {
				uint32_t v = bus->rd32(bus->ctx, addr + 4u * (uint32_t)wi);
				dst[0] = (uint8_t)( v        & 0xFFu);
				dst[1] = (uint8_t)((v >>  8) & 0xFFu);
				dst[2] = (uint8_t)((v >> 16) & 0xFFu);
				dst[3] = (uint8_t)((v >> 24) & 0xFFu);
				dst += 4;
			}
		}
	}
	return need;
}

int pspr_capture(const PsprBus* bus, const PsprCaptureIn* in, PsprCapture* cap) {
	if (!cap) return 0;
	memset(&cap->hdr, 0, sizeof cap->hdr);
	cap->diagFlags = 0;
	if (!bus || !in || !bus->rd8 || !bus->rd16 || !bus->rd32) {
		cap->hdr.reason = PSPR_R_NOPROF;
		return 0;
	}

	PsprRaw raw;
	memset(&raw, 0, sizeof raw);
	raw.gateDraw = in->gateDraw;
	raw.remote   = in->remote;
	raw.surfOk   = in->surfOk;
	raw.texOk    = in->texOk;
	raw.ctxOk    = in->ctxOk;
	raw.haveProf = (in->sprites != 0u && in->mapObjects != 0u) ? 1u : 0u;

	// The caller-side clauses are answered WITHOUT a single bus read: a peer presence is not drawing,
	// or a game with no profile, must cost nothing at all (SPEC S3.8's "costs nothing when off").
	if (!raw.gateDraw || raw.remote || !raw.haveProf || !raw.surfOk || !raw.texOk || !raw.ctxOk)
		return pspr_resolve(&raw, &cap->hdr);

	// ---- read 12: DISPCNT (SPEC S1.6 / Open Q1) ----
	// mGBA routes GBA_REGION_IO -> GBAIORead, so this is a supported read — but no code in this
	// project has ever read an IO register, so it is unproven HERE. An answer of exactly 0x0000 or
	// 0xFFFF is the signature of an unwired IO path: we raise PSPR_D_NOIO and fall back to the
	// pret-cited defaults (mode 0, 1D), which is a documented property of these five games. The bit
	// reaches g_presDiag, so a run that ever takes that path SAYS so.
	{
		uint16_t dc = bus->rd16(bus->ctx, PSPR_REG_DISPCNT);
		if (dc == 0x0000u || dc == 0xFFFFu) {
			cap->diagFlags |= PSPR_D_NOIO;
			raw.dispcntOk = 0;
		} else {
			raw.dispcntOk = 1;
			raw.dispcnt   = dc;
		}
	}

	// ---- reads 1-3: the player's object event (slot 0) ----
	{
		uint32_t oe = in->mapObjects;   // gObjectEvents[0]
		raw.objActive  = (uint8_t)(bus->rd32(bus->ctx, oe + PSPR_OE_ACTIVE) & 1u);
		raw.spriteId   = bus->rd8(bus->ctx, oe + PSPR_OE_SPRITEID);
		raw.graphicsId = bus->rd8(bus->ctx, oe + PSPR_OE_GFXID);
	}

	// ---- reads 4-5: gPlayerAvatar (the cross-check; 0 in the profile => skipped, never guessed) ----
	if (in->playerAvatar) {
		raw.haveAvatar     = 1;
		raw.avatarFlags    = bus->rd8(bus->ctx, in->playerAvatar + PSPR_PA_FLAGS);
		raw.avatarSpriteId = bus->rd8(bus->ctx, in->playerAvatar + PSPR_PA_SPRITEID);
	}
	cap->avatarFlags = raw.avatarFlags;   // LOGGING ONLY, and kept even on a refusal: "the peer was
	                                      //   on a bike when the sprite was refused" is exactly the
	                                      //   sentence a hardware photo needs.

	// ---- reads 6-11: gSprites[spriteId] ----
	// Guarded by clause 7 BEFORE the address is formed: a spriteId of 0xFF would otherwise index
	// 0x44 * 255 past gSprites, which is the one place in this phase a wrong read could reach a
	// completely unrelated struct.
	if (raw.spriteId != PSPR_SPRITE_NONE && raw.spriteId < PSPR_MAX_SPRITES) {
		uint32_t sp = in->sprites + PSPR_SPRITE_STRIDE * (uint32_t)raw.spriteId;
		raw.attr0  = bus->rd16(bus->ctx, sp + PSPR_SPR_OAM + 0u);
		raw.attr1  = bus->rd16(bus->ctx, sp + PSPR_SPR_OAM + 2u);
		raw.attr2  = bus->rd16(bus->ctx, sp + PSPR_SPR_OAM + 4u);
		raw.anim   = bus->rd16(bus->ctx, sp + PSPR_SPR_ANIM);
		raw.sflags = bus->rd16(bus->ctx, sp + PSPR_SPR_FLAGS);
		raw.subTbl = (uint8_t)(bus->rd8(bus->ctx, sp + PSPR_SPR_SUBTBL) & 0x3Fu);
	}

	if (!pspr_resolve(&raw, &cap->hdr)) return 0;

	// ---- reads 13-28: the palette, every frame ----
	// 16 rd16 is the steady-state cost of noticing a palette animation (a flashing sprite, a
	// re-fade). If a hardware run ever shows the frame budget move, S3.8's first lever is polling
	// this every 8th frame instead of every frame.
	uint16_t pal[PSPR_PAL_COLORS];
	pspr_gather_palette(bus, in->plttUnfaded, (int)cap->hdr.pal, pal);

	// ---- the burst: pixels, ONLY when the key or the palette actually moved ----
	int keyMoved = (!cap->havePixels) || pspr_hdr_changed(&cap->hdr, &cap->key)
	                                  || pspr_pal_changed(pal, cap->pal);
	if (keyMoved) {
		int n = pspr_gather_tiles(bus, &cap->hdr, cap->tiles, (int)sizeof cap->tiles);
		if (n <= 0) {
			// Unreachable given clause 15, but a partial gather must never become a drawn cell.
			cap->havePixels = 0;
			cap->hdr.ok     = 0;
			cap->hdr.reason = PSPR_R_TILE;
			return 0;
		}
		memcpy(cap->pal, pal, sizeof cap->pal);
		cap->key        = cap->hdr;
		cap->nTileBytes = (uint16_t)n;
		cap->havePixels = 1;
		cap->seq++;
		cap->gathers++;
	}
	return 1;
}

// ============================================================================================
// The 3DS tiled-texture encode (SPEC S3.6, S3.7)
// ============================================================================================
int pspr_morton8(int x, int y) {
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
	     | ((x & 4) << 2) | ((y & 4) << 3);
}

long pspr_tex_offset(int x, int y, int dim) {
	if (dim <= 0 || x < 0 || y < 0 || x >= dim || y >= dim) return -1;
	return (long)(((y / 8) * (dim / 8) + (x / 8)) * 256 + pspr_morton8(x & 7, y & 7) * 4);
}

// GPU_RGBA8 texel order, bytes ascending: A, B, G, R — the same order presence_art.c's put_px uses
// and documents, and the order the phase-18 screenshot already validated through the GX transfer.
static void pspr_put_px(uint8_t* p, uint32_t rgba) {
	p[0] = (uint8_t)( rgba        & 0xFFu);   // A
	p[1] = (uint8_t)((rgba >>  8) & 0xFFu);   // B
	p[2] = (uint8_t)((rgba >> 16) & 0xFFu);   // G
	p[3] = (uint8_t)((rgba >> 24) & 0xFFu);   // R
}

void pspr_blit_tiled(uint8_t* tex, int dim, int x0, int y0,
                     const uint32_t* px, int w, int h) {
	if (!tex || !px || dim <= 0 || w <= 0 || h <= 0) return;
	if (x0 < 0 || y0 < 0 || x0 + w > dim || y0 + h > dim) return;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			long o = pspr_tex_offset(x0 + x, y0 + y, dim);
			if (o < 0) continue;
			pspr_put_px(tex + o, px[(size_t)y * (size_t)w + (size_t)x]);
		}
	}
}

void pspr_encode_tiled(uint8_t* dst, const uint8_t* linear, int dim) {
	if (!dst || !linear || dim <= 0) return;
	// A pure reshuffle of 4-byte units, exactly what a GX_TRANSFER_FMT_RGBA8 -> RGBA8 display
	// transfer with OUT_TILED(1) and FLIP_VERT(0) performs: no channel conversion, no row flip.
	for (int y = 0; y < dim; y++) {
		for (int x = 0; x < dim; x++) {
			long o = pspr_tex_offset(x, y, dim);
			if (o < 0) continue;
			const uint8_t* s = linear + ((size_t)y * (size_t)dim + (size_t)x) * 4u;
			dst[o + 0] = s[0];
			dst[o + 1] = s[1];
			dst[o + 2] = s[2];
			dst[o + 3] = s[3];
		}
	}
}

int pspr_verify_tiling(const uint8_t* linear, const uint8_t* oracleTiled, int dim, uint8_t* scratch) {
	if (!linear || !oracleTiled || !scratch || dim <= 0) return 0;
	pspr_encode_tiled(scratch, linear, dim);
	return memcmp(scratch, oracleTiled, (size_t)dim * (size_t)dim * 4u) == 0 ? 1 : 0;
}
