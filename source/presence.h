// presence.h — Phase 15 co-op presence: the DATA half (SPEC-data.md), slice M0.
// ============================================================================================
// PURE C (CLAUDE.md rule #4 / PHASE.md invariant 8 / SPEC-data D6.1): <stdint.h>, <string.h> and
// "fieldgate.h" are the ONLY includes — no <3ds.h>, no citro*, no gamestate.h, no gbacore.h, no
// tilt.h, no <math.h>, no globals — so presence.c dual-compiles on the PC host harness
// (test/host/test_presence.c) exactly the way tilt.c does. Everything that touches game RAM
// (gbacore_read*, GameProfile) lives in main.c's `presence_fill` (SPEC-data D6.4, slice M1);
// this module sees plain ints and floats only.
//
// WHAT THIS MODULE IS. Both games already run in our process, so the peer's map, tile, facing and
// identity are a plain RAM read in the existing parked window (main.c, "Workers are parked here ->
// touch RAM access safe") — no network, no ROM patch. Project PM needed a romhack for the
// equivalent because a stock emulator has no game knowledge (docs/kb/external/pm-rom-abi.md §9
// "The crux"); we have that knowledge in gamestate.c's profiles, so the ENTIRE export half of
// their ABI is redundant for us. This module owns the record, the liveness rules, the gate ladder
// and the screen math; SPEC-avatar.md owns the pixels.
//
// THE THREE INVARIANTS THAT SHAPE EVERY LINE HERE (PHASE.md):
//   1. READ-ONLY with respect to both games. Nothing in presence ever writes emulated RAM — that
//      property is why the overlay design was chosen over gObjectEvents[] injection, so it is
//      preserved absolutely. This module physically cannot write: it has no core handle.
//   2. All cross-game reads happen in the EXISTING parked window. No new sync primitive; nothing
//      here is thread-safe and nothing here may be made so (D3.4.1).
//   3. TRANSPORT-AGNOSTIC. ONE record type, ONE publish call. Same-console fills PeerPresence from
//      sibling EWRAM today; M4 (deliberately out of this phase until HW run #13 reports) fills the
//      identical 48 bytes from a UDS beacon. Nothing downstream of presence_publish may learn
//      which — that is the whole reason the record is wire-shaped before anything puts it on a
//      wire.
//
// THE HONEST CEILING (state it, do not oversell): the GBA engine does not know the avatar exists.
// No collision, no z-ordering behind buildings, no talking to them. You see your friend walk
// around your map in real time; you cannot bump them.
#pragma once
#include <stdint.h>

#include "fieldgate.h"   // field_state_ok — the ONE field predicate, shared with the tilt gate

// ---- the frame geometry this module computes in (SPEC-data §0.3 / D5) -----------------------
// GBA FRAME SPACE, 240x160 — the same space calc_xform (main.c:720) and tilt_project (tilt.h:134)
// consume. main.c's GBA_W/GBA_H must agree; the M1 wiring pins that with a _Static_assert at the
// call site, exactly as GCTX_OVERWORLD == TILT_CTX_FIELD is pinned (main.c:1130).
#define PRES_FRAME_W 240
#define PRES_FRAME_H 160

// The FOOT ANCHOR of a peer standing on the host's own tile, in frame space (D5.4).
// X: corroborated twice — POP3D_PLAYER_GX 112 (main.c:691) + 8 for the cell centre, and pret's
//    chain 16*(ccX - px) = 16*7 = 112, then sprite->x += 8 (event_object_movement.c:1853), then
//    centerToCornerVecX = -8 for a 16-wide graphic => left edge 112, centre 120. VERIFIED-SRC.
// Y: the pret chain only — 16*(ccY - py) + SUB + 16 + 2*(-16) + (-32 - 8) = 56 is the drawn TOP of
//    the 32-tall art, so the foot baseline is 56 + 32 = 88 (event_object_movement.c:1850-1854,
//    field_camera.c:445/453/462). main.c:691-692's comment implies 96 instead, but that constant
//    (POP3D_PLAYER_GY 64 + 32) is UNUSED — grep finds only its definition — so it is a comment,
//    not a validated value, and it does not settle the question.
// *** PRES_ANCHOR_Y IS THE ONE NUMBER IN THIS MODULE THAT COULD BE 8 px WRONG. VERIFY-ON-HW: the
// M2 calibration run (SPEC-data D5.4.1 / Open Q2) stands both players on the SAME TILE of the same
// map and photographs the overlay against the host's own character; an 8 px error is unmistakable
// there. Promote or correct THIS ONE NUMBER in BUILDLOG.md — nothing else changes. ***
#define PRES_ANCHOR_X 120.0f
#define PRES_ANCHOR_Y  88.0f

