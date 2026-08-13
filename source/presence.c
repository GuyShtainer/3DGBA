// presence.c — Phase 15 co-op presence: the DATA half. Pure C, host-testable.
// ============================================================================================
// Spec: docs/phase15-presence/SPEC-data.md (D1 profile groundwork, D2 the snapshot, D3 the record
// + source abstraction, D4 the gate, D5 the screen math, D6 this module). Contract, invariants,
// constants and every citation: see presence.h.
//
// NOTHING here knows about the GPU, the screen rect, the scale mode, libctru, or a GbaCore.
// Callers compose:
//     presence_fill (main.c: gbacore_read* + GameProfile)  ->  presence_publish
//     presence_solve (frame space)  ->  tilt_project (optional)  ->  calc_xform  ->  citro2d
//
// SLICE M0 SHIPS THIS MODULE INERT: nothing calls it yet (no main.c wiring), which is what makes
// M0 provably neutral — the .3dsx it produces is byte-for-byte the same frame as before, and the
// only shipped behaviour change in the whole slice is three new (unread) GameProfile columns.
#include <string.h>

#include "presence.h"

// ---- tiny pure helpers (no <math.h>, no <stdlib.h> — see presence.h's include rule) ---------

static int   iabs_ (int   v) { return v < 0    ? -v    : v; }
static float fabsf_(float v) { return v < 0.0f ? -v    : v; }

// D1.3: the camera phase is guarded to -15..15 EXACTLY as main.c:854-855 guards it — out of range
// is treated as 0, never clamped to the edge. A clamp would invent a plausible-looking sub-tile
// offset out of garbage; 0 degrades to whole tiles, which is <=15 px of error that self-corrects
// at the next step boundary and can NEVER put the avatar on the wrong tile (D1.9).
static int cam_phase(int v) { return (v < -15 || v > 15) ? 0 : v; }

// ---- the engine's own map->screen sub-tile term (D5.3) --------------------------------------
// pokeemerald src/event_object_movement.c:4801-4819 (SetSpritePosToMapCoords), verbatim:
//     s16 dx = -gTotalCameraPixelOffsetX - gFieldCamera.x;
//     if (gFieldCamera.x > 0) dx += 16;
//     if (gFieldCamera.x < 0) dx -= 16;
//     *destX = ((mapX - gSaveBlock1Ptr->pos.x) << 4) + dx;
// gTotalCameraPixelOffsetX cancels at draw time against gSpriteCoordOffsetX (field_camera.c:461),
// leaving SUB(c) = -c + 16*sgn(c). It is direction-, speed- and role-agnostic: correct at 1 px/f
// (walk, 16 f/tile), 2 px/f (run, 8 f) and 4 px/f (bike, 4 f), because the engine derives the
// phase from the same movementSpeed in all three cases (D5.3.1). Nothing in presence needs to
// know which speed is in force, and there is NOTHING TO INTERPOLATE — coop-shared-overworld.md
// §4's previousCoords->currentCoords plan would be an approximation of a value we can read exactly.
int presence_sub(int c) {
	if (c > 0) return -c + 16;
	if (c < 0) return -c - 16;
	return 0;
}

// ---- D2.4: facing folding + clamp -----------------------------------------------------------
// gamestate.c:131 carries a "verify-on-hw offset" marking. D1.8.2 shows the offset is source-
// correct in BOTH engines (struct ObjectEvent is 0x24 bytes with facingDirection in the low nibble
// of +0x18, global.fieldmap.h) and the FR/LG base 0x02036E38 is confirmed by pokefirered.sym:205
// AND pokeleafgreen.sym:205 — but this is written as if it were still wrong, so the consequences
// stay bounded to "the avatar faces a constant wrong way while walking". It can never index art
// out of range, never move the avatar (position comes from SaveBlock1.pos, not from facing) and
// never crash. The diagonals (DIR_SOUTHWEST..DIR_NORTHEAST = 5..8) are folded to their nearest
// cardinal BEFORE the clamp: Gen-3 object events never take them in normal field movement, and
// silently collapsing them to SOUTH would hide a real bug (the M1 HUD prints the RAW nibble
// alongside — one hardware run where it reads 1/2/3/4 as the player walks D/U/L/R promotes the
// marking to VERIFIED for both games, D2.4.1).
int presence_dir(int f) {
	f &= 0x0F;                                     // low nibble only (gamestate.c:131 already masks)
	if (f == 5 || f == 6) f = PRES_DIR_SOUTH;      // SOUTHWEST / SOUTHEAST
	if (f == 7 || f == 8) f = PRES_DIR_NORTH;      // NORTHWEST / NORTHEAST
	return (f >= PRES_DIR_SOUTH && f <= PRES_DIR_EAST) ? f : PRES_DIR_SOUTH;
}

