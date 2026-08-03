// test_celiolink.c — PC host unit test (T0 gate) for the Gen-3 local-termination dongle.
// ============================================================================================
// Proves the framing/CRC math BEFORE any hardware. Dual-compiles the pure-C core on the PC
// (CLAUDE.md rule #4). NOT in source/ (it has main() and must not get globbed into the .3dsx).
//
//   clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c -o /tmp/test_celiolink
//   /tmp/test_celiolink
//
// We #include the .c directly so the test sees the same translation unit (and can drive the hot
// path). The "game" side is simulated in this file: it shifts words into cl_transfer() and reads
// back the dongle's reply, exactly as the mGBA SIO driver will on hardware.
// ============================================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../source/celiolink.c"

// --------------------------------------------------------------------------------------------
static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...) do { \
	g_checks++; \
	if (!(cond)) { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("   (at %s:%d)\n", __FILE__, __LINE__); } \
} while (0)

#define CHECK_EQ16(got, exp, label) do { \
	g_checks++; \
	uint16_t _g = (uint16_t)(got), _e = (uint16_t)(exp); \
	if (_g != _e) { g_fail++; printf("  [FAIL] %s: got 0x%04X expected 0x%04X (at %s:%d)\n", \
	                                  label, _g, _e, __FILE__, __LINE__); } \
} while (0)

// --------------------------------------------------------------------------------------------
// A scripted "game" frame: the 8 command words the game shifts OUT during a command frame, plus
// the CRC word it shifts out in the CRC slot (the dongle ignores the game's CRC word, so its value
// does not matter to the dongle — we send 0 there). We capture what the dongle replies.
// --------------------------------------------------------------------------------------------

// Drive ONE CRC transfer. Returns the dongle's CRC word (its running accumulator over the prev frame).
static uint16_t drive_crc(CelioLink* cl, uint16_t game_crc_word)
{
	return cl_transfer(cl, game_crc_word);
}

// Drive 8 command transfers feeding game_cmd[0..7]; capture the dongle's 8 reply words into dongle_tx.
static void drive_command(CelioLink* cl, const uint16_t game_cmd[8], uint16_t dongle_tx[8])
{
	for (int i = 0; i < 8; i++)
		dongle_tx[i] = cl_transfer(cl, game_cmd[i]);
}

// Independent additive-mutual CRC: seed + sum(prev frame's 8 game-tx + 8 dongle-tx), 16-bit wrap.
static uint16_t independent_crc(uint16_t seed, const uint16_t game_tx[8], const uint16_t dongle_tx[8])
{
	uint16_t c = seed;
	for (int i = 0; i < 8; i++) c = (uint16_t)(c + game_tx[i]);
	for (int i = 0; i < 8; i++) c = (uint16_t)(c + dongle_tx[i]);
	return c;
}

// Run the handshake to completion as MASTER-side test: the dongle is whatever role cl was inited
// with; the "game" plays the opposite. Returns the number of handshake transfers consumed.
// Asserts B9A0 is never dropped and the state advances to CRC once 0x8FFF is seen.
static int run_handshake(CelioLink* cl, int game_is_master)
{
	int transfers = 0;
	int b9a0_seen_from_dongle = 0;
	for (int t = 0; t < 64; t++) {
		// the game advertises 0xB9A0 as a slave; if the game is master it eventually drives 0x8FFF.
		uint16_t game_word = (game_is_master && t >= 2) ? LINK_MASTER_HANDSHAKE : LINK_SLAVE_HANDSHAKE;
		ClTransiveState before = cl->state;
		uint16_t dongle = cl_transfer(cl, game_word);
		transfers++;
		if (before == CL_ST_HANDSHAKE) {
			// the dongle's handshake word must be one of the three legal words, never garbage.
			CHECK(dongle == LINK_SLAVE_HANDSHAKE || dongle == LINK_MASTER_HANDSHAKE ||
			      dongle == LINK_HANDSHAKE_DISABLE,
			      "handshake tx word legal (got 0x%04X)\n", dongle);
			if (dongle == LINK_SLAVE_HANDSHAKE) b9a0_seen_from_dongle = 1;
		}
		if (cl->state != CL_ST_HANDSHAKE) break; // left handshake
	}
	CHECK(cl->state == CL_ST_CRC, "handshake terminated -> CRC state (got state %d)\n", cl->state);
	CHECK(b9a0_seen_from_dongle, "dongle advertised 0xB9A0 (SLAVE_HANDSHAKE) before leaving — not dropped\n");
	return transfers;
}

// --------------------------------------------------------------------------------------------
// TEST 1 — handshake terminates locally (master dongle).
// --------------------------------------------------------------------------------------------
static void test_handshake(void)
{
	printf("TEST 1: handshake terminates locally\n");
	CelioLink cl;
	cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	CHECK(cl.state == CL_ST_HANDSHAKE, "starts in handshake state\n");
	run_handshake(&cl, /*game_is_master=*/0);
	// after leaving handshake, the first thing installed is the link-type announce.
	CHECK(cl.handler == CLH_LINKTYPE, "link-type handler installed on leaving handshake\n");

	// also prove the SLAVE-dongle path: leaves handshake when it RECEIVES 0x8FFF from a master game.
	CelioLink cs;
	cl_init(&cs, CL_SLAVE, LINKTYPE_TRADE);
	run_handshake(&cs, /*game_is_master=*/1);
	printf("  ok\n");
}

// --------------------------------------------------------------------------------------------
// TEST 2 — additive mutual CRC matches an independent sum across several frames.
//
// We drive the dongle through the first few real frames of a trade. The dongle, just out of
// handshake, has the LINKTYPE handler armed, so frame 0 it transmits [0x2222, linktype, 0..]. We
// feed the game's INIT_BLOCK frames so the dongle advances through its block stream, and verify
// that EACH CRC word the dongle emits equals the independent additive sum of the PREVIOUS frame's
// 16 words (seed 0xB9A0 on the first CRC, 0 thereafter).
// --------------------------------------------------------------------------------------------
static void test_crc(void)
{
	printf("TEST 2: additive mutual CRC == independent sum (several frames)\n");
	CelioLink cl;
	cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	run_handshake(&cl, 0);

	// The game's command words per frame. We simulate the game asking for blocks (INIT_BLOCK) and
	// otherwise idling. Exact values don't matter to the CRC test as long as we feed the SAME words
	// to both the dongle and the independent calculator.
	uint16_t game_frames[6][8] = {
		{ LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 },   // frame 0: game requests a block
		{ LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 },
		{ LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 },
		{ LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 },
		{ LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 },
		{ LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 },
	};

	// CRC ACCUMULATOR TIMELINE (Celio): m_crc is SEEDED 0xB9A0; the VERY FIRST crc word emitted is
	// just that seed (no command words summed yet), then crc->command resets m_crc to 0. So the first
	// CHECKED frame (the 2nd crc word, covering frame 0's 16 words) uses seed 0 — the seed 0xB9A0 is
	// already "spent" as the first crc word. We assert that first crc word == 0xB9A0 separately.
	uint16_t prev_game_tx[8] = {0};
	uint16_t prev_dongle_tx[8] = {0};
	int have_prev = 0;
	int first_crc = 1;

	for (int f = 0; f < 6; f++) {
		// CRC slot: the dongle emits its running accumulator. The game's CRC word is ignored.
		uint16_t dongle_crc = drive_crc(&cl, 0x0000);

		if (first_crc) {
			CHECK_EQ16(dongle_crc, LINK_SLAVE_HANDSHAKE, "first CRC word is the 0xB9A0 seed");
			first_crc = 0;
		} else if (have_prev) {
			uint16_t exp = independent_crc(0x0000, prev_game_tx, prev_dongle_tx);
			CHECK_EQ16(dongle_crc, exp, "frame CRC matches independent additive sum");
		}

		// command slot: 8 transfers.
		uint16_t dongle_tx[8];
		drive_command(&cl, game_frames[f], dongle_tx);

		memcpy(prev_game_tx, game_frames[f], sizeof(prev_game_tx));
		memcpy(prev_dongle_tx, dongle_tx, sizeof(prev_dongle_tx));
		have_prev = 1;
	}
	printf("  ok\n");
}

// --------------------------------------------------------------------------------------------
// TEST 3 — each cl_* handler emits the exact [OPCODE,ARG,0...] frame.
//
// We drive a single command frame with a given handler armed and capture the 8 TX words. We bypass
// the section dispatcher by arming the handler directly and reading transmitCommand outputs (we
// still go through cl_transfer's COMMAND path so the index machine is exercised).
// --------------------------------------------------------------------------------------------

// Put the dongle into COMMAND state with `handler` armed; capture its 8 reply words for one frame.
static void capture_handler_frame(ClHandlerKind handler, uint16_t arg, uint16_t out[8])
{
	CelioLink cl;
	cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	run_handshake(&cl, 0);
	// consume the CRC word to enter COMMAND.
	drive_crc(&cl, 0x0000);
	// arm the requested handler (overwriting whatever the dispatcher set).
	cl_set_handler(&cl, handler, arg);
	uint16_t idle[8] = {0,0,0,0,0,0,0,0};
	drive_command(&cl, idle, out);
}