// MAP_OFFSET — the Gen-3 border bias. gObjectEvents[].currentCoords tracks SaveBlock1.pos + 7;
// touch.c:207 applies the same +7 (gx_ = wx + 7) and main.c:2346 relies on the same identity when
// it matches OAM sprites to object events by grid tile.
#define PRES_MAP_OFFSET 7

// ---- the wire record (D3.2) -----------------------------------------------------------------

#define PRES_REC_BYTES 48   // == Project PM's overworld record size (pm-rom-abi.md §3), field for
                            // field, with our game knowledge filling the slots their ROM patch had
                            // to export. Fixed so M4 can memcpy it into a beacon with no repacking.
#define PRES_REC_VER   1    // a wire that outlives a build

// Map universes (D4.3) — the gate coop-shared-overworld.md missed. (mapGroup, mapNum) is only
// meaningful WITHIN one game's map table: Emerald's (3,12) is not FireRed's (3,12). Without this
// rule an Emerald + FireRed pair — the user's actual carts — would happily draw a peer walking
// around an unrelated map, and it would look like a mysteriously wrong position rather than the
// category error it is. FR<->LG is the ONE cross-title pair allowed, because they share both the
// RAM map (docs/kb/gen3-ram-touch.md:14) and the map table. Ruby/Sapphire, if ever added, get
// their own id and must be argued separately against Emerald — do NOT assume PRES_GAME_HOENN.
#define PRES_GAME_NONE  0
#define PRES_GAME_HOENN 1   // BPEE
#define PRES_GAME_KANTO 2   // BPRE, BPGE

// flags (D3.3)
#define PRES_F_SB1VALID (1u << 0)   // save loaded: px/py/map* meaningful
#define PRES_F_FIELD    (1u << 1)   // the producer's field_state_ok verdict WITHOUT the textDlg
                                    //   term: profile + ctx == overworld + the save is loaded.
                                    //   "this game's position is meaningful" (PHASE.md invariant 5)
#define PRES_F_OBJOK    (1u << 2)   // gObjectEvents[0] agreed with SaveBlock1 pos at fill time
#define PRES_F_IDENT    (1u << 3)   // name/TID/gender latched (else identity unavailable)
#define PRES_F_CAM      (1u << 4)   // subX/subY are real (the profile has fieldCamera)
#define PRES_F_HB       (1u << 5)   // hb is real (the profile has hbCtr)
#define PRES_F_TEXT     (1u << 6)   // FIX PASS (review finding 6): a field textbox is up on THIS
                                    //   game. Carried as its own bit rather than folded into
                                    //   PRES_F_FIELD, because the two sides of the gate want
                                    //   OPPOSITE answers and the consumer could not separate them
                                    //   otherwise:
                                    //     SELF  — a textbox is drawn IN THE FRAME the avatar
                                    //             composites over, so a peer standing on the lower
                                    //             rows would be painted on top of the host's own
                                    //             dialog box. That is D4.7.3's "no stray sprite
                                    //             over their menu" rule, so SELF still closes.
                                    //     PEER  — their textbox is on THEIR screen and changes
                                    //             nothing about our frame; they are standing still
                                    //             on a valid tile, so their position stays perfectly
                                    //             meaningful. Gating on it made a friend VANISH for
                                    //             the length of every sign and every NPC line (the
                                    //             D4.7 hold only covers 0.5 s of it) — a visible
                                    //             defect in the phase's headline feature, inherited
                                    //             from tilt's G8, whose reason ("do not tilt a
                                    //             dialog") is about the HOST's own screen geometry
                                    //             and has no presence-side analogue.
                                    //   Residual, accepted and logged: Open Q4's unmodelled
                                    //   sHorizontalCameraPan mostly coincides with scripts, i.e.
                                    //   with textDlg. If a hardware run shows the avatar SLIDING
                                    //   against the ground during a scripted pan, this bit is what
                                    //   lets the CSV/HUD identify those frames — and the fix is
                                    //   D1.4's spriteCoordOff promotion, not re-closing the gate.

