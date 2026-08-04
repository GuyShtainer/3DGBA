// presence_ui.h — Phase 15 co-op presence, slice M3: identity + interaction, the pure-C core.
// ============================================================================================
// Spec: docs/phase15-presence/SPEC-avatar.md A4 (identity readout), A5 (the interaction trigger),
// A6 (the surfaces). PHASE.md invariant 8 applies to this half as it did to the other two: every
// DECISION the card and the prompt make lives here, so test/host/test_presence.c proves it on the
// PC and main.c is left with nothing but draw calls.
//
// PURE C (CLAUDE.md rule #4): <stdint.h>, "presence.h" and "gbatext.h" only — no <3ds.h>, no
// citro*, no <stdio.h>, no globals. The small integer formatters below exist precisely so this
// file does not need snprintf: a card field is a fixed-width number, and a hand-rolled formatter
// is both cheaper and immune to a format-string mistake in the one module that renders a value
// read out of somebody's save file.
//
// THE TWO RULES THIS FILE EXISTS TO ENFORCE:
//
//   1. THE CARD IS A PURE READ (PHASE.md invariant 1). Name, trainer ID and gender come out of the
//      PeerPresence record the avatar already uses — no extra RAM read, no second parked-window
//      pass, no link, no state machine. That is exactly why identity is in scope for this phase
//      while trade/battle is not.
//
//   2. NO TRADE, NO BATTLE, NOT EVEN A GREYED BUTTON (A5.5.3). See the block comment above
//      PRES_CARD_UNION_NOTE for the four independent blockers and the recorded design for later.
//      An implementer who adds a "Trade" affordance here is violating a phase invariant, not
//      taking an initiative.
#pragma once
#include <stdint.h>

#include "presence.h"   // PeerPresence, PRES_DIR_*, presence_same_map
#include "gbatext.h"    // the charmap decoder the card and the nameplate share

// ---- A5.1 / A5.2: the meeting predicate ------------------------------------------------------
// Tile-space adjacency in the SAME space as the anchor math (SaveBlock1.pos), plus "facing each
// other". Diagonals are deliberately NOT adjacent: Gen-3 characters cannot face diagonally, so a
// diagonal pair could never satisfy the facing half anyway, and excluding it here keeps the
// predicate one expression.
typedef struct {
	int sameMap;      // both records agree on gameId + (mapGroup, mapNum) and both have a position
	int dx, dy;       // peer tile - host tile (meaningful only when sameMap)
	int adjacent;     // exactly one orthogonal tile apart
	int dirToPeer;    // PRES_DIR_* from host to peer, 0 when not a unit step
	int hostFaceOk;   // the host's facing is a READING, not presence_dir's fold-to-SOUTH default
	int peerFaceOk;   // ...same for the peer
	int facingOk;     // adjacent AND both facings are the correct opposite pair
	int degraded;     // A5.2.1: a facing was unusable, so `meet` fell back to adjacency-only
	int meet;         // THE predicate the prompt and the card ride on
} PresMeet;

// `hostRawFacing` / `peerRawFacing` are the RAW gObjectEvents nibbles main.c already keeps beside
// the records for D2.4.1 (-1 = unavailable). They are needed because PeerPresence.facing is
// ALWAYS 1..4 — presence_fill_core folds an unavailable facing to SOUTH so a wrong offset can only
// cost a wrong sprite direction (SPEC-data D2.4) — so the record alone cannot distinguish "facing
// south" from "we have no idea". A5.2.1 requires that distinction: it is the difference between a
// prompt that never fires on FireRed and one that is slightly eager, and the spec picks eager.
//   usable raw nibble == 1..8 (the four cardinals plus the four diagonals presence_dir folds).
//   -1 (unavailable), 0 (DIR_NONE) and 9..15 (garbage) mean the SOUTH we hold is a fabrication.
void presence_meet(const PeerPresence* host, int hostRawFacing,
                   const PeerPresence* peer, int peerRawFacing, PresMeet* out);

// ---- A5.4.2: the card's one bool -------------------------------------------------------------
// "The card owns no timer and no state machine beyond one bool" — every close condition below is
// already computed elsewhere in the frame, so this is a fold of flags, not a controller.
typedef struct { uint8_t open; } PresCard;

typedef struct {
	int enabled;    // the presence pref (A5.4.4: everything is suppressed when it is off)
	int drawn;      // presence resolved to DRAW for this game this frame — this ONE flag carries
	                //   "the avatar is on screen", "ctx is still the overworld" and "the gate is
	                //   open", because presence_solve's ladder already decided all three
	int meet;       // PresMeet.meet
	int menuOpen;   // our pause menu
	int hudOn;      // this screen's HUD is enabled. A5.4.4 suppresses the prompt AND the card with
	                //   the rest of that screen's chrome, and the FSM carries it rather than the
	                //   draw site so the STATE matches what is visible — a card that is "open" but
	                //   invisible would swallow the next A press to close something nobody saw
	int aEdge;      // KEY_A pressed THIS frame on the focused game (A5.3: OBSERVED, never consumed)
	int bEdge;      // KEY_B likewise
} PresCardIn;

