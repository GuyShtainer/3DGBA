#pragma once
// progseq.h — THE INTERACT SEQUENCER, extracted pure (phase 25, lane C2).
//
// WHY THIS FILE EXISTS. The phase-24 adversarial audit's finding O2: "no host suite compiles
// source/touch.c", so six of the seven phase-24 fixes shipped with **zero** regression cover.
// touch.c cannot be host-compiled and should not be — it is the seam between <3ds.h>, citro2d and
// the emulator core. But the seam is not what those fixes changed. They changed DECISIONS, and a
// decision that is a function of (state in, observations in) -> (action out) is exactly the shape
// this project has extracted four times already: fieldpath.c, fieldtrav.c, touchgeom.c, uihit.c.
//
// So this file owns the traversal executor's phase machine — the thing that walks a planned
// FtProgram, turns to face an obstacle, presses A at it, answers the game's own yes/no and waits
// for the field move to land. It performs NO reads and NO writes: touch.c reads the game
// (facing, the surf bit, the object slot, the message-box flag), calls one step, and carries out
// the ACTION it is handed. There is no second copy of this logic — the shipped executor IS this
// file, which is the property that makes test_progseq a barrier rather than a decoration.
//
// PURE-C, per CLAUDE.md rule #4: <stdint.h>/<string.h> and fieldtrav.h (itself header-free) only.
#include <stdint.h>
#include "fieldtrav.h"      // FtProgram / FtMove / FT_HM_*

// --- the phases, unchanged from the shipped executor -------------------------------------------
//   WALK   walking a plain step of the plan
//   FACE   holding the direction until the GAME says the avatar faces the obstacle
//   A      settling, then the A pulse aimed at it
//   DLG    waiting for the script to talk ("This tree looks like it can be CUT down!")
//   YESNO  advancing the message box(es) until the yes/no menu exists
//   ANSWER pressing YES, on a level, because Gen 3's yes/no ignores its first five frames
//   DONE   waiting for the move to actually land (the surf bit, or the object going inactive)
enum { TPH_WALK = 0, TPH_FACE, TPH_A, TPH_DLG, TPH_YESNO, TPH_ANSWER, TPH_DONE };

// --- how a program ends. TPE_NONE = it did not. ------------------------------------------------
enum { TPE_NONE = 0, TPE_ARRIVED, TPE_HANDOFF, TPE_CANCEL, TPE_KEY, TPE_MAPCHANGE, TPE_CTX,
       TPE_TIMEOUT, TPE_STALL, TPE_UNEXPECTED, TPE_ELIG, TPE_REPLAN };

// All VERIFY-ON-EMULATOR then VERIFY-ON-HW, exactly like TERM_FRAMES (touch.c): they are counted
// in EMULATED frames, so they are independent of the 3DS frame rate, but a wireless session's
// degraded emulation rate still has to be re-measured (SPEC-door Open Q6).
#define TP_FACE_FRAMES   8     // fallback turn hold, used ONLY when the facing is unreadable
#define TP_WALK_BUDGET   48    // one tile step; a normal step is ~16 emulated frames
#define TP_DLG_BUDGET    120   // A press -> the field textbox appears
#define TP_YESNO_BUDGET  240   // the textbox(es) -> the yes/no menu ("Want-to-use" can be 2 boxes)
#define TP_DONE_BUDGET   480   // YES -> the cutscene finishes and the world changes
#define TP_ADVANCE_EVERY 24    // A-advance cadence while a message box is on screen
#define TP_MAX_REPLANS   4
// PHASE 24 (lane A, emulator-proven): the yes/no needs a LEVEL, not one edge — see the citation
// block in the TPH_ANSWER case. Pulse cadence, and a hard cap on how many times we may aim at the
// same prompt, so a menu that never closes ends the program instead of drumming A forever.
#define TP_ANSWER_EVERY  8
#define TP_ANSWER_MAX    8
// FACE is closed-loop on the avatar's own facing; this is only the give-up bound, and
// TP_SETTLE_FRAMES lets the turn/bump animation end before the A that aims at it.
#define TP_FACE_BUDGET   90
#define TP_SETTLE_FRAMES 10

// GameCtx, mirrored the way touchgeom.h mirrors it — this file must stay includable by a host test
// with nothing but <stdint.h>. touch.c MAPS its GameCtx onto these three (it does not cast), so
// there is no numeric coupling to break.
enum { PSQ_CTX_OTHER = 0, PSQ_CTX_OVERWORLD, PSQ_CTX_FIELDMENU };

// --- the machine's own state. Plain data: copyable, comparable, dumpable in a test. ------------
typedef struct {
	int on;          // a program is running
	int phase;       // TPH_*
	int frames;      // frames in THIS phase (reset on every transition)
	int aPulse;      // A-frames still owed (the shipped 3-frame pulse shape)
	int answers;     // YES presses aimed at the CURRENT yes/no (capped at TP_ANSWER_MAX)
	int step;        // index into FtProgram.mv[]
	int lpx, lpy;    // the player tile as of the last frame that moved
} ProgSeq;

