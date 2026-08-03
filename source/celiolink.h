// celiolink.h — Gen-3 link LOCAL-TERMINATION dongle (the "Celio core") for 3DGBA.
// ============================================================================================
// CLEAN-ROOM REIMPLEMENTATION. The local-termination architecture, the additive-mutual-CRC
// [CRC][8 cmd] framing, the TransiveStruct {init,transive,transiveDone} dispatch, and the
// EMU-section LOCAL PARTNER SYNTHESIS are the technique of:
//
//     Celio-Link (Celio-Firmware), GPL-3.0 -- https://github.com/Celio-Link/Celio-Firmware
//        studied files: src/layers/packetLayer.{hpp,cpp}, src/callbacks/* (TransiveStruct,
//        emptyCommand, blockCommand, blockCommandRequestCommand, moveCommand,
//        sendLinkTypeCommand, readyExitStandbyCommand, readyCloseLinkCommand, usbLinkCommand),
//        src/sections/{tradeSetup,tradeConnection,tradeDisconnected,tradeLounge}.cpp,
//        src/payloads/{linkPlayer,pokemon,mail,trainerCard}.*, src/link_defines.h.
//
// Celio is C++/Zephyr (k_sem / k_msgq / PIO); 3DGBA is header-free pure C (CLAUDE.md rule #4)
// so this is a near-total rewrite, not a copy. The Gen-3 wire protocol itself is documented by
// pret/pokeemerald + pret/pokefirered, cross-verified against Celio.
//
// 3DGBA is GPL-3.0 (compatible). This file is GPL-3.0.
// ============================================================================================
//
// THE TRANSPLANT. Celio is a USB dongle wedged between a real GBA and a host PC; it answers
// EVERY SIO transfer locally at full GBA speed, reconstructing the Gen-3 framing on-device,
// and ships only SEMANTIC, latency-tolerant data over the wire. On 3DGBA each console runs its
// game in an embedded mGBA core and UDS replaces the USB cable. So this module IS the link
// partner: the net SIO driver feeds it the game's outgoing word and it returns the partner's
// reply word, O(1), never blocking on the radio.
//
// THE THREAD->PER-TRANSFER CONVERSION (the key porting transformation). Celio runs the section
// as a thread blocking on Zephyr semaphores (awaitTransiveResults -> k_sem_take) and k_sleep.
// We have NO threads: the driver calls one function per SIO transfer. So:
//   - PacketLayer's per-word onReceive/onTransmit/onTransiveDone becomes one hot-path call
//     cl_transfer().
//   - The section's blocking `while{ awaitTransiveResults(); switch(command[0]){...} }` becomes
//     a frame-boundary step run INSIDE cl_transfer() the instant a full 8-word command frame
//     completes (m_commandIndex==8). k_sleep waits become "stay in the current state until the
//     next frame." setTransiveHandler becomes installing a handler enum into the packet layer.
//
// Header-free: <stdint.h> / <string.h> ONLY, so celiolink.c dual-compiles on the PC host test
// harness (no libctru, no mGBA, no C++).

#ifndef CELIOLINK_H
#define CELIOLINK_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------
// Gen-3 link constants (Celio src/link_defines.h, cross-verified vs pret + gen3-link-protocol.md)
// ---------------------------------------------------------------------------------------------

// Handshake words — SINGLE repeated words, NOT framed. Leaving handshake the instant EITHER side
// emits 0x8FFF is load-bearing; framing the handshake drops 0xB9A0 and hangs (a real costly bug).
#define LINK_MASTER_HANDSHAKE   0x8FFFu  // "I am master / force the link open"
#define LINK_SLAVE_HANDSHAKE    0xB9A0u  // "ready slave"  (also the CRC seed for the 1st frame)
#define LINK_HANDSHAKE_DISABLE  0xD15Eu  // "handshake disabled"