// DIR_* — pret include/constants/global.h:137-145, matching gamestate.h:119's "1=D 2=U 3=L 4=R".
#define PRES_DIR_NONE  0
#define PRES_DIR_SOUTH 1
#define PRES_DIR_NORTH 2
#define PRES_DIR_WEST  3
#define PRES_DIR_EAST  4

// THE record. Every field is naturally aligned at a fixed offset and the whole thing is
// _Static_assert-ed to 48 bytes. LITTLE-ENDIAN BY CONSTRUCTION (both ends are ARM LE); no
// byte-swap layer is specified and none may be added silently (D3.2).
typedef struct {
	/*0x00*/ uint8_t  ver;         // PRES_REC_VER
	/*0x01*/ uint8_t  seat;        // producer id: 0/1 same-console core; M4 = UDS role (PM "role")
	/*0x02*/ uint8_t  flags;       // PRES_F_*
	/*0x03*/ uint8_t  ctx;         // GameCtx as a plain int — compared to FIELD_CTX_OVERWORLD only,
	                               //   so no enum crosses this boundary
	/*0x04*/ int16_t  px, py;      // SaveBlock1.pos = the CAMERA tile        (PM +0x04/+0x06 x/z)
	/*0x08*/ int16_t  objX, objY;  // gObjectEvents[0].currentCoords (= px+7/py+7 in the field);
	                               //   -1 = slot inactive/unmapped => the P-G7 gate is UNAVAILABLE
	/*0x0C*/ uint32_t hb;          // gMain.vblankCounter2                 (PM +0x0C frameCounter)
	/*0x10*/ int8_t   subX, subY;  // gFieldCamera.x/y phase, -15..15                 (D1.3/D5.3)
	/*0x12*/ uint8_t  facing;      // DIR_* 0..4 after presence_dir folding
	/*0x13*/ uint8_t  gender;      // 0 MALE / 1 FEMALE                  (PM +0x18 "appearance")
	/*0x14*/ uint8_t  avatarFlags; // PLAYER_AVATAR_FLAG_* — RESERVED, ships 0 (D1.6: gPlayerAvatar
	                               //   is NOT in the profile until a bike/surf art variant exists)
	/*0x15*/ int8_t   mapGroup;    // s8 in pret (struct WarpData); COMPARED, never arithmetic
	/*0x16*/ int8_t   mapNum;
	/*0x17*/ uint8_t  gameId;      // PRES_GAME_* map universe, NOT the raw 4-char game code
	/*0x18*/ uint16_t tid;         // visible trainer ID = read16(sb2 + 0x0A)
	/*0x1A*/ uint16_t mapLayoutId; // SaveBlock1 + 0x32 — corroboration/logging only (D4.4); it must
	                               //   NOT gate the draw (mid-warp the two fields update at
	                               //   different instants and a gate would blink)
	/*0x1C*/ uint32_t round;       // PRODUCER's monotonic stamp (frames): ordering + wedge
	/*0x20*/ uint8_t  name[8];     // GBA charmap, 0xFF-terminated, carried RAW (D3.2.1) — decoding
	                               //   is presentation, and raw bytes keep the record independent
	                               //   of our font/UI (and identical to celiolink_payloads.h:35)
	/*0x28*/ uint8_t  rsv[8];      // zeroed; M4 growth room inside the fixed 48 B
} PeerPresence;
_Static_assert(sizeof(PeerPresence) == PRES_REC_BYTES, "PeerPresence is the 48-byte wire record");

// ---- liveness + staleness (D3.5, D3.6) ------------------------------------------------------

#define PRES_MAX_PEERS 1     /* 2-player today. The array + seat keying is PM's per-role array
                                shape (pm-rom-abi.md §4, "owImp + (r-1)*48"), so the flagged 3-4
                                player work (HANDOFF Next steps #3, deferred until 2-player is
                                fully clean) is a BOUND CHANGE, not a redesign. */

#define PRES_LIVE_NONE      0   /* never seen, or age > PRES_STALE_FRAMES */
#define PRES_LIVE_CONNECTED 1   /* record fresh, but the peer is NOT game-active */
#define PRES_LIVE_ACTIVE    2   /* record fresh AND game-active AND heartbeat advancing */