// ---- D6.3.1: the Gen-3 charmap decoder ------------------------------------------------------
// pret charmap.txt lines 1, 68-95, 120-156 (VERIFIED-SRC). Everything unmapped becomes '?': the
// gendered 0xB5/0xB6 and the accented 0x01..0x14 block included — a '?' in a name is cosmetic, a
// buffer overrun is not. Output is ALWAYS NUL-terminated within 9 bytes, even when all 8 source
// bytes are non-terminating. Returns the number of characters written (excluding the NUL).
int presence_name_ascii(const uint8_t name[8], char out[9]) {
	int n = 0;
	if (!name || !out) { if (out) out[0] = '\0'; return 0; }
	for (int i = 0; i < 8; i++) {
		uint8_t b = name[i];
		char    c;
		if (b == 0xFF) break;                                        // terminator (pret's '$')
		if      (b == 0x00)              c = ' ';
		else if (b >= 0xA1 && b <= 0xAA) c = (char)('0' + (int)(b - 0xA1));
		else if (b == 0xAB)              c = '!';
		else if (b == 0xAC)              c = '?';
		else if (b == 0xAD)              c = '.';
		else if (b == 0xAE)              c = '-';
		else if (b == 0xB8)              c = ',';
		else if (b == 0xBA)              c = '/';
		else if (b >= 0xBB && b <= 0xD4) c = (char)('A' + (int)(b - 0xBB));
		else if (b >= 0xD5 && b <= 0xEE) c = (char)('a' + (int)(b - 0xD5));
		else                             c = '?';
		out[n++] = c;
	}
	out[n] = '\0';
	return n;
}

static const char* const PRES_OFF_NAMES[PRES_OFF__COUNT] = {
	"ok", "off", "menu", "link", "noprof", "universe", "self", "field", "obj", "map", "stale", "cull",
	"selfobj"
};
const char* presence_off_reason(int reason) {
	return (reason >= 0 && reason < PRES_OFF__COUNT) ? PRES_OFF_NAMES[reason] : "?";
}

// ---- producer-side helpers ------------------------------------------------------------------

void presence_rec_init(PeerPresence* r, int seat, int gameId) {
	if (!r) return;
	memset(r, 0, sizeof *r);
	r->ver    = PRES_REC_VER;
	r->seat   = (uint8_t)seat;
	r->gameId = (uint8_t)gameId;
	r->objX   = -1;   // "gate unavailable" until a live gObjectEvents[0] says otherwise (D4.5.1)
	r->objY   = -1;
	r->mapGroup = -1;
	r->mapNum   = -1;
	r->facing = PRES_DIR_SOUTH;
	memset(r->name, 0xFF, sizeof r->name);   // an empty (immediately terminated) name
}

// D4.3: derived ONCE at core-attach time from the 4-char game code (gbacore_game_code), and stored
// in the record, so the gate compares map UNIVERSES rather than raw codes. FR and LG share one id.
int presence_game_id(const char* code4) {
	if (!code4) return PRES_GAME_NONE;
	if (!strncmp(code4, "BPEE", 4))                                   return PRES_GAME_HOENN;
	if (!strncmp(code4, "BPRE", 4) || !strncmp(code4, "BPGE", 4))     return PRES_GAME_KANTO;
	// Ruby + Sapphire are one build and one map table, but NOT Emerald's — see presence.h's
	// PRES_GAME_HOENN_RS note for the map_groups.json diff that settles it (SPEC-coop P3.1).
	if (!strncmp(code4, "AXVE", 4) || !strncmp(code4, "AXPE", 4))     return PRES_GAME_HOENN_RS;
	return PRES_GAME_NONE;   // no profile => presence silently OFF for the pair (invariant 5)
}