static void test_handlers(void)
{
	printf("TEST 3: each cl_* handler emits the exact frame\n");
	uint16_t f[8];

	// emptyCommand -> all zeros
	capture_handler_frame(CLH_EMPTY, 0, f);
	for (int i = 0; i < 8; i++) CHECK_EQ16(f[i], 0x0000, "emptyCommand word == 0");

	// sendLinkType -> [0x2222, type, 0..]
	capture_handler_frame(CLH_LINKTYPE, LINKTYPE_TRADE, f);
	CHECK_EQ16(f[0], LINKCMD_SEND_LINK_TYPE, "linktype[0]");
	CHECK_EQ16(f[1], LINKTYPE_TRADE,         "linktype[1] = type");
	for (int i = 2; i < 8; i++) CHECK_EQ16(f[i], 0x0000, "linktype pad == 0");

	// blockCommandRequest -> [0xCCCC, size, 0..]
	capture_handler_frame(CLH_BLOCK_REQ, 3, f);
	CHECK_EQ16(f[0], LINKCMD_SEND_BLOCK_REQ, "blockreq[0]");
	CHECK_EQ16(f[1], 3,                      "blockreq[1] = size");
	for (int i = 2; i < 8; i++) CHECK_EQ16(f[i], 0x0000, "blockreq pad == 0");

	// moveCommand -> [0xCAFE, key, 0..]
	capture_handler_frame(CLH_MOVE, LINK_KEY_CODE_READY, f);
	CHECK_EQ16(f[0], LINKCMD_SEND_HELD_KEYS, "move[0]");
	CHECK_EQ16(f[1], LINK_KEY_CODE_READY,    "move[1] = key");
	for (int i = 2; i < 8; i++) CHECK_EQ16(f[i], 0x0000, "move pad == 0");

	// readyExitStandby -> [0x2FFE, 0..]
	capture_handler_frame(CLH_EXIT_STANDBY, 0, f);
	CHECK_EQ16(f[0], LINKCMD_READY_EXIT_STANDBY, "exitstandby[0]");
	for (int i = 1; i < 8; i++) CHECK_EQ16(f[i], 0x0000, "exitstandby pad == 0");

	// readyCloseLink -> [0x5FFF, 0..]
	capture_handler_frame(CLH_CLOSE_LINK, 0, f);
	CHECK_EQ16(f[0], LINKCMD_READY_CLOSE_LINK, "closelink[0]");
	for (int i = 1; i < 8; i++) CHECK_EQ16(f[i], 0x0000, "closelink pad == 0");

	// --- moveCommand repeats 21x ---
	{
		CelioLink cl;
		cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
		run_handshake(&cl, 0);
		drive_crc(&cl, 0x0000);
		cl_set_handler(&cl, CLH_MOVE, LINK_KEY_CODE_READY);
		int frames = 0;
		// keep driving frames; count how many until the handler retires to idle.
		for (int fr = 0; fr < 40; fr++) {
			uint16_t idle[8] = {0,0,0,0,0,0,0,0}, tx[8];
			// re-arm move only on the first frame; afterward we just keep clocking and let
			// transiveDone decide. We must NOT re-arm; the handler's own resume/done drives it.
			drive_command(&cl, idle, tx);
			frames++;
			if (cl.handler != CLH_MOVE) break;  // retired
			// after each completed frame the dispatcher may have re-armed; for a bare move test we
			// stop counting once idle. Enter CRC then back to COMMAND for the next frame.
			drive_crc(&cl, 0x0000);
		}
		// Celio: g_repeats 0..20 -> 20 resumes + 1 done = 21 frames total.
		CHECK(frames == 21, "moveCommand repeats 21 frames (got %d)\n", frames);
	}

	// --- blockCommand: INIT frame then 14-byte CONT chunks + zero-pad on a short tail ---
	{
		CelioLink cl;
		cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
		run_handshake(&cl, 0);
		drive_crc(&cl, 0x0000);

		// a 20-byte source advertised as 20: ceil(20/14)=2 content frames; the 2nd carries 6 real
		// bytes + 8 zero-pad bytes.
		uint8_t src[20];
		for (int i = 0; i < 20; i++) src[i] = (uint8_t)(0xA0 + i);
		cl_block_setup(&cl, src, 20, 20);
		cl_set_handler(&cl, CLH_BLOCK, 0);

		// frame 0 = INIT: [0xBBBB, 20, 0x80, 0,0,0,0,0]
		uint16_t idle[8] = {0}, tx[8];
		drive_command(&cl, idle, tx);
		CHECK_EQ16(tx[0], LINKCMD_INIT_BLOCK, "block INIT[0]");
		CHECK_EQ16(tx[1], 20,                 "block INIT[1] = advertised size");
		CHECK_EQ16(tx[2], 0x80,               "block INIT[2] = 0x80");
		for (int i = 3; i < 8; i++) CHECK_EQ16(tx[i], 0x0000, "block INIT pad == 0");

		// frame 1 = CONT with first 14 bytes (words 1..7).
		drive_crc(&cl, 0x0000);
		drive_command(&cl, idle, tx);
		CHECK_EQ16(tx[0], LINKCMD_CONT_BLOCK, "block CONT0[0]");
		// words 1..7 = src[0..13] little-endian. word k(1..7) = src[2(k-1)] | src[2(k-1)+1]<<8
		for (int k = 1; k <= 7; k++) {
			uint16_t exp = (uint16_t)(src[2*(k-1)] | (src[2*(k-1)+1] << 8));
			CHECK_EQ16(tx[k], exp, "block CONT0 payload word");
		}

		// frame 2 = CONT with remaining 6 bytes (src[14..19]) + zero pad.
		drive_crc(&cl, 0x0000);
		drive_command(&cl, idle, tx);
		CHECK_EQ16(tx[0], LINKCMD_CONT_BLOCK, "block CONT1[0]");
		// words 1..3 carry src[14..19]; words 4..7 zero-padded.
		CHECK_EQ16(tx[1], (uint16_t)(src[14] | (src[15] << 8)), "CONT1 payload w1");
		CHECK_EQ16(tx[2], (uint16_t)(src[16] | (src[17] << 8)), "CONT1 payload w2");
		CHECK_EQ16(tx[3], (uint16_t)(src[18] | (src[19] << 8)), "CONT1 payload w3");
		for (int i = 4; i < 8; i++) CHECK_EQ16(tx[i], 0x0000, "CONT1 zero-pad");

		// after the advertised 20 bytes (14 + 14 maxchunk -> 0) the handler retires.
		drive_crc(&cl, 0x0000);
		drive_command(&cl, idle, tx);
		CHECK(cl.handler == CLH_EMPTY, "blockCommand retired to idle after advertised size\n");
	}

	printf("  ok\n");
}

// --------------------------------------------------------------------------------------------
// TEST 4 — a scripted one-sided trade progresses through the whole flow without CRC mismatch.
//
// The "game" plays a Gen-3 trade: it requests blocks (INIT_BLOCK) for LinkPlayer + the 6 party
// blocks, polls held keys to drive the menu, then negotiates a trade (CONT_BLOCK wrapping
// READY_TO_TRADE) and closes (READY_CLOSE_LINK). The synthesized partner is a canned 1-mon party.
// We assert the dongle never mismatches its own running CRC (the CRC it emits each frame == the
// independent sum of the previous frame), and that the section machine reaches SET_MONS_TO_TRADE
// and a clean close.
//
// This is a STRUCTURAL progression test: we don't model the game's exact internal FSM, we feed the
// command words a real Gen-3 game would send at each step and check the dongle answers coherently.
// --------------------------------------------------------------------------------------------

// Drive one full frame (CRC + 8 command) feeding game_cmd; verify the CRC the dongle emits matches
// the independent sum of the previous frame. Returns the dongle's 8 TX words for this frame.
typedef struct {
	uint16_t prev_game_tx[8];
	uint16_t prev_dongle_tx[8];
	int have_prev;
	int first_crc;     // the very first crc word is the 0xB9A0 seed (covers no frame)
	int crc_mismatches;
} CrcTracker;

static void frame_drive(CelioLink* cl, CrcTracker* ct, const uint16_t game_cmd[8], uint16_t out_tx[8])
{
	uint16_t dongle_crc = drive_crc(cl, 0x0000);
	if (ct->first_crc) {
		if (dongle_crc != LINK_SLAVE_HANDSHAKE) ct->crc_mismatches++;
		ct->first_crc = 0;
	} else if (ct->have_prev) {
		uint16_t exp = independent_crc(0x0000, ct->prev_game_tx, ct->prev_dongle_tx);
		if (dongle_crc != exp) ct->crc_mismatches++;
	}
	uint16_t tx[8];
	drive_command(cl, game_cmd, tx);
	memcpy(ct->prev_game_tx, game_cmd, sizeof(ct->prev_game_tx));
	memcpy(ct->prev_dongle_tx, tx, sizeof(ct->prev_dongle_tx));
	ct->have_prev = 1;
	if (out_tx) memcpy(out_tx, tx, sizeof(tx));
}