#define PRES_STALE_FRAMES 180u   /* ~3 s @60. PM ages peer bits out after exactly 180 frames
                                    (pm-rom-abi.md §7.3): long enough to survive a hiccup, short
                                    enough that a gone peer does not linger as a ghost. */
#define PRES_HOLD_FRAMES   30u   /* a peer that fails the FIELD/OBJ gate keeps its LAST GOOD anchor
                                    this long before it disappears — one bad frame (a warp
                                    boundary, a torn read, a frame inside a full-screen fade) must
                                    not blink the avatar (D4.7.2). */
#define PRES_WEDGE_FRAMES 180u   /* heartbeat frozen this long while the record is FRESH => the
                                    peer's game is wedged/paused. PM's crash-catcher rule verbatim
                                    (pm-rom-abi.md §3, "+0x0C frozen >180 frames"). */

// ---- the screen math constants (D5.5, D5.6, D5.7) -------------------------------------------

#define PRES_SMOOTH_A       0.35f  // single-pole smoothing for LOW-RATE (age > 0) sources only:
                                   //   ~90 % closed in 6 frames. Same-console age is always 0 and
                                   //   the filter is a bit-exact no-op there (D5.5, TEST 14).
#define PRES_SNAP_PX        24.0f  // 1.5 tiles: a warp/respawn/dropped beacon must TELEPORT, not
                                   //   glide across the map
#define PRES_CULL_M          0.0f  // rect-cull margin, frame px (D5.7.2). FIX PASS (review finding
                                   //   5): SPEC-data D5.7 specified 8, and 8 made `draw` LIE. The
                                   //   renderer's clip (presence_art_clip) trims at exactly
                                   //   [0,240]x[0,160], so any nonzero margin leaves a band where
                                   //   presence_solve returns draw = 1, the CO-OP chip lights
                                   //   accent, the M1 readout prints `ok @...` — and nothing is on
                                   //   the screen. Worse, the band lands on WHOLE-TILE positions the
                                   //   player crosses constantly (dTileX = +-8 puts the art rect at
                                   //   x = 240 / -16 exactly; dTileY = -6 puts it at y = -40..-8),
                                   //   so it inverts A7.2 H1's triage rule ("if the chip is dim this
                                   //   is a data/gate problem, not a draw problem") on ordinary
                                   //   frames. It also kept the walk accumulator running for an
                                   //   invisible peer, against A2.6.4. 0 makes the two agree exactly
                                   //   for every integer anchor (TEST 16b pins that).
                                   //   AND IT IS STILL RIGHT UNDER TILT: tilt_project maps the flat
                                   //   240x160 frame ONTO the tilted trapezoid, so a peer whose FLAT
                                   //   art rect misses the flat frame also misses the tilted image.
                                   //   D5.7.1's rule ("always cull against the flat frame; the
                                   //   render half may cull further") is unchanged — what changed is
                                   //   that the render half no longer culls MORE than this does.
#define PRES_TELEPORT_TILES  8     // |dpx| + |dpy| on ONE map between consecutive records (D5.6)
#define PRES_OBJ_TOL         1     // P-G7 Manhattan tolerance (D4.5) — NOT exact equality: pos
                                   //   advances inside CameraMove at the first frame of a step
                                   //   (field_camera.c:415) while currentCoords advances in the
                                   //   object's own movement action, so a one-frame one-tile
                                   //   disagreement is NORMAL and must not blink the avatar.
                                   //   Tolerance 1 still rejects a torn-down field (objX = -1) and
                                   //   a stale/garbage slot. If hardware shows PRES_OFF_OBJ firing
                                   //   during ordinary walking, widen to 2 — do NOT delete the
                                   //   gate, it is the only discriminator against the ctx
                                   //   fall-through (fieldgate.h D4.6.2 / Open Q3).