// D4.5 — the object-agreement discriminator. tilt.c:85-106 discloses a known residual we inherit:
// ctx == GCTX_OVERWORLD is game_read's FALL-THROUGH, so an undetected full-screen screen (Pokedex,
// town map, trainer card, summary, naming keyboard) reads as "overworld". For tilt that tilts a
// menu; for presence it would paste an avatar over a full-screen menu — more visible, and worth
// one extra guard we can afford. In the field gObjectEvents[0].currentCoords tracks
// SaveBlock1.pos + MAP_OFFSET; when the field is torn down the slot goes inactive and game_read
// reports objX = -1 (gamestate.c:91,128).
//   returns  1 = agrees        0 = DISAGREES (close the gate)      -1 = unavailable (SKIP the gate)
int presence_obj_agree(const PeerPresence* r) {
	if (!r) return -1;
	if (r->objX < 0 || r->objY < 0) return -1;   // D4.5.1: never report unmapped/inactive as a PASS
	int dx = (int)r->objX - ((int)r->px + PRES_MAP_OFFSET);
	int dy = (int)r->objY - ((int)r->py + PRES_MAP_OFFSET);
	return (iabs_(dx) + iabs_(dy) <= PRES_OBJ_TOL) ? 1 : 0;
}

// ---- slice M1: the pure producer core (D2.2/D3.2) -------------------------------------------
// Everything the producer DECIDES lives here; presence_read.c only fetches bytes. Nothing in this
// function can fail: a missing profile field, an unloaded save, a torn-down field or an unmapped
// gObjectEvents all land as a DEGRADED-but-sane record (the D1.9 ladder), never as garbage and
// never as a plausible-looking wrong position. presence_rec_init installs the safe defaults
// first — ver/seat/gameId, objX/objY = -1 (gate unavailable), mapGroup/mapNum = -1, facing SOUTH
// and an immediately-terminated name — so every early-out below is already correct.
void presence_fill_core(PeerPresence* r, int seat, int gameId, const PresenceSrc* s) {
	presence_rec_init(r, seat, gameId);
	if (!r || !s) return;
	r->round = s->round;
	r->ctx   = (uint8_t)((s->ctx >= 0 && s->ctx <= 255) ? s->ctx : 0);   // 0 == GCTX_NONE

	// Position. game_read reports px = -1 when the SaveBlock1 pointer is not ready, so the guard is
	// the same one the rest of the codebase uses (CtlIn.fieldValid, main.c:2420).
	if (s->sb1Valid && s->px >= 0 && s->py >= 0) {
		r->flags   |= PRES_F_SB1VALID;
		r->px       = (int16_t)s->px;
		r->py       = (int16_t)s->py;
		r->mapGroup = (int8_t)s->mapGroup;
		r->mapNum   = (int8_t)s->mapNum;
	}
	// D4.6 — the ONE field predicate, the same call tilt's G5-G8 make. ctx / sb1Valid / px cross the
	// wire raw, so the consumer re-runs the same function on the far side (rec_field_ok) rather than
	// trusting one flag.
	// FIX PASS (review finding 6): the textDlg term is passed as 0 here and recorded SEPARATELY as
	// PRES_F_TEXT, because the two sides of the gate want opposite answers — see presence.h's
	// PRES_F_TEXT comment for the full argument. In one line: the peer's dialog box is on the PEER's
	// screen (their position stays valid, so their avatar must stay visible), while the HOST's
	// dialog box is drawn in the very frame this avatar composites over.
	if (field_state_ok(s->ok, (int)r->ctx, (r->flags & PRES_F_SB1VALID) ? 1 : 0, (int)r->px, 0))
		r->flags |= PRES_F_FIELD;
	if (s->textDlg) r->flags |= PRES_F_TEXT;

	// D4.5/D4.5.1 — the object-agreement discriminator. A negative objX/objY means the field is torn
	// down (game_read's sentinel, gamestate.c:91,128) OR mapObjects is unmapped; either way the -1
	// sentinel rec_init installed must SURVIVE, so P-G7 reports "unavailable" instead of "pass".
	if (s->objX >= 0 && s->objY >= 0) { r->objX = (int16_t)s->objX; r->objY = (int16_t)s->objY; }
	if (presence_obj_agree(r) == 1) r->flags |= PRES_F_OBJOK;

	// D2.4 — fold + clamp here, at the producer, so a wrong facing offset can only ever cost a
	// wrong sprite DIRECTION. The raw nibble is kept out of the wire record on purpose (the field is
	// specified as "DIR_* 0..4 after folding"); main.c logs the raw value beside it for D2.4.1.
	r->facing = (uint8_t)presence_dir(s->facing < 0 ? PRES_DIR_SOUTH : s->facing);

	// D1.3 — the sub-tile phase, guarded to -15..15 exactly as main.c:854-855 guards it. Out of
	// range degrades to whole tiles (<=15 px, self-correcting at the next step boundary); it can
	// never move the avatar to the wrong tile.
	if (s->camOk) {
		r->flags |= PRES_F_CAM;
		r->subX   = (int8_t)cam_phase(s->camX);
		r->subY   = (int8_t)cam_phase(s->camY);
	}
	if (s->hbOk) { r->flags |= PRES_F_HB; r->hb = s->hb; }

	// D3.2.1 — the name crosses RAW (game charmap); decoding is presentation and happens at draw
	// time. gender folds to MALE for anything that is not exactly 1 (A0.4's "0xFF -> male" rule
	// generalised: the byte is a u8 from EWRAM and only 0/1 are defined).
	if (s->identOk) {
		r->flags |= PRES_F_IDENT;
		if (s->name) memcpy(r->name, s->name, sizeof r->name);
		r->gender = (uint8_t)(s->gender == 1 ? 1 : 0);
		r->tid    = (uint16_t)s->tid;
	}
}