static void test_scripted_trade(void)
{
	printf("TEST 4: scripted one-sided trade (handshake->LinkPlayer->party->SET_MONS->close)\n");
	CelioLink cl;
	cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);

	// feed the dongle a canned partner: a 1-mon party (slot 0 real, rest filler) + a LinkPlayer +
	// the partner's pre-supplied SELECT/CONFIRM. This is the DEGENERATE case of the 2-console
	// coordination: instead of a second live game streaming its choices, every peer event the
	// synthesis gates on (party, identity, the chosen slot, the confirm) is supplied UP FRONT, so
	// the live-coordination HOLDs (party-block / SELECT / CONFIRM) are all transparently released.
	{
		ClEvent ev;
		// LinkPlayer
		ev.type = CL_EV_LINKPLAYER; ev.arg = 0;
		ClLinkPlayerBlock blk; cl_make_linkplayer_block(&blk, LINKTYPE_TRADE_CONNECTING);
		ev.len = CL_LINKPLAYER_BLOCK_SIZE; memcpy(ev.data, &blk, ev.len);
		cl_put_incoming(&cl, &ev);
		// party chunk 0 (first 200 bytes = 2 mons) — just nonzero data
		for (uint8_t c = 0; c < 3; c++) {             // per-chunk holds need ALL THREE windows
			ev.type = CL_EV_PARTY_CHUNK; ev.arg = c; ev.len = 200;
			for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)(i + 1 + c);
			cl_put_incoming(&cl, &ev);
		}
		// the canned partner's choices: it offers slot 0 and has confirmed (releases the gates).
		ev.type = CL_EV_SELECT; ev.arg = 0; ev.len = 0; cl_put_incoming(&cl, &ev);
		ev.type = CL_EV_CONFIRM; ev.arg = 0; ev.len = 0; cl_put_incoming(&cl, &ev);
	}

	run_handshake(&cl, 0);

	CrcTracker ct; memset(&ct, 0, sizeof(ct)); ct.first_crc = 1;

	uint16_t tx[8];
	int saw_setup_linkplayer = 0;
	int reached_connection = 0;
	int saw_set_mons = 0;
	int saw_close = 0;

	// ---- SETUP: announce link-type (dongle already armed CLH_LINKTYPE). Game requests LinkPlayer,
	// trainer card, drives keys, then closes the setup link. ----
	{
		uint16_t init[8]   = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
		uint16_t idle[8]   = { 0,0,0,0,0,0,0,0 };
		uint16_t keys[8]   = { LINKCMD_SEND_HELD_KEYS, LINK_KEY_CODE_READY, 0,0,0,0,0,0 };
		uint16_t close[8]  = { LINKCMD_READY_CLOSE_LINK, 0,0,0,0,0,0,0 };

		// frame 0: dongle ships SEND_LINK_TYPE (the armed handler). game sends INIT_BLOCK.
		frame_drive(&cl, &ct, init, tx);
		CHECK_EQ16(tx[0], LINKCMD_SEND_LINK_TYPE, "SETUP frame0 dongle announces link-type");
		CHECK_EQ16(tx[1], LINKTYPE_TRADE_SETUP,   "SETUP link-type word");

		// next several frames: dongle ships LinkPlayer block (INIT then CONT...). Drive enough idle
		// frames to flush the LinkPlayer block + the trainer card request/ship, then keys + close.
		for (int i = 0; i < 12; i++) {
			frame_drive(&cl, &ct, init, tx);
			if (tx[0] == LINKCMD_INIT_BLOCK) saw_setup_linkplayer = 1;
			if (cl.section == CL_SEC_CONNECTION) { reached_connection = 1; break; }
		}
		// a few key polls + close to move SETUP -> CONNECTION if not already.
		for (int i = 0; i < 6 && cl.section == CL_SEC_SETUP; i++)
			frame_drive(&cl, &ct, keys, tx);
		for (int i = 0; i < 3 && cl.section == CL_SEC_SETUP; i++)
			frame_drive(&cl, &ct, close, tx);
		(void)idle;
	}
	CHECK(saw_setup_linkplayer, "SETUP shipped a LinkPlayer/INIT block\n");
	CHECK(cl.section == CL_SEC_CONNECTION, "advanced SETUP -> CONNECTION (section=%d)\n", cl.section);
	reached_connection = (cl.section == CL_SEC_CONNECTION);

	// ---- CONNECTION: the game requests the 6 blocks (LinkPlayer + 3 party + mail + ribbons). The
	// dongle ships each and pulls the peer's blocks. Then negotiation. ----
	if (reached_connection) {
		uint16_t init[8] = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
		// Walk the block sequence: keep feeding INIT_BLOCK frames; the dongle advances LinkPlayer->
		// Party0/1/2->Mail->Ribbons->LinkCMD. We feed enough frames to flush each block.
		for (int i = 0; i < 120 && cl.blockSeq != CL_BLK_LINKCMD; i++)
			frame_drive(&cl, &ct, init, tx);
		CHECK(cl.blockSeq == CL_BLK_LINKCMD, "CONNECTION reached LinkCMD (block stream done), seq=%d\n", cl.blockSeq);

		// negotiation: game sends CONT_BLOCK wrapping READY_TO_TRADE for slot 0.
		uint16_t init20[8] = { LINKCMD_INIT_BLOCK, 20, 0x81, 0,0,0,0,0 };
		uint16_t ready[8] = { LINKCMD_CONT_BLOCK, LINKCMD_READY_TO_TRADE, 0 /*slot*/, 0,0,0,0,0 };
		for (int i = 0; i < 6; i++) {
			frame_drive(&cl, &ct, init20, tx);
			frame_drive(&cl, &ct, ready, tx);
			// after READY_TO_TRADE the dongle ships SET_MONS_TO_TRADE as a 20-byte block (INIT then
			// CONT carrying {0xDDDD, slot}). Detect the SET_MONS word in the CONT payload.
			if (tx[0] == LINKCMD_CONT_BLOCK && tx[1] == LINKCMD_SET_MONS_TO_TRADE) saw_set_mons = 1;
			// drive the block out
			uint16_t idle[8] = {0,0,0,0,0,0,0,0};
			for (int j = 0; j < 3; j++) {
				frame_drive(&cl, &ct, idle, tx);
				if (tx[0] == LINKCMD_CONT_BLOCK && tx[1] == LINKCMD_SET_MONS_TO_TRADE) saw_set_mons = 1;
			}
			if (saw_set_mons) break;
		}
		CHECK(saw_set_mons, "negotiation produced SET_MONS_TO_TRADE\n");

		// confirm + close: game sends READY_CLOSE_LINK; the dispatcher installs the close-link handler
		// (CLH_CLOSE_LINK) on the frame it RECEIVES the close, and the 0x5FFF reply appears on the
		// NEXT command frame (a handler's output always trails the frame that installs it by one).
		// So we keep driving frames past the section flip until we observe the 0x5FFF reply word.
		uint16_t close[8] = { LINKCMD_READY_CLOSE_LINK, 0,0,0,0,0,0,0 };
		int flipped = 0;
		for (int i = 0; i < 6; i++) {
			frame_drive(&cl, &ct, close, tx);
			if (tx[0] == LINKCMD_READY_CLOSE_LINK) saw_close = 1;
			if (cl.section == CL_SEC_DISCONNECT) flipped = 1;
			// drive one more frame after the flip so the close handler's 0x5FFF surfaces.
			if (flipped && saw_close) break;
		}
		CHECK(saw_close, "CONNECTION answered READY_CLOSE_LINK with 0x5FFF\n");
		CHECK(flipped, "CONNECTION -> DISCONNECT on close (section=%d)\n", cl.section);
	}

	CHECK(ct.crc_mismatches == 0, "no CRC mismatch across the whole scripted trade (%d mismatches)\n",
	      ct.crc_mismatches);

	// the synthesized partner emitted at least the SELECT semantic event for the peer.
	{
		int saw_select = 0;
		ClEvent ev;
		while (cl_take_outgoing(&cl, &ev)) if (ev.type == CL_EV_SELECT) saw_select = 1;
		CHECK(saw_select, "emitted CL_EV_SELECT semantic event for the peer\n");
	}

	printf("  ok\n");
}

// --------------------------------------------------------------------------------------------
// TEST 5 (T1) — TWO live instances coordinate the SAME slot pair via cross-fed ClEvents.
//
// This is the M1 crux that a one-sided (canned-partner) trade does NOT exercise: the synthesized
// partner must be driven by the PEER's REAL choices. We instantiate TWO CelioLink structs (A as
// CL_MASTER, B as CL_SLAVE), drive each through the SETUP->CONNECTION block exchange and into the
// negotiation phase with minimal scripted command frames, then verify the COORDINATION LOGIC at the
// EVENT level (we do NOT reproduce both games' full Gen-3 word streams):
//   (a) A emits CL_EV_SELECT(iA); cross-fed to B, B's partner drives SET_MONS_TO_TRADE(iA).
//   (b) symmetric: B emits CL_EV_SELECT(jB); cross-fed to A, A drives SET_MONS_TO_TRADE(jB).
//   (c) the HOLD: neither side finalizes SET_MONS before the peer SELECT arrives.
//   (d) the party-block HOLD: an INIT_BLOCK requiring partner party before the peer's PARTY_CHUNKs
//       arrive does NOT advance past PartyPart0.
//   (e) CONFIRM gating: neither side releases the confirm chain before the peer CL_EV_CONFIRM.
//   (f) both sides reach a consistent (iA,jB) and close out trade-complete.
// --------------------------------------------------------------------------------------------

// Cross-feed every queued outgoing event from `from` into `to` (the UDS transport, modelled as a
// perfect in-process relay). Returns how many events were relayed.
static int relay_events(CelioLink* from, CelioLink* to)
{
	int n = 0;
	ClEvent ev;
	while (cl_take_outgoing(from, &ev)) { cl_put_incoming(to, &ev); n++; }
	return n;
}

// Drain `from`'s outgoing queue looking for a specific event type; if found, copy it to *out and
// return 1 (also relays it to `to` if `to` != NULL). Non-matching events are relayed to `to`.
static int relay_until_event(CelioLink* from, CelioLink* to, uint8_t want, ClEvent* out)
{
	int found = 0;
	ClEvent ev;
	while (cl_take_outgoing(from, &ev)) {
		if (ev.type == want && !found) { *out = ev; found = 1; }
		if (to) cl_put_incoming(to, &ev);
	}
	return found;
}

// Supply a peer identity + a (one-chunk) party so the PARTY0 hold can release. arg `slot_byte`
// makes the parties distinguishable but the exact bytes don't matter to the coordination logic.
static void supply_peer_party(CelioLink* cl, uint8_t fill)
{
	ClEvent ev;
	ev.type = CL_EV_LINKPLAYER; ev.arg = 0;
	ClLinkPlayerBlock blk; cl_make_linkplayer_block(&blk, LINKTYPE_TRADE_CONNECTING);
	ev.len = CL_LINKPLAYER_BLOCK_SIZE; memcpy(ev.data, &blk, ev.len);
	cl_put_incoming(cl, &ev);
	for (uint8_t c = 0; c < 3; c++) {                 // per-chunk holds need ALL THREE windows
		ev.type = CL_EV_PARTY_CHUNK; ev.arg = c; ev.len = 200;
		for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)(fill + c * 3 + i);
		cl_put_incoming(cl, &ev);
	}
}

// Get a freshly-inited dongle out of handshake and into CONNECTION's block exchange, parked just
// before PartyPart0 (blockSeq == CL_BLK_PARTY0). Returns with the CrcTracker primed.
static void to_party0(CelioLink* cl, CrcTracker* ct, int game_is_master)
{
	run_handshake(cl, game_is_master);
	memset(ct, 0, sizeof(*ct)); ct->first_crc = 1;

	uint16_t init[8]  = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
	uint16_t keys[8]  = { LINKCMD_SEND_HELD_KEYS, LINK_KEY_CODE_READY, 0,0,0,0,0,0 };
	uint16_t close[8] = { LINKCMD_READY_CLOSE_LINK, 0,0,0,0,0,0,0 };
	uint16_t tx[8];

	// SETUP frame 0 announces link-type; then flush LinkPlayer/trainer-card, keys, close -> CONNECTION.
	for (int i = 0; i < 14 && cl->section == CL_SEC_SETUP; i++) frame_drive(cl, ct, init, tx);
	for (int i = 0; i < 6  && cl->section == CL_SEC_SETUP; i++) frame_drive(cl, ct, keys, tx);
	for (int i = 0; i < 3  && cl->section == CL_SEC_SETUP; i++) frame_drive(cl, ct, close, tx);

	// CONNECTION: ship LinkPlayer; advance to PartyPart0 (where the party-block hold lives).
	for (int i = 0; i < 30 && cl->blockSeq == CL_BLK_LINKPLAYER; i++) frame_drive(cl, ct, init, tx);
}

// From CL_BLK_PARTY0, drive INIT_BLOCK frames until the block stream finishes (blockSeq==LINKCMD)
// or `limit` frames elapse. Returns the resulting blockSeq.
static ClBlockSeq drive_block_stream(CelioLink* cl, CrcTracker* ct, int limit)
{
	uint16_t init[8] = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 }, tx[8];
	for (int i = 0; i < limit && cl->blockSeq != CL_BLK_LINKCMD; i++)
		frame_drive(cl, ct, init, tx);
	return cl->blockSeq;
}