// ---- the gate ladder's reason codes (D4.1) --------------------------------------------------
// "It did not draw" must always answer "why" without a rebuild: every rule returns a distinct code
// that the M1 HUD prints and the gs log records.
//
// Mapping to SPEC-data D4.1's P-G1..P-G9 (this ladder is a strict REFINEMENT — same order, same
// short-circuit, two rules split so the D4.7 hold can tell the sides apart):
//   P-G1 -> DISABLED   P-G2 -> MENU   P-G3 -> LINK   P-G4 -> NOPROF   P-G5 -> UNIVERSE
//   P-G6 -> SELF    (self side) + FIELD (peer side)
//   P-G7 -> SELFOBJ (self side) + OBJ   (peer side)
//   P-G8 -> MAP        P-G9 -> LIVE
//   (+ CULL, which is not a gate rule but the D5.7 off-screen result)
// The split is load-bearing: D4.7 holds the last good anchor when the PEER's field/obj gate blinks
// but stops IMMEDIATELY when the HOST's does, because the host's screen is the frame the user is
// actually looking at and a stray sprite over their menu is the failure that rule prevents.
#define PRES_OFF_NONE     0   // drawing
#define PRES_OFF_DISABLED 1   // P-G1  the pref is off
#define PRES_OFF_MENU     2   // P-G2  our pause menu is up; the frame is not the game
#define PRES_OFF_LINK     3   // P-G3  linkOn || netOn || wlOn (D4.8)
#define PRES_OFF_NOPROF   4   // P-G4  either game unmapped, or no peer record published yet
#define PRES_OFF_UNIVERSE 5   // P-G5  map-universe mismatch (Emerald vs FireRed)
#define PRES_OFF_SELF     6   // P-G6, SELF side (not in the field, or a textbox is up on OUR frame)
                              //   — no hold, stop immediately (D4.7.3)
#define PRES_OFF_FIELD    7   // P-G6, PEER side — holdable
#define PRES_OFF_OBJ      8   // P-G7, PEER side — holdable
#define PRES_OFF_MAP      9   // P-G8  different (mapGroup, mapNum)
#define PRES_OFF_LIVE    10   // P-G9  liveness != PRES_LIVE_ACTIVE
#define PRES_OFF_CULL    11   // D5.7  on the same map but off this screen
#define PRES_OFF_SELFOBJ 12   // FIX PASS (review finding 7): P-G7, SELF side — split out of
                              //   PRES_OFF_SELF so the readout can tell "the host is not in the
                              //   field" from "the host's gObjectEvents[0] disagrees with its
                              //   SaveBlock1 pos". The second is what a WRONG mapObjects address
                              //   would look like, and reporting it as `self` reads as a state
                              //   problem rather than a profile problem. (The FR/LG suspicion the
                              //   finding rests on is REFUTED — SPEC-data D1.8.1: pokefirered.sym
                              //   :205 and pokeleafgreen.sym:205 both give gObjectEvents =
                              //   0x02036E38, and 0x02037078 is gPlayerAvatar, the NEXT symbol —
                              //   but the readout should still be able to say so.) Appended at the
                              //   END so every shipped code keeps its value in the D3 CSV.
#define PRES_OFF__COUNT  13

// ---- state (D3.4) ---------------------------------------------------------------------------
// Consumer-side state. NOT a wire type — it may grow freely; only PeerPresence is fixed.
typedef struct {
	PeerPresence rec        [PRES_MAX_PEERS];
	uint32_t     seenRound  [PRES_MAX_PEERS];  // tier 1: last publish of ANY record
	uint32_t     activeRound[PRES_MAX_PEERS];  // tier 2: last publish that was GAME-ACTIVE. PM
	                                           //   learned tier 2 the hard way: a lobby idler made
	                                           //   the game flash "wireless connected"
	                                           //   (pm-rom-abi.md §7.4, roleSeenAt vs gameSeenAt).
	                                           //   Also M4's "peer newly game-active => resend the
	                                           //   on-change channels" trigger (PM §7.5).
	uint32_t     hbVal      [PRES_MAX_PEERS];  // last heartbeat value seen
	uint32_t     hbRound    [PRES_MAX_PEERS];  // consumer round at which the heartbeat last CHANGED
	uint8_t      have       [PRES_MAX_PEERS];  // a record was ever published
	float        smX        [PRES_MAX_PEERS];  // dead-reckoning filter state (D5.5)
	float        smY        [PRES_MAX_PEERS];
	uint8_t      haveSm     [PRES_MAX_PEERS];
	uint8_t      snapNext   [PRES_MAX_PEERS];  // a warp/teleport/first-record was seen at publish:
	                                           //   the next solve must SNAP, never glide (D5.6)
	float        holdX      [PRES_MAX_PEERS];  // last good anchor + its round (the D4.7 hold)
	float        holdY      [PRES_MAX_PEERS];
	uint32_t     holdRound  [PRES_MAX_PEERS];
	uint8_t      holdHave   [PRES_MAX_PEERS];
	int16_t      lastPx     [PRES_MAX_PEERS];  // teleport/warp detector inputs (D5.6)
	int16_t      lastPy     [PRES_MAX_PEERS];
	int8_t       lastMapG   [PRES_MAX_PEERS];
	int8_t       lastMapN   [PRES_MAX_PEERS];
	// --- diagnostics counters (LOGGING ONLY; never gate on these) ---
	uint32_t     mapChgN    [PRES_MAX_PEERS];  // map changes observed (each clears filter + hold)
	uint32_t     teleN      [PRES_MAX_PEERS];  // D5.6 teleports observed (each snaps the filter)
	uint32_t     dropN      [PRES_MAX_PEERS];  // records dropped by the newest-wins rule (D3.4.2);
	                                           //   same-console this must stay 0, over a radio it
	                                           //   is the reorder count
	uint32_t     round;                        // CONSUMER's clock (render frames)
} PresenceState;

