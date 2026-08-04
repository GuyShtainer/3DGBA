// presence_read.h — Phase 15 co-op presence: the PRODUCER's game-RAM reader (slice M1).
// ============================================================================================
// This is the ONE file in the phase that touches emulated memory. It exists so `presence.c` can
// stay header-free pure C (CLAUDE.md #4 / PHASE.md invariant 8 / SPEC-data D6.1) while the reads
// themselves still live next to the profile that names their addresses. SPEC-data D6.4 allows
// exactly this ("presence_fill ... lives in main.c (or a thin presence_read.c), NOT in
// presence.c"); the thin file is chosen so main.c gains no game-RAM logic at all.
//
// THE TWO INVARIANTS THIS FILE CARRIES (PHASE.md):
//   1. READ-ONLY. Every access below is gbacore_read8/16/32. There is no write path here and none
//      may be added — that property is why the overlay design was chosen over object-event
//      injection, and it is what makes presence unable to corrupt either save.
//   2. PARKED WINDOW ONLY. presence_read_fill is called from main.c's existing parked block
//      ("Workers are parked here -> touch RAM access safe"), alongside the depth-grid / touch /
//      tilt-gate reads. It is NOT thread-safe, must never be called from a worker, and adds no
//      synchronisation primitive of its own.
//
// COST (SPEC-data D2.2, the quantified form of invariant 6): THREE bus reads per game per frame
// (camera phase x2 + heartbeat) plus a cached identity that refreshes at most every 600 frames.
// Six reads per frame total, next to the ~150 build_depth_grid already performs in the same block.
// When the presence pref is off, main.c never calls this at all (the D6.4 cost guard).
#pragma once
#include <stdint.h>

#include "gbacore.h"
#include "gamestate.h"
#include "presence.h"

// The D2.3 identity latch, one per GAME (not per screen — it must survive an X screen swap).
// `name` holds RAW GBA charmap bytes exactly as SaveBlock2 stores them (0xFF-terminated); the
// decode to text is presentation and happens at draw time (presence_name_ascii).
typedef struct {
	uint8_t  have;        // a well-formed identity has been latched
	uint8_t  tried;       // a read has been ATTEMPTED (so "never tried" is always due)
	uint8_t  gender;      // 0 MALE / 1 FEMALE
	uint8_t  name[8];     // raw charmap, 0xFF-terminated
	uint16_t tid;         // visible trainer ID = read16(sb2 + 0x0A)
	int16_t  mapG, mapN;  // the map the latch was taken on (drives the D4.7.1 re-latch)
	uint32_t lastTry;     // producer round of the last ATTEMPT (success or not)
} PresenceIdent;

void presence_ident_reset(PresenceIdent* id);

// Fill one PeerPresence for the game running on `c`, in the parked window.
//   seat  = the producer id stamped into the record (0/1 = same-console core; M4 = the UDS role)
//   p     = profile_for(c), or NULL          -> the record lands with gameId 0 => presence OFF
//   gs    = the GameState game_read ALREADY produced this frame (D2.1: do NOT re-read)
//   id    = the per-game identity latch (may be NULL: identity then reports unavailable)
//   round = the producer's monotonic stamp (g_renderSeq)
void presence_read_fill(PeerPresence* r, int seat, GbaCore* c, const GameProfile* p,
                        const GameState* gs, PresenceIdent* id, uint32_t round);