// --- one frame of the world, as touch.c read it -----------------------------------------------
// Everything here is a value touch.c already reads today. `facing`, `surfing` and `objActive` are
// only CONSULTED in the phase that needs them (FACE / DONE), so touch.c only reads them there —
// the call site says so, and the suite's fixtures leave them at their "unreadable" values
// everywhere else to keep that honest.
typedef struct {
	int ctx;         // PSQ_CTX_*
	int textDlg;     // sFieldMessageBoxMode != 0 — "the script is PRINTING" (not "a box is up")
	int padKeys;     // any physical key held: the player owns the console, always
	int newPress;    // a new touch press this frame
	int mapChanged;  // SaveBlock1.location != the map the program was planned on
	int px, py;      // live player tile (px < 0 = no loaded overworld)
	int facing;      // gObjectEvents[0].facingDirection low nibble: 1 D / 2 U / 3 L / 4 R, -1 = ?
	int surfing;     // gPlayerAvatar PLAYER_AVATAR_FLAG_SURFING
	int objActive;   // the tracked object slot's active:1 bit (the Cut/Smash proof)
	// PHASE 26 / lane W. Two more "the game answers for itself" reads, each consulted by exactly
	// one HM in exactly one phase (TPH_DONE), and each ONE byte on the console.
	int onWaterfall; // gObjectEvents[0].currentMetatileBehavior (+0x1E) is MB_WATERFALL. This is
	                 //   literally the condition the ride loop itself tests (pokeemerald
	                 //   src/field_effect.c:1885 WaterfallFieldEffect_ContinueRideOrEnd), so the
	                 //   ride is over exactly when it goes false — no frame count, no tile count.
	int strengthOn;  // FlagGet(FLAG_SYS_USE_STRENGTH) — the ONLY proof a Strength activation
	                 //   landed, because unlike Cut and Smash nothing on the map changes
	                 //   (data/scripts/field_move_scripts.inc:147 `setflag FLAG_SYS_USE_STRENGTH`).
	// PHASE 29 / lane F — DEFECT X1. "The tile this interact FACES is one a step would ENTER."
	// Read by touch.c in TPH_FACE only: the behaviour of (px,py) + the move's direction, tested
	// with fieldtrav_is_waterfall / fieldtrav_is_current while the player is surfing. It defaults
	// to 0 — "the tile is impassable" — which is the assumption the whole FACE phase was written
	// on and which is true for every obstacle Cut, Rock Smash, Surf and Strength face.
	int faceEnterable;
} ProgObs;

// --- what touch.c must DO about it ------------------------------------------------------------
// Order of service matters and is fixed: needElig first (a refusal cancels everything else in the
// act), then chipRefresh, then end, then replan, then the keys.
typedef struct {
	int keyDir;      // -1 none, else 0..3 in s_keyDir order (R/L/D/U) — hold this direction
	int pressA;      // emit A this frame (a pulse frame drained). Counts one progAKeys.
	int runSpan;     // >0: a plain walk step whose LEG is this many tiles — the caller runs
	                 //     rungeom_decide(runSpan, run_elig()) and ORs in the B
	int writeYes;    // write the yes/no cursor to row 0 (YES) before the pulse lands
	int needElig;    // re-check fieldtrav_usable for `eligHm`; a refusal = end TPE_ELIG, and
	                 //     every other field of this act is then void
	int eligHm;      // ...the move's hm
	int replan;      // re-plan from LIVE state (prog_replan); nothing else is injected this frame
	int end;         // TPE_* — the program is over (0 = still running)
	int farewell;    // queue the one A that closes a dangling textbox AFTER the program is gone
	int swallow;     // swallow the touch that cancelled us
	int stamp;       // the debug mirror should be stamped for this frame
	int stampSurf;   // ...and progSurf specifically (DONE re-reads it, so it is fresh)
	int chipRefresh; // the HUD verb chip must be recomputed (the plan step advanced)
	int answered;    // count one progAnswers (a YES aimed at a predicted prompt)
	int stampStep;   // the step/phase the frame STARTED in — what the shipped per-frame mirror
	int stampPhase;  //   reports (an END stamp reports where it finished; keep the asymmetry)
} ProgAct;

// Clear the machine (the app's all_reset path).
void progseq_reset(ProgSeq* s);

// Arm a freshly planned program: step 0, WALK, from the player's current tile.
void progseq_arm(ProgSeq* s, int px, int py);

// ONE FRAME. Fills `a` (always fully initialised) and advances `s`. See ProgAct for the contract.
void progseq_step(ProgSeq* s, const FtProgram* pr, const ProgObs* o, ProgAct* a);

// The caller's answer to `needElig`: the move is no longer usable, so the program ends. Separated
// out because the eligibility read (a party walk + a flag read) is far too expensive to do on
// every frame just so a pure function can be handed the answer it will usually ignore.
void progseq_elig_fail(ProgSeq* s);

// dir (0 R / 1 L / 2 D / 3 U — s_keyDir order) -> the game's own facing code (1 D / 2 U / 3 L /
// 4 R). Exposed because the FACE loop's whole correctness is this mapping being right.
int progseq_face_of_dir(int dir);
