// peersprite.h — Phase 20: the peer's GENUINE overworld trainer frame, read live from the
// OTHER core's emulated OAM + OBJ VRAM + palette RAM.
// ============================================================================================
// Spec: docs/phase20-peersprite/SPEC.md — S1 (the chain + every address), S3 (conversion and
// upload), S4 (the fallback ladder + reason codes), S5 (how it slots into the phase-15 draw).
//
// WHAT THIS REPLACES, AND WHY. Phase 15 shipped a deliberately-fake magenta walker and wrote the
// reason down three times ("ripping the peer's VRAM frames is not worth it — swapped VRAM window +
// CPU frame DMA", docs/kb/coop-shared-overworld.md:131/152, PHASE.md invariant 7). That was a COST
// judgement made before anyone had seen the feature work; the user has now seen it and asked for
// the real thing, so SPEC.md S0.2 overturns it. The honest bill is in S3.8 and restated at
// pspr_capture below: 28 emulated-bus reads per game per frame in steady state, a ~90-read burst
// about seven times a second while the peer walks, ~2 KB of CPU pixel work per burst, ZERO new
// texture memory and ZERO GPU work. The original estimate was wrong by about two orders of
// magnitude, which is why the judgement it supported does not survive contact.
//
// THE INSIGHT THAT MAKES IT CHEAP (S0.3): both games run in OUR OWN PROCESS. The peer's trainer
// has already been decompressed, palette-loaded, animated and DMA'd into THEIR emulated OBJ VRAM by
// THEIR own game, this frame. We ship no art, decompress nothing, guess nothing, and reimplement no
// animation system — we read the 512 bytes their game is displaying right now. The walk cycle, the
// turn frames, the East mirror, the bike/surf forms and the correct gender arrive FOR FREE, because
// they are properties of the bytes and not of our code.
//
// PURE C (CLAUDE.md rule #4): <stdint.h> and <string.h> only. No <3ds.h>, no citro*, no libctru, no
// gbacore.h, no floats. Every byte-level decision — the OAM decode, the resolve ladder, the tile
// gather, the detile, the 4bpp -> RGBA8 conversion, the flip, the edge bleed, the change key and
// the 3DS tiled-texture encode — lives here so test/host/test_peersprite.c can prove it on the PC
// (SPEC I5). main.c owns the texture and the draw; presence_read.c owns nothing but the bus shim.
//
// READ-ONLY, ABSOLUTELY (SPEC I1). The PsprBus below has rd8/rd16/rd32 and no write of any kind.
// That is not an oversight and it is not to be "fixed": read-only is the property that makes
// presence unable to corrupt either save. A phase that adds a write here is a phase that failed.
#pragma once
#include <stdint.h>

// ---- reason codes (SPEC S4.2) ----------------------------------------------------------------
// 0 == the live sprite resolved and may be drawn. ANYTHING ELSE means "draw the phase-15
// placeholder for that peer, unchanged" — never a partial cell, never a stale cell whose key no
// longer matches, and never a skipped draw (a peer who vanishes is worse than a peer who is
// magenta, SPEC S4.1).
#define PSPR_R_OK        0
#define PSPR_R_REMOTE    1   /* M4 peer: their VRAM is on another console (SPEC S2.5)          */
#define PSPR_R_NOPROF    2   /* GameProfile.sprites == 0 (or mapObjects == 0)                  */
#define PSPR_R_NOSURF    3   /* the presence sheet was never built                             */
#define PSPR_R_TILEFMT   4   /* the S3.7 init proof failed -> the live path is off for the run */
#define PSPR_R_CTX       5   /* the peer is not in a readable overworld state                  */
#define PSPR_R_NOOBJ     6   /* gObjectEvents[0].active == 0                                   */
#define PSPR_R_BADID     7   /* spriteId >= MAX_SPRITES or == SPRITE_NONE                      */
#define PSPR_R_MISMATCH  8   /* gPlayerAvatar.spriteId disagrees with the object event's       */
#define PSPR_R_NOTINUSE  9   /* gSprites[id].inUse == 0                                        */
#define PSPR_R_HIDDEN   10   /* gSprites[id].invisible                                         */
#define PSPR_R_AFFINE   11   /* affineMode != 0 -> attr1[9:13] is a matrix index, not flips    */
#define PSPR_R_BPP      12   /* 256-colour OBJ                                                 */
#define PSPR_R_SIZE     13   /* shape 3 (prohibited), or larger than 32x32                     */
#define PSPR_R_MODE     14   /* DISPCNT bitmap mode, or OBJ drawing disabled                   */
#define PSPR_R_TILE     15   /* the tile range would leave OBJ VRAM                            */
#define PSPR_R_PENDING  16   /* resolved, but no decode has landed in the sheet yet            */
#define PSPR_R_COUNT    17