// D2.3 — the identity refresh cadence. Pure policy, so the reader stays a dumb fetcher and the
// rule is enumerable by a host test. A map change always re-latches (D4.7.1 requires it: a warp is
// the one same-console event that can mean "a different save is loaded now").
int presence_ident_due(int have, int mapChanged, uint32_t framesSinceTry) {
	if (mapChanged) return 1;
	if (!have)      return framesSinceTry >= PRES_IDENT_RETRY_FRAMES;
	return framesSinceTry >= PRES_IDENT_REFRESH_FRAMES;
}

// D4.2 — the same-map predicate. mapGroup/mapNum are s8 in pret (struct WarpData) and u8 in
// game_read (gamestate.c:100-101, widened to int); both sides come through the same code path and
// are ONLY ever compared, never used in arithmetic, so the signedness is immaterial — but the
// record stores them as int8_t to match pret and the comparison is on the stored type, so a future
// arithmetic use cannot silently disagree with the engine. (FR/LG map numbers >= 128 already
// forced this care once — gamestate.c:220's int16_t widening in the gs log.)
int presence_same_map(const PeerPresence* a, const PeerPresence* b) {
	if (!a || !b) return 0;
	return a->gameId == b->gameId && a->mapGroup == b->mapGroup && a->mapNum == b->mapNum;
}

// The shared field predicate (D4.6) applied to a RECORD: "is this game's position meaningful?"
// ctx / sb1Valid / px cross the wire raw, so this side re-runs the SAME field_state_ok rather than
// trusting one flag. Belt and braces, and it is literally what D4.6 requires ("presence.c calls the
// same function for both sides"). The textDlg term is deliberately NOT part of this predicate — it
// rides PRES_F_TEXT and is applied to the SELF side only (review finding 6; see presence.h).
static int rec_field_ok(const PeerPresence* r) {
	if (!(r->flags & PRES_F_FIELD)) return 0;
	return field_state_ok(1, (int)r->ctx, (r->flags & PRES_F_SB1VALID) ? 1 : 0, (int)r->px, 0);
}

// ---- state, rounds, publish -----------------------------------------------------------------

void presence_reset(PresenceState* ps) {
	if (!ps) return;
	memset(ps, 0, sizeof *ps);
}

void presence_begin_round(PresenceState* ps, uint32_t round) {
	if (!ps) return;
	ps->round = round;
}

