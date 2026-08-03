// celiolink.c — Gen-3 link LOCAL-TERMINATION dongle implementation for 3DGBA.
// ============================================================================================
// CLEAN-ROOM REIMPLEMENTATION of the Celio-Link (GPL-3.0) local-termination technique.
//   upstream: https://github.com/Celio-Link/Celio-Firmware
//   studied : src/layers/packetLayer.{hpp,cpp}, src/callbacks/*, src/sections/trade*.cpp,
//             src/payloads/*, src/link_defines.h.
//   protocol reference: pret/pokeemerald + pret/pokefirered (include/link.h, src/link.c, trade.c).
// 3DGBA is GPL-3.0; this file is GPL-3.0. Header-free pure C so it dual-compiles on the PC host
// test harness (CLAUDE.md rule #4): <stdint.h>/<string.h> only — no libctru, no mGBA, no C++.
//
// Every non-obvious piece cites the Celio file:line it is based on.
// ============================================================================================

#include "celiolink.h"
#include "celiolink_payloads.h"

// ============================================================================================
// 0. Small helpers
// ============================================================================================

static int cl_out_push(CelioLink* cl, uint8_t type, uint8_t arg,
                       const uint8_t* data, uint16_t len)
{
	int next = (cl->outTail + 1) % CL_EVENT_QUEUE_DEPTH;
	if (next == cl->outHead) return 0; // queue full -> dropped. Callers with emit-once latches MUST
	                                   // only latch on success or the event is lost forever.
	ClEvent* e = &cl->outQueue[cl->outTail];
	e->type = type;
	e->arg  = arg;
	e->len  = len;
	if (len > sizeof(e->data)) len = sizeof(e->data);
	if (data && len) memcpy(e->data, data, len);
	e->len = len;
	cl->outTail = next;
	return 1;
}

// ============================================================================================
// 1. The reply handlers (Celio src/callbacks/*). Each console's active handler produces the 8
//    TX words of the current command frame, one word per cl_transfer() call, walking an index
//    0..7 and wrapping at 8 (Celio's per-handler `static index` machine). transiveDone (run at
//    the frame boundary) returns resume/done.
//
//    KEY PORTING NOTE: Celio's handlers use FILE-SCOPE STATICS (one trade at a time on a dongle).
//    We have TWO consoles potentially in one process (the PC test, and conceptually loopback), so
//    every handler's state lives in `cl` (per-instance), not in statics. The logic is identical.
// ============================================================================================

// --- blockCommand (Celio blockCommand.cpp) -------------------------------------------------
// blockCommandSetup(src,size,blockMaxSize): primes the INIT frame [0xBBBB, blockMaxSize, 0x80, 0..]
// and arms the chunked CONT stream. (blockCommand.cpp:46-55)
static void cl_block_setup(CelioLink* cl, const void* src, uint16_t size, uint16_t blockMaxSize)
{
	ClBlockState* b = &cl->block;
	b->src = (const uint8_t*)src;
	b->srcSize = size;
	b->index = 0;
	b->initSend = 0;
	b->pos = 0;
	b->blockMaxSize = blockMaxSize;
	// INIT frame template [INIT_BLOCK, blockMaxSize, 0x80, 0,0,0,0,0]  (blockCommand.cpp:17-19,54)
	b->initFrame[0] = LINKCMD_INIT_BLOCK;
	b->initFrame[1] = blockMaxSize;
	b->initFrame[2] = (uint16_t)(0x80 + (cl->role == CL_SLAVE ? 1 : 0));   // mpId+0x80 (link.c:680): master=0x80, slave partner=0x81
	b->initFrame[3] = 0; b->initFrame[4] = 0; b->initFrame[5] = 0;
	b->initFrame[6] = 0; b->initFrame[7] = 0;
	// CONT frame template [CONT_BLOCK, 0..]  (blockCommand.cpp:21)
	b->contFrame[0] = LINKCMD_CONT_BLOCK;
	for (int i = 1; i < 8; i++) b->contFrame[i] = 0;
}

// blockCommandTransive (blockCommand.cpp:57-69): first 8-word frame = INIT, then CONT frames.
static uint16_t cl_block_transive(CelioLink* cl)
{
	ClBlockState* b = &cl->block;
	uint16_t ret = b->initSend ? b->contFrame[b->index] : b->initFrame[b->index];
	b->index++;
	if (b->index == 8) { b->index = 0; b->initSend = 1; }
	return ret;
}

// blockCommandChunk = transiveDone (blockCommand.cpp:24-44): prepare the NEXT content frame.
// 14 payload bytes/frame into bytes 2.. (words 1..7); the rest zero-padded; termination gated on
// the ADVERTISED blockMaxSize (NOT bytes copied) so a final short chunk + over-advertised tail are
// zero-filled and the receiver still gets the full advertised length.
static int cl_block_done(CelioLink* cl) // returns 1=done, 0=resume
{
	ClBlockState* b = &cl->block;
	if (b->blockMaxSize == 0) return 1;        // blockCommand.cpp:26
	if (!b->initSend) return 0;                // blockCommand.cpp:28 (degenerate pre-INIT guard)

	// zero the 14 payload bytes of the CONT frame (words 1..7)  (blockCommand.cpp:30)
	memset((uint8_t*)b->contFrame + 2, 0x00, CL_MAX_CHUNK);

	uint16_t chunkSize    = b->srcSize      < CL_MAX_CHUNK ? b->srcSize      : CL_MAX_CHUNK; // :32
	uint16_t maxChunkSize = b->blockMaxSize < CL_MAX_CHUNK ? b->blockMaxSize : CL_MAX_CHUNK; // :33

	if (b->blockMaxSize > 0 && b->src && chunkSize > 0)                                       // :35-38
		memcpy((uint8_t*)b->contFrame + 2, b->src + b->pos, chunkSize);

	b->srcSize      -= chunkSize;      // :40
	b->blockMaxSize -= maxChunkSize;   // :41
	b->pos          += chunkSize;      // :42
	return 0;                          // :43 resume
}

// --- the per-word transive() dispatch (Celio onTransmit -> handler.transive) ----------------
static uint16_t cl_handler_transive(CelioLink* cl)
{
	switch (cl->handler) {
	case CLH_EMPTY:        // emptyCommand.cpp:3-6 — all zeros
		return 0x0000;

	case CLH_LINKTYPE:     // sendLinkTypeCommand.cpp:7-24 — [0x2222, type, 0..]
		switch (cl->hIndex) {
		case 0: cl->hIndex++; return LINKCMD_SEND_LINK_TYPE;
		case 1: cl->hIndex++; return cl->hArg;
		case 7: cl->hIndex = 0; return 0x00;
		default: cl->hIndex++; return 0x00;
		}

	case CLH_BLOCK_REQ:    // blockCommandRequestCommand.cpp:7-24 — [0xCCCC, size, 0..]
		switch (cl->hIndex) {
		case 0: cl->hIndex++; return LINKCMD_SEND_BLOCK_REQ;
		case 1: cl->hIndex++; return cl->hArg;
		case 7: cl->hIndex = 0; return 0x00;
		default: cl->hIndex++; return 0x00;
		}

	case CLH_MOVE:         // moveCommand.cpp:15-32 — [0xCAFE, key, 0..]
		switch (cl->hIndex) {
		case 0: cl->hIndex++; return LINKCMD_SEND_HELD_KEYS;
		case 1: cl->hIndex++; return cl->hArg;
		case 7: cl->hIndex = 0; return 0x00;
		default: cl->hIndex++; return 0x00;
		}

	case CLH_EXIT_STANDBY: // readyExitStandbyCommand.cpp:6-20 — [0x2FFE, 0..]
		switch (cl->hIndex) {
		case 0: cl->hIndex++; return LINKCMD_READY_EXIT_STANDBY;
		case 7: cl->hIndex = 0; return 0x00;
		default: cl->hIndex++; return 0x00;
		}

	case CLH_CLOSE_LINK:   // readyCloseLinkCommand.cpp:6-20 — [0x5FFF, 0..]
		switch (cl->hIndex) {
		case 0: cl->hIndex++; return LINKCMD_READY_CLOSE_LINK;
		case 7: cl->hIndex = 0; return 0x00;
		default: cl->hIndex++; return 0x00;
		}

	case CLH_BLOCK:        // blockCommand
		return cl_block_transive(cl);
	}
	return 0x0000;
}

// transiveDone() per handler: 1=done(retire->idle), 0=resume(keep for another 8-word frame).
static int cl_handler_done(CelioLink* cl)
{
	switch (cl->handler) {
	case CLH_MOVE:   // moveCommand.cpp:40-48 — repeat 21x (resume while g_repeats!=20)
		if (cl->hRepeats != 20) { cl->hRepeats++; return 0; }
		return 1;
	case CLH_BLOCK:  // blockCommandChunk
		return cl_block_done(cl);
	default:         // every simple command: one frame then done (callbacks.md cross-cutting)
		return 1;
	}
}

// Install a handler (Celio setTransiveHandler, packetLayer.hpp:76-81): set handler, clear idle,
// run its init. We reset the per-handler index/repeat here (Celio resets in the factory/setup).
static void cl_set_handler(CelioLink* cl, ClHandlerKind kind, uint16_t arg)
{
	cl->handler  = kind;
	cl->idle     = 0;
	cl->hIndex   = 0;
	cl->hArg     = arg;
	if (kind == CLH_MOVE) cl->hRepeats = 0; // moveCommandInit (moveCommand.cpp:8-13)
}