// Diagnostic bits (never gate anything; they reach g_presDiag.sprFlags so a run SAYS what it did).
#define PSPR_D_NOIO     0x01u   /* DISPCNT read back 0x0000/0xFFFF -> the S1.6 defaults assumed */

// ---- geometry limits -------------------------------------------------------------------------
#define PSPR_MAX_W          32
#define PSPR_MAX_H          32
#define PSPR_TILE_BYTES     32                 /* one 4bpp 8x8 tile */
#define PSPR_MAX_TILES      ((PSPR_MAX_W / 8) * (PSPR_MAX_H / 8))          /* 16  */
#define PSPR_MAX_TILE_BYTES (PSPR_MAX_TILES * PSPR_TILE_BYTES)             /* 512 */
#define PSPR_MAX_PIXELS     (PSPR_MAX_W * PSPR_MAX_H)                      /* 1024 */
#define PSPR_PAL_COLORS     16

// ---- the live-cell block inside the EXISTING 128x128 presence sheet (SPEC S3.6) ---------------
// Rows 0..95 hold the two placeholder variants (PRES_ART_ROWS * PRES_CELL_H == 96). Rows 96..127
// are free across the full width: exactly four 32x32 live cells at x = 0/32/64/96, which is the
// 3-4-player ceiling with no further layout work. NO NEW TEXTURE is allocated by this phase.
#define PSPR_LIVE_DIM       32
#define PSPR_LIVE_Y         96
#define PSPR_LIVE_X(slot)   ((slot) * PSPR_LIVE_DIM)
#define PSPR_LIVE_SLOTS     4

// ---- fixed GBA addresses (SPEC S1.1, S1.6, S1.7) ---------------------------------------------
#define PSPR_OBJ_VRAM       0x06010000u   /* OBJ character base in BG modes 0-2                  */
#define PSPR_OBJ_VRAM_END   0x06018000u   /* one past the last OBJ tile byte (tile 1023 ends here)*/
#define PSPR_PLTT_OBJ       0x05000200u   /* hardware OBJ palette RAM (the plttUnfaded fallback)  */
#define PSPR_REG_DISPCNT    0x04000000u
#define PSPR_MAX_TILENUM    1023          /* attr2 tileNum is 10 bits                            */

// ---- struct Sprite / struct ObjectEvent geometry (SPEC S1.2, re-verified this session) -------
// struct Sprite: pret/pokeemerald include/sprite.h:194-242 — oam @+0x00 (8 B), animNum @+0x2A,
// animCmdIndex @+0x2B, the inUse/coordOffsetEnabled/invisible bitfield @+0x3E, subspriteTableNum
// @+0x42. Size 0x44, and `gSprites`' symbol SIZE is 0x1144 == 65 * 0x44 (MAX_SPRITES + 1) in ALL
// NINE symbol maps on this machine — which is itself strong evidence the struct is unchanged
// across RS / FRLG / Emerald.
#define PSPR_SPRITE_STRIDE  0x44u
#define PSPR_SPR_OAM        0x00u
#define PSPR_SPR_ANIM       0x2Au   /* u16: low byte animNum, high byte animCmdIndex */
#define PSPR_SPR_FLAGS      0x3Eu   /* bit0 inUse, bit1 coordOffsetEnabled, bit2 invisible */
#define PSPR_SPR_SUBTBL     0x42u   /* subspriteTableNum:6 | subspriteMode:2 — DIAGNOSTICS ONLY */
#define PSPR_MAX_SPRITES    64      /* include/sprite.h:5 */
#define PSPR_SPRITE_NONE    0xFFu