// THE SEAM (D3.4). Two callers will ever exist and neither may be special-cased downstream:
//   today  (same-console): main.c fills a PeerPresence from the OTHER screen's GameState + the
//                          three new reads + the cached identity, once per screen per frame, in
//                          the parked window;
//   M4     (wireless):     netlink.c's beacon RX fills the identical struct from a 48-byte payload.
// Nothing else changes — not presence_solve, not the gate, not the draw.
void presence_publish(PresenceState* ps, int slot, const PeerPresence* rec) {
	if (!ps || !rec || slot < 0 || slot >= PRES_MAX_PEERS) return;

	// D3.4.2 newest-wins: a record whose round is not STRICTLY newer than the stored one is
	// dropped (PM's "fire-and-forget, newest-wins, stale dropped by round"). Same-console this
	// never triggers (main.c publishes with the monotonically increasing render sequence); over a
	// radio it is the reorder guard. The signed-delta form is wrap-safe, which costs nothing and
	// removes a 2.2-year-uptime footnote.
	if (ps->have[slot] && (int32_t)(rec->round - ps->rec[slot].round) <= 0) { ps->dropN[slot]++; return; }

	int hadPrev    = ps->have[slot] ? 1 : 0;
	int mapChanged = hadPrev && (rec->mapGroup != ps->lastMapG[slot] || rec->mapNum != ps->lastMapN[slot]);
	// D5.6 teleport guard: same map, but the peer jumped further than a legitimate step could
	// carry it between two consecutive records. Same-console this fires only on a warp WITHIN one
	// map or a torn read; over the radio it is the packet-loss guard. It NEVER suppresses the
	// draw — a legitimately fast peer must not vanish — it only forbids gliding there.
	int teleport   = hadPrev && !mapChanged &&
	                 (iabs_((int)rec->px - (int)ps->lastPx[slot]) +
	                  iabs_((int)rec->py - (int)ps->lastPy[slot]) > PRES_TELEPORT_TILES);

	if (mapChanged) {
		// D4.7.1: no interpolation may EVER cross a map boundary — the tile delta between two maps
		// is meaningless. Clear the filter AND the hold; main.c re-latches identity (D2.3).
		ps->mapChgN[slot]++;
		ps->haveSm[slot]   = 0;
		ps->holdHave[slot] = 0;
	}
	if (teleport) ps->teleN[slot]++;
	if (!hadPrev || mapChanged || teleport) ps->snapNext[slot] = 1;

	ps->rec[slot]       = *rec;
	ps->have[slot]      = 1;
	ps->seenRound[slot] = ps->round;
	ps->lastPx[slot]    = rec->px;
	ps->lastPy[slot]    = rec->py;
	ps->lastMapG[slot]  = rec->mapGroup;
	ps->lastMapN[slot]  = rec->mapNum;

	// Heartbeat (D3.6). PRES_F_HB clear => the profile has no hbCtr for that game, so wedge
	// detection is DISABLED rather than wrong (D1.9): keep hbRound pinned to now so the stall is
	// always 0 and the peer can never be reported "wedged" by default.
	if ((rec->flags & PRES_F_HB) && hadPrev && rec->hb == ps->hbVal[slot]) {
		/* frozen — leave hbRound where it was, that IS the stall */
	} else {
		ps->hbVal[slot]   = rec->hb;
		ps->hbRound[slot] = ps->round;
	}

	// Tier 2 (PM §7.4): stamped ONLY by records that are actually game-active, never by mere
	// arrival. This is the distinction that stops a paused / menu / wedged peer from being
	// rendered as a live person standing in your grass.
	if (presence_liveness(ps, slot) == PRES_LIVE_ACTIVE) ps->activeRound[slot] = ps->round;
}

// D3.5/D3.6. Ageing is computed against the CONSUMER's round (arrival), never the producer's
// `round` field — clock skew across two consoles must not be able to make a live peer look stale.
// The producer's round is used only for newest-wins (D3.4.2) and for the wedge check.
int presence_liveness(const PresenceState* ps, int slot) {
	if (!ps || slot < 0 || slot >= PRES_MAX_PEERS || !ps->have[slot]) return PRES_LIVE_NONE;
	if (ps->round - ps->seenRound[slot] > PRES_STALE_FRAMES)           return PRES_LIVE_NONE;
	const PeerPresence* r = &ps->rec[slot];
	if (!(r->flags & PRES_F_SB1VALID) || !(r->flags & PRES_F_FIELD))   return PRES_LIVE_CONNECTED;
	if ((r->flags & PRES_F_HB) && (ps->round - ps->hbRound[slot]) > PRES_WEDGE_FRAMES)
		return PRES_LIVE_CONNECTED;   // fresh record, frozen game = PM's wedge signal (§3, §8)
	return PRES_LIVE_ACTIVE;
}

// D3.6.1 — the free hang detector. A peer whose heartbeat has been frozen for > 180 frames while
// its record is fresh is precisely PM's wedge signal; same-console it also cleanly distinguishes
// "the user paused that seat" from "that game hung". Belongs in the game-state log.
uint32_t presence_hb_stall(const PresenceState* ps, int slot) {
	if (!ps || slot < 0 || slot >= PRES_MAX_PEERS || !ps->have[slot]) return 0;
	return ps->round - ps->hbRound[slot];
}