// ---- solve: input and output (D6.2) ---------------------------------------------------------

typedef struct {          // filled from state ALREADY in hand in the parked window
	int          enabled;     // the user pref                                            (P-G1)
	int          menuOpen;    // OUR pause menu                                           (P-G2)
	int          linkAny;     // linkOn || netOn || wlOn                                  (P-G3)
	int          slot;        // which peer (0 .. PRES_MAX_PEERS-1)
	PeerPresence self;        // the game ON THIS SCREEN, the SAME record type as the peer
} PresenceIn;

typedef struct {
	int   draw;      // 0 => draw NOTHING. Never "draw something approximate".
	int   reason;    // PRES_OFF_* whenever the ladder closed — INCLUDING the D4.7 hold frames,
	                 //   where draw and held are both 1 (so the HUD can say "held: field").
	                 //   Consumers check `draw` first, always.
	int   liveness;  // PRES_LIVE_*
	float footX, footY;  // GBA FRAME space (240x160): the FOOT ANCHOR. gen1-render.md finding 2 —
	                     //   the render half projects THIS point through tilt_project and
	                     //   TRANSLATES the upright art by the difference; it never scales.
	int   dir;       // PRES_DIR_SOUTH..PRES_DIR_EAST after folding + clamping
	int   moving;    // the peer's camera phase is nonzero this frame => run the walk cycle
	int   held;      // this anchor is a D4.7 hold (last good), not a live read
	int   dTileX, dTileY;  // tile delta — the M1 HUD line, filled whenever both sides have a real
	                       //   position, gate or no gate, so a closed gate is still diagnosable
	int   gender;    // 0/1; 0 when identity is unavailable
} PresenceOut;

// ---- API (D6.3) -----------------------------------------------------------------------------
// NONE of these is thread-safe and none may be made so (D3.4.1): every call happens on the render
// thread, inside or after the existing parked window. M4's RX thread will hand its 48 bytes to the
// render thread through netlink's existing event queue (exactly as net_event_recv already does)
// and the RENDER thread publishes.

void presence_reset      (PresenceState* ps);
void presence_begin_round(PresenceState* ps, uint32_t round);                    // once per frame
void presence_publish    (PresenceState* ps, int slot, const PeerPresence* rec); // THE seam
int  presence_solve      (PresenceState* ps, const PresenceIn* in, PresenceOut* out);  // == draw

int      presence_liveness (const PresenceState* ps, int slot);   // PRES_LIVE_*
uint32_t presence_hb_stall (const PresenceState* ps, int slot);   // frames since the hb moved
int      presence_same_map (const PeerPresence* a, const PeerPresence* b);

// Producer-side helpers (pure C, so main.c's presence_fill stays a dumb reader):
void presence_rec_init   (PeerPresence* r, int seat, int gameId);  // zero + ver + objX/objY = -1
int  presence_game_id    (const char* code4);       // "BPEE"->HOENN, "BPRE"/"BPGE"->KANTO, else 0
int  presence_obj_agree  (const PeerPresence* r);   // 1 agrees / 0 disagrees / -1 UNAVAILABLE.
                                                    //   -1 is NOT a pass: P-G7 is skipped and
                                                    //   reported unavailable (D4.5.1), because an
                                                    //   unmapped address must never read as "ok".