// ============================================================================================
// 2. The trade-section dispatcher — the EMU LOCAL PARTNER SYNTHESIS.
//
//    Celio runs this as a thread:  while{ result = awaitTransiveResults();   // blocks per frame
//                                          switch(result.received[0]){ ...setTransiveHandler... } }
//    We have no thread: cl_section_step() is called from cl_transfer() the instant a full 8-word
//    command frame completes. k_sleep waits collapse to "stay in this state until the next frame";
//    the `idle()` gate (only act proactively when the active handler has retired) is preserved.
//
//    Sections ported: TradeSetup (tradeSetup.cpp), TradeConnection initial-exchange +
//    negotiation (tradeConnection.cpp), TradeDisconnect (tradeDisconnected.cpp), TradeLounge
//    (tradeLounge.cpp). The block sequence LinkPlayer->Party0/1/2->Mail->Ribbons and the
//    CONT_BLOCK-wrapped negotiation (real cmd in command[1]) are carried 1:1.
//
//    ============================  THE 2-CONSOLE LIVE-TRADE TWIST  ============================
//    Celio synthesizes the partner from a ONE-SHOT uploaded party FILE and answers select/confirm
//    from a canned movement script — there is no second live human. 3DGBA's M1 payoff is the
//    INVERSE: two real games trade, so the synthesized partner must be driven by the OTHER live
//    console. Hence:
//      * OUTGOING: when the local game streams ITS party to us (CONT_BLOCK capture below) and its
//        LinkPlayer block, we emit CL_EV_PARTY_CHUNK / CL_EV_LINKPLAYER via cl_take_outgoing for
//        the transport agent to ship over UDS. The peer's dongle ingests them as ITS partner.
//      * INCOMING: cl_put_incoming feeds the PEER's party/identity into partnerParty/partner
//        LinkPlayer; the synthesis serves THAT as the offered partner instead of a canned file.
//      * SELECT/CONFIRM: the partner's choice is driven by peer CL_EV_SELECT/CONFIRM. UNTIL the
//        peer's selection arrives we HOLD in the pre-selection state (we keep answering the
//        game's polls but do not advance the trade) — that is the game's benign "waiting for your
//        friend" screen, NOT an error. This live coordination is the part BEYOND Celio.
//    See the "STUBBED vs IMPLEMENTED" note at the bottom of this file for exactly what is wired.
// ============================================================================================

// linkType for the current section (Celio: each section announces its own).
static uint16_t cl_section_linktype(const CelioLink* cl)
{
	switch (cl->section) {
	case CL_SEC_SETUP:      return LINKTYPE_TRADE_SETUP;
	case CL_SEC_CONNECTION: return LINKTYPE_TRADE_CONNECTING;
	case CL_SEC_DISCONNECT: return LINKTYPE_TRADE_DISCONNECTED;
	case CL_SEC_LOUNGE:     return LINKTYPE_TRADE;
	}
	return cl->linkType;
}

// Build our partner's outgoing party into a scratch buffer: PEER's captured party where present,
// else the canonical filler mon (so empty/not-yet-arrived slots are still well-formed).
// (Celio uses a hardcoded filler for empty slots; pokemon.cpp:13-25,49-52.)
static void cl_partner_party(const CelioLink* cl, uint8_t out[CL_PARTY_BYTES])
{
	for (int s = 0; s < CL_PARTY_SLOTS; s++) {
		uint32_t off = (uint32_t)s * CL_PARTY_SLOT_BYTES;
		if (cl->partnerPartyBytes >= off + CL_PARTY_SLOT_BYTES)
			memcpy(out + off, cl->partnerParty + off, CL_PARTY_SLOT_BYTES);
		else
			memcpy(out + off, CL_FILLER_PKMN, CL_PARTY_SLOT_BYTES);
	}
}

// LIVE-COORDINATION PARTY-BLOCK HOLD predicate. Before we can ship the partner's party to the local
// game we must actually HAVE the peer's party — otherwise we'd offer the local game canned filler and
// the two consoles would NOT swap the same mons. So the block exchange HOLDS at the PartyPart0 boundary
// until the peer's party has arrived over UDS (cl_put_incoming CL_EV_PARTY_CHUNK). "Arrived" = the
// peer's LinkPlayer identity is in AND at least its first party chunk has landed (>=1 chunk = the
// offered party is materially present; the rest streams in well before the game polls those bytes).
// A canned/one-sided trade (T0) pre-supplies these before the handshake, so this gate is transparent
// there — it only ever HOLDS in the genuine two-console race where the peer is still streaming.
static int cl_partner_bytes_ready(const CelioLink* cl, uint32_t needed)
{
	// Per-chunk gating (run #6): serving party block N requires the peer's bytes for exactly that
	// window — the exchange then PIPELINES chunk-by-chunk (each side's capture of block N feeds the
	// peer's serve of block N one cycle later). Identity is latched separately (canned fallback), so
	// the gate is on party bytes only.
	return cl->partnerPartyBytes >= needed;
}

// Static scratch for the block source (must outlive the frame stream). Single trade in flight per
// console; one per console is fine since cl_transfer is single-threaded per CelioLink.
// We stash these inside `cl` would bloat it; instead a per-call refresh keeps the pointer valid by
// rebuilding into cl->partnerParty-adjacent scratch. Simpler: keep a static aligned to the call.
static uint8_t  s_blockScratch[CL_PARTY_BYTES];     // party/linkplayer/mail/ribbons source
static uint16_t s_blockScratchLen;

// Negotiation: ship a tiny {cmd,arg} 2-word command as a 20-byte block (Celio sendLinkCommand,
// tradeConnection.cpp:212-219).
static uint16_t s_negWords[2];
static void cl_send_link_command(CelioLink* cl, uint16_t cmd, uint16_t arg)
{
	s_negWords[0] = cmd;
	s_negWords[1] = arg;
	cl_block_setup(cl, s_negWords, sizeof(s_negWords), 20);
	cl_set_handler(cl, CLH_BLOCK, 0);
}

// Ship a LinkPlayer block for the current section. Uses the PEER's real LinkPlayer when we have it
// (so the local game sees the other trainer), else a canned identity stamped with the linkType.
static void cl_ship_linkplayer(CelioLink* cl)
{
	ClLinkPlayerBlock blk;
	// IDENTITY LATCH (run #6): the game saves the partner's identity at the club counter and VERIFIES
	// it on every later session (CheckLinkPlayersMatchSaved, cable_club.c:797). Whatever identity we
	// ship FIRST is frozen for the whole club visit — switching canned->real mid-visit would error.
	if (!cl->identityLatched) {
		if (cl->partnerHasLinkPlayer && cl->partnerLinkPlayerLen >= CL_LINKPLAYER_BLOCK_SIZE) {
			memcpy(cl->shippedIdentity, cl->partnerLinkPlayer, CL_LINKPLAYER_BLOCK_SIZE);
			cl->identityWasReal = 1;   // the served trainer card must match this identity's VERSION
		} else {
			cl_make_linkplayer_block(&blk, cl_section_linktype(cl));
			memcpy(cl->shippedIdentity, &blk, CL_LINKPLAYER_BLOCK_SIZE);
			cl->identityWasReal = 0;
		}
		cl->identityLatched = 1;
	}
	memcpy(s_blockScratch, cl->shippedIdentity, CL_LINKPLAYER_BLOCK_SIZE);
	{ // stamp this section's linkType (Celio linkPLayer(linkType) clobbers it per section)
		uint32_t lt = cl_section_linktype(cl);
		memcpy(s_blockScratch + 16 + 0x14, &lt, sizeof(lt));
	}
	s_blockScratchLen = CL_LINKPLAYER_BLOCK_SIZE;
	cl_block_setup(cl, s_blockScratch, CL_LINKPLAYER_BLOCK_SIZE, CL_LINKPLAYER_BLOCK_SIZE);
	cl_set_handler(cl, CLH_BLOCK, 0);
}

// Capture the local game's outgoing party as it streams it (during our PartyPart shipping the game
// concurrently sends ITS party via CONT_BLOCK). 14 payload bytes/frame, clamped to 600. Emits a
// CL_EV_PARTY_CHUNK over UDS each time a 200-byte chunk completes.
static void cl_capture_local_party(CelioLink* cl, const uint16_t cmd[8]);

// On the game's INIT header: classify + (re)open the capture window. A re-streamed block (the game
// re-answers a repeated 0xCCCC while our serve is held) simply overwrites the same window; the
// emit-once flags make the chunk push idempotent.
static void cl_capture_feed_init(CelioLink* cl, const uint16_t cmd[8])
{
	uint16_t size = cmd[1];
	if (size == 200) {                    // a party chunk stream (2 x 100-byte mons)
		// The window is the game's CURRENT stage = how far OUR serves have advanced it (its
		// BufferTradeParties only moves past a block once it received OURS, status==3). A held
		// game RE-streams the same stage on every pull — same window, idempotent overwrite
		// (run #7: the lowest-unemitted rule mis-filed re-streams as later chunks = duplicates).
		uint32_t win = 0;
		if      (cl->blockSeq == CL_BLK_PARTY1) win = 1 * CL_PARTY_CHUNK_BYTES;
		else if (cl->blockSeq != CL_BLK_PARTY0) win = 2 * CL_PARTY_CHUNK_BYTES;   // PARTY2/MAIL+
		cl->localPartyBytes = win;
		cl->gameInitSize = 200;
	} else if (size == CL_LINKPLAYER_BLOCK_SIZE) {   // the game's LinkPlayerBlock (on 0x2222)
		cl->localLPBytes = 0;
		cl->gameInitSize = CL_LINKPLAYER_BLOCK_SIZE;
	} else if (size == CL_TRAINERCARD_WIRE_BYTES) {  // the game's trainer card (linkup block req type 2)
		cl->localCardBytes = 0;
		cl->gameInitSize = CL_TRAINERCARD_WIRE_BYTES;
	} else {
		cl->gameInitSize = 0;              // mail / ribbons / 20B command blocks: not captured
	}
}