void presence_card_reset(PresCard* c);
int  presence_card_step (PresCard* c, const PresCardIn* in);   // returns the new `open`

// ---- A4.4.2 / A5.4.4 / open question O-A4: which surfaces are up ----------------------------
// ONE policy function rather than the same condition scattered across three draw sites, which is
// what O-A4 ("nameplate always-on or on-approach?") explicitly asks for: when that taste call is
// made on real hardware it is a one-line edit HERE and nothing else moves.
#define PRES_SURF_PLATE  (1u << 0)   // the nameplate pill over the peer's head       (A4.4.1)
#define PRES_SURF_PROMPT (1u << 1)   // the "A - CARD" chip under it                  (A5.4.1)

// `hudOn` = this screen's HUD is enabled (A4.4.2: one user control, both surfaces).
// `haveName` = the decoded name is non-empty (A0.4: nameplate suppressed, avatar still drawn).
// `nearTiles` = Chebyshev tile distance to the peer, for the O-A4 "fade in within N tiles"
// alternative. The SHIPPED policy ignores it — it is a parameter so the alternative is a one-line
// change with a test already pointed at it (TEST 35).
unsigned presence_surfaces(int drawn, int hudOn, int haveName, int meet, int cardOpen, int nearTiles);

// ---- A4.4.4: the Card's text, built once per frame from the record --------------------------
// 8 charmap bytes, worst case 3 UTF-8 bytes each (the two gendered signs), plus the NUL.
#define PRES_CARD_NAME_CAP 25

typedef struct {
	char name[PRES_CARD_NAME_CAP];  // decoded UTF-8; "?" when no identity is latched
	char id  [8];                   // "01234" — A4.3.2's five-digit trainer-card form; "-----" when
	                                //   no identity. The SECRET id (sb2 + 0x0C) is NEVER shown: it
	                                //   is not on a Gen-3 trainer card and surfacing it is a
	                                //   save-data leak with no player value.
	char loc [32];                  // "MAP 3-12   TILE 14,9"
	int  gender;                    // 0 = M, 1 = F, -1 = unknown
	int  haveIdent;                 // 0 => name/id are the placeholders above
} PresCardText;

void presence_card_fill(const PeerPresence* peer, PresCardText* out);

// "M" / "F" / "?" — deliberately ASCII. A4.4.4 draws a gendered glyph here, but the card's text is
// rendered with the BAKED bcfnt faces (Space Grotesk / JetBrains Mono, tools/build_assets.sh),
// whose coverage of U+2642/U+2640 is unverified on this machine, and a missing bcfnt glyph draws
// as nothing. A blank where a value should be is the one failure a VERIFICATION readout must not
// have (A7.2 H11 compares this field against the game's own trainer card). Names still decode
// through gbatext with the real signs, because that is where they actually occur and because the
// nameplate draws them with the 3DS SHARED font, which does cover them.
const char* presence_gender_label(int gender);

// The two fixed lines, here rather than as literals in main.c so the host suite can assert the
// disclosure exists and cannot be quietly dropped in a later edit.
#define PRES_CARD_READONLY_NOTE "same map - read-only"

