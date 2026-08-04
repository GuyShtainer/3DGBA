// presence_ui.c — phase 15 slice M3: the identity/interaction core. Contract: presence_ui.h.
#include <string.h>

#include "presence_ui.h"

// ---- small formatters -----------------------------------------------------------------------
// Deliberately hand-rolled: see presence_ui.h. Both write at most cap-1 characters plus the NUL,
// and both return strlen(out), so a caller can append without measuring twice.

int presence_fmt_u5(unsigned v, char* out, int cap) {
	if (!out || cap <= 0) return 0;
	if (cap < 6) { out[0] = '\0'; return 0; }     // 5 digits + NUL, or nothing at all
	if (v > 99999u) v = 99999u;                   // a visible TID is a u16, so this is unreachable
	for (int i = 4; i >= 0; i--) { out[i] = (char)('0' + (int)(v % 10u)); v /= 10u; }
	out[5] = '\0';
	return 5;
}

int presence_fmt_int(int v, char* out, int cap) {
	if (!out || cap <= 0) return 0;
	out[0] = '\0';
	char tmp[12];
	int  n = 0, neg = 0;
	unsigned u;
	// FIX PASS (review finding 1): NEVER negate through a signed type here. `long` is 32 bits on
	// devkitARM (ILP32), so `-(long)INT32_MIN` is not representable — signed-overflow UB, and GCC at
	// -O2 is entitled to assume `v != INT32_MIN` and fold the range test away. The host's 64-bit
	// `long` hides it, so it can only ever be found by reading. `0u - (unsigned)v` is well-defined
	// modular arithmetic for EVERY input including INT32_MIN, and identical for every other one.
	if (v < 0) { neg = 1; u = 0u - (unsigned)v; } else u = (unsigned)v;
	do { tmp[n++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u && n < (int)sizeof tmp);
	int len = n + neg;
	if (len + 1 > cap) return 0;                  // never a truncated number: a half-printed tile
	                                              // coordinate is worse than an empty field
	int w = 0;
	if (neg) out[w++] = '-';
	while (n > 0) out[w++] = tmp[--n];
	out[w] = '\0';
	return w;
}

// Append `s` at *w inside out[cap]; silently stops at the cap. Local, so the card builder below
// reads as a sequence of fields rather than as pointer arithmetic.
static void app(char* out, int cap, int* w, const char* s) {
	if (!out || !s || !w) return;
	while (*s && *w + 1 < cap) out[(*w)++] = *s++;
	out[*w] = '\0';
}

// ---- A5.2: facing ---------------------------------------------------------------------------

// The unit step from host to peer, as a PRES_DIR_*. Screen-down is increasing tile y (the anchor
// math's own convention: build_depth_grid walks gy = py + r + 2 with r increasing downward), so
// D is +y, U is -y, L is -x, R is +x — matching gamestate.h:119's "1=D 2=U 3=L 4=R" and pret's
// include/constants/global.h:137-145 (DIR_SOUTH 1, DIR_NORTH 2, DIR_WEST 3, DIR_EAST 4).
static int dir_from_delta(int dx, int dy) {
	if (dx ==  1 && dy ==  0) return PRES_DIR_EAST;
	if (dx == -1 && dy ==  0) return PRES_DIR_WEST;
	if (dx ==  0 && dy ==  1) return PRES_DIR_SOUTH;
	if (dx ==  0 && dy == -1) return PRES_DIR_NORTH;
	return 0;                                     // not a unit step (includes dx == dy == 0)
}

// Is this raw gObjectEvents facing nibble a READING? 1..8 covers the four cardinals plus the four
// diagonals presence_dir folds to a cardinal. -1 (game_read's "unavailable"), 0 (DIR_NONE) and
// 9..15 (garbage) all mean the record's SOUTH is presence_dir's default, not an observation.
static int face_usable(int raw) { return raw >= 1 && raw <= 8; }

void presence_meet(const PeerPresence* host, int hostRawFacing,
                   const PeerPresence* peer, int peerRawFacing, PresMeet* out) {
	if (!out) return;
	memset(out, 0, sizeof *out);
	if (!host || !peer) return;

	// Same map is a precondition, not a result: a tile delta across two different maps is a number
	// with no meaning, and presence_same_map carries the D4.3 MAP-UNIVERSE test with it (Emerald's
	// (3,12) is not FireRed's (3,12)). Both sides must also actually have a position — SB1VALID is
	// what makes px/py meaningful at all.
	if (!presence_same_map(host, peer)) return;
	if (!(host->flags & PRES_F_SB1VALID) || !(peer->flags & PRES_F_SB1VALID)) return;
	out->sameMap = 1;

	out->dx = (int)peer->px - (int)host->px;
	out->dy = (int)peer->py - (int)host->py;
	// A5.1: exactly one orthogonal step. dx*dx + dy*dy == 1 is true iff one of |dx|,|dy| is 1 and
	// the other 0, which excludes the diagonals in the same expression.
	out->adjacent  = (out->dx * out->dx + out->dy * out->dy) == 1;
	out->dirToPeer = dir_from_delta(out->dx, out->dy);

	out->hostFaceOk = face_usable(hostRawFacing);
	out->peerFaceOk = face_usable(peerRawFacing);
	out->facingOk = out->adjacent
	             && (int)host->facing == out->dirToPeer
	             && (int)peer->facing == dir_from_delta(-out->dx, -out->dy);

	// A5.2.1 — the honest caveat. gamestate.c:131 marks the facingDirection offset verify-on-hw and
	// the FR/LG mapObjects base carried a standing suspicion (refuted by SPEC-data D1.8.1, but the
	// code is still written as if it could be wrong). So: when either side's facing is not a
	// reading, the meeting predicate DEGRADES TO ADJACENCY-ONLY rather than silently never firing.
	// Better a slightly eager prompt than a feature that is dead on FireRed — and the avatar's
	// POSITION never depends on facing (it comes from SaveBlock1.pos), so a wrong facing costs a
	// wrong sprite direction and an eager prompt, never a wrong location.
	out->degraded = out->adjacent && !(out->hostFaceOk && out->peerFaceOk);
	out->meet     = out->degraded ? out->adjacent : out->facingOk;
}

// ---- A5.4.2: the card ------------------------------------------------------------------------

void presence_card_reset(PresCard* c) { if (c) c->open = 0; }

int presence_card_step(PresCard* c, const PresCardIn* in) {
	if (!c) return 0;
	if (!in) { c->open = 0; return 0; }

	// The involuntary closes first, so a card can never survive the condition that raised it.
	// Every one of these is already computed elsewhere in the frame (A5.4.2): the pref, our pause
	// menu, the whole presence gate ladder (which is where "ctx left the overworld" lives), and
	// the meeting predicate. `drawn` is doing a lot of work here on purpose — if the avatar is not
	// on screen there is nothing for a card to be about.
	if (!in->enabled || in->menuOpen || !in->hudOn || !in->drawn || !in->meet) { c->open = 0; return 0; }

	// A5.3 — presence NEVER touches input. These are OBSERVED edges: the same A press is delivered
	// to the game exactly as it always was. That is safe because in Gen 3, pressing A while facing
	// an empty tile does nothing, and the avatar's tile IS empty as far as the engine is concerned
	// (the honest ceiling: the engine does not know it exists). If the player happens to be facing
	// a real sign or NPC, the game opens a textbox, the field predicate goes false, and this gate
	// closes on its own — the correct behaviour, obtained for free rather than coded.
	if (!c->open) { if (in->aEdge) c->open = 1; }        // open on A
	else if (in->aEdge || in->bEdge) c->open = 0;        // ...and close on the next A or B
	return c->open;
}

// ---- A4.4.2 / O-A4: the surface policy -------------------------------------------------------

unsigned presence_surfaces(int drawn, int hudOn, int haveName, int meet, int cardOpen, int nearTiles) {
	(void)nearTiles;   // the O-A4 "fade in within N tiles" alternative hangs off this parameter;
	                   // the SHIPPED policy is always-on, which is what A4.4.1 describes
	unsigned s = 0;
	if (!drawn || !hudOn) return 0;    // A4.4.2: one user control (hudMode) drives both surfaces
	if (haveName) s |= PRES_SURF_PLATE;
	// The prompt is the only thing that teaches the interaction, so it is up whenever the meeting
	// predicate holds — except while the card it opens is already up, where it would be noise.
	if (meet && !cardOpen) s |= PRES_SURF_PROMPT;
	return s;
}

// ---- A4.4.1 / A5.4.1: the head-pill stack (fix pass, review finding 8) -----------------------

float presence_pill_x(float headX, float w, float screenW) {
	float x = headX - w * 0.5f;
	if (x > screenW - w - PRES_PILL_EDGE) x = screenW - w - PRES_PILL_EDGE;
	if (x < PRES_PILL_EDGE)               x = PRES_PILL_EDGE;
	return x;
}

void presence_pill_y(float headY, int hasPlate, int hasPrompt, float screenH,
                     float* plateY, float* promptY) {
	// The unclamped stack: the prompt sits immediately above the head, the nameplate above the
	// prompt. (A4.4.1's literal "y = headY - 13" is the plate's position when it is ALONE, which is
	// what the single -PRES_PILL_H below gives.)
	float prompt = headY - PRES_PILL_H;
	float plate  = hasPrompt ? prompt - PRES_PILL_H : prompt;

	if (hasPlate || hasPrompt) {
		// ONE offset for the whole stack. Clamping the members independently is what collapsed them
		// onto each other; clamping the extremes and translating both preserves the 15 px gap at
		// every position on the screen.
		float top = hasPlate  ? plate  : prompt;    // the topmost pill's top edge
		float bot = hasPrompt ? prompt : plate;     // the bottom-most pill's top edge
		float d   = 0.0f;
		if (bot + PRES_PILL_H > screenH) d = screenH - PRES_PILL_H - bot;   // push the stack UP
		if (top + d < PRES_PILL_TOP)     d = PRES_PILL_TOP - top;           // ...but the top wins:
		                                                                    // a stack too tall for
		                                                                    // the panel overflows
		                                                                    // DOWNWARD, off the
		                                                                    // bottom, never up into
		                                                                    // the HUD bar
		plate += d; prompt += d;
	}
	if (plateY)  *plateY  = plate;
	if (promptY) *promptY = prompt;
}

// ---- A4.4.4: the Card's text -----------------------------------------------------------------

const char* presence_gender_label(int gender) {
	return gender == 0 ? "M" : (gender == 1 ? "F" : "?");
}

void presence_card_fill(const PeerPresence* peer, PresCardText* out) {
	if (!out) return;
	memset(out, 0, sizeof *out);
	out->gender = -1;
	// The placeholders are what a card shows when sb2ptr is 0, its deref failed, or the save was
	// not loaded when the latch was attempted (SPEC-data D1.9: identity unavailable => name "?",
	// TID hidden, gender defaults — the AVATAR still draws). They are set first so an early return
	// on a NULL record still yields a well-formed card rather than empty fields.
	out->name[0] = '?'; out->name[1] = '\0';
	memcpy(out->id, "-----", 6);
	out->loc[0] = '\0';
	if (!peer) return;

	if (peer->flags & PRES_F_IDENT) {
		// A0.1.3 / D3.2.1: the 8 name bytes cross the seam RAW so the record stays the byte-exact
		// wire format for M4; decoding is presentation and happens HERE. gbatext_decode is
		// cap-driven and never writes past the field, whatever those 8 bytes contain.
		char nm[PRES_CARD_NAME_CAP];
		int  n = gbatext_decode(peer->name, 8, nm, (int)sizeof nm);
		if (n > 0) { memcpy(out->name, nm, (size_t)n + 1); out->haveIdent = 1; }
		if (out->haveIdent) {
			presence_fmt_u5((unsigned)peer->tid, out->id, (int)sizeof out->id);
			out->gender = peer->gender ? 1 : 0;
		}
	}

	// "MAP 3-12   TILE 14,9". Built only when the position is real: printing a tile off an invalid
	// SaveBlock1 would be a plausible-looking wrong number, which is worse than a blank line.
	if (peer->flags & PRES_F_SB1VALID) {
		int w = 0;
		app(out->loc, (int)sizeof out->loc, &w, "MAP ");
		w += presence_fmt_int((int)peer->mapGroup, out->loc + w, (int)sizeof out->loc - w);
		app(out->loc, (int)sizeof out->loc, &w, "-");
		w += presence_fmt_int((int)peer->mapNum, out->loc + w, (int)sizeof out->loc - w);
		app(out->loc, (int)sizeof out->loc, &w, "   TILE ");
		w += presence_fmt_int((int)peer->px, out->loc + w, (int)sizeof out->loc - w);
		app(out->loc, (int)sizeof out->loc, &w, ",");
		w += presence_fmt_int((int)peer->py, out->loc + w, (int)sizeof out->loc - w);
	}
}