// On the game's CONT frames: append into the open window; push the semantic event on completion.
static void cl_capture_feed_cont(CelioLink* cl, const uint16_t cmd[8])
{
	if (cl->gameInitSize == CL_LINKPLAYER_BLOCK_SIZE) {
		const uint8_t* p = (const uint8_t*)&cmd[1];
		uint32_t n = CL_MAX_CHUNK;
		if ((uint32_t)cl->localLPBytes + n > CL_LINKPLAYER_BLOCK_SIZE) n = CL_LINKPLAYER_BLOCK_SIZE - cl->localLPBytes;
		if (n) { memcpy(cl->localLP + cl->localLPBytes, p, n); cl->localLPBytes = (uint8_t)(cl->localLPBytes + n); }
		if (cl->localLPBytes >= CL_LINKPLAYER_BLOCK_SIZE && !cl->localLPEmitted) {
			if (cl_out_push(cl, CL_EV_LINKPLAYER, 0, cl->localLP, CL_LINKPLAYER_BLOCK_SIZE))
				cl->localLPEmitted = 1;
		}
		return;
	}
	if (cl->gameInitSize == CL_TRAINERCARD_WIRE_BYTES) {   // the game's own trainer card streaming in
		const uint8_t* p = (const uint8_t*)&cmd[1];
		uint32_t n = CL_MAX_CHUNK;
		if ((uint32_t)cl->localCardBytes + n > CL_TRAINERCARD_WIRE_BYTES)
			n = CL_TRAINERCARD_WIRE_BYTES - cl->localCardBytes;
		if (n) { memcpy(cl->localCard + cl->localCardBytes, p, n); cl->localCardBytes = (uint8_t)(cl->localCardBytes + n); }
		if (cl->localCardBytes >= CL_TRAINERCARD_WIRE_BYTES && !cl->localCardEmitted) {
			if (cl_out_push(cl, CL_EV_TRAINERCARD, 0, cl->localCard, CL_TRAINERCARD_WIRE_BYTES))
				cl->localCardEmitted = 1;
		}
		return;
	}
	if (cl->gameInitSize != 200) return;
	cl_capture_local_party(cl, cmd);
}

static void cl_capture_local_party(CelioLink* cl, const uint16_t cmd[8])
{
	const uint8_t* payload = (const uint8_t*)&cmd[1]; // bytes 2.. of the 16-byte frame = words 1..7
	// cap at the CURRENT chunk window's end (window opened by cl_capture_feed_init)
	uint32_t winEnd = ((cl->localPartyBytes / CL_PARTY_CHUNK_BYTES) + 1u) * CL_PARTY_CHUNK_BYTES;
	if (winEnd > CL_PARTY_BYTES) winEnd = CL_PARTY_BYTES;
	uint32_t n = CL_MAX_CHUNK;
	if (cl->localPartyBytes + n > winEnd) n = winEnd - cl->localPartyBytes;
	if (n == 0) return;
	memcpy(cl->localParty + cl->localPartyBytes, payload, n);

	uint32_t before = cl->localPartyBytes;
	cl->localPartyBytes += n;

	// emit any 200-byte chunk that just completed
	for (int c = 0; c < 3; c++) {
		uint32_t end = (uint32_t)(c + 1) * CL_PARTY_CHUNK_BYTES;
		if (before < end && cl->localPartyBytes >= end &&
		    !(cl->localPartyEmittedChunks & (1u << c))) {
			if (cl_out_push(cl, CL_EV_PARTY_CHUNK, (uint8_t)c,
			                cl->localParty + (uint32_t)c * CL_PARTY_CHUNK_BYTES,
			                CL_PARTY_CHUNK_BYTES))
				cl->localPartyEmittedChunks |= (1u << c);
		}
	}
}

// One trade-section step, run when a full 8-word command frame completes. `cmd` = received[8].
static void cl_trace_rec(CelioLink* cl, uint16_t cmd0)
{
	uint8_t idx = (uint8_t)((cl->traceHead) % 32);
	cl->trace[idx].frame    = cl->frameCount;
	cl->trace[idx].cmd0     = cmd0;
	cl->trace[idx].section  = (uint8_t)cl->section;
	cl->trace[idx].state    = (uint8_t)cl->state;
	cl->trace[idx].blockSeq = (uint8_t)cl->blockSeq;
	cl->traceHead = (uint8_t)(cl->traceHead + 1);
	if (cl->traceN < 32) cl->traceN++;
}

int cl_get_trace(const CelioLink* cl, uint32_t* frames, uint16_t* cmd0s, uint8_t* secs,
                 uint8_t* states, uint8_t* blks, int max)
{
	int n = cl->traceN < max ? cl->traceN : max;
	for (int i = 0; i < n; i++) {
		int idx = (cl->traceHead - n + i + 64) % 32;
		frames[i] = cl->trace[idx].frame; cmd0s[i] = cl->trace[idx].cmd0;
		secs[i] = cl->trace[idx].section; states[i] = cl->trace[idx].state; blks[i] = cl->trace[idx].blockSeq;
	}
	return n;
}