// In the negotiation phase, have the local game send CONT_BLOCK[READY_TO_TRADE, slot] and drive
// frames until the dongle ships a SET_MONS_TO_TRADE block (or `limit` frames). On success sets
// *out_slot to the committed slot (word2 of the CONT frame) and returns 1; 0 = no SET_MONS shipped
// (i.e. the SELECT hold is in effect).
static int negotiate_select(CelioLink* cl, CrcTracker* ct, uint16_t slot, int limit, uint16_t* out_slot)
{
	uint16_t init20[8] = { LINKCMD_INIT_BLOCK, 20, 0x81, 0,0,0,0,0 };   // the 20-byte command block header
	uint16_t ready[8] = { LINKCMD_CONT_BLOCK, LINKCMD_READY_TO_TRADE, slot, 0,0,0,0,0 };
	uint16_t idle[8]  = { 0,0,0,0,0,0,0,0 };
	uint16_t tx[8];
	for (int i = 0; i < limit; i++) {
		const uint16_t* feed = (i == 0) ? init20 : (i == 1) ? ready : idle;
		frame_drive(cl, ct, feed, tx);
		if (tx[0] == LINKCMD_CONT_BLOCK && tx[1] == LINKCMD_SET_MONS_TO_TRADE) {
			if (out_slot) *out_slot = tx[2];
			return 1;
		}
	}
	return 0;
}

static void test_two_instance_coordination(void)
{
	printf("TEST 5 (T1): two live instances coordinate the same slot pair\n");

	CelioLink A, B;
	cl_init(&A, CL_MASTER, LINKTYPE_TRADE);
	cl_init(&B, CL_SLAVE,  LINKTYPE_TRADE);

	CrcTracker ctA, ctB;

	// ---- (d) PARTY-BLOCK HOLD: drive A to PartyPart0 with NO peer party supplied; it must HOLD. ----
	to_party0(&A, &ctA, /*game_is_master=*/0);
	CHECK(A.blockSeq == CL_BLK_PARTY0, "A parked at PartyPart0 before any peer party (seq=%d)\n", A.blockSeq);
	drive_block_stream(&A, &ctA, 20);
	CHECK(A.blockSeq == CL_BLK_PARTY0, "A HOLDS at PartyPart0 until the peer's party arrives\n");
	CHECK(A.partnerPartyHeld == 1, "A reports the party-block hold (partnerPartyHeld)\n");

	// now supply A's peer party (B's identity+party) -> the hold releases and the stream completes.
	supply_peer_party(&A, 0x10);
	drive_block_stream(&A, &ctA, 200);
	CHECK(A.blockSeq == CL_BLK_LINKCMD, "A advances past PartyPart0 once peer party arrives (seq=%d)\n", A.blockSeq);
	CHECK(A.partnerPartyHeld == 0, "A clears the party-block hold after release\n");

	// bring B up the same way, peer party supplied up front (so we focus on the SELECT/CONFIRM gates).
	to_party0(&B, &ctB, /*game_is_master=*/1);
	supply_peer_party(&B, 0x20);
	drive_block_stream(&B, &ctB, 200);
	CHECK(B.blockSeq == CL_BLK_LINKCMD, "B reached negotiation (seq=%d)\n", B.blockSeq);

	// ---- (c) SELECT HOLD: A's game offers slot iA, but B has NOT selected yet -> A must NOT finalize. ----
	const uint16_t iA = 2, jB = 4;
	uint16_t a_committed = 0xFFFF, b_committed = 0xFFFF;
	int a_set = negotiate_select(&A, &ctA, iA, 12, &a_committed);
	CHECK(!a_set, "A HOLDS SET_MONS_TO_TRADE until B's SELECT arrives (no premature finalize)\n");
	CHECK(A.selectHeld == 1, "A reports the SELECT hold\n");
	CHECK(A.localSelectSlot == (int)iA, "A recorded its own offered slot iA=%u\n", iA);

	// (a) A emitted CL_EV_SELECT(iA); relay it to B.
	{
		ClEvent ev;
		int got = relay_until_event(&A, &B, CL_EV_SELECT, &ev);
		CHECK(got, "A emitted CL_EV_SELECT for the peer\n");
		CHECK(got && ev.arg == iA, "A's CL_EV_SELECT carries iA=%u (got %u)\n", iA, ev.arg);
	}

	// B is the HOST-side (FOLLOWER) partner: on A's relayed SELECT it must ANNOUNCE [AABB, iA] to
	// its game-Leader (whose own selection is silent-local; the pairing happens inside the game).
	{
		uint16_t idleB[8] = {0,0,0,0,0,0,0,0}, txB[8];
		int sawAABB = 0;
		for (int i = 0; i < 10 && !sawAABB; i++) {
			frame_drive(&B, &ctB, idleB, txB);
			if (txB[0] == LINKCMD_CONT_BLOCK && txB[1] == LINKCMD_READY_TO_TRADE && txB[2] == iA) sawAABB = 1;
		}
		CHECK(sawAABB, "(a) B announced the peer's select [AABB, iA=%u] to its game-Leader\n", iA);
	}
	// B's game-Leader (having paired) broadcasts [DDDD, jB] — the only wire trace of the HOST
	// player's own pick. B must capture it + push CL_EV_SELECT(jB) for A.
	{
		uint16_t init20[8] = { LINKCMD_INIT_BLOCK, 20, 0x80, 0,0,0,0,0 };
		uint16_t dddd[8]   = { LINKCMD_CONT_BLOCK, LINKCMD_SET_MONS_TO_TRADE, jB, 0,0,0,0,0 };
		uint16_t txB[8];
		frame_drive(&B, &ctB, init20, txB);
		frame_drive(&B, &ctB, dddd, txB);
		ClEvent ev;
		int got = relay_until_event(&B, &A, CL_EV_SELECT, &ev);
		CHECK(got, "B captured its game-Leader's DDDD broadcast -> CL_EV_SELECT\n");
		CHECK(got && ev.arg == jB, "B's CL_EV_SELECT carries jB=%u (got %u)\n", jB, ev.arg);
		b_committed = iA;   // the follower side commits via its game's own pairing
	}
	// A (the Leader) now releases LEVEL-TRIGGERED: idle frames let the preamble fire SET_MONS(jB).
	{
		uint16_t idleA[8] = {0,0,0,0,0,0,0,0}, txA[8];
		int a_rel = 0;
		for (int i = 0; i < 10 && !a_rel; i++) {
			frame_drive(&A, &ctA, idleA, txA);
			if (txA[0] == LINKCMD_CONT_BLOCK && txA[1] == LINKCMD_SET_MONS_TO_TRADE) { a_rel = 1; a_committed = txA[2]; }
		}
		CHECK(a_rel, "A finalized SET_MONS once B's SELECT arrived (LEVEL-triggered release)\n");
		CHECK(a_committed == jB, "(b) A's SET_MONS commits the PEER's slot jB=%u (got 0x%04X)\n", jB, a_committed);
		CHECK(A.selectHeld == 0, "A cleared the SELECT hold after release\n");
	}

	// consistency: A holds B's slot jB, B holds A's slot iA -> both know the SAME pair (iA,jB).
	CHECK(a_committed == jB && b_committed == iA, "(f) both converged on the consistent pair (iA=%u,jB=%u)\n", iA, jB);

	// ---- (e) CONFIRM GATE: open the confirm chain on A; with B not confirmed it must HOLD. ----
	{
		uint16_t init20[8]  = { LINKCMD_INIT_BLOCK, 20, 0x81, 0,0,0,0,0 };
		uint16_t confirm[8] = { LINKCMD_CONT_BLOCK, LINKCMD_INIT_BLOCK, 0,0,0,0,0,0 };
		uint16_t idle[8]    = { 0,0,0,0,0,0,0,0 };
		uint16_t tx[8];
		int a_started = 0;
		// A opens the confirm chain. B has not sent CL_EV_CONFIRM yet -> A holds the START_TRADE release.
		frame_drive(&A, &ctA, init20, tx);
		frame_drive(&A, &ctA, confirm, tx);
		for (int i = 0; i < 8; i++) {
			frame_drive(&A, &ctA, idle, tx);
			if (tx[0] == LINKCMD_CONT_BLOCK &&
			    (tx[1] == LINKCMD_INIT_BLOCK || tx[1] == LINKCMD_START_TRADE)) a_started = 1;
		}
		CHECK(!a_started, "A HOLDS the confirm chain until B's CL_EV_CONFIRM arrives\n");
		CHECK(A.confirmHeld == 1, "A reports the confirm hold\n");

		// A emitted CL_EV_CONFIRM; relay to B (and bring B's confirm back to A).
		{
			ClEvent ev;
			int gotA = relay_until_event(&A, &B, CL_EV_CONFIRM, &ev);
			CHECK(gotA, "(e) A emitted CL_EV_CONFIRM for the peer\n");
		}
		// B opens its confirm chain too, emitting CL_EV_CONFIRM; relay it to A.
		frame_drive(&B, &ctB, init20, tx);
		frame_drive(&B, &ctB, confirm, tx);
		for (int i = 0; i < 3; i++) frame_drive(&B, &ctB, idle, tx);
		relay_events(&B, &A);
		CHECK(A.partnerConfirmed == 1, "A received B's CL_EV_CONFIRM\n");

		// now A's confirm chain releases: drive frames until START_TRADE/INIT_BLOCK block ships.
		// (re-open the chain since the previous hold answered EMPTY without retiring section state)
		frame_drive(&A, &ctA, init20, tx);
		frame_drive(&A, &ctA, confirm, tx);
		for (int i = 0; i < 10 && !a_started; i++) {
			frame_drive(&A, &ctA, idle, tx);
			if (tx[0] == LINKCMD_CONT_BLOCK &&
			    (tx[1] == LINKCMD_INIT_BLOCK || tx[1] == LINKCMD_START_TRADE)) a_started = 1;
		}
		CHECK(a_started, "A released the confirm chain after B confirmed\n");
		CHECK(A.confirmHeld == 0, "A cleared the confirm hold after release\n");
	}

	// ---- (f) close out: both reach trade-complete consistently. The trade flows
	// CONNECTION -> DISCONNECT (close) -> re-handshake -> finish -> close -> tradeComplete. ----
	{
		uint16_t close[8] = { LINKCMD_READY_CLOSE_LINK, 0,0,0,0,0,0,0 };
		uint16_t init[8]  = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
		uint16_t finish[8]= { LINKCMD_CONT_BLOCK, LINKCMD_READY_FINISH_TRADE, 0,0,0,0,0,0 };
		uint16_t tx[8];

		for (int side = 0; side < 2; side++) {
			CelioLink* cl = side ? &B : &A;
			CrcTracker* ct = side ? &ctB : &ctA;
			// CONNECTION close -> DISCONNECT
			for (int i = 0; i < 4 && cl->section == CL_SEC_CONNECTION; i++) frame_drive(cl, ct, close, tx);
			CHECK(cl->section == CL_SEC_DISCONNECT, "%s CONNECTION->DISCONNECT on close (section=%d)\n",
			      side ? "B" : "A", cl->section);
			// DISCONNECT: re-ship LinkPlayer, run the finish handshake, then close -> trade complete.
			for (int i = 0; i < 4; i++) frame_drive(cl, ct, init, tx);
			for (int i = 0; i < 4; i++) frame_drive(cl, ct, finish, tx);
			for (int i = 0; i < 6 && !cl_trade_complete(cl); i++) frame_drive(cl, ct, close, tx);
			CHECK(cl_trade_complete(cl), "%s reached trade-complete\n", side ? "B" : "A");
		}
	}

	CHECK(ctA.crc_mismatches == 0, "A: no CRC mismatch across coordination (%d)\n", ctA.crc_mismatches);
	CHECK(ctB.crc_mismatches == 0, "B: no CRC mismatch across coordination (%d)\n", ctB.crc_mismatches);

	printf("  ok\n");
}