// struct ObjectEvent: pret/pokeemerald include/global.fieldmap.h:194-254 and pokefirered's copy,
// both re-read this session and BYTE-IDENTICAL for every field below; pokeruby's is identical too
// (verified in phase 18, BUILDLOG S3). Stride 0x24 — already used by main.c's depth pass.
#define PSPR_OE_STRIDE      0x24u
#define PSPR_OE_ACTIVE      0x00u   /* u32 bitfield, bit 0 */
#define PSPR_OE_SPRITEID    0x04u
#define PSPR_OE_GFXID       0x05u

// struct PlayerAvatar: flags @+0x00, spriteId @+0x04 in BOTH engines (global.fieldmap.h:342-352
// EM, :365-375 FR — FR's struct diverges only AFTER +0x08, which we never touch).
#define PSPR_PA_FLAGS       0x00u
#define PSPR_PA_SPRITEID    0x04u

// ---- the change key (SPEC S3.5) --------------------------------------------------------------
// Every field comes from a HEADER read; no pixels are involved in deciding whether to read pixels.
//
// WHY animNum + animCmdIndex AND NOT tileNum ALONE. Overworld object events take the non-sheet
// branch of src/sprite.c:936-939 (`tileTag == TAG_NONE`), so `tileNum` is a FIXED allocation for
// the life of the sprite and the animation changes the VRAM CONTENTS, not the index. Keying on
// tileNum alone would freeze a walking peer on its first frame forever — test T8 is that exact
// regression. (animNum, animCmdIndex) is 1:1 with `imageValue` within a graphicsId, and graphicsId
// is in the key, so the pair is a complete frame identity. tileNum stays in the key anyway because
// it IS the discriminator for the `usingSheet` branch and costs nothing.
typedef struct {
	uint8_t  ok;            // 1 iff reason == PSPR_R_OK
	uint8_t  reason;        // PSPR_R_*
	uint8_t  spriteId;
	uint8_t  graphicsId;    // the FORM: normal / bike / surf / ...
	uint16_t tileNum;       // attr2 bits 0-9
	uint8_t  pal;           // attr2 bits 12-15 (the OBJ palette BANK)
	uint8_t  hFlip;         // attr1 bit 12 — THE MOONWALK BIT (SPEC S1.5)
	uint8_t  vFlip;         // attr1 bit 13
	uint8_t  w, h;          // 8/16/32 each, from shape+size
	uint8_t  animNum;
	uint8_t  animCmdIndex;
	uint8_t  map1d;         // DISPCNT bit 6: OBJ character mapping (1 = 1D, 0 = 2D)
	uint8_t  subTbl;        // subspriteTableNum — DIAGNOSTICS ONLY, deliberately NOT in the key
} PsprHdr;

// ---- the raw header reads, as one flat struct (so the resolve ladder is pure) -----------------
typedef struct {
	uint8_t  gateDraw;       // clause 1: presence_solve already decided to DRAW this peer
	uint8_t  remote;         // clause 2: 1 = an M4 UDS record, not a same-console core
	uint8_t  haveProf;       // clause 3: profile has sprites (and mapObjects) != 0
	uint8_t  surfOk;         // clause 4a: the 128x128 presence sheet was actually built
	uint8_t  texOk;          // clause 4b: the S3.7 tiling proof passed at init
	uint8_t  ctxOk;          // clause 5: the peer's GameState is valid/OVERWORLD/sb1Valid
	uint8_t  objActive;      // clause 6
	uint8_t  spriteId;       // clause 7
	uint8_t  graphicsId;
	uint8_t  haveAvatar;     // clause 8: profile.playerAvatar != 0 (0 => the cross-check is skipped)
	uint8_t  avatarSpriteId;
	uint8_t  avatarFlags;    // gPlayerAvatar.flags — PLAYER_AVATAR_FLAG_* (ON_FOOT / MACH_BIKE /
	                         //   ACRO_BIKE / SURFING / UNDERWATER / CONTROLLABLE / FORCED_MOVE /
	                         //   DASH). DIAGNOSTICS ONLY: it names the FORM independently of
	                         //   graphicsId, so a hardware photo can say "the peer was on a bike"
	                         //   even when the sprite is refused. Deliberately NOT in the change
	                         //   key (graphicsId already carries every form change that moves a
	                         //   pixel) and deliberately NOT written into PeerPresence.avatarFlags,
	                         //   which stays the RESERVED 0 phase 15 shipped — growing the 48-byte
	                         //   wire record is an M4 decision, not a rendering one.
	uint16_t attr0, attr1, attr2;
	uint16_t anim;           // read16(sprite + 0x2A)
	uint16_t sflags;         // read16(sprite + 0x3E)
	uint8_t  subTbl;
	uint8_t  dispcntOk;      // 0 => the IO read looked unwired; the S1.6 defaults are assumed
	uint16_t dispcnt;
} PsprRaw;