static void cl_section_step(CelioLink* cl, const uint16_t cmd[8])
{
	cl->frameCount++;
	// trace every non-idle dispatched command + any section/blockSeq change (cheap ring)
	{
		// record ONLY transitions: a section/state/blockSeq change OR a NEW command word. Runs of the
		// same command (e.g. the CAFE room heartbeat) collapse to one entry, so the 32-slot ring spans
		// the whole trade's phase history instead of 32 identical CAFE frames.
		uint8_t lastIdx = (uint8_t)((cl->traceHead + 31) % 32);
		int changed = (cl->traceN == 0) ||
		              cl->trace[lastIdx].section  != (uint8_t)cl->section  ||
		              cl->trace[lastIdx].state    != (uint8_t)cl->state    ||
		              cl->trace[lastIdx].blockSeq != (uint8_t)cl->blockSeq ||
		              cl->trace[lastIdx].cmd0     != cmd[0];
		if (changed) cl_trace_rec(cl, cmd[0]);
	}

	// ROOM-EXIT FADE DETECTOR (run #12). Once the game commits the exit (EXIT_ROOM key), its
	// Task_ReturnFromLinkRoomWarp runs ClearLinkCallback_2 at the fade and the game sends ALL-ZERO
	// frames from then on. Its LinkCB_ReadyCloseLink will NOT send the 0x5FFF close until its recv
	// queue is EMPTY (pret link.c:1319 gLastRecvQueueCount==0) — so the moment the game goes silent,
	// the heartbeat MUST stop (we keep clocking; all-zero frames are RECEIVED_NOTHING and never
	// enqueue). A run is required: normal room traffic has isolated zero frames between CAFEs.
	if (cl->exitPending && cl->roomKeys) {
		int allZero = 1;
		for (int i = 0; i < 8; i++) if (cmd[i]) { allZero = 0; break; }
		if (allZero) {
			if (cl->exitZeroRun < 255) cl->exitZeroRun++;
			if (cl->exitZeroRun >= CL_EXIT_ZERO_RUN) {
				cl->roomKeys = 0;                       // go quiet -> the game's recv queue drains
				if (cl->handler == CLH_MOVE) {          // retire a mid-repeat key handler NOW (21x
					cl->handler = CLH_EMPTY;            // repeats would linger ~20 more frames)
					cl->hIndex  = 0;
					cl->idle    = 1;
				}
			}
		} else {
			cl->exitZeroRun = 0;
		}
	}

	// ---- SETUP / LOUNGE / CONNECTION / DISCONNECT share the switch(cmd[0]) idiom. ----
	switch (cl->section) {

	// ----------------------------------------------------------------------------------------
	// CONNECTION — the heart of the trade (tradeConnection.cpp). Two phases: initial data
	// exchange (block stream) then negotiation. We use blockSeq to know which phase/state.
	// ----------------------------------------------------------------------------------------
	case CL_SEC_CONNECTION:
	{
		// --- initial data exchange (handleInitialDataExchange, tradeConnection.cpp:13-129) ---
		if (cl->blockSeq != CL_BLK_LINKCMD) {
			if (cl->reqDelay) cl->reqDelay--;          // Celio's per-block request timer (k_timer 2000ms)
			switch (cmd[0]) {
			case LINKCMD_INIT_BLOCK: {
				cl_capture_feed_init(cl, cmd);         // the game's stream header (all-see-all: same word)
				switch (cl->blockSeq) {
				case CL_BLK_LINKPLAYER:                    // :41-50 ship LinkPlayer; init partner
					cl_ship_linkplayer(cl);
					cl->blockSeq = CL_BLK_PARTY0;
					cl->requestBlockSize = 1;
					cl->reqDelay = 60;                 // space the first party pull (the game must stage its buffer)
					// partnerPartyInit() — keep what cl_put_incoming gave us; only zero if none
					break;
				case CL_BLK_PARTY0: {                      // :52-60 ship party[0:200]
					// 2-CONSOLE PER-CHUNK HOLD (run #6 deadlock fix). Serving chunk N requires the
					// PEER's bytes for exactly window N. While held: answer EMPTY (idle-safe at the
					// menu) — but the block-request pull below KEEPS RUNNING, so the local game keeps
					// streaming ITS party, which we capture + push to the peer. Both sides do this ->
					// the exchange pipelines chunk-by-chunk instead of deadlocking (both-hold, run #6:
					// txSeq=0 both consoles).
					if (!cl_partner_bytes_ready(cl, 1 * CL_PARTY_CHUNK_BYTES)) {
						cl->partnerPartyHeld = 1;
						cl_set_handler(cl, CLH_EMPTY, 0);
						break; // stay in CL_BLK_PARTY0
					}
					cl->partnerPartyHeld = 0;
					memcpy(s_blockScratch, cl->partnerParty, CL_PARTY_CHUNK_BYTES);   // FRESH window copy
					cl_block_setup(cl, s_blockScratch, CL_PARTY_CHUNK_BYTES, CL_PARTY_CHUNK_BYTES);
					cl_set_handler(cl, CLH_BLOCK, 0);
					cl->blockSeq = CL_BLK_PARTY1;
					cl->requestBlockSize = 1;
					cl->reqDelay = 60;
					break;
				}
				case CL_BLK_PARTY1: {                      // :62-70 ship party[200:400]
					if (!cl_partner_bytes_ready(cl, 2 * CL_PARTY_CHUNK_BYTES)) {
						cl->partnerPartyHeld = 1;
						cl_set_handler(cl, CLH_EMPTY, 0);
						break; // stay in CL_BLK_PARTY1 until the peer's chunk 1 lands
					}
					cl->partnerPartyHeld = 0;
					memcpy(s_blockScratch + 200, cl->partnerParty + 200, CL_PARTY_CHUNK_BYTES);
					cl_block_setup(cl, s_blockScratch + 200, CL_PARTY_CHUNK_BYTES, CL_PARTY_CHUNK_BYTES);
					cl_set_handler(cl, CLH_BLOCK, 0);
					cl->blockSeq = CL_BLK_PARTY2;
					cl->requestBlockSize = 1;
					cl->reqDelay = 60;
					break;
				}
				case CL_BLK_PARTY2: {                      // :72-80 ship party[400:600]
					if (!cl_partner_bytes_ready(cl, 3 * CL_PARTY_CHUNK_BYTES)) {
						cl->partnerPartyHeld = 1;
						cl_set_handler(cl, CLH_EMPTY, 0);
						break; // stay in CL_BLK_PARTY2 until the peer's chunk 2 lands
					}
					cl->partnerPartyHeld = 0;
					memcpy(s_blockScratch + 400, cl->partnerParty + 400, CL_PARTY_CHUNK_BYTES);
					cl_block_setup(cl, s_blockScratch + 400, CL_PARTY_CHUNK_BYTES, CL_PARTY_CHUNK_BYTES);
					cl_set_handler(cl, CLH_BLOCK, 0);
					cl->blockSeq = CL_BLK_MAIL;
					cl->requestBlockSize = 3;
					cl->reqDelay = 60;
					break;
				}
				case CL_BLK_MAIL:                          // :82-90 empty mail (220B advertised, 0 data)
					cl_block_setup(cl, 0, 0, CL_MAIL_BYTES);
					cl_set_handler(cl, CLH_BLOCK, 0);
					cl->blockSeq = CL_BLK_RIBBONS;
					cl->requestBlockSize = 4;
					break;
				case CL_BLK_RIBBONS:                       // :92-97 empty ribbons (40B, 0 data)
					cl_block_setup(cl, 0, 0, CL_RIBBONS_BYTES);
					cl_set_handler(cl, CLH_BLOCK, 0);
					cl->blockSeq = CL_BLK_LINKCMD;
					break;
				default: break;
				}
				break;
			}
			case LINKCMD_CONT_BLOCK:                       // the game streaming ITS block (its 0xCCCC response)
				cl_capture_feed_cont(cl, cmd);             // window-managed: party chunks + LinkPlayer
				break;
			default:
				break;
			}

			// Proactive block-request pull (Celio's k_timer, tradeConnection.cpp:23-30,48). Runs EVEN
			// DURING the party hold (run #6 deadlock fix): the pull makes the LOCAL game stream its
			// party, which we capture + push to the peer — the peer's hold releases on those chunks,
			// and ours on its. reqDelay both spaces re-pulls and protects the game's buffer staging
			// (requesting before BufferTradeParties staged gBlockSendBuffer would stream stale bytes).
			if (cl->idle && cl->reqDelay == 0 &&
			    cl->blockSeq != CL_BLK_LINKCMD && cl->blockSeq != CL_BLK_LINKPLAYER) {
				cl_set_handler(cl, CLH_BLOCK_REQ, cl->requestBlockSize);
				cl->reqDelay = 60;                         // one pull per ~second until the stage advances
			}
			return;
		}

		// --- negotiation (handleTradeNegotiations, tradeConnection.cpp:131-204) ---
		// ORDER (run #8 lesson): RECORD the incoming frame FIRST — an outbound action that returned
		// early was swallowing the game's INIT header, so the command CONT behind it got dropped by
		// the negArmed gate. The switch below only records/holds (cancel paths install handlers);
		// then AT MOST ONE outbound action fires per idle frame.
		switch (cmd[0]) {
		case LINKCMD_INIT_BLOCK:                       // a 20-byte command block header: arm ONE command CONT
			cl->negArmed = 1;
			break;
		case LINKCMD_CONT_BLOCK:                       // :153-191 real cmd in command[1]
			if (!cl->negArmed) break;                  // the block's 2nd CONT carries STALE bytes (trade.c:315)
			cl->negArmed = 0;
			switch (cmd[1]) {
			case LINKCMD_INIT_BLOCK:                    // :157-162 the CONFIRM chain
				// 2-CONSOLE CONFIRM GATE. The local game opening the INIT_BLOCK-wrapped confirm chain
				// is OUR side confirming the trade -> emit CL_EV_CONFIRM to the peer (once). The
				// partner's reply that begins the trade (INIT_BLOCK + followup START_TRADE) must be
				// gated on the PEER having ALSO confirmed: until partnerConfirmed, HOLD (answer EMPTY,
				// the benign "waiting on your friend to confirm" state — no link timeout). Never
				// START_TRADE on a guess; both sides must independently confirm.
				if (!cl->localConfirmed) {
					cl->localConfirmed = 1;
					cl_out_push(cl, CL_EV_CONFIRM, 0, 0, 0);
				}
				// mark HELD (once): the LEVEL-TRIGGERED outbound below releases it. After the chain
				// started, further confirm frames from the game must NOT re-arm the hold.
				if (!cl->confirmChainStarted) {
					cl->confirmHeld = 1;
					cl_set_handler(cl, CLH_EMPTY, 0);
				}
				break;
			case LINKCMD_READY_TO_TRADE: {             // :165-169 the SELECT gate
				// 2-CONSOLE SELECT GATE — the crux. The local game's READY_TO_TRADE carries the slot
				// IT chose in cmd[2]; record it and emit CL_EV_SELECT(i) to the peer (once). The
				// partner's SET_MONS_TO_TRADE(j) MUST use the PEER's chosen slot (partnerSelectSlot)
				// and may only ship once the peer's CL_EV_SELECT has arrived. Until then HOLD: answer
				// EMPTY (the benign "waiting for your friend to choose" screen, no link timeout). We
				// NEVER feed a guessed/default slot — that is what would make the two consoles swap a
				// DIFFERENT pair and corrupt the save.
				int i = (int)(uint8_t)cmd[2];
				if (cl->localSelectSlot < 0) {
					cl->localSelectSlot = i;
					cl_out_push(cl, CL_EV_SELECT, (uint8_t)i, 0, 0);
				}
				// mark HELD (once): the LEVEL-TRIGGERED outbound below releases it (run #8: the release
				// must not depend on the game re-sending a frame it only sends once).
				if (!cl->setMonsCommitted) {
					cl->selectHeld = 1;
					cl_set_handler(cl, CLH_EMPTY, 0);
				}
				break;
			}
			case LINKCMD_SET_MONS_TO_TRADE:            // the game-LEADER's broadcast (HOST role): its own
				// selection is silent-local; this broadcast is the only wire trace of it. Record + push
				// CL_EV_SELECT so the PEER console's leader can release its SET_MONS.
				if (cl->role == CL_SLAVE && cl->localSelectSlot < 0) {
					cl->localSelectSlot = (int)(uint8_t)cmd[2];
					cl_out_push(cl, CL_EV_SELECT, (uint8_t)cmd[2], 0, 0);
				}
				break;
			case LINKCMD_REQUEST_CANCEL:               // :172-180 local game cancelled
				cl_send_link_command(cl, LINKCMD_REQUEST_CANCEL, 0);
				cl->followupPending = 1;
				cl->followupCmd = LINKCMD_BOTH_CANCEL_TRADE;
				cl->cancelDriven = 1;
				cl->nextIsLounge = 1;                  // cancel exit -> the ROOM session (0x1111), not the anim
				cl_out_push(cl, CL_EV_CANCEL, 0, 0, 0);
				break;
			case LINKCMD_READY_CANCEL_TRADE:           // :182-188
				cl_send_link_command(cl, LINKCMD_INIT_BLOCK, 0);
				cl->followupPending = 1;
				cl->followupCmd = LINKCMD_PLAYER_CANCEL_TRADE;
				cl->nextIsLounge = 1;                  // cancel path -> room session next
				break;
			default: break;
			}
			break;

		case LINKCMD_READY_CLOSE_LINK:                 // :193-198 close -> anim/finish session, or the ROOM on a cancel
			cl_set_handler(cl, CLH_CLOSE_LINK, 0);
			cl->pendingClose = 1;
			if (cl->nextIsLounge) cl->roomKeys = 0;   // re-entry: the heartbeat re-arms on this room's standby
			cl->section = cl->nextIsLounge ? CL_SEC_LOUNGE : CL_SEC_DISCONNECT;
			cl->nextIsLounge = 0;
			cl->blockSeq = CL_BLK_LINKPLAYER;
			break;
		default: break;
		}

		if (!cl->idle) return;                         // a reply/close is streaming; outbound waits

		// followup: ship a queued 2nd command on the next idle tick (:143-149)
		if (cl->followupPending) {
			cl->followupPending = 0;
			cl_send_link_command(cl, cl->followupCmd, 0);
			return;
		}

		// A peer CL_EV_CANCEL drives the partner's cancel path (tradeConnection.cpp:172-180).
		if (cl->partnerCancelled && !cl->cancelDriven) {
			cl->cancelDriven = 1;
			cl_send_link_command(cl, LINKCMD_REQUEST_CANCEL, 0);
			cl->followupPending = 1;
			cl->followupCmd = LINKCMD_BOTH_CANCEL_TRADE;
			cl->nextIsLounge = 1;                      // cancel path -> room session next
			return;
		}

		// LEVEL-TRIGGERED coordination (run #8): the game sends its select/confirm ONCE then waits
		// silently; the peer's event lands seconds later over the radio, so the release/announce must
		// be re-evaluated EVERY idle frame — never edge-triggered by a game frame.
		// Role split (trade.c): the LEADER (CL_MASTER partner, JOIN console) pairs + broadcasts
		// SET_MONS/START_TRADE; the FOLLOWER (CL_SLAVE partner, HOST console) ANNOUNCES the peer's
		// select/confirm to the game-Leader ([AABB, slot] / [BBBB, 0]) — the game-Leader's own select
		// is silent-local (SetReadyToTrade, trade.c:1434-1449), so the announcement is OUR job.
		if (cl->role == CL_MASTER) {
			if (cl->selectHeld && cl->partnerSelectSlot >= 0) {
				cl->selectHeld = 0;
				cl->setMonsCommitted = 1;
				cl_send_link_command(cl, LINKCMD_SET_MONS_TO_TRADE, (uint16_t)cl->partnerSelectSlot);
				return;
			}
			if (cl->confirmHeld && cl->partnerConfirmed) {
				cl->confirmHeld = 0;
				cl->confirmChainStarted = 1;
				cl_send_link_command(cl, LINKCMD_INIT_BLOCK, 0);
				cl->followupPending = 1;
				cl->followupCmd = LINKCMD_START_TRADE;
				return;
			}
		} else {
			if (!cl->sentPeerSelect && cl->partnerSelectSlot >= 0) {
				cl->sentPeerSelect = 1;
				cl_send_link_command(cl, LINKCMD_READY_TO_TRADE, (uint16_t)cl->partnerSelectSlot);
				return;
			}
			if (!cl->sentPeerConfirm && cl->partnerConfirmed) {
				cl->sentPeerConfirm = 1;
				cl_send_link_command(cl, LINKCMD_INIT_BLOCK, 0);   // the follower confirm; the game-Leader broadcasts CCDD
				return;
			}
		}
	}

	// ----------------------------------------------------------------------------------------
	// SETUP (tradeSetup.cpp): LinkPlayer + TrainerCard, scripted menu nav, then -> connection.
	// ----------------------------------------------------------------------------------------
	case CL_SEC_SETUP:
	{
		// master pre-switch: when at RequestTrainerCard and idle, pull the peer's card (:31-38)
		if (cl->role == CL_MASTER && cl->blockSeq == CL_BLK_REQ_TRAINERCARD && cl->idle) {
			cl_set_handler(cl, CLH_BLOCK_REQ, 2);
			cl->blockSeq = CL_BLK_TRAINERCARD;
			return;
		}
		switch (cmd[0]) {
		case LINKCMD_INIT_BLOCK:                       // :43-72
			cl_capture_feed_init(cl, cmd);             // the game's own stream header (LinkPlayer=60B)
			switch (cl->blockSeq) {
			case CL_BLK_LINKPLAYER:
				cl_ship_linkplayer(cl);
				cl->blockSeq = (cl->role == CL_MASTER) ? CL_BLK_REQ_TRAINERCARD : CL_BLK_TRAINERCARD;
				break;
			case CL_BLK_TRAINERCARD: {
				// Run #12: the ALL-ZERO canned card black-screened the card viewer the first time
				// the user actually looked at it (gTrainerCards[] is rendered as-is; a 0x00-filled
				// name has no 0xFF terminator). Serve the PEER's REAL card when we both have it AND
				// the latched identity is the peer's real one (the receiver parses the card by the
				// VERSION in the identity we served — a real card under a canned identity would be
				// misparsed); otherwise a WELL-FORMED canned card coherent with the canned identity.
				if (cl->identityWasReal && cl->partnerHasCard)
					memcpy(s_blockScratch, cl->partnerCard, CL_TRAINERCARD_WIRE_BYTES);
				else
					cl_make_trainer_card(s_blockScratch);
				cl_block_setup(cl, s_blockScratch, CL_TRAINERCARD_WIRE_BYTES, CL_TRAINERCARD_WIRE_BYTES);
				cl_set_handler(cl, CLH_BLOCK, 0);
				break;
			}
			default: break;
			}
			break;
		case LINKCMD_CONT_BLOCK:                       // the game streaming its LinkPlayer/trainer card
			cl_capture_feed_cont(cl, cmd);
			break;
		case LINKCMD_READY_EXIT_STANDBY:               // :74-76
			cl_set_handler(cl, CLH_EXIT_STANDBY, 0);
			cl->roomKeys = 1;                          // leaving standby = entering the room phase
			break;
		case LINKCMD_SEND_HELD_KEYS:                   // :78-96 the room key exchange is live
			cl->roomKeys = 1;
			if (cmd[1] == LINK_KEY_CODE_EXIT_ROOM) {   // the game is LEAVING the room (run #12)
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_EXIT_ROOM);   // our exit key passes stage 1
				cl->exitPending = 1;
				if (!cl->exitEmitted) {                // walk the PEER's game out too (real-partner rule)
					if (cl_out_push(cl, CL_EV_EXIT_ROOM, 0, 0, 0))
						cl->exitEmitted = 1;
				}
			}
			break;                                     // the proactive heartbeat below does the rest
		case LINKCMD_READY_CLOSE_LINK:                 // :98-101 -> connection, or the EXIT's close
			cl_set_handler(cl, CLH_CLOSE_LINK, 0);
			cl->pendingClose = 1;
			if (cl->exitPending) {
				// The ROOM-EXIT close (run #12): answer the 0x5FFF (the game's WaitCloseLink needs a
				// 5FFF from EVERY slot) and the session is OVER — the game runs CloseLink/DisableSerial
				// and warps out. Do NOT arm CONNECTION; the driver's no-transfer gap detector runs
				// cl_reset_session for the next club visit.
				cl->sessionEnded = 1;
				cl->roomKeys = 0;
				break;
			}
			cl->section = CL_SEC_CONNECTION;
			cl->blockSeq = CL_BLK_LINKPLAYER;
			cl->movementIndex = 0;
			cl->roomKeys = 0;                          // the machine conversation is menu-based, not keys
			break;
		default: break;
		}
		// PROACTIVE ROOM HEARTBEAT (HW run #5: the reactive keepalive failed — a FROZEN slave cannot
		// send the CAFE frame that would trigger a reactive reply). A Gen-3 SLAVE advances its main
		// loop ONLY on received REAL frames (received-nothing = a skipped frame), so once the room is
		// live the MASTER partner must send key frames CONTINUOUSLY, unprompted — it IS the slave's
		// heartbeat. The script walks the fake player in; then standing-still (EMPTY) frames forever.
		if (cl->roomKeys && cl->idle) {
			if (cl->partnerExitRoom && !cl->exitPending) {
				// The PEER's player is leaving (run #12): a real partner broadcasts EXIT_ROOM, which
				// makes OUR game auto-run its terminate-link script (both players leave together).
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_EXIT_ROOM);
			} else if (cl->movementIndex < 6) {
				static const uint16_t script[6] = {
					LINK_KEY_CODE_DPAD_UP, LINK_KEY_CODE_DPAD_UP, LINK_KEY_CODE_DPAD_LEFT,
					LINK_KEY_CODE_DPAD_UP, LINK_KEY_CODE_DPAD_RIGHT, LINK_KEY_CODE_READY
				};
				cl_set_handler(cl, CLH_MOVE, script[cl->movementIndex]);
				cl->movementIndex++;
			} else {
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_EMPTY);
			}
		}
		return;
	}

	// ----------------------------------------------------------------------------------------
	// DISCONNECT (tradeDisconnected.cpp): re-announce identity, READY/CONFIRM finish, -> connection.
	// ----------------------------------------------------------------------------------------
	case CL_SEC_DISCONNECT:
	{
		// FinishTrade follow-up: ship CONFIRM_FINISH_TRADE (tradeDisconnected.cpp:46-56)
		if (cl->followupPending && cl->idle) {
			cl->followupPending = 0;
			s_negWords[0] = LINKCMD_CONFIRM_FINISH_TRADE; s_negWords[1] = 0;
			cl_block_setup(cl, s_negWords, sizeof(s_negWords), 20);
			cl_set_handler(cl, CLH_BLOCK, 0);
			return;
		}
		switch (cmd[0]) {
		case LINKCMD_INIT_BLOCK:                        // :21-28 re-ship LinkPlayer
			if (cl->blockSeq == CL_BLK_LINKPLAYER) {
				cl_ship_linkplayer(cl);
				cl->blockSeq = CL_BLK_PARTY0; // reuse as "LinkPlayer sent" marker
			}
			break;
		case LINKCMD_READY_EXIT_STANDBY:               // :60-62
			cl_set_handler(cl, CLH_EXIT_STANDBY, 0);
			break;
		case LINKCMD_CONT_BLOCK:                        // :64-74 READY_FINISH_TRADE
			if (cmd[1] == LINKCMD_READY_FINISH_TRADE) {
				s_negWords[0] = LINKCMD_READY_FINISH_TRADE; s_negWords[1] = 0;
				cl_block_setup(cl, s_negWords, sizeof(s_negWords), 20);
				cl_set_handler(cl, CLH_BLOCK, 0);
				cl->followupPending = 1; // next idle -> CONFIRM_FINISH_TRADE
			}
			break;
		case LINKCMD_READY_CLOSE_LINK:                 // :77-82 -> connection (next trade)
			cl_set_handler(cl, CLH_CLOSE_LINK, 0);
			cl->pendingClose = 1;
			cl->tradeComplete = 1;       // a full trade went through and finished
			cl->section = CL_SEC_CONNECTION;
			cl->blockSeq = CL_BLK_LINKPLAYER;
			// re-arm the whole negotiation + party pipeline for the NEXT trade round (the menu
			// re-opens and the 5-block exchange runs again with the POST-trade parties)
			cl->localSelectSlot = -1; cl->partnerSelectSlot = -1;
			cl->localConfirmed = 0;   cl->partnerConfirmed = 0;
			cl->selectHeld = 0; cl->confirmHeld = 0; cl->setMonsCommitted = 0; cl->confirmChainStarted = 0;
			cl->sentPeerSelect = 0; cl->sentPeerConfirm = 0;
			cl->localPartyBytes = 0; cl->localPartyEmittedChunks = 0;
			cl->partnerPartyBytes = 0;   // the peer re-captures + re-sends its post-trade party
			cl->reqDelay = 60;
			break;
		default: break;
		}
		return;
	}

	// ----------------------------------------------------------------------------------------
	// LOUNGE (tradeLounge.cpp): trade-room idle; LinkPlayer + READY/EXIT echo.
	// ----------------------------------------------------------------------------------------
	case CL_SEC_LOUNGE:
	{
		// The room AFTER a trade/cancel. Same protocol as SETUP (LinkPlayer -> READY_EXIT_STANDBY ->
		// keys) MINUS the walk script. HW run #10: the old LOUNGE ignored READY_EXIT_STANDBY and ran
		// its heartbeat UNGATED, so the re-entry jumped straight to CAFE frames and the SLAVE game hung
		// on a black CB2_Overworld with its LinkPlayer/standby never served. Now it re-establishes fully.
		switch (cmd[0]) {
		case LINKCMD_INIT_BLOCK:                        // re-ship LinkPlayer for the re-entry exchange
			cl_ship_linkplayer(cl);
			break;
		case LINKCMD_READY_EXIT_STANDBY:                // the re-entry's standby round — MUST be answered
			cl_set_handler(cl, CLH_EXIT_STANDBY, 0);
			cl->roomKeys = 1;                           // room now live -> the heartbeat below may run
			break;
		case LINKCMD_SEND_HELD_KEYS:                    // room key exchange is live
			cl->roomKeys = 1;
			if (cmd[1] == LINK_KEY_CODE_EXIT_ROOM) {    // the game is LEAVING the room (run #12)
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_EXIT_ROOM);
				cl->exitPending = 1;
				if (!cl->exitEmitted) {
					if (cl_out_push(cl, CL_EV_EXIT_ROOM, 0, 0, 0))
						cl->exitEmitted = 1;
				}
			} else if (cmd[1] == LINK_KEY_CODE_READY)
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_READY);
			break;
		case LINKCMD_READY_CLOSE_LINK:                  // sit at the machine -> CONNECTION, or the EXIT's close
			cl_set_handler(cl, CLH_CLOSE_LINK, 0);
			cl->pendingClose = 1;
			if (cl->exitPending) {                      // the ROOM-EXIT close (run #12): answer + session over
				cl->sessionEnded = 1;
				cl->roomKeys = 0;
				break;
			}
			cl->section = CL_SEC_CONNECTION;
			cl->blockSeq = CL_BLK_LINKPLAYER;
			cl->roomKeys = 0;
			break;
		default: break;
		}
		// PROACTIVE ROOM HEARTBEAT — gated on roomKeys so the re-entry's handshake + LinkPlayer +
		// standby setup runs FIRST (identical gating to SETUP). No walk script here (the fake player is
		// already placed): just standing-still EMPTY key frames, forever, to clock the slave's main loop.
		if (cl->roomKeys && cl->idle) {
			if (cl->partnerExitRoom && !cl->exitPending)   // the PEER's player is leaving (run #12)
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_EXIT_ROOM);
			else
				cl_set_handler(cl, CLH_MOVE, LINK_KEY_CODE_EMPTY);
		}
		return;
	}
	}
}