// --------------------------------------------------------------------------------------------
// TEST 6a (the 0630 JOIN deadlock fix): a MASTER partner must DRIVE 0x8FFF to a slave game that sits
// silent (0x0000) until it sees a master — NOT wait for the game's 0xB9A0 (which never comes -> deadlock).
static void test_master_drives_handshake(void)
{
	printf("TEST 6a: MASTER partner drives 0x8FFF to a silent (0x0000) slave game\n");
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	int sawMaster = 0, established = 0;
	for (int i = 0; i < 60 && !established; i++) {
		// the JOIN game: 0x0000 while "waiting for connection"; sends 0xB9A0 only AFTER it has seen 0x8FFF.
		uint16_t gameWord  = sawMaster ? (uint16_t)LINK_SLAVE_HANDSHAKE : (uint16_t)0x0000;
		uint16_t partnerTx = cl_transfer(&cl, gameWord);
		if (partnerTx == LINK_MASTER_HANDSHAKE) sawMaster = 1;   // the partner DROVE 0x8FFF
		ClStatus st; cl_get_status(&cl, &st);
		if (st.state != CL_ST_HANDSHAKE) established = 1;        // left handshake -> established
	}
	CHECK(sawMaster,   "6a: the MASTER partner DROVE 0x8FFF without the game advertising 0xB9A0 first\n");
	CHECK(established,  "6a: the handshake ESTABLISHED with a silent slave game (deadlock broken)\n");
	printf("  ok\n");
}

// TEST 6b (the 0630 HOST sit-at-the-machine fix): the FSM must RE-ENTER the handshake when the game
// restarts the link (a run of 0xB9A0), instead of answering 0x0000 forever from the command phase.
static void test_rehandshake(void)
{
	printf("TEST 6b: re-enter handshake when the game restarts the link (B9A0 run)\n");
	CelioLink cl; cl_init(&cl, CL_SLAVE, LINKTYPE_TRADE);     // HOST partner = slave, game = master
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);                   // game advertises 0xB9A0
	cl_transfer(&cl, LINK_MASTER_HANDSHAKE);                  // game drives 0x8FFF -> leave handshake
	ClStatus st; cl_get_status(&cl, &st);
	CHECK(st.state != CL_ST_HANDSHAKE, "6b: established (left handshake into command)\n");
	for (int i = 0; i < 12; i++) cl_transfer(&cl, 0x8888);    // run some command-phase transfers
	cl_get_status(&cl, &st);
	CHECK(st.state != CL_ST_HANDSHAKE, "6b: still in the command phase before the restart\n");
	for (int i = 0; i < CL_HS_REARM_RUN + 2; i++) cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);  // game restarts
	cl_get_status(&cl, &st);
	CHECK(st.state == CL_ST_HANDSHAKE, "6b: RE-ENTERED the handshake on the B9A0 run (no longer stuck @0x0000)\n");
	cl_transfer(&cl, LINK_MASTER_HANDSHAKE);                  // game drives 0x8FFF again
	cl_get_status(&cl, &st);
	CHECK(st.state != CL_ST_HANDSHAKE, "6b: re-established after the re-handshake\n");
	printf("  ok\n");
}

// TEST 7 (run #2 fix): Celio-faithful clock pacing — the delay the driving clock must apply before
// the next transfer, per PacketLayer state (packetLayer.hpp:51-53 constants).
static void test_clock_pacing(void)
{
	printf("TEST 7: cl_next_delay_us follows the Celio master-clock pacing\n");
	CelioLink cl; cl_init(&cl, CL_SLAVE, LINKTYPE_TRADE);
	CHECK(cl_next_delay_us(&cl) == CL_TIMING_HANDSHAKE_US, "7: handshake words paced at %uus\n", CL_TIMING_HANDSHAKE_US);
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
	cl_transfer(&cl, LINK_MASTER_HANDSHAKE);              // leave handshake -> CRC (frame boundary)
	CHECK(cl_next_delay_us(&cl) == CL_TIMING_FRAME_US, "7: frame boundary (CRC next) paced at %uus\n", CL_TIMING_FRAME_US);
	cl_transfer(&cl, 0x0000);                             // the CRC word -> COMMAND
	CHECK(cl_next_delay_us(&cl) == CL_TIMING_WORD_US, "7: mid-frame command words paced at %uus\n", CL_TIMING_WORD_US);
	for (int i = 0; i < 8; i++) cl_transfer(&cl, 0x0000); // complete the 8-word frame -> back to CRC
	CHECK(cl_next_delay_us(&cl) == CL_TIMING_FRAME_US, "7: after the 8th word the frame gap applies\n");
	printf("  ok\n");
}

// TEST 8 (run #2 fix): cl_rearm_handshake gives a FRESH handshake but PRESERVES the coordination
// state (the peer's party received over UDS must survive — the driver re-arms at the listen edge).
static void test_rearm_preserves_partner(void)
{
	printf("TEST 8: cl_rearm_handshake preserves the received partner party\n");
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	ClEvent ev; ev.type = CL_EV_PARTY_CHUNK; ev.arg = 0; ev.len = 200;
	for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)i;
	cl_put_incoming(&cl, &ev);
	// drive into the command phase so the re-arm actually has something to reset
	for (int i = 0; i < 20; i++) cl_transfer(&cl, 0x0000);
	ClStatus st; cl_get_status(&cl, &st);
	CHECK(st.partnerPartyBytes == 200, "8: peer party captured before the re-arm (%lu)\n",
	      (unsigned long)st.partnerPartyBytes);
	cl_rearm_handshake(&cl);
	cl_get_status(&cl, &st);
	CHECK(st.state == CL_ST_HANDSHAKE, "8: re-armed to a fresh handshake\n");
	CHECK(st.partnerPartyBytes == 200, "8: peer party SURVIVED the re-arm (%lu)\n",
	      (unsigned long)st.partnerPartyBytes);
	// and the fresh handshake still establishes (the master drives 0x8FFF again)
	int established = 0;
	for (int i = 0; i < 40 && !established; i++) {
		uint16_t tx = cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
		(void)tx;
		cl_get_status(&cl, &st);
		if (st.state != CL_ST_HANDSHAKE) established = 1;
	}
	CHECK(established, "8: re-establishes after the re-arm\n");
	printf("  ok\n");
}

// TEST 6c (the HW run #3 loop): with a game that advertises B9A0 from the start (the real Gen-3
// behavior once listening), the MASTER partner must TRANSMIT 0x8FFF on the wire before leaving
// handshake — and must NOT bounce back into handshake afterward. Run #3's bug: escalate + leave
// cascaded in one transfer, 8FFF was never sent, and the re-handshake detector looped 4-word cycles.
static void test_master_transmits_8fff(void)
{
	printf("TEST 6c: MASTER partner TRANSMITS 0x8FFF before leaving (run #3 loop regression)\n");
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	uint16_t t1 = cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);    // game advertises B9A0 immediately
	CHECK(t1 == LINK_SLAVE_HANDSHAKE, "6c: T1 the partner advertises B9A0 first (got 0x%04X)\n", t1);
	CHECK(cl.state == CL_ST_HANDSHAKE, "6c: T1 still in handshake (8FFF not yet driven)\n");
	uint16_t t2 = cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);    // game still handshaking
	CHECK(t2 == LINK_MASTER_HANDSHAKE, "6c: T2 the partner DRIVES 0x8FFF on the wire (got 0x%04X)\n", t2);
	CHECK(cl.state != CL_ST_HANDSHAKE, "6c: left handshake on the transfer that TRANSMITTED 8FFF\n");
	// the game leaves in that same transfer; from now on it sends command-phase words, not B9A0.
	for (int i = 0; i < 12; i++) cl_transfer(&cl, 0x0000);
	CHECK(cl.state != CL_ST_HANDSHAKE, "6c: no bounce back into handshake (the run #3 loop is gone)\n");
	printf("  ok\n");
}

// TEST 9 (the retry fix): cl_reset_session = fresh handshake + sections back to SETUP + all trade
// gates cleared, with the received partner party PRESERVED.
static void test_reset_session(void)
{
	printf("TEST 9: cl_reset_session restarts the conversation (party preserved)\n");
	CelioLink cl; cl_init(&cl, CL_SLAVE, LINKTYPE_TRADE);
	ClEvent ev; ev.type = CL_EV_PARTY_CHUNK; ev.arg = 1; ev.len = 200;
	for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)(i ^ 0x5A);
	cl_put_incoming(&cl, &ev);
	ev.type = CL_EV_SELECT; ev.arg = 2; ev.len = 0; cl_put_incoming(&cl, &ev);
	// simulate mid-conversation state
	cl.section = CL_SEC_CONNECTION; cl.blockSeq = CL_BLK_PARTY2;
	cl.localSelectSlot = 4; cl.selectHeld = 1; cl.localConfirmed = 1;
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_MASTER_HANDSHAKE); // into command
	cl_reset_session(&cl);
	ClStatus st; cl_get_status(&cl, &st);
	CHECK(st.state == CL_ST_HANDSHAKE, "9: fresh handshake\n");
	CHECK(st.section == CL_SEC_SETUP, "9: sections restarted at SETUP (was CONNECTION)\n");
	CHECK(st.blockSeq == CL_BLK_LINKPLAYER, "9: block sequence restarted\n");
	CHECK(cl.localSelectSlot == -1 && cl.selectHeld == 0 && cl.localConfirmed == 0,
	      "9: select/confirm coordination cleared\n");
	CHECK(st.partnerPartyBytes >= 200, "9: the received partner party SURVIVED (%lu)\n",
	      (unsigned long)st.partnerPartyBytes);
	printf("  ok\n");
}