// ---- the capture: one per GAME, owned by main.c ----------------------------------------------
// POD, memset-able, no pointers. `hdr` is THIS frame's verdict (what the draw branches on); `key`
// is the header the bytes in `tiles`/`pal` actually belong to. Splitting them is what lets a
// transient refusal (a menu, a battle, a doorway) fall back to the placeholder for a few frames and
// then resume WITHOUT re-reading 512 bytes of unchanged VRAM.
typedef struct {
	PsprHdr  hdr;
	PsprHdr  key;
	uint16_t pal  [PSPR_PAL_COLORS];
	uint8_t  tiles[PSPR_MAX_TILE_BYTES];   // RASTER tile order: tile k == (k % (w/8), k / (w/8))
	uint16_t nTileBytes;
	uint8_t  havePixels;    // tiles/pal hold a complete frame matching `key`
	uint8_t  diagFlags;     // PSPR_D_*
	uint8_t  avatarFlags;   // gPlayerAvatar.flags, LOGGING ONLY (see PsprRaw.avatarFlags)
	uint32_t seq;           // bumps on every fresh gather -> main.c decodes when it moves
	uint32_t gathers;       // lifetime gather count (g_presDiag.sprUploads; should track WALKING)
} PsprCapture;

// ---- the bus: the ONLY way this module touches emulated memory -------------------------------
// Three reads, no writes, ever (SPEC I1). presence_read.c supplies gbacore_read8/16/32; the host
// suite supplies a fake VRAM. Nothing else in the module dereferences anything.
typedef struct {
	uint8_t  (*rd8 )(void* ctx, uint32_t addr);
	uint16_t (*rd16)(void* ctx, uint32_t addr);
	uint32_t (*rd32)(void* ctx, uint32_t addr);
	void*    ctx;
} PsprBus;

// The profile columns + the caller-side clauses, in one input struct.
typedef struct {
	uint32_t mapObjects;     // gObjectEvents (the profile column presence already uses)
	uint32_t sprites;        // gSprites          — NEW profile column
	uint32_t plttUnfaded;    // gPlttBufferUnfaded — NEW profile column (0 => PLTT fallback)
	uint32_t playerAvatar;   // gPlayerAvatar     — NEW profile column (0 => cross-check skipped)
	uint8_t  gateDraw;       // clause 1
	uint8_t  remote;         // clause 2
	uint8_t  surfOk;         // clause 4a
	uint8_t  texOk;          // clause 4b
	uint8_t  ctxOk;          // clause 5
} PsprCaptureIn;

// ============================================================================================
// API
// ============================================================================================

// OBJ shape+size -> pixel extent. The standard GBA OBJ table:
//   shape 0 SQUARE      8x8   16x16  32x32  64x64
//   shape 1 HORIZONTAL 16x8   32x8   32x16  64x32
//   shape 2 VERTICAL    8x16   8x32  16x32  32x64
//   shape 3 is PROHIBITED -> (0, 0).
// A standard Gen-3 overworld trainer is 16x32 == shape 2, size 2 (pret's SPRITE_SHAPE(16x32) /
// SPRITE_SIZE(16x32) in src/data/object_events/object_event_subsprites.h, re-read this session).
// *** DEVIATION FROM SPEC S1.6, which says "shape = 2 (TALL), size = 1". That is a spec typo:
// shape 2 size 1 is 8x32. The table above is the hardware's, and it is what ships. ***
void pspr_oam_size(int shape, int size, int* w, int* h);