// LINKCMD command words (word 0 of an 8-word frame).
#define LINKCMD_EMPTY               0x0000u
#define LINKCMD_SEND_LINK_TYPE      0x2222u
#define LINKCMD_READY_EXIT_STANDBY  0x2FFEu
#define LINKCMD_DUMMY_1             0x5555u
#define LINKCMD_READY_CLOSE_LINK    0x5FFFu
#define LINKCMD_COUNTDOWN           0x7FFFu  // NOT idle — a real command. Idle is 0x0000.
#define LINKCMD_CONT_BLOCK          0x8888u  // block-continuation / negotiation carrier (word1=cmd)
#define LINKCMD_READY_TO_TRADE      0xAABBu
#define LINKCMD_READY_FINISH_TRADE  0xABCDu
#define LINKCMD_INIT_BLOCK          0xBBBBu  // start a block transfer (NOT proof of a trade)
#define LINKCMD_READY_CANCEL_TRADE  0xBBCCu
#define LINKCMD_SEND_HELD_KEYS      0xCAFEu
#define LINKCMD_SEND_BLOCK_REQ      0xCCCCu  // request a block of size N from peer
#define LINKCMD_START_TRADE         0xCCDDu
#define LINKCMD_CONFIRM_FINISH_TRADE 0xDCBAu
#define LINKCMD_SET_MONS_TO_TRADE   0xDDDDu  // commit which slot is being traded
#define LINKCMD_PLAYER_CANCEL_TRADE 0xDDEEu
#define LINKCMD_REQUEST_CANCEL      0xEEAAu
#define LINKCMD_BOTH_CANCEL_TRADE   0xEEBBu
#define LINKCMD_NONE                0xEFFFu

// LINKTYPE words (word 1 of a SEND_LINK_TYPE frame).
#define LINKTYPE_TRADE              0x1111u  // lounge / trade-room idle
#define LINKTYPE_TRADE_CONNECTING   0x1122u  // active trade data-exchange + negotiation
#define LINKTYPE_TRADE_SETUP        0x1133u  // initial setup / room entry
#define LINKTYPE_TRADE_DISCONNECTED 0x1144u  // post-trade reconnect / finish
#define LINKTYPE_BATTLE             0x2211u
#define LINKTYPE_SINGLE_BATTLE      0x2233u
#define LINKTYPE_DOUBLE_BATTLE      0x2244u
#define LINKTYPE_RECORD_MIX_BEFORE  0x3311u
#define LINKTYPE_RECORD_MIX_AFTER   0x3322u
#define LINKTYPE_BERRY_BLENDER      0x4422u

// Menu-nav key codes (Celio link_defines.h:65-75) — used by the canned movement script.
#define LINK_KEY_CODE_NULL       0x00u
#define LINK_KEY_CODE_EMPTY      0x11u   // "no input" — standing still IS a key frame, not silence
#define LINK_KEY_CODE_DPAD_DOWN  0x12u
#define LINK_KEY_CODE_DPAD_UP    0x13u
#define LINK_KEY_CODE_DPAD_LEFT  0x14u
#define LINK_KEY_CODE_DPAD_RIGHT 0x15u
#define LINK_KEY_CODE_READY      0x16u
#define LINK_KEY_CODE_EXIT_ROOM  0x17u

// Frame geometry.
#define CL_CMD_LENGTH 8       // 8 command words per frame (Celio std::array<uint16_t,8>)
#define CL_MAX_CHUNK  14      // blockCommand payload bytes per CONT frame (words 1..7)

// ---------------------------------------------------------------------------------------------
// Roles. The HOST (UDS seat 0 / mGBA playerId 0) is the GBA-link MASTER; the JOIN (seat 1) is
// the SLAVE. Both run their OWN local PacketLayer + trade section synthesizing THE OTHER game.
// (Note: in the Gen-3 cable, "master" only means who owns the SIO clock; both sides answer.)
// ---------------------------------------------------------------------------------------------
typedef enum { CL_MASTER = 0, CL_SLAVE = 1 } ClRole;