// ============================================================================================
// 3. The PacketLayer — the local SIO terminator. (Celio packetLayer.{hpp,cpp}.)
//
//    cl_transfer() = onReceive(local) + onTransmit() + onTransiveDone(), folded into ONE call per
//    SIO transfer. The Zephyr semaphore (awaitTransiveResults) becomes a direct call to
//    cl_section_step() at the m_commandIndex==8 frame boundary.
// ============================================================================================

void cl_init(CelioLink* cl, ClRole role, uint16_t linkType)
{
	memset(cl, 0, sizeof(*cl));
	cl->role     = role;
	cl->linkType = linkType;

	// PacketLayer init state (packetLayer.hpp:234-254)
	cl->state    = CL_ST_HANDSHAKE;
	cl->hsState  = CL_HS_DISABLED;
	cl->crc      = LINK_SLAVE_HANDSHAKE;   // 0xB9A0 — "first crc is always handshake"
	cl->idle     = 1;
	cl->handler  = CLH_EMPTY;
	cl->lastTxHandshake = LINK_HANDSHAKE_DISABLE;
	cl->lastRxHandshake = LINK_HANDSHAKE_DISABLE;

	// Connect drive. Celio's Section::connectAsMaster waits to RECEIVE 0xB9A0 from the GBA, then
	// enableHandshake() (send 0xB9A0), +500ms, connectHandshake() (send 0x8FFF) which flips into
	// command mode (section.hpp:24-37). With no thread/timer we cannot k_sleep 500ms, so we drive
	// the handshake reactively in cl_transfer: start ENABLED (advertise 0xB9A0); the MASTER
	// escalates to CONNECT (0x8FFF) once it has seen the GBA's 0xB9A0. The SLAVE stays ENABLED and
	// leaves handshake when it RECEIVES the GBA's 0x8FFF (the GBA became master). Either path
	// satisfies the "leave on either side == 0x8FFF" rule (packetLayer.cpp:16-23).
	cl->hsState = CL_HS_ENABLED;

	// section / trade defaults — start at SETUP (Celio EmuModule starts at NextSection::setup).
	cl->section  = CL_SEC_SETUP;
	cl->blockSeq = CL_BLK_LINKPLAYER;
	cl->movementIndex = 0;
	cl->requestBlockSize = 2;
	cl->partnerSelectSlot = -1;
	cl->localSelectSlot   = -1;
	cl->outHead = cl->outTail = 0;
}