// ---- the gate ladder (D4.1) -----------------------------------------------------------------
// Deliberately shaped like tilt_target_level (tilt.c:143-156): a flat input struct, one pure
// function, a ladder of rules, first hit wins — so the host suite can enumerate it exhaustively
// and no rule can ever "re-open" an earlier one. Rule-to-P-Gn mapping: see presence.h.
//
// P-G3 blocks linkOn || netOn || wlOn, which is STRICTLY STRONGER than tilt's G4 (D4.8). The
// reason that matters most is not cost: main.c only parks the workers when !linkOn && !netOn &&
// !wlOn, so DURING A LINK THE PARKED-WINDOW GUARANTEE DOES NOT HOLD. The gs-log and smart-touch
// reads accept that benign EWRAM race because a torn row is a bad log line — but a torn (px, py)
// is a VISIBLY TELEPORTING AVATAR. Presence draws its reads, so it must not take that race. It
// also costs nothing real: during a cable-club/Union-Room trade neither game is in the free-roam
// overworld, so P-G6 would close the gate anyway. Revisitable ONLY with a positional sanity filter
// in place and a hardware run showing no jitter — not before.
static int gate_reason(const PresenceState* ps, const PresenceIn* in, int liveness) {
	const PeerPresence* se = &in->self;
	const PeerPresence* pe = &ps->rec[in->slot];

	if (!in->enabled)                                    return PRES_OFF_DISABLED;   // P-G1
	if (in->menuOpen)                                    return PRES_OFF_MENU;       // P-G2
	if (in->linkAny)                                     return PRES_OFF_LINK;       // P-G3
	if (se->gameId == PRES_GAME_NONE ||
	    !ps->have[in->slot] || pe->gameId == PRES_GAME_NONE)
	                                                     return PRES_OFF_NOPROF;     // P-G4
	if (se->gameId != pe->gameId)                        return PRES_OFF_UNIVERSE;   // P-G5
	// SELF side. The textDlg term (PRES_F_TEXT) applies HERE and only here: the host's own dialog
	// box is drawn in the frame this avatar composites over, so a peer standing on the lower rows
	// would be painted on top of it — D4.7.3's "no stray sprite over their menu", applied to the
	// textbox. The PEER's textbox is on the peer's screen and is deliberately NOT a gate (finding 6).
	if (!rec_field_ok(se) || (se->flags & PRES_F_TEXT))  return PRES_OFF_SELF;       // P-G6 self
	if (presence_obj_agree(se) == 0)                     return PRES_OFF_SELFOBJ;    // P-G7 self
	if (!rec_field_ok(pe))                               return PRES_OFF_FIELD;      // P-G6 peer
	if (presence_obj_agree(pe) == 0)                     return PRES_OFF_OBJ;        // P-G7 peer
	if (!presence_same_map(se, pe))                      return PRES_OFF_MAP;        // P-G8
	if (liveness != PRES_LIVE_ACTIVE)                    return PRES_OFF_LIVE;       // P-G9
	return PRES_OFF_NONE;
}