// ---------------------------------------------------------------------------------------------
// The semantic seam — the ONLY things that cross UDS (everything else is answered locally).
//
// Celio's EMU trade synthesizes the partner from a ONE-SHOT uploaded party file; it never talks
// to a second live game. 3DGBA's twist (the M1 crux, BEYOND Celio): two real games trade, so we
// feed each console's synthesized partner the OTHER live game's party + identity + selection.
// These events carry that semantic data asynchronously, OUT of the per-transfer hot path.
// ---------------------------------------------------------------------------------------------
typedef enum {
	CL_EV_NONE       = 0,
	CL_EV_LINKPLAYER = 1,  // a 56/60-byte LinkPlayerBlock (this side's trainer identity)
	CL_EV_PARTY_CHUNK= 2,  // a 200-byte party chunk; arg = chunk index 0..2 (this side's 600B party)
	CL_EV_SELECT     = 3,  // this side chose to trade party slot `arg` (0..5)
	CL_EV_CONFIRM    = 4,  // this side confirmed the trade
	CL_EV_CANCEL     = 5,  // this side cancelled
	CL_EV_CLOSE      = 6,  // this side closed the link / left
	CL_EV_EXIT_ROOM  = 7,  // this side's game began the ROOM EXIT (sent LINK_KEY_CODE_EXIT_ROOM) —
	                       // the peer's partner must walk ITS game out too (both players leave together)
	CL_EV_TRAINERCARD= 8   // this side's 100-byte trainer-card block (captured at linkup; run #12:
	                       // rendering the old all-zero canned card black-screened the card view)
} ClEventType;

typedef struct {
	uint8_t  type;     // ClEventType
	uint8_t  arg;      // chunk index (PARTY_CHUNK) or slot (SELECT)
	uint16_t len;      // bytes valid in data[]
	uint8_t  data[256];
} ClEvent;

// ---------------------------------------------------------------------------------------------
// Active reply-handler kinds (the ported TransiveStruct factories, as an enum + small state).
// ---------------------------------------------------------------------------------------------
typedef enum {
	CLH_EMPTY = 0,        // all 0x0000 (the steady-state idle frame), 1 frame then done
	CLH_LINKTYPE,         // [0x2222, type, 0..]
	CLH_BLOCK,            // INIT_BLOCK then N CONT_BLOCK 14-byte chunks
	CLH_BLOCK_REQ,        // [0xCCCC, size, 0..]
	CLH_MOVE,             // [0xCAFE, key, 0..] repeated 21x
	CLH_EXIT_STANDBY,     // [0x2FFE, 0..]
	CLH_CLOSE_LINK        // [0x5FFF, 0..]
} ClHandlerKind;

// ---------------------------------------------------------------------------------------------
// PacketLayer state (per console). Mirrors Celio's PacketLayer but flattened for per-transfer.
// ---------------------------------------------------------------------------------------------
typedef enum { CL_ST_HANDSHAKE = 0, CL_ST_CRC, CL_ST_COMMAND } ClTransiveState;
typedef enum { CL_HS_DISABLED = 0, CL_HS_ENABLED, CL_HS_CONNECT } ClHandshakeState;
#define CL_HS_ESCALATE_AFTER 8   // MASTER: advertise 0xB9A0 this many transfers, then DRIVE 0x8FFF
#define CL_HS_REARM_RUN      4   // re-enter the handshake after this many consecutive handshake words
// Celio master-clock pacing (packetLayer.hpp:51-53): the dongle paces the link it drives.
#define CL_TIMING_HANDSHAKE_US 30097  // between single handshake words
#define CL_TIMING_WORD_US      350    // paced BELOW the real 752us: the per-word ISR-ack wait adds the rest
                                      // (run #6 measured ~2 game frames per burst at nominal-1.0 pacing)