// transmitHandshake (packetLayer.hpp:181-193)
static uint16_t cl_transmit_handshake(const CelioLink* cl)
{
	switch (cl->hsState) {
	case CL_HS_DISABLED: return LINK_HANDSHAKE_DISABLE;
	case CL_HS_ENABLED:  return LINK_SLAVE_HANDSHAKE;
	case CL_HS_CONNECT:  return LINK_MASTER_HANDSHAKE;
	}
	return 0xDEAD;
}

void cl_rearm_handshake(CelioLink* cl)
{
	cl->state            = CL_ST_HANDSHAKE;
	cl->hsState          = CL_HS_ENABLED;
	cl->hsAdvertiseCount = 0;
	cl->hsRunCount       = 0;
	cl->crc              = LINK_SLAVE_HANDSHAKE;   // "first crc is always handshake"
	cl->commandIndex     = 0;
	cl->transmitIndex    = 0;
	cl->idle             = 1;
	cl->handler          = CLH_EMPTY;
	cl->hIndex           = 0;
	cl->lastTxHandshake  = LINK_HANDSHAKE_DISABLE;
	cl->lastRxHandshake  = LINK_HANDSHAKE_DISABLE;
}

void cl_reset_session(CelioLink* cl)
{
	cl_rearm_handshake(cl);
	cl->section          = CL_SEC_SETUP;       // the game restarts its conversation from the top
	cl->blockSeq         = CL_BLK_LINKPLAYER;
	cl->movementIndex    = 0;
	cl->roomKeys         = 0;
	cl->requestBlockSize = 2;
	cl->partnerPartyHeld = 0;
	cl->selectHeld       = 0;
	cl->confirmHeld      = 0;
	cl->localSelectSlot  = -1;
	cl->partnerSelectSlot= -1;
	cl->localConfirmed   = 0;
	cl->partnerConfirmed = 0;
	cl->setMonsCommitted = 0;
	cl->confirmChainStarted = 0;
	cl->partnerCancelled = 0;
	cl->cancelDriven     = 0;
	cl->nextIsLounge     = 0;
	cl->negArmed         = 0;
	cl->gameInitSize     = 0;
	cl->sentPeerSelect   = 0;
	cl->sentPeerConfirm  = 0;
	cl->reqDelay         = 60;
	cl->localPartyBytes  = 0;
	cl->localPartyEmittedChunks = 0;
	cl->localLPBytes     = 0;
	cl->localLPEmitted   = 0;
	cl->identityLatched  = 0;   // a fresh club visit re-saves identities at the counter
	cl->identityWasReal  = 0;
	cl->followupPending  = 0;
	cl->pendingClose     = 0;
	cl->tradeComplete    = 0;
	// room exit (run #12): the exit is over once the session restarts; re-arm everything
	cl->exitPending      = 0;
	cl->exitZeroRun      = 0;
	cl->exitEmitted      = 0;
	cl->partnerExitRoom  = 0;
	cl->sessionEnded     = 0;
	// trainer card capture: the game re-streams its card at the next linkup
	cl->localCardBytes   = 0;
	cl->localCardEmitted = 0;
	// PRESERVED: partnerParty/partnerPartyBytes/partnerHasLinkPlayer AND partnerCard/partnerHasCard
	// (the peer resends on ITS restart; the cached identity+card make the NEXT club visit show the
	// REAL partner name and card with no first-serve race).
}