// *** A5.5.3, BINDING. The card shows `Card` ONLY. It must NOT render a greyed-out "Trade" or
// "Battle" affordance: a greyed button reads as "coming in the next build", and this is not that.
// ONE dim line of body text is the sanctioned disclosure, and this is it. ***
//
// WHY TRADE/BATTLE CANNOT BE BUILT IN THIS PHASE — four independent blockers, any one sufficient
// (A5.5.1); recorded here so the next reader does not re-derive them:
//   1. PHASE.md invariant 1 FORBIDS THE WRITE. Every candidate route (setting gSpecialVar_0x8004
//      so TryTradeLinkup/TryBattleLinkup runs; warping both games into the Union Room) is a WRITE
//      to emulated RAM. That invariant is stated as absolute and as the reason the overlay design
//      was chosen over injection. It is a rule, not a risk, and it has no workaround.
//   2. A trade is a LIVE LINK STATE MACHINE, not a callable function: it needs
//      GetLinkPlayerCount() >= 2, populated gLinkPlayers[], a matching gLinkType, both games idle
//      in the overworld, and a real block exchange over gBlockSend/RecvBuffer. Poking callback2
//      skips the linkup task that establishes gLinkPlayers[] => near-certain crash.
//   3. THE CORE CANNOT MAKE THE CALL. gbacore.h exposes read8/16/32 and write8/16 — no write32, no
//      ROM-call trampoline, no way to invoke a `static` ROM function, and every driver function
//      the native path needs is static in the decomp.
//   4. THE LINK PATH IS FROZEN pending hardware run #13. PHASE.md defers even the presence BEACON
//      (M4) for that reason; reaching into the trade path from a cosmetic overlay phase is the
//      worst possible moment to touch it.
//
// THE LATER DESIGN, so it is not re-derived and so the profile work can be costed (A5.5.2):
//   * preferred route (a): warp both games into the REAL Union Room via the game's own specials
//     (RunUnionRoom / TryBecomeLinkLeader / TryJoinLinkGroup, data/specials.inc), where every map
//     assumption TryInteractWithUnionRoomMember makes is already satisfied, and let the existing
//     emulated SIO link carry the trade;
//   * experimental route (b): set gSpecialVar_0x8004 and let TryTradeLinkup/TryBattleLinkup run
//     from arbitrary route coordinates — "the fragile sub-path": the map-state assumptions a
//     cable-club seat guarantees are not met on a route tile;
//   * SYMBOLS THAT MUST FIRST JOIN GameProfile, none of which exist today (gamestate.h has
//     sb2ptr from M0 and nothing else of these): gLinkType, gSpecialVar_0x8004, gBlockSendBuffer,
//     gBlockRecvBuffer, gLinkPlayers, and the Union-Room / linkup special entries — PER GAME CODE,
//     byte-verified, each marked verified vs verify-on-hw-pending. (gLinkType is partially known:
//     EM 0x020229c6 / FR 0x0202271a from the auto-mode work — a starting point, not a
//     verification.)
//   * BATTLE IS A DIFFERENT TIER ENTIRELY: docs/kb/wireless-strategy.md puts battles in the
//     input-sync / mirrored-pair tier, not the local-termination tier. Celio has no battle
//     synthesis and the observed failure ("chose differently") is a linkType mismatch, i.e.
//     expected-unsupported. A "Battle" button here would promise something the whole link strategy
//     does not yet deliver.
#define PRES_CARD_UNION_NOTE "trade & battle use the game's own Union Room - not from here"

// ---- A4.4.1 / A5.4.1: where the two head pills sit ------------------------------------------
// FIX PASS (review finding 8). These were two independent clamps in main.c, and independent clamps
// on a STACK collapse it: `plateY = headY - 30` and `promptY = headY - 15` were each clamped to
// y >= 2, so for any peer roughly 3+ tiles above the player (hy < 17, i.e. 3 of the ~10 on-screen
// rows the D5.7 cull admits) BOTH pills landed on y = 2 and drew one on top of the other. They also
// landed INSIDE the 0..16 px HUD bar, which is drawn AFTER them (main.c's chrome block) and is
// translucent — so the collision with the bar's game name / FOCUS / clock / TILT / CO-OP text was
// guaranteed rather than incidental. Clamping the stack AS A UNIT fixes both: the pills keep their
// fixed 15 px separation at every position on the screen, and they never enter the bar.
//
// Pure C and host-tested (TEST 38) for the usual reason: this is arithmetic, and arithmetic in
// main.c is arithmetic no PC test can reach.
#define PRES_PILL_H    15.0f   // one pill row: the 13 px chip + 2 px of air
#define PRES_PILL_TOP  18.0f   // the HUD bar is 14 px + a 2 px focus rule = 16; +2 px of air.
                               //   Unconditional, and that is correct: presence_surfaces returns 0
                               //   unless hudOn, so a pill can only exist on a screen whose bar is
                               //   drawn. (The right eye draws no chrome, but it draws the pills at
                               //   the SAME y as the left — two eyes that disagree would be
                               //   binocular rivalry, which is the defect A2.5.2 is about.)
#define PRES_PILL_EDGE  2.0f   // horizontal screen margin

// A5.4.1's prompt label, here rather than as a literal in main.c so the MEASUREMENT and the DRAW
// cannot drift apart now that the width is computed at a different site from the chip (finding 9).
#define PRES_PROMPT_TEXT "A - CARD"

// Centre a pill of width `w` on the head point, then keep it on the panel. RIGHT edge first, LEFT
// second, so a pill wider than the screen is pinned at the left rather than pushed off it — the
// opposite order silently produces a negative x for the one input that could break it.
float presence_pill_x(float headX, float w, float screenW);

// The two pill tops, stacked UPWARD from the head so neither covers the avatar's face (prompt
// immediately above the head, nameplate above the prompt), then translated AS A UNIT so the whole
// stack clears the HUD bar and stays on the panel. Either pointer may be NULL; `hasPlate` /
// `hasPrompt` are the PRES_SURF_* bits, and a surface that is not up takes no space.
void presence_pill_y(float headY, int hasPlate, int hasPrompt, float screenH,
                     float* plateY, float* promptY);

// ---- small formatters (so this module needs no <stdio.h>) -----------------------------------
// Both always NUL-terminate within `cap` and return the number of bytes written (== strlen).
int presence_fmt_u5 (unsigned v, char* out, int cap);          // zero-padded 5 digits, "%05u"
int presence_fmt_int(int v, char* out, int cap);               // plain decimal, "-" for negatives