#define CL_TIMING_FRAME_US     2500   // with the word gaps this targets ~2x nominal => ~1.2-1.5x effective
                                      // drain (must EXCEED the game's 1.0x send-queue fill; catch-up absorbs
                                      // the surplus, the heartbeat keeps over-drained bursts nonzero)

// blockCommand handler state (Celio blockCommand.cpp globals, made per-instance).
typedef struct {
	const uint8_t* src;       // g_src
	uint16_t srcSize;         // g_srcSize  (real bytes remaining)
	uint16_t pos;             // g_pos
	uint16_t index;           // g_index (0..7 within a frame)
	uint8_t  initSend;        // g_initSend (false=streaming INIT frame, true=CONT frames)
	uint16_t blockMaxSize;    // g_blockMaxSize (advertised size; drives termination + zero-pad)
	uint16_t initFrame[8];    // g_blockCommandInit  [0xBBBB, size, 0x80, 0..]
	uint16_t contFrame[8];    // g_blockCommandContent [0x8888, <14 payload bytes>]
} ClBlockState;

// Trade-section block-streaming state (Celio TradeConnectionState / tradeSetup BlockCommandState).
typedef enum {
	CL_SEC_SETUP = 0,         // LINKTYPE_TRADE_SETUP
	CL_SEC_CONNECTION,        // LINKTYPE_TRADE_CONNECTING (the trade itself)
	CL_SEC_DISCONNECT,        // LINKTYPE_TRADE_DISCONNECTED
	CL_SEC_LOUNGE             // LINKTYPE_TRADE
} ClSection;

typedef enum {
	// Connection-section block stream (Celio TradeConnection.hpp:51-60)
	CL_BLK_LINKPLAYER = 0,
	CL_BLK_PARTY0,
	CL_BLK_PARTY1,
	CL_BLK_PARTY2,
	CL_BLK_MAIL,
	CL_BLK_RIBBONS,
	CL_BLK_LINKCMD,
	// Setup-section extra states (Celio tradeSetup.hpp:14-19)
	CL_BLK_TRAINERCARD,
	CL_BLK_REQ_TRAINERCARD
} ClBlockSeq;

// Status / phase getter for the netlog (mirrors the §4 milestone diagnostics).
typedef struct {
	uint8_t  role;            // ClRole
	uint8_t  state;           // ClTransiveState
	uint8_t  handler;         // ClHandlerKind
	uint8_t  section;         // ClSection
	uint8_t  blockSeq;        // ClBlockSeq
	uint8_t  tradeComplete;   // 1 once the trade has gone all the way through + closed
	uint16_t crc;             // running additive CRC accumulator
	uint32_t frameCount;      // completed 8-word frames since cl_init
	uint32_t partnerPartyBytes; // bytes of the peer's party captured so far (0..600)
	// --- live-coordination diagnostics (the M1 hold gates) ---
	uint8_t  partnerPartyHeld; // 1 while block-exchange is held waiting for the peer's party
	uint8_t  selectHeld;       // 1 while SET_MONS is held waiting for the peer's SELECT
	uint8_t  confirmHeld;      // 1 while the confirm chain is held waiting for the peer's CONFIRM
	int8_t   localSelectSlot;  // slot OUR game offered (-1 = not yet)
	int8_t   partnerSelectSlot;// slot the PEER offered (-1 = not yet)
	// --- run-#12 diagnostics (room exit + trainer card) ---
	uint8_t  exitPending;      // the game committed a room exit (EXIT_ROOM key seen)
	uint8_t  sessionEnded;     // the exit's close was answered; awaiting the gap-reset
	uint8_t  partnerHasCard;   // the peer's real trainer card is cached
	uint8_t  identityWasReal;  // the latched partner identity is the peer's real one
} ClStatus;

