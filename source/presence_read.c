// presence_read.c — Phase 15 co-op presence: the PRODUCER's game-RAM reader (slice M1).
// Contract, invariants and the read budget: see presence_read.h. Every DECISION about what the
// resulting record says lives in presence_fill_core (presence.c, pure C, host-tested); this file
// only fetches bytes and hands over a flat PresenceSrc.
#include <string.h>

#include "presence_read.h"

void presence_ident_reset(PresenceIdent* id) {
	if (!id) return;
	memset(id, 0, sizeof *id);
	memset(id->name, 0xFF, sizeof id->name);   // an empty (immediately terminated) name
	id->mapG = id->mapN = -1;
}

// D1.2 — the identity read. Deref gSaveBlock2Ptr, reject unless the high byte is 0x02 (the SAME
// EWRAM-pointer sanity game_read applies to sb1 at gamestate.c:96), then 8 name bytes, gender at
// +0x08 and the visible TID as an LE u16 at +0x0A (pret include/global.h:508-513, identical head
// in both engines). A failed deref leaves ANY PREVIOUS LATCH INTACT (D1.9: identity unavailable,
// never a read at a garbage address, and never a good name replaced by a bad one).
//   returns 1 = a well-formed identity was latched, 0 = nothing changed
static int ident_refresh(GbaCore* c, const GameProfile* p, PresenceIdent* id) {
	if (!c || !p || !p->sb2ptr || !id) return 0;          // D1.9: sb2ptr == 0 -> identity off
	// SPEC-coop P3.2.4 — call site 2 of 2. On Ruby/Sapphire (sbDirect) gSaveBlock2 is a static
	// struct, not a pointer: the column IS the address. See gamestate.h's sbDirect note.
	uint32_t sb2 = p->sbDirect ? p->sb2ptr : gbacore_read32(c, p->sb2ptr);
	if ((sb2 >> 24) != 0x02u) return 0;                   // not an EWRAM pointer -> not ready
	uint8_t nm[8];
	for (int i = 0; i < 8; i++) nm[i] = gbacore_read8(c, sb2 + (uint32_t)i);
	// A name whose first byte is already the terminator means the save block is not populated yet
	// (a fresh boot, a title screen). Latching that would freeze an empty nameplate for the whole
	// session, because D2.3 keeps the FIRST well-formed result — so it does not count as one.
	if (nm[0] == 0xFF) return 0;
	// SPEC-coop P3.2.5 — the direct-block profiles need a SECOND emptiness test, because their
	// validity signal is genuinely weaker: gSaveBlock2 exists (zeroed) from the first frame after
	// boot, so the deref check above can no longer say "a save is loaded". A zeroed block gives
	// nm[0] == 0x00, which the Gen-3 charmap decodes to ' ' — well-formed by the 0xFF test, and
	// therefore latched FOREVER as a blank nameplate. Rejecting an all-zero name is cheap, exact,
	// and cannot reject a real one (Gen-3 name entry cannot produce eight leading spaces with no
	// terminator). Pointer profiles are unaffected: their deref fails long before this.
	if (p->sbDirect) {
		int allZero = 1;
		for (int i = 0; i < 8; i++) if (nm[i] != 0x00) { allZero = 0; break; }
		if (allZero) return 0;
	}
	memcpy(id->name, nm, sizeof id->name);
	id->gender = (uint8_t)(gbacore_read8(c, sb2 + 0x08u) == 1u ? 1 : 0);   // MALE 0 / FEMALE 1
	id->tid    = gbacore_read16(c, sb2 + 0x0Au);                           // trainer_card.c:722's LE u16
	id->have   = 1;
	return 1;
}

void presence_read_fill(PeerPresence* r, int seat, GbaCore* c, const GameProfile* p,
                        const GameState* gs, PresenceIdent* id, uint32_t round) {
	PresenceSrc s;
	memset(&s, 0, sizeof s);
	s.round   = round;
	s.px      = s.py   = -1;
	s.mapGroup = s.mapNum = -1;
	s.objX    = s.objY = -1;    // D4.5.1: the "gate UNAVAILABLE" sentinel is the default
	s.facing  = -1;
	int gameId = PRES_GAME_NONE;

	if (c && p && gs && gs->valid) {
		// D4.3 — the map universe, from the profile's own 4-char game code. profile_for() matched
		// that code against the ROM header, so it IS gbacore_game_code's answer, cached: Emerald's
		// (3,12) is not FireRed's (3,12), and without this gate an Emerald + FireRed pair (the
		// user's actual carts) would draw a peer walking around an unrelated map.
		gameId = presence_game_id(p->code);

		// D2.1 — everything below this line is ALREADY in hand: reuse game_read's snapshot, never
		// call it again (the D3 CSV rule, main.c:2366-2367).
		s.ok       = 1;
		s.ctx      = (int)gs->ctx;
		s.sb1Valid = gs->sb1Valid ? 1 : 0;
		s.textDlg  = gs->textDlg  ? 1 : 0;
		s.px       = gs->px;       s.py     = gs->py;
		s.mapGroup = gs->mapGroup; s.mapNum = gs->mapNum;
		s.objX     = gs->objX;     s.objY   = gs->objY;
		s.facing   = gs->facing;

		// ---- D2.2: THE THREE NEW READS. This is the entire per-frame cost of the phase. ----
		// 1/2: the camera phase = the sub-tile pixel offset of BOTH the camera and (because Gen 3
		//      copies movementSpeed off the tracked player sprite every frame, field_camera.c:336)
		//      the player. Guarded to -15..15 inside presence_fill_core, exactly as main.c:854.
		if (p->fieldCamera) {
			s.camOk = 1;
			s.camX  = (int)(int32_t)gbacore_read32(c, p->fieldCamera + 0x10u);
			s.camY  = (int)(int32_t)gbacore_read32(c, p->fieldCamera + 0x14u);
		}
		// 3:  the liveness heartbeat. gMain.vblankCounter2 (gMain + 0x24), incremented
		//     UNCONDITIONALLY in VBlankIntr in BOTH engines — NOT vblankCtr, whose FR/LG slot is a
		//     pointer that is usually NULL (D1.5.2 / Open Q1). 0 => wedge detection disabled.
		if (p->hbCtr) { s.hbOk = 1; s.hb = gbacore_read32(c, p->hbCtr); }

		// ---- D2.3: identity, CACHED, not polled ----
		if (id) {
			int mapChanged = id->have && (gs->mapGroup != (int)id->mapG || gs->mapNum != (int)id->mapN);
			uint32_t since = id->tried ? (round - id->lastTry) : 0xFFFFFFFFu;   // never tried => due
			if (presence_ident_due(id->have ? 1 : 0, mapChanged, since)) {
				id->tried   = 1;
				id->lastTry = round;
				ident_refresh(c, p, id);
			}
			if (id->have) {
				id->mapG = (int16_t)gs->mapGroup;   // the map the latch is now associated with
				id->mapN = (int16_t)gs->mapNum;
				s.identOk = 1;
				s.name    = id->name;
				s.gender  = (int)id->gender;
				s.tid     = (int)id->tid;
			}
		}
	}

	presence_fill_core(r, seat, gameId, &s);
}