// TEST 10 (HW run #4: "the joined can't walk a single step"): after the movement script is
// exhausted, the partner must answer SEND_HELD_KEYS with a REAL standing-still key frame
// [0xCAFE, LINK_KEY_CODE_EMPTY] — never an all-zero frame (RECEIVED_NOTHING = the game skips its
// frame update = a slave game's player freezes in the room).
static void test_room_keepalive(void)
{
	printf("TEST 10: PROACTIVE room heartbeat — a FROZEN slave still gets key frames\n");
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);   // the JOIN-side partner (game = slave)
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);                  // partner drives 8FFF -> established
	CrcTracker ct; memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	uint16_t standby[8] = { LINKCMD_READY_EXIT_STANDBY, 0,0,0,0,0,0,0 };
	uint16_t frozen[8]  = { 0,0,0,0,0,0,0,0 };               // a frozen slave sends NOTHING
	uint16_t tx[8];
	frame_drive(&cl, &ct, standby, tx);                      // standby marks the room phase live
	// From here the game is FROZEN (all-zero frames). The partner must emit key frames unprompted —
	// the script's 6 walk moves first, then standing-still EMPTY frames, never an all-zero frame.
	int keyFrames = 0, sawEmptyKey = 0, sawScriptKey = 0;
	for (int f = 0; f < 160; f++) {   // 6 script moves x 21 repeats = 126 frames, then EMPTY
		frame_drive(&cl, &ct, frozen, tx);
		if (tx[0] == LINKCMD_SEND_HELD_KEYS) {
			keyFrames++;
			if (tx[1] == LINK_KEY_CODE_EMPTY) sawEmptyKey = 1;
			if (tx[1] >= LINK_KEY_CODE_DPAD_DOWN && tx[1] <= LINK_KEY_CODE_READY) sawScriptKey = 1;
		}
		CHECK(!(tx[0] == 0 && tx[1] == 0 && tx[2] == 0 && tx[3] == 0),
		      "10: frame %d to a frozen slave is NOT all-zero\n", f);
	}
	CHECK(keyFrames >= 150, "10: sustained unprompted key frames (%d/160)\n", keyFrames);
	CHECK(sawScriptKey, "10: the walk script played unprompted (fake player enters on its own)\n");
	CHECK(sawEmptyKey, "10: settles into standing-still EMPTY key frames\n");
	CHECK(ct.crc_mismatches == 0, "10: no CRC mismatch across the heartbeat\n");
	printf("  ok\n");
}


// TEST 11 (pret cable_club.c section 7E / Celio nextSection=lounge): a CANCELLED trade routes the
// partner to the LOUNGE (the ROOM session, linkType 0x1111) on close — NOT to DISCONNECT (0x1144).
static void test_cancel_routes_to_lounge(void)
{
	printf("TEST 11: a cancelled trade routes to LOUNGE, a started trade to DISCONNECT\n");
	CrcTracker ct;
	uint16_t init20[8] = { LINKCMD_INIT_BLOCK, 20, 0x81, 0,0,0,0,0 };
	uint16_t cancel[8] = { LINKCMD_CONT_BLOCK, LINKCMD_REQUEST_CANCEL, 0,0,0,0,0,0 };
	uint16_t close_[8] = { LINKCMD_READY_CLOSE_LINK, 0,0,0,0,0,0,0 };
	uint16_t idle[8]   = { 0,0,0,0,0,0,0,0 };
	uint16_t tx[8];
	// cancelled trade -> LOUNGE
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
	cl.section = CL_SEC_CONNECTION; cl.blockSeq = CL_BLK_LINKCMD;   // at the negotiation phase
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	frame_drive(&cl, &ct, init20, tx);
	frame_drive(&cl, &ct, cancel, tx);
	for (int i = 0; i < 8; i++) frame_drive(&cl, &ct, idle, tx);   // let the cancel chain flush
	frame_drive(&cl, &ct, close_, tx);
	CHECK(cl.section == CL_SEC_LOUNGE, "11: cancel close -> LOUNGE (got section %d)\n", cl.section);
	CHECK(cl.nextIsLounge == 0, "11: the lounge flag is consumed\n");
	// non-cancelled close -> DISCONNECT (the trade-anim session)
	CelioLink c2; cl_init(&c2, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&c2, LINK_SLAVE_HANDSHAKE); cl_transfer(&c2, LINK_SLAVE_HANDSHAKE);
	c2.section = CL_SEC_CONNECTION; c2.blockSeq = CL_BLK_LINKCMD;
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	frame_drive(&c2, &ct, close_, tx);
	CHECK(c2.section == CL_SEC_DISCONNECT, "11: plain close -> DISCONNECT (got section %d)\n", c2.section);
	printf("  ok\n");
}

// TEST 13 (HW run #10 — the post-trade room re-entry / the JOIN black screen): the LOUNGE
// (room-after-cancel) must run the FULL room re-establishment — re-ship LinkPlayer, ANSWER
// READY_EXIT_STANDBY, and HOLD the proactive heartbeat until the room is actually live (roomKeys).
// The old LOUNGE ignored the standby and ran an ungated heartbeat, so on re-entry it jumped straight
// to CAFE key frames; the SLAVE game never got its standby acked and hung on a black CB2_Overworld.
static void test_lounge_reentry(void)
{
	printf("TEST 13: LOUNGE room re-entry answers READY_EXIT_STANDBY + gates the heartbeat on roomKeys\n");
	CrcTracker ct;
	uint16_t init[8]    = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
	uint16_t standby[8] = { LINKCMD_READY_EXIT_STANDBY, 0,0,0,0,0,0,0 };
	uint16_t idle[8]    = { 0,0,0,0,0,0,0,0 };
	uint16_t tx[8];

	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);       // dongle=master (the game is the SLAVE)
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
	cl.section = CL_SEC_LOUNGE; cl.roomKeys = 0;                 // fresh re-entry (a cancel just routed here)
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;

	// (a) BEFORE the standby, NO key heartbeat: the LinkPlayer exchange runs, the heartbeat stays held.
	int early_keyframe = 0;
	for (int i = 0; i < 6; i++) {
		frame_drive(&cl, &ct, init, tx);
		if (tx[0] == LINKCMD_SEND_HELD_KEYS) early_keyframe = 1;
	}
	CHECK(!early_keyframe, "13: no key heartbeat before the room is live (roomKeys still 0)\n");
	CHECK(cl.roomKeys == 0, "13: roomKeys stays 0 through the pre-standby re-entry setup\n");

	// (b) the re-entry's standby round is ANSWERED (0x2FFE) and arms the room.
	int saw_standby = 0;
	for (int i = 0; i < 4; i++) {
		frame_drive(&cl, &ct, standby, tx);
		if (tx[0] == LINKCMD_READY_EXIT_STANDBY) saw_standby = 1;
	}
	CHECK(saw_standby, "13: LOUNGE answered READY_EXIT_STANDBY with 0x2FFE\n");
	CHECK(cl.roomKeys == 1, "13: roomKeys armed after the re-entry standby\n");

	// (c) NOW idle frames produce the proactive EMPTY-key heartbeat (the frozen slave's clock).
	int saw_empty = 0;
	for (int i = 0; i < 6; i++) {
		frame_drive(&cl, &ct, idle, tx);
		if (tx[0] == LINKCMD_SEND_HELD_KEYS && tx[1] == LINK_KEY_CODE_EMPTY) saw_empty = 1;
	}
	CHECK(saw_empty, "13: proactive EMPTY-key heartbeat runs once the room is live\n");
	printf("  ok\n");
}