// ---------------------------------------------------------------------------------------------
// The CelioLink object. Opaque-ish (fields defined so a caller can stack-allocate; do not poke).
// ---------------------------------------------------------------------------------------------
#define CL_PARTNER_PARTY_BYTES 600
#define CL_EVENT_QUEUE_DEPTH   8     // outgoing semantic events the local side wants to ship
#define CL_TRAINERCARD_WIRE_BYTES 100 // the on-wire trainer-card block (pret sBlockRequests[2] = {buf,100})
// ROOM EXIT: consecutive ALL-ZERO game command frames that prove the game ran ClearLinkCallback_2
// (the exit fade). Normal room traffic has isolated zero frames between CAFEs (run-#12 trace), never
// runs; after the exit fade the game is all-zero forever. 4 = safely past any isolated zero.
#define CL_EXIT_ZERO_RUN 4

typedef struct CelioLink {
	// --- role / link type ---
	ClRole   role;
	uint16_t linkType;        // the LINKTYPE_* this session is for (TRADE etc.)

	// --- PacketLayer ---
	ClTransiveState  state;
	ClHandshakeState hsState;
	uint16_t crc;             // additive mutual CRC accumulator (seed 0xB9A0, reset 0 per frame)
	int      commandIndex;    // m_commandIndex: rx words received this frame (the 8-gate)
	int      transmitIndex;   // m_transmitCommandIndex
	uint16_t received[8];     // m_receivedCommand
	uint16_t transmitted[8];  // m_transmittedCommand
	uint16_t lastTxHandshake; // m_transmitedHandShake
	uint16_t lastRxHandshake; // m_receivedHandshake
	uint16_t hsAdvertiseCount; // MASTER: 0xB9A0 advertisements before escalating to 0x8FFF (deadlock breaker)
	uint16_t hsRunCount;       // consecutive handshake words in CRC/COMMAND (re-handshake trigger)
	uint8_t  idle;            // m_idle (true when the active handler is emptyCommand)
	uint8_t  roomKeys;        // the interactive room phase is live -> proactive key-frame heartbeat
	// --- ROOM EXIT (run #12: the JOIN post-trade exit black screen) ---
	// The game's LinkCB_ReadyCloseLink is HARD-GATED on gLastRecvQueueCount==0 (pret link.c:1319):
	// it will not even SEND its 0x5FFF until its recv queue is quiet. A real exiting partner goes
	// silent (its own ClearLinkCallback_2); our unconditional heartbeat held the queue full forever.
	uint8_t  exitPending;     // our game sent LINK_KEY_CODE_EXIT_ROOM — the room exit is committed
	uint8_t  exitZeroRun;     // consecutive all-zero game frames since exitPending (fade proof)
	uint8_t  exitEmitted;     // CL_EV_EXIT_ROOM pushed once
	uint8_t  partnerExitRoom; // the PEER's game is leaving -> drive EXIT_ROOM keys at OUR game
	uint8_t  sessionEnded;    // the exit's 0x5FFF close was answered; session over (await gap-reset)
	uint8_t  nextIsLounge;    // a cancel happened -> the post-close session is the ROOM (LOUNGE), not DISCONNECT
	uint8_t  negArmed;        // negotiation: an INIT header arrived; ONLY the next CONT carries the command
	uint16_t gameInitSize;    // the game's current inbound block-stream size (from ITS INIT header); 0=not capturing
	uint16_t reqDelay;        // frames before the next proactive block request (Celio's k_timer, tradeConnection.cpp:48)
	uint8_t  localLP[60];     // captured local game LinkPlayerBlock (streamed on 0x2222 in SETUP)
	uint8_t  localLPBytes;    // reassembly progress into localLP
	uint8_t  localLPEmitted;  // CL_EV_LINKPLAYER pushed once
	uint8_t  identityLatched; // the partner identity is FROZEN for this club visit (CheckLinkPlayersMatchSaved!)
	uint8_t  shippedIdentity[60]; // the latched LinkPlayerBlock we ship every session of this visit
	uint8_t  identityWasReal; // the latched identity came from the PEER's real LinkPlayer (not canned).
	                          // The game parses the partner's trainer card by the VERSION in this
	                          // identity, so the served card must match: real identity -> real card.
	// --- trainer card (run #12: the HOST card-view black screen) ---
	uint8_t  localCard[CL_TRAINERCARD_WIRE_BYTES];   // the LOCAL game's card, captured at linkup
	uint8_t  localCardBytes;
	uint8_t  localCardEmitted;                       // CL_EV_TRAINERCARD pushed once
	uint8_t  partnerCard[CL_TRAINERCARD_WIRE_BYTES]; // the PEER's real card (preserved across resets)
	uint8_t  partnerHasCard;
	// --- transition trace (diagnostics): one entry whenever (section|state|blockSeq) changes or a
	// non-idle command is dispatched; dumped into the netlog header as # celio-trace lines. ---
	struct { uint32_t frame; uint16_t cmd0; uint8_t section, state, blockSeq; } trace[32];
	uint8_t  traceN;          // entries used (saturates at 32; ring via traceHead)
	uint8_t  traceHead;

	// --- active reply handler ---
	ClHandlerKind handler;
	uint16_t hArg;            // generic arg word (linkType / req size / move key)
	int      hIndex;          // simple handlers' word index (0..7)
	int      hRepeats;        // moveCommand repeat counter (0..20)
	ClBlockState block;       // blockCommand state

	// --- trade-section dispatcher (the EMU synthesis) ---
	ClSection  section;
	ClBlockSeq blockSeq;
	int        movementIndex; // canned {UP,UP,LEFT,UP,RIGHT,READY} script cursor
	uint8_t    requestBlockSize; // m_requestBlockSize (sizes we ask the peer for)
	uint8_t    followupPending;  // negotiation: a 2nd command queued for the next idle tick
	uint16_t   followupCmd;
	uint8_t    pendingClose;     // close section pending after a READY_CLOSE_LINK
	uint8_t    tradeComplete;

	// --- 2-CONSOLE LIVE COORDINATION (the M1 crux, BEYOND Celio's one-shot file) ---
	// These gates make the synthesized partner mirror the PEER's REAL choices instead of a canned
	// script. Each gate, when its peer event has not yet arrived, HOLDS the section in its current
	// state (we answer the game's frame with a benign EMPTY/idle frame and do NOT advance) — that is
	// the game's normal latency-tolerant "waiting for your friend" screen, which has no link timeout.
	int      localSelectSlot;     // the slot OUR game offered (cmd[2] of its READY_TO_TRADE); -1=none
	uint8_t  localConfirmed;      // our game reached the confirm chain (we emitted CL_EV_CONFIRM)
	uint8_t  setMonsCommitted;    // the partner's SET_MONS_TO_TRADE(j) was finalized (peer SELECT in)
	uint8_t  confirmChainStarted; // the partner's confirm chain (START_TRADE...) was released
	uint8_t  sentPeerSelect;      // FOLLOWER role: the peer's [AABB, slot] was announced to the game-Leader
	uint8_t  sentPeerConfirm;     // FOLLOWER role: the peer's confirm [BBBB,0] was sent to the game-Leader
	uint8_t  cancelDriven;        // a peer CL_EV_CANCEL drove the partner cancel path
	uint8_t  partnerPartyHeld;    // 1 while we are HOLDING block-exchange for the peer's party
	uint8_t  selectHeld;          // 1 while we are HOLDING SET_MONS for the peer's SELECT
	uint8_t  confirmHeld;         // 1 while we are HOLDING the confirm chain for the peer's CONFIRM

	// --- the synthesized partner model (fed by cl_put_incoming from the peer's live game) ---
	// Celio's party::g_partnerParty + tradePkmnAtIndex. Here it is the PEER's offered party.
	uint8_t  partnerParty[CL_PARTNER_PARTY_BYTES];
	uint32_t partnerPartyBytes;  // how much of the partner party we have (0..600)
	uint8_t  partnerHasLinkPlayer;
	uint8_t  partnerLinkPlayer[64]; // the peer's LinkPlayerBlock (<=60B), served as our partner id
	uint16_t partnerLinkPlayerLen;
	int      partnerSelectSlot;  // peer's chosen trade slot (-1 = not yet selected -> HOLD)
	uint8_t  partnerConfirmed;   // peer pressed confirm
	uint8_t  partnerCancelled;
	uint8_t  partnerClosed;

	// --- capture of THE LOCAL game's outgoing semantic data (to emit over UDS) ---
	// The local game streams its own party to us during CONT_BLOCK; we capture it and emit chunks.
	uint8_t  localParty[CL_PARTNER_PARTY_BYTES];
	uint32_t localPartyBytes;
	uint8_t  localPartyEmittedChunks; // bitmask of party chunks already pushed via cl_take_outgoing

	// --- outgoing semantic-event queue (SPSC, drained by cl_take_outgoing) ---
	ClEvent  outQueue[CL_EVENT_QUEUE_DEPTH];
	int      outHead;
	int      outTail;

	// --- diag ---
	uint32_t frameCount;
} CelioLink;