// ---- D5.1/D5.3: the anchor, in frame space --------------------------------------------------
// The tile formula is NOT re-derived here (PHASE.md "Decisions already made"); it is verified from
// two independent places in our own source:
//   touch.c:276  ddx = gx/16 - 7, ddy = gy/16 - 5, goal = (s_downPx + ddx, s_downPy + ddy)
//                => screen tile (c, r) IS map coordinate (px + c - 7, py + r - 5)
//   main.c:865   build_depth_grid maps screen tile (c, r) -> layout index (px + c, py + r + 2),
//                and touch.c:207 shows the layout index carries the +7 MAP_OFFSET bias
// Composing, the two agree exactly — which is why main.c:862 can say "the border +7 and the -7
// player-centring cancel". Both games read px/py from gSaveBlock1Ptr->pos through ONE code path
// (gamestate.c:98-99) and pret puts `pos` at +0x00 of struct SaveBlock1 in BOTH engines, so the
// bias cancels in the DIFFERENCE and
//     peer A appears on host B's screen at screen tile (7 + Apx - Bpx, 5 + Apy - Bpy)
// holds for BPEE, BPRE and BPGE identically. A per-game bias term would be a symptom of a wrong
// sb1ptr, not a fix (D5.1).
//
// The sub-tile halves come from SUB() (D5.3): SUB(Bc) tracks the HOST's BG scroll pixel-exactly
// (the same term places every static tile), and -SUB(Ac) is the PEER's own sub-tile motion (their
// player is pixel-locked to their camera). Both roles, one expression, nothing interpolated.
static void solve_target(const PeerPresence* se, const PeerPresence* pe, float* tx, float* ty) {
	int sX = (se->flags & PRES_F_CAM) ? cam_phase((int)se->subX) : 0;
	int sY = (se->flags & PRES_F_CAM) ? cam_phase((int)se->subY) : 0;
	int pX = (pe->flags & PRES_F_CAM) ? cam_phase((int)pe->subX) : 0;
	int pY = (pe->flags & PRES_F_CAM) ? cam_phase((int)pe->subY) : 0;
	*tx = PRES_ANCHOR_X + 16.0f * (float)((int)pe->px - (int)se->px)
	                    + (float)(presence_sub(sX) - presence_sub(pX));
	*ty = PRES_ANCHOR_Y + 16.0f * (float)((int)pe->py - (int)se->py)
	                    + (float)(presence_sub(sY) - presence_sub(pY));
}

// D5.7 — culling, two stages, cheapest first. Stage 1 is a tile early-out: the peer's screen tile
// is (7 + dx, 5 + dy) and the visible grid is 15x10, so a fully on-screen peer has dx in [-7, 7]
// and dy in [-5, 4]; the bounds below add 2 tiles of slack for a 16x32 sprite straddling an edge
// and for the sub-tile term (which can never exceed 15 px = 1 tile). Stage 2 is the rect test on
// the art rect (footX - 8, footY - 32, 16, 32). A PARTIALLY visible avatar returns 1 and is
// clipped by the renderer. Under tilt the visible source region shrinks (tilt_coverage), but that
// crop is the render half's business: this module always culls against the FLAT frame and never
// imports the tilt geometry (D5.7.1 / §0.3).
//
// FIX PASS (review finding 5). Two changes, and they belong together:
//   * PRES_CULL_M is 0 (see presence.h for the full argument) — an 8 px margin made `draw` report
//     "an avatar is on the screen" in a band where presence_art_clip renders nothing.
//   * the edge tests are CLOSED (<=, >=), not open. With integer anchors the clip culls at
//     x1 == 0 / x0 == 240 exactly (16 - cutL - cutR <= 0), so an open test disagreed with it on
//     precisely the whole-tile positions a walking player crosses: dTileX = +8 -> x0 = 240,
//     dTileY = -6 -> y1 = -8. Closed tests make the two functions agree bit for bit on every
//     integer anchor; the only residual is the sub-pixel band the clip's round-OUTWARD source-column
//     rule owns (a 0.5 px overhang is dropped there and kept here), which is <= 1 px and cannot be
//     removed without teaching this module about source columns.
static int on_screen(int dTileX, int dTileY, float fx, float fy) {
	if (iabs_(dTileX) > 9 || iabs_(dTileY) > 8) return 0;
	float x0 = fx - 8.0f,  x1 = x0 + 16.0f;
	float y0 = fy - 32.0f, y1 = y0 + 32.0f;
	if (x1 <= -PRES_CULL_M || x0 >= (float)PRES_FRAME_W + PRES_CULL_M) return 0;
	if (y1 <= -PRES_CULL_M || y0 >= (float)PRES_FRAME_H + PRES_CULL_M) return 0;
	return 1;
}