// The resolve ladder, SPEC S4.1 clauses 1..15 in order, each with its own reason code. Returns 1
// and fills a complete `out` when everything passes; returns 0 and fills out->reason (with the
// remaining fields ZEROED, never half-parsed) otherwise. Pure: no memory is touched.
int pspr_resolve(const PsprRaw* in, PsprHdr* out);

// Human-readable reason, for the HUD and the gs log. Never NULL, unique for 0..PSPR_R_COUNT-1.
const char* pspr_reason_name(int reason);

// ---- tile addressing (SPEC S1.6) -------------------------------------------------------------
// The OBJ tile INDEX offset of tile (tx, ty) of a w x h sprite, relative to attr2's tileNum.
//   1D (DISPCNT bit 6 set):   ty * (w/8) + tx        — the sprite's tiles are one linear run
//   2D (bit 6 clear):         ty * 32     + tx       — OBJ VRAM is a 32-tile-wide grid
// Getting this backwards produces "tile soup" that looks like a corrupted sprite; T4 pins both.
int pspr_tile_offset(int tx, int ty, int tilesW, int map1d);

// The LARGEST tile-index offset a w x h sprite reaches under `map1d` — the bounds-check input.
int pspr_max_tile_offset(int w, int h, int map1d);

// ---- conversion (SPEC S3.1, S3.2, S3.3, S3.4) ------------------------------------------------
// BGR555 -> 0xRRGGBBAA (presence_art.h's convention, deliberately NOT C2D_Color32's). The 5->8
// expansion replicates the high bits into the low ones, so 31 maps to 255 exactly.
uint32_t pspr_bgr555_to_rgba8(uint16_t c);

// One 4bpp tile byte -> the palette index at (x, y) within that tile. The LOW NIBBLE IS THE LEFT
// PIXEL — the classic way to ship a mirrored sprite without noticing (T2).
int pspr_tile_index(const uint8_t* tile32, int x, int y);

// Detile + palette + flip, in ONE pass, into a w*h buffer of 0xRRGGBBAA words.
//   `tiles` is RASTER tile order (what pspr_gather_tiles produces): tile k == (k % (w/8), k / (w/8)).
//   Index 0 is TRANSPARENT and emits 0x00000000 — never colour 0's RGB at A = 255.
//   The flip is applied to the DESTINATION coordinate, which is what makes this a single pass and
//   what keeps the DRAW path flip-free (SPEC S1.5: the flip is BAKED, so presence_art_clip's
//   mirrored-source-offset subtlety is untouched by this phase).
// Returns the number of pixels written, or 0 if anything is out of range.
int pspr_decode(const uint8_t* tiles, int nTileBytes, const uint16_t* pal,
                const PsprHdr* h, uint32_t* out, int outCap);

// Copy the RGB of an opaque 4-neighbour into every fully transparent texel that touches the
// silhouette, LEAVING ALPHA AT ZERO. Under tilt the presence draw samples GPU_LINEAR, and a tap
// that straddles the silhouette would otherwise blend opaque pixels with transparent BLACK ones and
// draw a dark fringe. Alpha is never modified — T6 pins that, so this can never paint a halo.
void pspr_bleed_edges(uint32_t* px, int w, int h);

// ---- the change key (SPEC S3.5) --------------------------------------------------------------
// Field by field, NOT memcmp: PsprHdr has padding, and `subTbl` must NOT trigger a re-read (it is
// diagnostics-only and tracks elevation / long grass, neither of which changes a pixel).
int pspr_hdr_changed(const PsprHdr* a, const PsprHdr* b);
int pspr_pal_changed(const uint16_t* a, const uint16_t* b);