// ---------------------------------------------------------------------------------------------
// PUBLIC API
// ---------------------------------------------------------------------------------------------

// Reset the dongle for a fresh session. role = our seat's link role; linkType = the activity
// (LINKTYPE_TRADE etc.). Installs the idle handler and starts in the handshake state.
void cl_init(CelioLink* cl, ClRole role, uint16_t linkType);

// THE HOT PATH. Feed the local game's outgoing SIO word (what the dongle RECEIVES this transfer);
// returns the dongle's reply word (what the game will receive). O(1), NEVER blocks, ALWAYS
// returns a valid word (handshake word, CRC word, command word, or 0x0000 idle). When a full
// 8-word command frame completes inside this call, the trade-section dispatcher runs (synthesis).
uint16_t cl_transfer(CelioLink* cl, uint16_t local_word);

// THE SEMANTIC SEAM (the only data that crosses UDS).
// Pop a semantic event the LOCAL side must send to the peer; returns 1 if one was produced
// (filled into *out), 0 if the queue is empty. Call after each cl_transfer / each frame.
int  cl_take_outgoing(CelioLink* cl, ClEvent* out);

// Feed a peer event that drives the synthesized partner (party/identity/selection/confirm/close).
void cl_put_incoming(CelioLink* cl, const ClEvent* in);