// ---- slice M1: the PRODUCER, split into a pure core + a thin reader (D2.2/D3.2) --------------
// SPEC-data D6.4 puts `presence_fill` in main.c because it is the one function that touches
// gbacore_read* and GameProfile. Slice M1 splits that in two, which is a strict improvement and
// the house rule #4 shape ("pure-C cores ... so a host test harness dual-compiles them"):
//
//   presence_read_fill (presence_read.c) = the ONLY code that touches game RAM. It performs the
//        three new reads (D2.2), keeps the D2.3 identity latch, and fills the flat POD below.
//   presence_fill_core (HERE, pure C)    = every DECISION about what the record says: the flag
//        assembly, the shared field predicate, the sub-tile guard, the facing fold, the objX/objY
//        "gate unavailable" sentinel. All of it is therefore host-tested (TEST 5b/17b) instead of
//        living in the one file the PC harness can never compile.
//
// Every field is a value the caller ALREADY has in hand inside the parked window: the GameState
// game_read produced (D2.1 — presence must not call game_read again) plus the three new reads.
typedef struct {
	int      ok;         // this game has a profile (GameState.valid)                  (tilt G5)
	int      ctx;        // GameCtx as a plain int — compared to FIELD_CTX_OVERWORLD only
	int      sb1Valid;   // gSaveBlock1Ptr deref valid
	int      textDlg;    // a field textbox is up (sFieldMessageBoxMode/sMessageBoxType != 0)
	int      px, py;     // SaveBlock1.pos; -1 when sb1 is not ready (game_read's sentinel)
	int      mapGroup, mapNum;   // SaveBlock1.location; -1 when unavailable
	int      objX, objY; // gObjectEvents[0].currentCoords; -1 = slot inactive OR mapObjects == 0,
	                     //   which is the D4.5.1 "gate UNAVAILABLE" case, never a pass
	int      facing;     // RAW nibble as game_read reports it (-1 = N/A); folded here, not there
	int      camOk;      // the profile has fieldCamera (else the sub-tile term degrades to 0)
	int      camX, camY; // gFieldCamera + 0x10 / + 0x14, raw — the +-15 guard is applied HERE
	int      hbOk;       // the profile has hbCtr (else wedge detection is disabled, not wrong)
	uint32_t hb;         // gMain.vblankCounter2
	int      identOk;    // a well-formed identity is latched for this game (D2.3)
	const uint8_t* name; // 8 RAW charmap bytes, or NULL => leave the empty (0xFF) name
	int      gender;     // 0 MALE / 1 FEMALE (anything else is folded to MALE)
	int      tid;        // visible trainer ID = read16(sb2 + 0x0A)
	uint32_t round;      // the producer's monotonic stamp (render frames)
} PresenceSrc;

void presence_fill_core(PeerPresence* r, int seat, int gameId, const PresenceSrc* s);

// D2.3 — identity is CACHED, NOT POLLED. Name/gender/TID are constant for the life of a save, so
// the deref + 4 reads happen only when (a) nothing is latched yet, (b) the map changed (D4.7.1
// re-latches there), or (c) the refresh cadence elapsed. Latch the first well-formed result and
// keep it: a name that flickers mid-session is worse than one that is a map stale — the same
// lesson celiolink's CheckLinkPlayersMatchSaved identity latch cost us (MEMORY, run #6).
#define PRES_IDENT_RETRY_FRAMES    60u   // nothing latched yet: retry ~1 s (a save that is not
                                         //   loaded at boot must not cost a 10 s blank nameplate)
#define PRES_IDENT_REFRESH_FRAMES 600u   // == GS_HEARTBEAT_FRAMES (gamestate.c:212)
int  presence_ident_due  (int have, int mapChanged, uint32_t framesSinceTry);

// The engine's own sub-tile term (D5.3), exported because TEST 3's whole-step continuity sweep is
// the reason it exists:  SUB(c) = -c + 16*sgn(c)   for c in (-16, 16); SUB(0)=0, SUB(1)=15,
// SUB(8)=8, SUB(15)=1, SUB(-1)=-15. Pure function — the +-15 sanity guard is applied by the
// CALLER (presence_solve clamps out-of-range phases to 0, mirroring main.c:854-855).
int  presence_sub        (int camPhase);
int  presence_dir        (int rawFacingNibble);                  // D2.4 folding + clamp
int  presence_name_ascii (const uint8_t name[8], char out[9]);   // returns the length written
const char* presence_off_reason(int reason);                     // PRES_OFF_* -> short HUD string