// ---- the gathers: the only functions that read emulated memory -------------------------------
// 16 rd16 of the OBJ palette bank. Primary source gPlttBufferUnfaded (S1.7: the FADED buffer and
// hardware PLTT carry the PEER's screen-wide fades, so a peer walking through a door would turn
// into a black silhouette on OUR screen for a transition happening on someone else's); falls back
// to hardware PLTT when the profile column is 0.
void pspr_gather_palette(const PsprBus* bus, uint32_t plttUnfaded, int pal, uint16_t* out);

// (w/8)*(h/8) tiles, 8 rd32 each, written in RASTER tile order. Returns the byte count, or 0 if
// the header is not OK or the range would leave OBJ VRAM (which pspr_resolve already refused).
int pspr_gather_tiles(const PsprBus* bus, const PsprHdr* h, uint8_t* out, int outCap);

// THE WHOLE PER-FRAME READ, in one call. Runs the resolve ladder off fresh header reads and
// re-gathers pixels ONLY when the key or the palette moved.
//
// COST (SPEC S1.8 / S3.8), stated where the reads are:
//   steady state  12 header reads + 16 palette reads = 28 emulated-bus reads per game per frame
//   on change     + 64 rd32 (16x32) or + 128 (32x32), about 7x per second WHILE THE PEER WALKS
//                   and ZERO times per second while they stand
// For scale, the same parked window already performs ~150 reads in build_depth_grid.
//
// Returns 1 when cap->hdr.ok (a live cell is available or pending), 0 otherwise. NEVER writes
// emulated memory, and every address it forms is either a compile-time profile constant, a
// constant offset from one, a 0x44 * spriteId with spriteId < 64 CHECKED, a tile address with
// tileNum + maxOffset <= 1023 CHECKED, or a palette address with pal <= 15 by construction
// (SPEC S4.4: there is no unchecked pointer in this phase).
int pspr_capture(const PsprBus* bus, const PsprCaptureIn* in, PsprCapture* cap);

// ---- the 3DS tiled-texture encode (SPEC S3.6, S3.7) ------------------------------------------
// A 3DS texture is 8x8 blocks in row-major order, each block Morton (z-order) internally. This is
// the same morton8 test/host/test_typography.c:150-164 already decodes A4 font sheets with.
int  pspr_morton8(int x, int y);

// Byte offset of texel (x, y) in a dim x dim GPU_RGBA8 tiled texture. Bytes at that offset are
// A, B, G, R ascending — the same order presence_art.c's put_px uses and documents.
long pspr_tex_offset(int x, int y, int dim);

// Write a decoded w x h cell into a tiled RGBA8 texture at texel (x0, y0). A PLAIN CPU STORE: no
// GPU command is issued, which is why this is legal INSIDE C3D_FrameBegin/End and must be placed
// there (S3.6 — doing it in the parked window, before FrameBegin, would race the still-in-flight
// previous frame).
void pspr_blit_tiled(uint8_t* tex, int dim, int x0, int y0,
                     const uint32_t* px, int w, int h);

// Re-encode a whole LINEAR dim x dim RGBA8 buffer into tiled order. Used once, at init, to build
// the S3.7 probe.
void pspr_encode_tiled(uint8_t* dst, const uint8_t* linear, int dim);

// THE TILING PROOF (SPEC S3.7). The one genuine risk in the upload is that this CPU encoder's
// layout (Morton order, block-row order, byte order, and whether GX_TRANSFER_FLIP_VERT(0) inverts
// rows) does not match what C3D_SyncDisplayTransfer actually produced for the placeholder — and
// getting it wrong yields an upside-down or channel-swapped or scrambled sprite whose failure is
// only visible in a photo. So we prove it at init, on the real device, with a memcmp: re-encode the
// linear staging buffer the placeholder was built in and compare against the texture the GX
// transfer produced from that same buffer. Match => the encoder is byte-identical to the path the
// phase-18 screenshot already validated visually. Mismatch => the live path is OFF for the whole
// run, the diag says PSPR_R_TILEFMT, and the placeholder (which came from the trusted transfer)
// still draws. `scratch` must hold dim*dim*4 bytes. Returns 1 on match.
int pspr_verify_tiling(const uint8_t* linear, const uint8_t* oracleTiled, int dim, uint8_t* scratch);