// Has the trade gone all the way through (select -> set-mons -> close) and saved-class completed?
int  cl_trade_complete(const CelioLink* cl);

// Snapshot for the netlog.
void cl_get_status(const CelioLink* cl, ClStatus* out);

// Copy up to `max` trace entries (oldest first) into out arrays; returns the count.
int  cl_get_trace(const CelioLink* cl, uint32_t* frames, uint16_t* cmd0s, uint8_t* secs,
                  uint8_t* states, uint8_t* blks, int max);

// Re-arm the PacketLayer to a FRESH handshake (state/CRC/frame indexes reset; the section, the
// received partner party and the select/confirm coordination state are PRESERVED). Used by the
// in-protocol re-handshake detector and by the driver at the game's not-listening -> listening edge.
void cl_rearm_handshake(CelioLink* cl);

// FULL session restart: fresh handshake + sections back to SETUP + all trade-coordination gates
// cleared (select/confirm/cancel/holds). The received partner party is PRESERVED (the peer resends
// it on its own restart; identical data). For the user exit-the-club-and-retry flow — a long
// no-transfer gap means the game restarts its conversation from the top, and so must the partner.
void cl_reset_session(CelioLink* cl);

// Celio-faithful inter-transfer pacing: how long (us) the driving clock should wait before the NEXT
// transfer, from the current PacketLayer state. (Divergence from Celio: the CRC word after a fresh
// handshake gets the 13ms frame gap instead of 1.4ms — slower = safe.)
uint32_t cl_next_delay_us(const CelioLink* cl);

#ifdef __cplusplus
}
#endif

#endif // CELIOLINK_H