uint32_t cl_next_delay_us(const CelioLink* cl)
{
	switch (cl->state) {
	case CL_ST_HANDSHAKE: return CL_TIMING_HANDSHAKE_US;
	case CL_ST_CRC:       return CL_TIMING_FRAME_US;   // frame boundary (next word = the CRC)
	default:              return CL_TIMING_WORD_US;    // mid-frame command words
	}
}

uint16_t cl_transfer(CelioLink* cl, uint16_t local_word)
{
	uint16_t tx = 0x0000;

	// RE-HANDSHAKE (Fix 2): the game tears down + restarts the link at each cable-club phase boundary
	// (room -> trade-data exchange when you sit at the machine). It signals the restart by sending
	// handshake words again (0xB9A0 advertise / 0xD15E disable) while we are mid-command. Celio spins up
	// a FRESH PacketLayer per section; we have ONE persistent layer, so detect the restart (a short run
	// of handshake words) and RE-ARM the PacketLayer. Without this the partner answers 0x0000 to the
	// game's 0xB9A0 -> RECEIVED_NOTHING (the 0630 'sit at the trade machine' failure: w0=B9A0,w1=0000).
	// section/blockSeq were already advanced by READY_CLOSE_LINK, so we only re-arm the PacketLayer.
	if (cl->state != CL_ST_HANDSHAKE) {
		if (local_word == LINK_SLAVE_HANDSHAKE || local_word == LINK_HANDSHAKE_DISABLE) {
			if (++cl->hsRunCount >= CL_HS_REARM_RUN) {
				cl_rearm_handshake(cl);
			}
		} else {
			cl->hsRunCount = 0;
		}
	}

	switch (cl->state) {

	// -------- HANDSHAKE: single repeated words, NOT framed (packetLayer state handshake) --------
	case CL_ST_HANDSHAKE: {
		uint16_t rx = local_word;
		cl->lastRxHandshake = rx;

		// Transmit the CURRENT handshake word FIRST, then update state for the NEXT transfer. This
		// preserves Celio's connectAsMaster ordering: enableHandshake() (advertise 0xB9A0) is held
		// for a while (Celio: +500ms) BEFORE connectHandshake() (drive 0x8FFF). Escalating in the
		// same transfer that first sees the GBA's 0xB9A0 would DROP our 0xB9A0 advertisement (the
		// costly gen3-link bug). So the master sends 0xB9A0 at least once, then escalates.
		tx = cl_transmit_handshake(cl);
		cl->lastTxHandshake = tx;

		// MASTER: once we have BOTH advertised 0xB9A0 (tx just now) AND seen the GBA advertise as a
		// slave (rx==0xB9A0), escalate to CONNECT so the NEXT transfer drives 0x8FFF.
		// Fix 1: MASTER drives 0x8FFF. Escalate when we've SEEN the GBA's 0xB9A0 (Celio connectAsMaster)
		// OR after advertising 0xB9A0 for CL_HS_ESCALATE_AFTER transfers. CRITICAL for the JOIN/slave-game
		// side: an emulated game 'waiting for connection' writes 0x0000 and will NOT send 0xB9A0 until it
		// SEES a master's 0x8FFF -> the master must DRIVE 8FFF (the 0630 deadlock: partner stuck at B9A0).
		if (cl->role == CL_MASTER && cl->hsState == CL_HS_ENABLED) {
			cl->hsAdvertiseCount++;
			if (rx == LINK_SLAVE_HANDSHAKE || cl->hsAdvertiseCount >= CL_HS_ESCALATE_AFTER)
				cl->hsState = CL_HS_CONNECT;
		}

		// Leave handshake the instant EITHER side's word == 0x8FFF (packetLayer.cpp:16-23). Then the
		// first CRC word (seeded 0xB9A0) is emitted next transfer.
		// Leave handshake EXACTLY when 0x8FFF actually crosses the wire — transmitted OR received THIS
		// transfer (Celio packetLayer.cpp:16-23). The GBA that receives 8FFF leaves in the same transfer
		// we transmit it, so alignment is automatic. (HW run #3 bug: a cleverer "wait for the game's
		// B9A0 ack while in CONNECT" condition cascaded with the escalate check — the partner escalated
		// AND left in ONE transfer, so 0x8FFF was NEVER transmitted; the game kept handshaking (B9A0),
		// which tripped the re-handshake detector -> a 4-word loop, 19580 transfers of it. Don't be
		// clever: the escalate above only takes effect NEXT transfer, when 8FFF is genuinely driven.)
		int leaveHs = (rx == LINK_MASTER_HANDSHAKE) || (tx == LINK_MASTER_HANDSHAKE);
		if (leaveHs) {
			cl->state = CL_ST_CRC;
			// Install the first section's link-type announce (Celio sets it right after connect,
			// e.g. tradeSetup.cpp:21-23 sendLinkTypeCommand(m_linkType)).
			cl_set_handler(cl, CLH_LINKTYPE, cl_section_linktype(cl));
		}
		return tx;
	}

	// -------- CRC: exactly ONE word. transmitCrc returns running m_crc; received CRC ignored. -----
	case CL_ST_CRC: {
		(void)local_word;                  // receiveCrc is a no-op (packetLayer.hpp:197-200)
		tx = cl->crc;                      // transmitCrc (packetLayer.hpp:202-205)
		// onTransiveDone crc-case (packetLayer.cpp:27-33): -> command, reset CRC to 0 for the frame.
		cl->state = CL_ST_COMMAND;
		cl->crc = 0x0000;
		cl->commandIndex = 0;
		cl->transmitIndex = 0;
		return tx;
	}

	// -------- COMMAND: 8 words. Additive mutual CRC (tx + rx). Gate on commandIndex==8. ---------
	case CL_ST_COMMAND: {
		// receiveCommand (packetLayer.hpp:209-214): store rx, fold into CRC.
		uint16_t rx = local_word;
		cl->received[cl->commandIndex] = rx;
		cl->crc = (uint16_t)(cl->crc + rx);
		cl->commandIndex++;

		// transmitCommand (packetLayer.hpp:216-223): get the handler's word, fold into CRC, record.
		tx = cl_handler_transive(cl);
		cl->crc = (uint16_t)(cl->crc + tx);
		cl->transmitted[cl->transmitIndex] = tx;
		cl->transmitIndex++;

		// onTransiveDone command-case (packetLayer.cpp:35-62): frame completes at index 8.
		if (cl->commandIndex == 8) {
			uint16_t frame[8];
			memcpy(frame, cl->received, sizeof(frame)); // snapshot received[] (awaitTransiveResults)
			cl->commandIndex = 0;
			cl->transmitIndex = 0;
			cl->state = CL_ST_CRC;

			// run the active handler's transiveDone; if done, retire -> idle + emptyCommand.
			if (cl_handler_done(cl)) {
				cl->idle = 1;
				cl->handler = CLH_EMPTY;
				cl->hIndex = 0;
			}

			// THE SECTION STEP — Celio's awaitTransiveResults() wakeup. Dispatch on the completed
			// command (the synthesis) and install the next reply handler.
			cl_section_step(cl, frame);
		}
		return tx;
	}
	}

	return tx;
}