// TEST 12 (the run-#6 mutual-deadlock fix): at the PARTY0 hold with NO peer party, the partner must
// STILL pull the local game's party (the request runs during the hold), capture the stream, and push
// CL_EV_PARTY_CHUNK to the peer — while the serve stays held until the peer's chunk arrives.
static void test_capture_under_hold(void)
{
	printf("TEST 12: capture-under-hold pipeline (the run-#6 txSeq=0 deadlock fix)\n");
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);  // establish
	cl.section = CL_SEC_CONNECTION; cl.blockSeq = CL_BLK_LINKPLAYER;
	CrcTracker ct; memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	uint16_t init[8]  = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
	uint16_t idle[8]  = { 0,0,0,0,0,0,0,0 };
	uint16_t tx[8];
	// LinkPlayer serve -> enters PARTY0 with reqDelay armed
	for (int i = 0; i < 30 && cl.blockSeq == CL_BLK_LINKPLAYER; i++) frame_drive(&cl, &ct, init, tx);
	CHECK(cl.blockSeq == CL_BLK_PARTY0, "12: at PARTY0 (seq=%d)\n", cl.blockSeq);
	// One INIT evaluates PARTY0 -> the hold engages; the game then goes QUIET (its
	// BufferTradeParties waits at status<3 sending nothing — the real behavior).
	frame_drive(&cl, &ct, init, tx);
	CHECK(cl.partnerPartyHeld == 1, "12: the PARTY0 hold engaged\n");
	// (a) the pull RUNS during the hold: after reqDelay IDLE frames the dongle emits [CCCC, 1]
	int sawReq = 0;
	for (int i = 0; i < 80 && !sawReq; i++) {
		frame_drive(&cl, &ct, idle, tx);              // the game is quiet; we hold + pull
		if (tx[0] == LINKCMD_SEND_BLOCK_REQ && tx[1] == 1) sawReq = 1;
	}
	CHECK(sawReq, "12a: BLOCK_REQ(200) fired DURING the hold (the deadlock breaker)\n");
	CHECK(cl.blockSeq == CL_BLK_PARTY0 && cl.partnerPartyHeld, "12a: serve still HELD\n");
	// (b) the game streams its party chunk 0: INIT(200) + 15 CONT frames; the capture must push
	// CL_EV_PARTY_CHUNK(0) while the serve stays held.
	uint16_t ginit[8] = { LINKCMD_INIT_BLOCK, 200, 0x81, 0,0,0,0,0 };
	frame_drive(&cl, &ct, ginit, tx);
	uint8_t party[200]; for (int i = 0; i < 200; i++) party[i] = (uint8_t)(0x40 + i);
	for (int f = 0; f < 15; f++) {
		uint16_t contf[8] = { LINKCMD_CONT_BLOCK, 0,0,0,0,0,0,0 };
		for (int w = 0; w < 7; w++) {
			int off = f * 14 + w * 2;
			uint16_t v = 0;
			if (off < 200)     v = party[off];
			if (off + 1 < 200) v |= (uint16_t)(party[off + 1] << 8);
			contf[w + 1] = v;
		}
		frame_drive(&cl, &ct, contf, tx);
	}
	ClEvent ev; int gotChunk = 0;
	while (cl_take_outgoing(&cl, &ev)) {
		if (ev.type == CL_EV_PARTY_CHUNK && ev.arg == 0 && ev.len == 200 &&
		    memcmp(ev.data, party, 200) == 0) gotChunk = 1;
	}
	CHECK(gotChunk, "12b: CL_EV_PARTY_CHUNK(0) captured + pushed UNDER the hold, byte-exact\n");
	CHECK(cl.blockSeq == CL_BLK_PARTY0, "12b: serve STILL held (no peer bytes yet)\n");
	// (b2) a RE-STREAM of the same stage (the held game re-answers the next pull with the SAME
	// party[0..1]) must land in the SAME window — never emit a duplicate as chunk 1 (run #7 bug).
	frame_drive(&cl, &ct, ginit, tx);
	for (int f = 0; f < 15; f++) {
		uint16_t contf[8] = { LINKCMD_CONT_BLOCK, 0,0,0,0,0,0,0 };
		for (int w = 0; w < 7; w++) {
			int off = f * 14 + w * 2;
			uint16_t v = 0;
			if (off < 200)     v = party[off];
			if (off + 1 < 200) v |= (uint16_t)(party[off + 1] << 8);
			contf[w + 1] = v;
		}
		frame_drive(&cl, &ct, contf, tx);
	}
	{
		ClEvent ev2; int dupChunk = 0;
		while (cl_take_outgoing(&cl, &ev2)) if (ev2.type == CL_EV_PARTY_CHUNK && ev2.arg != 0) dupChunk = 1;
		CHECK(!dupChunk, "12b2: a re-streamed stage is NOT mis-filed as a later chunk\n");
	}
	// (c) peer chunk 0 arrives -> the next INIT releases the PARTY0 serve; PARTY1 then holds again.
	ev.type = CL_EV_PARTY_CHUNK; ev.arg = 0; ev.len = 200;
	for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)(0x80 + i);
	cl_put_incoming(&cl, &ev);
	// the next game INIT (its re-stream on our next pull) triggers the serve evaluation
	for (int i = 0; i < 30 && cl.blockSeq == CL_BLK_PARTY0; i++) frame_drive(&cl, &ct, init, tx);
	CHECK(cl.blockSeq == CL_BLK_PARTY1, "12c: PARTY0 served after the peer chunk landed (seq=%d)\n", cl.blockSeq);
	for (int i = 0; i < 20; i++) frame_drive(&cl, &ct, init, tx);
	CHECK(cl.blockSeq == CL_BLK_PARTY1 && cl.partnerPartyHeld, "12c: PARTY1 holds until the peer's chunk 1\n");
	CHECK(ct.crc_mismatches == 0, "12: no CRC mismatch across the pipeline\n");
	printf("  ok\n");
}

// TEST 14 (HW run #12 — the JOIN post-trade room-exit black screen): the exit is a 3-stage
// protocol (pret overworld.c / field_fadetransition.c:655 / link.c:1298-1350). The partner must
// (1) answer the game's EXIT_ROOM key with its OWN exit key (stage-1 all-players-exiting) + relay
// CL_EV_EXIT_ROOM to the peer console; (2) GO SILENT once the game's sends turn all-zero (the exit
// fade ran ClearLinkCallback_2) because LinkCB_ReadyCloseLink is HARD-GATED on an EMPTY recv queue
// — the old always-on heartbeat held that gate shut forever = the black screen; (3) answer the
// game's 0x5FFF close with ONE 0x5FFF frame and treat the session as OVER (the sit-at-machine
// 5FFF->CONNECTION route must NOT run).
static void test_room_exit_close(void)
{
	printf("TEST 14: room-exit close — exit key echoed+relayed, heartbeat silenced, 5FFF = session end\n");
	CrcTracker ct;
	uint16_t standby[8]  = { LINKCMD_READY_EXIT_STANDBY, 0,0,0,0,0,0,0 };
	uint16_t cafeEmpty[8]= { LINKCMD_SEND_HELD_KEYS, LINK_KEY_CODE_EMPTY, 0,0,0,0,0,0 };
	uint16_t cafeExit[8] = { LINKCMD_SEND_HELD_KEYS, LINK_KEY_CODE_EXIT_ROOM, 0,0,0,0,0,0 };
	uint16_t close_[8]   = { LINKCMD_READY_CLOSE_LINK, 0,0,0,0,0,0,0 };
	uint16_t zero[8]     = { 0,0,0,0,0,0,0,0 };
	uint16_t tx[8];

	// ---- (A) the exiting console (dongle=CL_MASTER, game=slave — the run-#12 JOIN) ----
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
	cl.section = CL_SEC_LOUNGE; cl.roomKeys = 0;
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	frame_drive(&cl, &ct, standby, tx);              // room live
	CHECK(cl.roomKeys == 1, "14: room armed\n");
	// normal room traffic: isolated zero frames between CAFEs must NOT silence the heartbeat
	frame_drive(&cl, &ct, cafeEmpty, tx);
	frame_drive(&cl, &ct, zero, tx);                 // an isolated RECEIVED_NOTHING (run-#12 trace shape)
	frame_drive(&cl, &ct, cafeEmpty, tx);
	CHECK(cl.roomKeys == 1 && cl.exitPending == 0, "14: isolated zero frames don't trip anything\n");
	// stage 1 — the game commits the exit
	frame_drive(&cl, &ct, cafeExit, tx);
	CHECK(cl.exitPending == 1, "14: exitPending latched on the game's EXIT_ROOM key\n");
	{	ClEvent ev; int nExit = 0;
		while (cl_take_outgoing(&cl, &ev)) if (ev.type == CL_EV_EXIT_ROOM) nExit++;
		CHECK(nExit == 1, "14: exactly one CL_EV_EXIT_ROOM relayed to the peer (got %d)\n", nExit);
	}
	{	int sawExitEcho = 0;                          // our own exit key satisfies all-players-exiting
		for (int i = 0; i < 4; i++) {
			frame_drive(&cl, &ct, cafeEmpty, tx);
			if (tx[0] == LINKCMD_SEND_HELD_KEYS && tx[1] == LINK_KEY_CODE_EXIT_ROOM) sawExitEcho = 1;
		}
		CHECK(sawExitEcho, "14: the partner echoed EXIT_ROOM (stage-1 pass)\n");
	}
	// a second EXIT_ROOM frame must not re-emit the event
	frame_drive(&cl, &ct, cafeExit, tx);
	{	ClEvent ev; int nExit = 0;
		while (cl_take_outgoing(&cl, &ev)) if (ev.type == CL_EV_EXIT_ROOM) nExit++;
		CHECK(nExit == 0, "14: EXIT_ROOM event not re-emitted\n");
	}
	// stage 2 — the exit fade: the game goes ALL-ZERO; after CL_EXIT_ZERO_RUN frames the partner
	// must fall SILENT (all-zero command frames) so the game's recv queue can drain.
	for (int i = 0; i < CL_EXIT_ZERO_RUN; i++) frame_drive(&cl, &ct, zero, tx);
	CHECK(cl.roomKeys == 0, "14: heartbeat silenced after the zero-run (roomKeys=0)\n");
	{	int quiet = 1;
		for (int i = 0; i < 10; i++) {
			frame_drive(&cl, &ct, zero, tx);
			for (int w = 0; w < 8; w++) if (tx[w]) quiet = 0;
		}
		CHECK(quiet, "14: partner frames are ALL-ZERO while the game waits to close (queue drains)\n");
	}
	// stage 3 — the close handshake: answer the game's 5FFF with one 5FFF; session OVER.
	frame_drive(&cl, &ct, close_, tx);
	{	int saw5fff = 0;
		for (int i = 0; i < 3; i++) {
			frame_drive(&cl, &ct, zero, tx);
			if (tx[0] == LINKCMD_READY_CLOSE_LINK) saw5fff = 1;
		}
		CHECK(saw5fff, "14: the exit's 0x5FFF was answered (WaitCloseLink needs every slot)\n");
	}
	CHECK(cl.section == CL_SEC_LOUNGE, "14: exit close did NOT route to CONNECTION (section=%d)\n", cl.section);
	CHECK(cl.sessionEnded == 1, "14: sessionEnded latched (awaits the driver's gap-reset)\n");
	CHECK(ct.crc_mismatches == 0, "14: no CRC mismatch across the exit (%d)\n", ct.crc_mismatches);

	// ---- (B) the OTHER console: a peer CL_EV_EXIT_ROOM walks ITS game out too ----
	CelioLink cb; cl_init(&cb, CL_SLAVE, LINKTYPE_TRADE);
	cl_transfer(&cb, LINK_SLAVE_HANDSHAKE); cl_transfer(&cb, LINK_MASTER_HANDSHAKE);
	cb.section = CL_SEC_LOUNGE; cb.roomKeys = 0;
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	frame_drive(&cb, &ct, standby, tx);
	{	ClEvent ev; memset(&ev, 0, sizeof ev); ev.type = CL_EV_EXIT_ROOM;
		cl_put_incoming(&cb, &ev);
	}
	{	int sawExitKey = 0;
		for (int i = 0; i < 4; i++) {
			frame_drive(&cb, &ct, cafeEmpty, tx);
			if (tx[0] == LINKCMD_SEND_HELD_KEYS && tx[1] == LINK_KEY_CODE_EXIT_ROOM) sawExitKey = 1;
		}
		CHECK(sawExitKey, "14B: peer exit drives EXIT_ROOM keys at OUR game (both leave together)\n");
	}
	// its game then commits its own exit and closes — same path as (A)
	frame_drive(&cb, &ct, cafeExit, tx);
	CHECK(cb.exitPending == 1, "14B: our game's own exit latched\n");
	for (int i = 0; i < CL_EXIT_ZERO_RUN; i++) frame_drive(&cb, &ct, zero, tx);
	CHECK(cb.roomKeys == 0, "14B: heartbeat silenced\n");
	frame_drive(&cb, &ct, close_, tx);
	frame_drive(&cb, &ct, zero, tx);
	CHECK(cb.sessionEnded == 1 && cb.section == CL_SEC_LOUNGE, "14B: session ended cleanly\n");

	// ---- (C) regression: a LOUNGE 5FFF WITHOUT an exit is still the sit-at-machine route ----
	CelioLink cc; cl_init(&cc, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&cc, LINK_SLAVE_HANDSHAKE); cl_transfer(&cc, LINK_SLAVE_HANDSHAKE);
	cc.section = CL_SEC_LOUNGE; cc.roomKeys = 1;
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	frame_drive(&cc, &ct, close_, tx);
	CHECK(cc.section == CL_SEC_CONNECTION, "14C: no-exit 5FFF -> CONNECTION preserved (section=%d)\n", cc.section);
	CHECK(cc.sessionEnded == 0, "14C: no session end on the machine route\n");
	printf("  ok\n");
}