// ---- solve (D6.2) ---------------------------------------------------------------------------
// The ONLY place the gate ladder, the anchor math, the hold and the filter live. main.c contains
// NO presence gating logic, exactly as it contains no tilt gating logic. Mutates only `ps`
// (filter/hold/diagnostic state) and never `in`.
int presence_solve(PresenceState* ps, const PresenceIn* in, PresenceOut* out) {
	if (!out) return 0;
	memset(out, 0, sizeof *out);
	out->reason = PRES_OFF_NOPROF;
	if (!ps || !in) return 0;
	int slot = in->slot;
	if (slot < 0 || slot >= PRES_MAX_PEERS) return 0;

	const PeerPresence* se = &in->self;
	const PeerPresence* pe = &ps->rec[slot];

	out->liveness = presence_liveness(ps, slot);
	out->dir      = presence_dir((int)pe->facing);
	out->gender   = (pe->flags & PRES_F_IDENT) ? (pe->gender ? 1 : 0) : 0;
	out->moving   = ((pe->flags & PRES_F_CAM) && (pe->subX != 0 || pe->subY != 0)) ? 1 : 0;
	// The tile delta is filled whenever BOTH sides have a real position, gate or no gate: it is
	// the M1 HUD's readout and it must stay diagnosable precisely on the frames that do not draw.
	if (ps->have[slot] && (se->flags & PRES_F_SB1VALID) && (pe->flags & PRES_F_SB1VALID)) {
		out->dTileX = (int)pe->px - (int)se->px;
		out->dTileY = (int)pe->py - (int)se->py;
	}

	int reason = gate_reason(ps, in, out->liveness);
	out->reason = reason;

	float fx, fy;
	if (reason == PRES_OFF_NONE) {
		float tx, ty;
		solve_target(se, pe, &tx, &ty);

		// D5.5 — the low-rate filter, and the M4 hinge in the math. Same-console every record is
		// one frame old (age == 0) and D5.3 is EXACT, so the filter is a bit-exact no-op there and
		// TEST 14 pins that it is. A beacon at 2-8 Hz delivers a phase up to 30 frames stale, and
		// a raw plot of it would visibly stutter — hence the single pole, and hence the snap: a
		// warp, a respawn or a dropped beacon must teleport, not glide across the map.
		uint32_t age = ps->round - ps->seenRound[slot];
		if (age == 0) {
			fx = tx; fy = ty;
			ps->smX[slot] = tx; ps->smY[slot] = ty; ps->haveSm[slot] = 1;
		} else {
			int snap = (ps->snapNext[slot] || !ps->haveSm[slot]);
			if (!snap && (fabsf_(tx - ps->smX[slot]) > PRES_SNAP_PX ||
			              fabsf_(ty - ps->smY[slot]) > PRES_SNAP_PX)) snap = 1;
			if (snap) { ps->smX[slot] = tx; ps->smY[slot] = ty; }
			else {
				ps->smX[slot] += (tx - ps->smX[slot]) * PRES_SMOOTH_A;
				ps->smY[slot] += (ty - ps->smY[slot]) * PRES_SMOOTH_A;
			}
			ps->haveSm[slot] = 1;
			fx = ps->smX[slot]; fy = ps->smY[slot];
		}
		ps->snapNext[slot] = 0;   // consumed either way

		// Latch the last good anchor for the D4.7 hold. holdRound is stamped ONLY here, on a real
		// draw, so a hold can never extend itself.
		ps->holdX[slot] = fx; ps->holdY[slot] = fy;
		ps->holdRound[slot] = ps->round; ps->holdHave[slot] = 1;
	} else if ((reason == PRES_OFF_FIELD || reason == PRES_OFF_OBJ) &&
	           ps->holdHave[slot] && (ps->round - ps->holdRound[slot]) <= PRES_HOLD_FRAMES) {
		// D4.7.2 — the PEER's field/obj gate blinked (a warp boundary, a torn read, a frame inside
		// a full-screen fade). Keep drawing the last good anchor for up to 30 frames; a real
		// departure still removes the avatar within half a second.
		fx = ps->holdX[slot]; fy = ps->holdY[slot];
		out->held = 1;
	} else {
		// Everything else stops NOW, and clears both the hold and the filter so a re-appearance
		// SNAPS to truth rather than sliding in from a stale position (D4.7.3, D4.7.4). That
		// includes the self-side failure by design: the host's own screen is the frame the user is
		// looking at, and a stray sprite over their menu is the failure D4.7.3 prevents.
		ps->holdHave[slot] = 0;
		ps->haveSm[slot]   = 0;
		ps->snapNext[slot] = 1;
		return 0;
	}

	if (!on_screen(out->dTileX, out->dTileY, fx, fy)) {
		out->reason = PRES_OFF_CULL;
		return 0;   // off this screen: the honest ceiling is that you see them when they are on it.
		            // No edge-clamping, no off-screen arrow, no "peer is nearby" indicator.
	}
	out->footX = fx;
	out->footY = fy;
	out->draw  = 1;
	return 1;
}