// ============================================================================================
// 4. The semantic seam (the only data that crosses UDS).
// ============================================================================================

int cl_take_outgoing(CelioLink* cl, ClEvent* out)
{
	if (cl->outHead == cl->outTail) return 0;
	*out = cl->outQueue[cl->outHead];
	cl->outHead = (cl->outHead + 1) % CL_EVENT_QUEUE_DEPTH;
	return 1;
}

void cl_put_incoming(CelioLink* cl, const ClEvent* in)
{
	switch (in->type) {
	case CL_EV_LINKPLAYER:
		if (in->len <= sizeof(cl->partnerLinkPlayer)) {
			memcpy(cl->partnerLinkPlayer, in->data, in->len);
			cl->partnerLinkPlayerLen = in->len;
			cl->partnerHasLinkPlayer = 1;
		}
		break;
	case CL_EV_PARTY_CHUNK: {
		uint32_t off = (uint32_t)in->arg * CL_PARTY_CHUNK_BYTES;
		uint16_t n = in->len;
		if (off + n > CL_PARTY_BYTES) n = (uint16_t)(CL_PARTY_BYTES - off);
		if (n) {
			memcpy(cl->partnerParty + off, in->data, n);
			if (off + n > cl->partnerPartyBytes) cl->partnerPartyBytes = off + n;
		}
		break;
	}
	case CL_EV_SELECT:
		cl->partnerSelectSlot = in->arg;
		break;
	case CL_EV_CONFIRM:
		cl->partnerConfirmed = 1;
		break;
	case CL_EV_CANCEL:
		cl->partnerCancelled = 1;
		break;
	case CL_EV_CLOSE:
		cl->partnerClosed = 1;
		break;
	case CL_EV_EXIT_ROOM:              // the peer's player left the room -> walk our game out too
		cl->partnerExitRoom = 1;
		break;
	case CL_EV_TRAINERCARD:            // the peer's real trainer card (cached across sessions/visits)
		if (in->len >= CL_TRAINERCARD_WIRE_BYTES) {
			memcpy(cl->partnerCard, in->data, CL_TRAINERCARD_WIRE_BYTES);
			cl->partnerHasCard = 1;
		}
		break;
	default: break;
	}
}

int cl_trade_complete(const CelioLink* cl)
{
	return cl->tradeComplete ? 1 : 0;
}

void cl_get_status(const CelioLink* cl, ClStatus* out)
{
	out->role          = (uint8_t)cl->role;
	out->state         = (uint8_t)cl->state;
	out->handler       = (uint8_t)cl->handler;
	out->section       = (uint8_t)cl->section;
	out->blockSeq      = (uint8_t)cl->blockSeq;
	out->tradeComplete = cl->tradeComplete;
	out->crc           = cl->crc;
	out->frameCount    = cl->frameCount;
	out->partnerPartyBytes = cl->partnerPartyBytes;
	out->partnerPartyHeld  = cl->partnerPartyHeld;
	out->selectHeld        = cl->selectHeld;
	out->confirmHeld       = cl->confirmHeld;
	out->localSelectSlot   = (int8_t)cl->localSelectSlot;
	out->partnerSelectSlot = (int8_t)cl->partnerSelectSlot;
	out->exitPending       = cl->exitPending;
	out->sessionEnded      = cl->sessionEnded;
	out->partnerHasCard    = cl->partnerHasCard;
	out->identityWasReal   = cl->identityWasReal;
}

// ============================================================================================
// STUBBED vs FULLY IMPLEMENTED — honest accounting of the 2-console live coordination.
//
// FULLY IMPLEMENTED (ported 1:1 from Celio, exercised by the T0 test):
//   * PacketLayer: handshake polarity + leave-on-0x8FFF, single-word handshake, additive mutual
//     CRC (seed 0xB9A0 once, reset 0 per frame), 8-word command gate, idle-on-handler-done.
//   * All cl_* handlers: emptyCommand, sendLinkType, blockCommand (14-byte chunk + advertised-size
//     zero-pad + done-gated-on-advertised-count), blockCommandRequest, moveCommand (21x), exit-
//     standby, close-link — each emits the exact [OPCODE,ARG,0..] frame.
//   * Trade-section dispatcher: SETUP/CONNECTION/DISCONNECT/LOUNGE switch(cmd[0]); the block
//     sequence LinkPlayer->Party0/1/2->Mail->Ribbons; CONT_BLOCK-wrapped negotiation
//     READY_TO_TRADE->SET_MONS_TO_TRADE, START_TRADE, cancel paths, READY_FINISH/CONFIRM_FINISH.
//
// WIRED (the 2-console seam — beyond Celio's one-shot-file model):
//   * cl_capture_local_party() captures the LOCAL game's streamed party (CONT_BLOCK) and emits
//     CL_EV_PARTY_CHUNK; cl_ship_linkplayer emits/uses the LinkPlayer; cl_put_incoming feeds the
//     PEER's party+identity into the synthesized partner. The transport agent carries these over UDS.
//
// LIVE 2-CONSOLE COORDINATION — NOW FULLY WIRED (proven at the EVENT level by the PC T1 test):
//   * PARTY-BLOCK HOLD. The block exchange HOLDS at PartyPart0 (answers EMPTY, does not advance)
//     until the peer's party has arrived (cl_partner_party_ready) — so the partner offered to the
//     local game is the PEER's real party, never canned filler. Released the instant the peer's
//     LinkPlayer + first CL_EV_PARTY_CHUNK land.
//   * SELECT GATE. On the local game's READY_TO_TRADE we record its chosen slot i and emit
//     CL_EV_SELECT(i) (once). The partner's SET_MONS_TO_TRADE uses the PEER's slot j and is HELD
//     (EMPTY frame) until the peer's CL_EV_SELECT arrives (partnerSelectSlot>=0). A guessed/default
//     slot is NEVER fed. The held state IS the game's "waiting for your friend to choose" screen,
//     which has no link timeout, so arbitrary radio latency is safe.
//   * CONFIRM GATE. Opening the INIT_BLOCK-wrapped confirm chain emits CL_EV_CONFIRM (once); the
//     partner's START_TRADE release is HELD until the peer's CL_EV_CONFIRM arrives.
//   * CANCEL / CLOSE. A peer CL_EV_CANCEL drives the partner's REQUEST_CANCEL->BOTH_CANCEL_TRADE
//     path; the local game's own REQUEST_CANCEL emits CL_EV_CANCEL outbound; close is the existing
//     READY_CLOSE_LINK -> DISCONNECT path (and a peer CL_EV_CLOSE is captured for the driver).
//   Both sides independently converge on the SAME (i,j) and each already holds the other's full
//   party, so the swap+save is consistent with NO per-frame data exchange.
//
// PARTIALLY STUBBED / TO REVISIT ON HARDWARE (called out per the prompt):
//   1. PROACTIVE BLOCK-REQUEST timing. Celio pulls the peer's block on a k_timer (2000ms) so the
//      pull interleaves with its own sends (tradeConnection.cpp:23-30,48). We have no timer; we
//      pull SEND_BLOCK_REQ whenever idle between block states (and SUPPRESS the pull while the
//      party-block HOLD is active). On hardware the exact interleave with the GBA's own INIT_BLOCK
//      requests must be verified — if the pull races the game's own request the block stream could
//      stall. (Low risk: the game drives INIT_BLOCK; our pull is a best-effort nudge.)
//   2. The HOLD is proven at the EVENT level on the PC (the synthesis answers EMPTY and does not
//      advance until the peer event lands). What the PC test CANNOT prove: that a REAL Gen-3 game
//      core, parked on its "waiting for your friend" screen, truly tolerates the arbitrary-latency
//      EMPTY/idle frames without tripping gRemoteLinkPlayersNotReceived. That is the M1 hardware
//      gate (two real cores + UDS), not a PC-provable property. (gen3-link-protocol.md: a WHOLE
//      idle frame is RECEIVED_NOTHING and skipped before any CRC check, so this SHOULD be safe — but
//      "should" here is exactly what only real hardware can settle.)
//   3. Mail / Ribbons content is canned/empty (matches Celio). TrainerCard is now EXCHANGED for
//      real (run #12): the local game's 100-byte card is captured at linkup (CL_EV_TRAINERCARD)
//      and the peer's real card is served — but only when the latched identity is also the peer's
//      real one (the receiver parses the card by the identity's VERSION field). First-ever linkup
//      still serves a WELL-FORMED canned card (cl_make_trainer_card — never the all-zero card that
//      black-screened the run-#12 card view); the cache makes later linkups/visits real.
//   4. ROOM EXIT (run #12): committed by the game's EXIT_ROOM key -> echo + CL_EV_EXIT_ROOM to the
//      peer (both players leave together); the fade detector silences the heartbeat (the game's
//      LinkCB_ReadyCloseLink is gated on an EMPTY recv queue, pret link.c:1319) and the exit's
//      0x5FFF is answered as SESSION END (not the sit-at-machine -> CONNECTION route). The next
//      club visit re-establishes via the driver's no-transfer gap reset.
// ============================================================================================