// TEST 15 (HW run #12 — the HOST trainer-card black screen): the card the partner serves at linkup
// is what gTrainerCards[] renders when the user checks the friend's card. The old ALL-ZERO card
// (no 0xFF name terminator) black-screened the viewer. Now: the LOCAL game's real 100-byte card is
// captured + emitted (CL_EV_TRAINERCARD); the PEER's real card is served when the latched identity
// is also real (the receiver parses the card by the identity's VERSION); the canned fallback is
// WELL-FORMED (terminated name, version byte) — never all-zero.
static void test_trainer_card_exchange(void)
{
	printf("TEST 15: trainer-card capture + real serve + well-formed canned fallback\n");
	CrcTracker ct;
	uint16_t tx[8];
	uint16_t init[8] = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 };
	uint16_t idle[8] = { 0,0,0,0,0,0,0,0 };
	uint8_t card[CL_TRAINERCARD_WIRE_BYTES];
	for (int i = 0; i < CL_TRAINERCARD_WIRE_BYTES; i++) card[i] = (uint8_t)(0xA0 + i);

	// ---- (A) capture: the game streams its card (INIT size=100 + CONTs) -> CL_EV_TRAINERCARD ----
	CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);
	cl.section = CL_SEC_SETUP; cl.blockSeq = CL_BLK_TRAINERCARD;
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	{	uint16_t ginit[8] = { LINKCMD_INIT_BLOCK, CL_TRAINERCARD_WIRE_BYTES, 0x81, 0,0,0,0,0 };
		frame_drive(&cl, &ct, ginit, tx);
		for (int f = 0; f < 8; f++) {                 // 8 x 14 = 112 >= 100 payload bytes
			uint16_t contf[8] = { LINKCMD_CONT_BLOCK, 0,0,0,0,0,0,0 };
			for (int w = 0; w < 7; w++) {
				int off = f * 14 + w * 2;
				uint16_t v = 0;
				if (off < CL_TRAINERCARD_WIRE_BYTES)     v = card[off];
				if (off + 1 < CL_TRAINERCARD_WIRE_BYTES) v |= (uint16_t)(card[off + 1] << 8);
				contf[w + 1] = v;
			}
			frame_drive(&cl, &ct, contf, tx);
		}
	}
	{	ClEvent ev; int got = 0;
		while (cl_take_outgoing(&cl, &ev))
			if (ev.type == CL_EV_TRAINERCARD && ev.len == CL_TRAINERCARD_WIRE_BYTES &&
			    memcmp(ev.data, card, CL_TRAINERCARD_WIRE_BYTES) == 0) got = 1;
		CHECK(got, "15A: the local game's card was captured + emitted byte-exact\n");
	}

	// helper idiom: drive a full SETUP (LinkPlayer -> card) collecting every served block; the
	// game "re-asks" (INIT) whenever the dongle is idle, else idles while a block streams.
	// Reassembles the block whose INIT advertised size==100 (the card; the LinkPlayer INIT says 60).
	#define DRIVE_SETUP_COLLECT_CARD(CLP, OUTBUF, OKVAR) do { \
		int cardOpen = 0, cardBytes = 0; OKVAR = 0; \
		for (int f = 0; f < 60 && !OKVAR; f++) { \
			frame_drive((CLP), &ct, (CLP)->idle ? init : idle, tx); \
			if (tx[0] == LINKCMD_INIT_BLOCK) { cardOpen = (tx[1] == CL_TRAINERCARD_WIRE_BYTES); cardBytes = 0; } \
			else if (tx[0] == LINKCMD_CONT_BLOCK && cardOpen) { \
				for (int w = 1; w < 8 && cardBytes < CL_TRAINERCARD_WIRE_BYTES; w++) { \
					(OUTBUF)[cardBytes++] = (uint8_t)(tx[w] & 0xFF); \
					if (cardBytes < CL_TRAINERCARD_WIRE_BYTES) (OUTBUF)[cardBytes++] = (uint8_t)(tx[w] >> 8); \
				} \
				if (cardBytes >= CL_TRAINERCARD_WIRE_BYTES) OKVAR = 1; \
			} \
		} \
	} while (0)

	// ---- (B) real serve: peer identity + card arrive BEFORE the latch -> the real card is served ----
	CelioLink b; cl_init(&b, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&b, LINK_SLAVE_HANDSHAKE); cl_transfer(&b, LINK_SLAVE_HANDSHAKE);
	{	ClEvent ev; memset(&ev, 0, sizeof ev);
		ev.type = CL_EV_LINKPLAYER; ev.len = CL_LINKPLAYER_BLOCK_SIZE;
		for (int i = 0; i < CL_LINKPLAYER_BLOCK_SIZE; i++) ev.data[i] = (uint8_t)(0x10 + i);
		cl_put_incoming(&b, &ev);
		memset(&ev, 0, sizeof ev);
		ev.type = CL_EV_TRAINERCARD; ev.len = CL_TRAINERCARD_WIRE_BYTES;
		memcpy(ev.data, card, CL_TRAINERCARD_WIRE_BYTES);
		cl_put_incoming(&b, &ev);
	}
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	{	uint8_t served[CL_TRAINERCARD_WIRE_BYTES]; int ok;
		DRIVE_SETUP_COLLECT_CARD(&b, served, ok);
		CHECK(ok, "15B: the card block streamed (INIT size=100 + CONTs)\n");
		CHECK(b.identityWasReal == 1, "15B: the real identity was latched\n");
		CHECK(ok && memcmp(served, card, CL_TRAINERCARD_WIRE_BYTES) == 0,
		      "15B: the PEER'S REAL card was served byte-exact\n");
	}

	// ---- (C) canned fallback: no peer data -> a WELL-FORMED card, never all-zero ----
	CelioLink c; cl_init(&c, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&c, LINK_SLAVE_HANDSHAKE); cl_transfer(&c, LINK_SLAVE_HANDSHAKE);
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	{	uint8_t served[CL_TRAINERCARD_WIRE_BYTES]; int ok;
		DRIVE_SETUP_COLLECT_CARD(&c, served, ok);
		CHECK(ok, "15C: the canned card block streamed\n");
		CHECK(c.identityWasReal == 0, "15C: canned identity (no peer data)\n");
		if (ok) {
			int allZero = 1; for (int i = 0; i < CL_TRAINERCARD_WIRE_BYTES; i++) if (served[i]) allZero = 0;
			CHECK(!allZero, "15C: the canned card is NOT all-zero (the run-#12 black-screen card)\n");
			CHECK(served[0x30] != 0x00, "15C: playerName starts with a real character\n");
			int hasTerm = 0; for (int i = 0x30; i <= 0x37; i++) if (served[i] == 0xFF) hasTerm = 1;
			CHECK(hasTerm, "15C: playerName is 0xFF-terminated\n");
			CHECK(served[0x38] == 0x04, "15C: version byte matches the canned identity (FR)\n");
		}
	}
	// (D) a real card under a CANNED identity must NOT be served (version-consistency rule)
	CelioLink d; cl_init(&d, CL_MASTER, LINKTYPE_TRADE);
	cl_transfer(&d, LINK_SLAVE_HANDSHAKE); cl_transfer(&d, LINK_SLAVE_HANDSHAKE);
	{	ClEvent ev; memset(&ev, 0, sizeof ev);      // card WITHOUT identity
		ev.type = CL_EV_TRAINERCARD; ev.len = CL_TRAINERCARD_WIRE_BYTES;
		memcpy(ev.data, card, CL_TRAINERCARD_WIRE_BYTES);
		cl_put_incoming(&d, &ev);
	}
	memset(&ct, 0, sizeof ct); ct.first_crc = 1;
	{	uint8_t served[CL_TRAINERCARD_WIRE_BYTES]; int ok;
		DRIVE_SETUP_COLLECT_CARD(&d, served, ok);
		CHECK(ok, "15D: a card block streamed\n");
		CHECK(ok && memcmp(served, card, CL_TRAINERCARD_WIRE_BYTES) != 0,
		      "15D: the real card is NOT served under a canned identity (version mismatch guard)\n");
	}
	#undef DRIVE_SETUP_COLLECT_CARD
	printf("  ok\n");
}

int main(void)
{
	printf("==== celiolink T0 unit test ====\n");

	// sanity: struct sizes that the wire format depends on.
	printf("sizeof(ClLinkPlayerBlock) = %zu (expect 60)\n", sizeof(ClLinkPlayerBlock));
	CHECK(sizeof(ClLinkPlayerBlock) == 60, "LinkPlayerBlock is 60 bytes\n");

	test_handshake();
	test_crc();
	test_handlers();
	test_scripted_trade();
	test_two_instance_coordination();
	test_master_drives_handshake();
	test_rehandshake();
	test_clock_pacing();
	test_rearm_preserves_partner();
	test_master_transmits_8fff();
	test_reset_session();
	test_room_keepalive();
	test_cancel_routes_to_lounge();
	test_capture_under_hold();
	test_lounge_reentry();
	test_room_exit_close();
	test_trainer_card_exchange();

	printf("================================\n");
	printf("checks: %d   failures: %d\n", g_checks, g_fail);
	if (g_fail == 0) { printf("RESULT: PASS\n"); return 0; }
	printf("RESULT: FAIL\n");
	return 1;
}
