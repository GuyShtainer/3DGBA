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
#include "../source/fingerprint.c"   // D6 link-surface fingerprint (TEST 16 golden / TEST 17 mutation)

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

// D7a: forward decl — the lag shim hooks EVERY frame drive (no-op unless a lagged pair is armed).
// One frame_drive == one "pump" == one game frame (~16.7ms), which is the unit the delay is in.
static void lag_pump_for(CelioLink* cl);

static void frame_drive(CelioLink* cl, CrcTracker* ct, const uint16_t game_cmd[8], uint16_t out_tx[8])
{
	lag_pump_for(cl);                 // D7a: deliver whatever the modelled radio owes this side
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

// NOTE (D7): the original instant relays — `relay_events()` / `relay_until_event()`, a bare
// `while (cl_take_outgoing(from,&ev)) cl_put_incoming(to,&ev)` — were REPLACED by their lagged
// twins below (relay_events_lagged / relay_until_event_lagged) and deleted, because a lag of 0
// reproduces them exactly and dead code that "documents history" is what git is for. The instant
// relay was never a faithful model in the first place: "loopbackPair delivers instantly, which is
// the one thing the real relay never does" (link_desync_fuzz.lua:65-91 via gen1-parity.md §7).

// ============================================================================================
// D7a — THE laggyPair DELAY SHIM (SPEC-suite-hardening.md §D7a; design source gen1recomp's
// tests/engine/link_desync_fuzz.lua via docs/kb/external/gen1-parity.md §7).
//
// WHY. relay_events()/relay_until_event() above deliver semantic events SYNCHRONOUSLY and
// INSTANTLY — "loopbackPair delivers instantly, which is the one thing the real relay never
// does" (link_desync_fuzz.lua:65-91). Our real channel is the UDS event plane at a measured
// ~23ms RTT (HANDOFF: rtt~24ms, pure-ping 5-24ms) => 1-2 game frames of ONE-WAY delay. Every
// hold in the FSM is therefore released by an event that arrives LATE, which is exactly the
// defect class run #8 shipped (edge- vs LEVEL-triggered hold releases).
//
// WHAT IS MODELLED: latency only. In-order, no loss, no duplication — because the transport is
// reliable-ordered by construction (netlink.h event plane: seq/ack + retransmit), and loss /
// reorder / duplication are already the netlink host suite's job
// (test/host/test_netlink_reliability.c, 66 checks). Modelling them here would test the
// transport twice and the FSM never.
//
// UNIT: one lag_pump == one frame_drive == one game frame. delay 1-2 == the measured radio; the
// sweep runs 0..4 (the charter) so the FSM is proven well past the real channel.
// ============================================================================================
#define LAG_Q_DEPTH 64

typedef struct { ClEvent ev; int due; } LagSlot;

typedef struct {
	LagSlot  q[LAG_Q_DEPTH];
	int      n;            // events in flight
	int      now;          // pump clock of THIS direction (advances when the RECEIVER drives a frame)
	int      fixed;        // fixed delay in pumps (used when rndMax == 0)
	int      rndMax;       // >0: delay ~ U[0, rndMax] drawn from `rng`
	uint32_t rng;          // Park-Miller state — "a failure replays from its seed alone" (:44-54)
	int      lastDue;      // in-order delivery: due is monotone non-decreasing
	int      overflow;     // events the shim could not queue (must stay 0)
	int      delivered;    // total delivered (anti-vacuity)
	int      maxInFlight;
} LagQ;

static void lag_init(LagQ* q, int fixed, int rndMax, uint32_t seed)
{
	memset(q, 0, sizeof(*q));
	q->fixed = fixed; q->rndMax = rndMax;
	q->rng = seed ? seed : 1u;          // Park-Miller state must never be 0
}

static int lag_next_delay(LagQ* q)
{
	if (q->rndMax <= 0) return q->fixed;
	q->rng = (uint32_t)(((uint64_t)q->rng * 48271u) % 2147483647u);
	return (int)(q->rng % (uint32_t)(q->rndMax + 1));
}

// Deliver one event into `to`, then MIRROR-CHECK the transport->state path byte-exactly for the
// payload-carrying event types (the "A's bytes == B's captured bytes" half of D7b).
static void lag_deliver(const ClEvent* ev, CelioLink* to)
{
	cl_put_incoming(to, ev);
	if (ev->type == CL_EV_PARTY_CHUNK && ev->len) {
		uint32_t off = (uint32_t)ev->arg * 200u;
		if (off + ev->len <= CL_PARTNER_PARTY_BYTES)
			CHECK(memcmp(to->partnerParty + off, ev->data, ev->len) == 0,
			      "D7b: relayed PARTY_CHUNK(%u) landed byte-exact in the peer's window\n", ev->arg);
	}
}

static void lag_put(LagQ* q, const ClEvent* ev, int delay)
{
	int due = q->now + delay;
	if (due < q->lastDue) due = q->lastDue;   // reliable-ORDERED: never let a later event overtake
	q->lastDue = due;
	if (q->n >= LAG_Q_DEPTH) { q->overflow++; return; }
	q->q[q->n].ev = *ev; q->q[q->n].due = due; q->n++;
	if (q->n > q->maxInFlight) q->maxInFlight = q->n;
}

// Advance this direction's clock one frame and deliver everything now due, in order.
static int lag_pump(LagQ* q, CelioLink* to)
{
	q->now++;
	int delivered = 0, keep = 0;
	for (int i = 0; i < q->n; i++) {
		if (q->q[i].due <= q->now && keep == i) {   // in-order: stop at the first not-yet-due slot
			lag_deliver(&q->q[i].ev, to);
			delivered++; q->delivered++;
		} else {
			if (keep != i) q->q[keep] = q->q[i];
			keep++;
		}
	}
	q->n = keep;
	return delivered;
}

// Pump until the queue is empty (the drain-before-compare rule: "let it drain ... or the check
// reads a mid-match frame", gen1-parity.md §8). Used where the test asserts arrival immediately.
static int lag_drain(LagQ* q, CelioLink* to)
{
	int n = 0, guard = 0;
	while (q->n > 0 && guard++ < 256) n += lag_pump(q, to);
	return n;
}

// Take everything `from` wants to ship and hand it to the modelled radio (one draw per event).
static int relay_events_lagged(CelioLink* from, LagQ* q)
{
	int n = 0; ClEvent ev;
	while (cl_take_outgoing(from, &ev)) { lag_put(q, &ev, lag_next_delay(q)); n++; }
	return n;
}

// The lagged twin of relay_until_event: draining OUR OWN outgoing queue is local and instant (the
// app does it every frame); only the DELIVERY to the peer is delayed.
static int relay_until_event_lagged(CelioLink* from, LagQ* q, uint8_t want, ClEvent* out)
{
	int found = 0; ClEvent ev;
	while (cl_take_outgoing(from, &ev)) {
		if (ev.type == want && !found) { *out = ev; found = 1; }
		if (q) lag_put(q, &ev, lag_next_delay(q));
	}
	return found;
}

// ============================================================================================
// D7b — PER-PUMP MIRROR AGREEMENT ACROSS BOTH SYNTHESIZED SIDES (SPEC §D7b).
//
// NOT a cross-instance CRC compare: under local termination the two consoles run DIFFERENT
// game<->dongle conversations by design (celiolink.h:21-27), so cross-instance CRC equality is a
// FALSE invariant (SPEC §D7b.1) — per-instance CRC correctness is already asserted every frame by
// CrcTracker. The real cross-console invariant is the MIRROR: A's "local" facts must equal B's
// "partner" facts once the relay settles (gen1's final-HP mirror, link_desync_fuzz.lua:216-240).
//
// FIELD-BY-FIELD, NOT A DIGEST — deliberately: gen1's v1 single-hash "ended in a draw that
// explained nothing"; a named field + pump index + seed localizes a failure completely.
// Violations are ACCUMULATED per field and asserted once per run (one CHECK per field per lag
// combo) instead of one CHECK per pump — same localization, sane check counter.
// ============================================================================================
typedef struct {
	CelioLink *A, *B;
	LagQ      *qAB, *qBA;        // A->B and B->A
	int        pumpIdx;
	uint32_t   seed;
	int        lagAB, lagBA, rndMax;
	int        armed;            // equality/monotone sampling on (off across a session re-arm)
	int        settledSamples;   // anti-vacuity: the equality assert must actually have run
	// violation accumulators (one per mirrored fact)
	int vSelAB, vSelBA, vConfAB, vConfBA, vSection, vMono, vTrade;
	int vPump; char vField[80];  // first violation: pump index + the field that broke
	// monotone trackers (a set slot never changes; a confirm/complete never reverts)
	int mLocalA, mLocalB, mPartA, mPartB;
	int mConfA, mConfB, mPConfA, mPConfB, mDoneA, mDoneB;
} Pair;

static Pair* g_pair = NULL;

static const char* sec_name(int s)
{
	switch (s) {
	case CL_SEC_SETUP:      return "SETUP";
	case CL_SEC_CONNECTION: return "CONNECTION";
	case CL_SEC_DISCONNECT: return "DISCONNECT";
	case CL_SEC_LOUNGE:     return "LOUNGE";
	default:                return "?";
	}
}

// The legal (A.section, B.section) pairs — derived from the dispatcher's transitions:
// SETUP->CONNECTION (celiolink.c:779), CONNECTION->DISCONNECT|LOUNGE (:650), DISCONNECT->CONNECTION
// (:845), LOUNGE->CONNECTION (:899), any->SETUP (cl_reset_session :953). One side may lead the other
// by ONE transition during a hand-off; anything else means the two consoles diverged.
static int section_pair_legal(int a, int b)
{
	static const uint8_t legal[][2] = {
		{ CL_SEC_SETUP,      CL_SEC_SETUP      }, { CL_SEC_SETUP,      CL_SEC_CONNECTION },
		{ CL_SEC_CONNECTION, CL_SEC_CONNECTION }, { CL_SEC_CONNECTION, CL_SEC_DISCONNECT },
		{ CL_SEC_DISCONNECT, CL_SEC_DISCONNECT }, { CL_SEC_CONNECTION, CL_SEC_LOUNGE     },
		{ CL_SEC_LOUNGE,     CL_SEC_LOUNGE     }, { CL_SEC_DISCONNECT, CL_SEC_CONNECTION },
	};
	for (int i = 0; i < (int)(sizeof legal / sizeof legal[0]); i++)
		if ((legal[i][0] == a && legal[i][1] == b) || (legal[i][0] == b && legal[i][1] == a))
			return 1;
	return 0;
}

static void mirror_note(Pair* p, const char* field)
{
	if (p->vPump) return;                       // keep the FIRST violation (the causal one)
	p->vPump = p->pumpIdx;
	snprintf(p->vField, sizeof p->vField, "%s", field);
}

// Sampled once per frame_drive of either side (see lag_pump_for).
static void mirror_sample(Pair* p)
{
	CelioLink* A = p->A; CelioLink* B = p->B;
	p->pumpIdx++;

	// --- section-pair legality: always armed (a divergence here is never legitimate) ---
	if (!section_pair_legal(A->section, B->section)) {
		p->vSection++;
		char f[80]; snprintf(f, sizeof f, "sectionPair(%s,%s)", sec_name(A->section), sec_name(B->section));
		mirror_note(p, f);
	}
	if (!p->armed) return;

	// --- MONOTONE facts: asserted even while events are legitimately in flight ---
	// trackers start at -1 ("never seen"); once a slot is set it may never change again
	#define MONO(track, live, name) do { \
		if ((live) >= 0) { \
			if ((track) >= 0 && (track) != (live)) { p->vMono++; mirror_note(p, name); } \
			(track) = (live); \
		} \
	} while (0)
	MONO(p->mLocalA, A->localSelectSlot,   "monotone:A.localSelectSlot");
	MONO(p->mLocalB, B->localSelectSlot,   "monotone:B.localSelectSlot");
	MONO(p->mPartA,  A->partnerSelectSlot, "monotone:A.partnerSelectSlot");
	MONO(p->mPartB,  B->partnerSelectSlot, "monotone:B.partnerSelectSlot");
	#undef MONO
	#define NOREVERT(track, live, name) do { \
		if ((track) && !(live)) { p->vMono++; mirror_note(p, name); } \
		if (live) (track) = 1; \
	} while (0)
	NOREVERT(p->mConfA,  A->localConfirmed,   "monotone:A.localConfirmed");
	NOREVERT(p->mConfB,  B->localConfirmed,   "monotone:B.localConfirmed");
	NOREVERT(p->mPConfA, A->partnerConfirmed, "monotone:A.partnerConfirmed");
	NOREVERT(p->mPConfB, B->partnerConfirmed, "monotone:B.partnerConfirmed");
	NOREVERT(p->mDoneA,  A->tradeComplete,    "monotone:A.tradeComplete");
	NOREVERT(p->mDoneB,  B->tradeComplete,    "monotone:B.tradeComplete");
	#undef NOREVERT

	// --- FULL equality only once nothing is in flight (drain-before-compare, §D7b.3) ---
	int settled = (p->qAB->n == 0 && p->qBA->n == 0 &&
	               A->outHead == A->outTail && B->outHead == B->outTail);
	if (!settled) return;
	p->settledSamples++;
	if (A->localSelectSlot >= 0 && B->partnerSelectSlot != A->localSelectSlot) {
		p->vSelAB++; mirror_note(p, "mirror:A.localSelectSlot!=B.partnerSelectSlot");
	}
	if (B->localSelectSlot >= 0 && A->partnerSelectSlot != B->localSelectSlot) {
		p->vSelBA++; mirror_note(p, "mirror:B.localSelectSlot!=A.partnerSelectSlot");
	}
	if (A->localConfirmed && !B->partnerConfirmed) {
		p->vConfAB++; mirror_note(p, "mirror:A.localConfirmed!=B.partnerConfirmed");
	}
	if (B->localConfirmed && !A->partnerConfirmed) {
		p->vConfBA++; mirror_note(p, "mirror:B.localConfirmed!=A.partnerConfirmed");
	}
}

// The hook frame_drive calls: deliver this side's due events, then sample the mirror. No-op when
// no lagged pair is armed (every other TEST in this file drives instances standalone).
static void lag_pump_for(CelioLink* cl)
{
	Pair* p = g_pair;
	if (!p) return;
	if      (cl == p->B) lag_pump(p->qAB, p->B);
	else if (cl == p->A) lag_pump(p->qBA, p->A);
	else return;
	mirror_sample(p);
}

// Supply a peer identity + a (one-chunk) party so the PARTY0 hold can release. arg `slot_byte`
// makes the parties distinguishable but the exact bytes don't matter to the coordination logic.
// When a lag queue is given the events ride the modelled radio (and get the byte-exact mirror
// check in lag_deliver) instead of being poked straight into the struct.
static void supply_peer_party_q(CelioLink* cl, uint8_t fill, LagQ* q)
{
	ClEvent ev;
	ev.type = CL_EV_LINKPLAYER; ev.arg = 0;
	ClLinkPlayerBlock blk; cl_make_linkplayer_block(&blk, LINKTYPE_TRADE_CONNECTING);
	ev.len = CL_LINKPLAYER_BLOCK_SIZE; memcpy(ev.data, &blk, ev.len);
	if (q) lag_put(q, &ev, lag_next_delay(q)); else cl_put_incoming(cl, &ev);
	for (uint8_t c = 0; c < 3; c++) {                 // per-chunk holds need ALL THREE windows
		ev.type = CL_EV_PARTY_CHUNK; ev.arg = c; ev.len = 200;
		for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)(fill + c * 3 + i);
		if (q) lag_put(q, &ev, lag_next_delay(q)); else cl_put_incoming(cl, &ev);
	}
	if (q) lag_drain(q, cl);      // the party must be in before the block stream is driven
}

// (the no-lag wrapper `supply_peer_party(cl, fill)` was folded into supply_peer_party_q(...,NULL)
//  in D7 — a NULL queue IS the instant path.)

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

// D7a/D7b: the whole TEST-5 flow, parameterized by the modelled radio delay. lagAB/lagBA are the
// one-way delays (in pumps == game frames) for A->B and B->A; rndMax>0 draws each event's delay
// from U[0,rndMax] with the printed seed. lag(0,0) reproduces the original zero-latency run.
static void run_two_instance_coordination(int lagAB, int lagBA, int rndMax, uint32_t seed)
{
	CelioLink A, B;
	cl_init(&A, CL_MASTER, LINKTYPE_TRADE);
	cl_init(&B, CL_SLAVE,  LINKTYPE_TRADE);

	LagQ qAB, qBA;
	lag_init(&qAB, lagAB, rndMax, seed);
	lag_init(&qBA, lagBA, rndMax, seed ^ 0x5EEDu);
	Pair P; memset(&P, 0, sizeof P);
	P.A = &A; P.B = &B; P.qAB = &qAB; P.qBA = &qBA;
	P.seed = seed; P.lagAB = lagAB; P.lagBA = lagBA; P.rndMax = rndMax; P.armed = 1;
	P.mLocalA = P.mLocalB = P.mPartA = P.mPartB = -1;
	g_pair = &P;
	// worst-case one-way delay in pumps — every "wait for the peer" loop bound grows by it.
	const int lagW = (rndMax > 0 ? rndMax : (lagAB > lagBA ? lagAB : lagBA)) + 2;

	CrcTracker ctA, ctB;

	// ---- (d) PARTY-BLOCK HOLD: drive A to PartyPart0 with NO peer party supplied; it must HOLD. ----
	to_party0(&A, &ctA, /*game_is_master=*/0);
	CHECK(A.blockSeq == CL_BLK_PARTY0, "A parked at PartyPart0 before any peer party (seq=%d)\n", A.blockSeq);
	drive_block_stream(&A, &ctA, 20);
	CHECK(A.blockSeq == CL_BLK_PARTY0, "A HOLDS at PartyPart0 until the peer's party arrives\n");
	CHECK(A.partnerPartyHeld == 1, "A reports the party-block hold (partnerPartyHeld)\n");

	// now supply A's peer party (B's identity+party) -> the hold releases and the stream completes.
	// It rides the modelled radio (qBA) so lag_deliver byte-checks it into A's partner window.
	supply_peer_party_q(&A, 0x10, &qBA);
	drive_block_stream(&A, &ctA, 200);
	CHECK(A.blockSeq == CL_BLK_LINKCMD, "A advances past PartyPart0 once peer party arrives (seq=%d)\n", A.blockSeq);
	CHECK(A.partnerPartyHeld == 0, "A clears the party-block hold after release\n");

	// bring B up the same way, peer party supplied up front (so we focus on the SELECT/CONFIRM gates).
	to_party0(&B, &ctB, /*game_is_master=*/1);
	supply_peer_party_q(&B, 0x20, &qAB);
	drive_block_stream(&B, &ctB, 200);
	CHECK(B.blockSeq == CL_BLK_LINKCMD, "B reached negotiation (seq=%d)\n", B.blockSeq);

	// ---- (c) SELECT HOLD: A's game offers slot iA, but B has NOT selected yet -> A must NOT finalize. ----
	const uint16_t iA = 2, jB = 4;
	uint16_t a_committed = 0xFFFF, b_committed = 0xFFFF;
	int a_set = negotiate_select(&A, &ctA, iA, 12, &a_committed);
	CHECK(!a_set, "A HOLDS SET_MONS_TO_TRADE until B's SELECT arrives (no premature finalize)\n");
	CHECK(A.selectHeld == 1, "A reports the SELECT hold\n");
	CHECK(A.localSelectSlot == (int)iA, "A recorded its own offered slot iA=%u\n", iA);

	// (a) A emitted CL_EV_SELECT(iA); ship it to B over the modelled radio.
	{
		ClEvent ev;
		int got = relay_until_event_lagged(&A, &qAB, CL_EV_SELECT, &ev);
		CHECK(got, "A emitted CL_EV_SELECT for the peer\n");
		CHECK(got && ev.arg == iA, "A's CL_EV_SELECT carries iA=%u (got %u)\n", iA, ev.arg);
	}

	// B is the HOST-side (FOLLOWER) partner: on A's relayed SELECT it must ANNOUNCE [AABB, iA] to
	// its game-Leader (whose own selection is silent-local; the pairing happens inside the game).
	// Under lag the announce cannot happen before the event lands — the loop budget grows by lagW,
	// and the LEVEL-triggered re-assert (run-#8 fix) is what makes the late release work at all.
	{
		uint16_t idleB[8] = {0,0,0,0,0,0,0,0}, txB[8];
		int sawAABB = 0, framesToAnnounce = 0;
		for (int i = 0; i < 10 + lagW && !sawAABB; i++) {
			frame_drive(&B, &ctB, idleB, txB);
			framesToAnnounce++;
			if (txB[0] == LINKCMD_CONT_BLOCK && txB[1] == LINKCMD_READY_TO_TRADE && txB[2] == iA) sawAABB = 1;
		}
		CHECK(sawAABB, "(a) B announced the peer's select [AABB, iA=%u] to its game-Leader (lag %d/%d seed %u)\n",
		      iA, lagAB, lagBA, seed);
		CHECK(!sawAABB || framesToAnnounce > lagAB || rndMax > 0,
		      "(a) the announce could not have preceded the delayed event (took %d frames, lagAB=%d)\n",
		      framesToAnnounce, lagAB);
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
		int got = relay_until_event_lagged(&B, &qBA, CL_EV_SELECT, &ev);
		CHECK(got, "B captured its game-Leader's DDDD broadcast -> CL_EV_SELECT\n");
		CHECK(got && ev.arg == jB, "B's CL_EV_SELECT carries jB=%u (got %u)\n", jB, ev.arg);
		b_committed = iA;   // the follower side commits via its game's own pairing
	}
	// A (the Leader) now releases LEVEL-TRIGGERED: idle frames let the preamble fire SET_MONS(jB).
	// This is the run-#8 defect class under lag: an EDGE-triggered release would have fired while
	// the event was still in flight (never) — the hold must re-assert every idle frame until it lands.
	{
		uint16_t idleA[8] = {0,0,0,0,0,0,0,0}, txA[8];
		int a_rel = 0;
		for (int i = 0; i < 10 + lagW && !a_rel; i++) {
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

		// A emitted CL_EV_CONFIRM; ship to B (and bring B's confirm back to A).
		{
			ClEvent ev;
			int gotA = relay_until_event_lagged(&A, &qAB, CL_EV_CONFIRM, &ev);
			CHECK(gotA, "(e) A emitted CL_EV_CONFIRM for the peer\n");
		}
		// B opens its confirm chain too, emitting CL_EV_CONFIRM; ship it to A and DRAIN before
		// comparing (gen1-parity §8: compare a settled state, never a mid-flight one).
		frame_drive(&B, &ctB, init20, tx);
		frame_drive(&B, &ctB, confirm, tx);
		for (int i = 0; i < 3; i++) frame_drive(&B, &ctB, idle, tx);
		relay_events_lagged(&B, &qBA);
		lag_drain(&qBA, &A);
		CHECK(A.partnerConfirmed == 1, "A received B's CL_EV_CONFIRM\n");

		// now A's confirm chain releases: drive frames until START_TRADE/INIT_BLOCK block ships.
		// (re-open the chain since the previous hold answered EMPTY without retiring section state)
		frame_drive(&A, &ctA, init20, tx);
		frame_drive(&A, &ctA, confirm, tx);
		for (int i = 0; i < 10 + lagW && !a_started; i++) {
			frame_drive(&A, &ctA, idle, tx);
			if (tx[0] == LINKCMD_CONT_BLOCK &&
			    (tx[1] == LINKCMD_INIT_BLOCK || tx[1] == LINKCMD_START_TRADE)) a_started = 1;
		}
		CHECK(a_started, "A released the confirm chain after B confirmed\n");
		CHECK(A.confirmHeld == 0, "A cleared the confirm hold after release\n");
	}

	// D7b: the PARTY half of the mirror set, sampled HERE — before the close. The post-trade
	// re-arm deliberately zeroes partnerPartyBytes (celiolink.c:854: "the peer re-captures +
	// re-sends its post-trade party"), so asserting it after the close would be a FALSE invariant.
	// Each side must be holding the peer's FULL party at the moment the trade is agreed; the
	// byte-exactness of every chunk was already checked as it landed (lag_deliver).
	{
		ClStatus sa, sb;
		cl_get_status(&A, &sa); cl_get_status(&B, &sb);
		CHECK(sa.partnerPartyBytes == CL_PARTY_BYTES && sb.partnerPartyBytes == CL_PARTY_BYTES,
		      "D7b: both sides hold the peer's FULL party at agreement (A=%u B=%u of %u; lag %d/%d seed %u)\n",
		      sa.partnerPartyBytes, sb.partnerPartyBytes, (unsigned)CL_PARTY_BYTES, lagAB, lagBA, seed);
	}

	// ---- (f) close out: both reach trade-complete consistently. The trade flows
	// CONNECTION -> DISCONNECT (close) -> re-handshake -> finish -> close -> tradeComplete. ----
	// The DISCONNECT close deliberately RE-ARMS the whole negotiation for the next trade round
	// (celiolink.c:845-856 clears localSelectSlot/partnerConfirmed/...), so the slot/confirm mirror
	// and its monotone rule legitimately stop applying here. Disarm those; section-pair legality
	// and the CRC trackers stay armed through the close.
	P.armed = 0;
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

	// ---- D7b, the CONVERGENCE-POINT half of the mirror set (SPEC §D7b.2 rows "tradeComplete"
	// and "holds") ---------------------------------------------------------------------------
	// The per-pump mirror above compares the facts that must track each other DURING the flow.
	// These two are only meaningful once the flow has finished, so they are asserted here, at the
	// point where nothing is in flight: the trade either converged on BOTH consoles or on neither
	// (a one-sided tradeComplete is the "one console kept my Pokemon" failure), and no hold may be
	// left standing — a hold that survives convergence is a partner that will never speak again.
	CHECK(cl_trade_complete(&A) == cl_trade_complete(&B),
	      "D7b: tradeComplete CONVERGED on both sides (A=%d B=%d; lag %d/%d rnd %d seed %u)\n",
	      cl_trade_complete(&A), cl_trade_complete(&B), lagAB, lagBA, rndMax, seed);
	{
		ClStatus sa, sb;
		cl_get_status(&A, &sa); cl_get_status(&B, &sb);
		CHECK(sa.partnerPartyHeld == 0 && sa.selectHeld == 0 && sa.confirmHeld == 0,
		      "D7b: A holds all clear at convergence (party=%u select=%u confirm=%u; lag %d/%d seed %u)\n",
		      sa.partnerPartyHeld, sa.selectHeld, sa.confirmHeld, lagAB, lagBA, seed);
		CHECK(sb.partnerPartyHeld == 0 && sb.selectHeld == 0 && sb.confirmHeld == 0,
		      "D7b: B holds all clear at convergence (party=%u select=%u confirm=%u; lag %d/%d seed %u)\n",
		      sb.partnerPartyHeld, sb.selectHeld, sb.confirmHeld, lagAB, lagBA, seed);
	}

	// ---- D7b VERDICT: one CHECK per mirrored fact, each naming the field + pump + seed ----
	#define MIRROR_OK(counter, what) CHECK((counter) == 0, \
		"D7b %s violated %d× (lag %d/%d rnd %d seed %u; first at pump %d: %s)\n", \
		what, (counter), lagAB, lagBA, rndMax, seed, P.vPump, P.vField)
	MIRROR_OK(P.vSection, "section-pair legality");
	MIRROR_OK(P.vMono,    "monotone coordination facts");
	MIRROR_OK(P.vSelAB,   "A.localSelectSlot == B.partnerSelectSlot");
	MIRROR_OK(P.vSelBA,   "B.localSelectSlot == A.partnerSelectSlot");
	MIRROR_OK(P.vConfAB,  "A.localConfirmed => B.partnerConfirmed");
	MIRROR_OK(P.vConfBA,  "B.localConfirmed => A.partnerConfirmed");
	#undef MIRROR_OK
	// ANTI-VACUITY (gen1-parity §12: an unwired tier must never report itself exercised).
	CHECK(P.settledSamples > 0, "D7b: the settled-state mirror actually ran (%d samples, lag %d/%d)\n",
	      P.settledSamples, lagAB, lagBA);
	CHECK(qAB.overflow == 0 && qBA.overflow == 0, "D7a: the lag queues never overflowed (%d/%d)\n",
	      qAB.overflow, qBA.overflow);
	CHECK(qAB.delivered > 0 && qBA.delivered > 0, "D7a: both directions actually delivered (%d/%d)\n",
	      qAB.delivered, qBA.delivered);
	CHECK((lagAB == 0 && lagBA == 0 && rndMax == 0) || qAB.maxInFlight > 0 || qBA.maxInFlight > 0,
	      "D7a: a nonzero lag really did hold events in flight\n");

	g_pair = NULL;
}

// TEST 5 wrapper: the zero-lag run (the original, unchanged assertions) plus the D7a sweep and the
// seeded randomized runs. `runs`/`firstSeed` come from argv (default 3 runs from seed 12345) so a
// failure replays with `/tmp/tc <runs> <seed>` — "a failure replays from its seed alone".
static void test_two_instance_coordination(int runs, uint32_t firstSeed)
{
	printf("TEST 5 (T1): two live instances coordinate the same slot pair\n");
	run_two_instance_coordination(0, 0, 0, 1);           // the original zero-latency run
	printf("  ok (lag 0/0)\n");

	// D7a fixed sweep: one-way delay 0..4 pumps (~0..67ms; the measured radio is 1-2).
	for (int lag = 1; lag <= 4; lag++) {
		for (int sym = 0; sym < 2; sym++) {              // asymmetric (a->b only) then symmetric
			int lagBA = sym ? lag : 0;
			run_two_instance_coordination(lag, lagBA, 0, 1);
			printf("  ok (lag %d/%d)\n", lag, lagBA);
		}
	}
	// Randomized runs: per-event delay ~ U[0,4], seed printed so any failure replays exactly.
	for (int r = 0; r < runs; r++) {
		uint32_t seed = firstSeed + (uint32_t)r * 7919u;
		run_two_instance_coordination(0, 0, 4, seed);
		printf("  ok (lag rnd<=4 seed %u)\n", seed);
	}
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

// --------------------------------------------------------------------------------------------
// TEST 12L (D7a.3, second bullet) — THE PARTY0 HOLD UNDER RADIO LAG.
//
// TEST 12 above proves the capture-under-hold pipeline with the peer's chunk poked straight into
// the FSM. TEST 5 runs the two-instance flow through the lag shim. Neither one proves the thing
// the SPEC asks for here: that the PARTY0 hold survives a DELAYED chunk for exactly as long as
// the chunk is in flight and releases on the next evaluation once it lands.
//
// This is the LEVEL-TRIGGER proof at the smallest possible scale. The run-#8 defect class was
// holds released by an EDGE: with an edge-triggered release the game's INIT_BLOCK frames that
// arrive WHILE the event is still in the air consume the only edges there will ever be, and the
// hold then never lifts. We therefore drive a real INIT_BLOCK frame on EVERY lagged frame (each
// one an evaluation the FSM must decline) and require the release to come from the arrival alone.
//
// Sweep k = 0..4 one-way delay in pumps (~0..67ms; the measured UDS radio is 1-2 pumps).
// --------------------------------------------------------------------------------------------
static void test_capture_under_hold_lagged(void)
{
	printf("TEST 12L (D7a): the PARTY0 hold under modelled radio lag (level-trigger proof)\n");
	for (int k = 0; k <= 4; k++) {
		CelioLink cl; cl_init(&cl, CL_MASTER, LINKTYPE_TRADE);
		cl_transfer(&cl, LINK_SLAVE_HANDSHAKE); cl_transfer(&cl, LINK_SLAVE_HANDSHAKE);   // establish
		cl.section = CL_SEC_CONNECTION; cl.blockSeq = CL_BLK_LINKPLAYER;
		CrcTracker ct; memset(&ct, 0, sizeof ct); ct.first_crc = 1;
		uint16_t init[8] = { LINKCMD_INIT_BLOCK, 0,0,0,0,0,0,0 }, tx[8];

		for (int i = 0; i < 30 && cl.blockSeq == CL_BLK_LINKPLAYER; i++) frame_drive(&cl, &ct, init, tx);
		frame_drive(&cl, &ct, init, tx);                       // one INIT evaluates PARTY0 -> hold
		CHECK(cl.blockSeq == CL_BLK_PARTY0 && cl.partnerPartyHeld == 1,
		      "12L(k=%d): the PARTY0 hold engaged before the chunk was sent (seq=%d held=%u)\n",
		      k, cl.blockSeq, cl.partnerPartyHeld);

		// The peer ships party chunk 0 — into the modelled radio, not into the FSM.
		LagQ q; lag_init(&q, k, 0, 1);
		ClEvent ev; ev.type = CL_EV_PARTY_CHUNK; ev.arg = 0; ev.len = 200;
		for (int i = 0; i < 200; i++) ev.data[i] = (uint8_t)(0x80 + i);
		lag_put(&q, &ev, lag_next_delay(&q));

		// Drive INIT frames; the chunk lands on pump max(k,1) (lag_pump advances the clock, THEN
		// delivers everything due — so delay 0 still costs one pump).
		int deliveredAt = -1, heldViolations = 0, releaseFrames = -1;
		for (int pump = 1; pump <= k + 40; pump++) {
			if (lag_pump(&q, &cl) > 0 && deliveredAt < 0) deliveredAt = pump;
			frame_drive(&cl, &ct, init, tx);                   // a real evaluation edge, every frame
			if (deliveredAt < 0) {                             // still in flight: the hold MUST stand
				if (cl.blockSeq != CL_BLK_PARTY0 || cl.partnerPartyHeld != 1) heldViolations++;
			} else if (releaseFrames < 0 && cl.blockSeq == CL_BLK_PARTY1) {
				releaseFrames = pump - deliveredAt;             // frames from arrival to the serve
			}
		}
		CHECK(deliveredAt == (k > 0 ? k : 1),
		      "12L(k=%d): the shim delivered on the expected pump (got %d)\n", k, deliveredAt);
		CHECK(heldViolations == 0,
		      "12L(k=%d): the hold stood for EVERY in-flight frame despite %d INIT evaluations (%d violations)\n",
		      k, deliveredAt - 1, heldViolations);
		CHECK(releaseFrames >= 0,
		      "12L(k=%d): the hold RELEASED after the delayed chunk landed (level-triggered, no new edge)\n", k);
		// Measured: 0 for every k — the evaluation on the very frame the chunk lands releases the
		// serve. The bound is deliberately TIGHT (<=2): a level-triggered hold has nothing to wait
		// for once the data is in, so a creeping delay here would be a real regression, not noise.
		CHECK(releaseFrames < 0 || releaseFrames <= 2,
		      "12L(k=%d): the release followed the arrival immediately (%d frames)\n", k, releaseFrames);
		CHECK(cl.partnerPartyBytes >= 200,
		      "12L(k=%d): the delayed chunk landed in the partner window (%u bytes)\n", k, cl.partnerPartyBytes);
		CHECK(ct.crc_mismatches == 0, "12L(k=%d): no CRC mismatch while holding (%d)\n", k, ct.crc_mismatches);
	}
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

// ============================================================================================
// D6 — LINK-SURFACE FINGERPRINT (source/fingerprint.c). Phase-13-prep slice D6; spec:
// docs/phase13-diagnostics/SPEC-suite-hardening.md §D6.7. Design source: gen1recomp's
// tests/engine/gate_fingerprint.lua via docs/kb/external/gen1-parity.md §9.
//
// TWO gates, and they only work TOGETHER:
//   TEST 16 GOLDEN  — pinned hex constants. Catches the accidental edit that "no schema check or
//                     self-comparing parity gate would catch ... would silently make two builds of
//                     the same engine refuse to link. Here it flips one hex string and fails"
//                     (gate_fingerprint.lua:1-13).
//   TEST 17 MUTATION— every surface field must MOVE the digest: "If any of these pass unchanged
//                     the gate is decorative" (gate_fingerprint.lua:50-110).
//
// BLESS DISCIPLINE (celiolink.h CL_PROTO_REV comment): the constants below may only change in a
// commit that also records WHY (KNOWN-DIFFERENCES.md / BUILDLOG.md) — moving the fingerprint breaks
// linking between every existing build and every new one. There is no --bless tool by design.
// (source/fingerprint.c is #included at the top of this file, the same way celiolink.c is.)
// ============================================================================================

// The pinned surfaces. Field order == the wire order == the hash order (fingerprint.h).
//   G1 = {"BPEE", rev 0, clProtoRev 1, netProto 1, modeFlags 5}   — Emerald, the common case
//   G2 = {"BPRE", rev 1, clProtoRev 1, netProto 1, modeFlags 5}   — the user's REAL FireRed rev1
// The pair proves gameCode AND gameRev both participate (a digest that ignored either would give
// these two the same value on some field flip).
#define G1_LANE_A 0x0DC57870u
#define G1_LANE_B 0x2A5C56C7u
#define G1_HASH   0x0DC578702A5C56C7ull
#define G2_LANE_A 0x8B048F76u
#define G2_LANE_B 0xA51D3329u
#define G2_HASH   0x8B048F76A51D3329ull

static void fp_base(DgbaFprint* f, const char* code, uint8_t rev)
{
	// NOTE the live values this pins: dgba_fprint_fill hard-wires modeFlags = DGBA_NET_EXP_DEFAULT
	// (fingerprint.h, shared with gbacore.c's session init) and the caller passes CL_PROTO_REV
	// (celiolink.h) + DGBA_PROTO (netlink.h — not includable here, it drags in <3ds.h>, so the
	// golden pins the VALUE 1 and the checks below pin the two constants the test CAN see).
	dgba_fprint_fill(f, code, rev, CL_PROTO_REV, 1);
}

static void test_fingerprint_golden(void)
{
	printf("TEST 16 (D6): link-surface fingerprint GOLDEN (pinned digests)\n");

	CHECK(sizeof(DgbaFprint) == 8, "16: the surface is 8 packed bytes (the wire format)\n");
	CHECK(DGBA_FPRINT_BYTES == 8, "16: DGBA_FPRINT_BYTES agrees with the struct\n");
	// The two live constants folded into the golden. If either moves, the golden below is stale —
	// which is exactly the parity-change signal (bump + re-pin + record, never re-pin silently).
	CHECK(CL_PROTO_REV == 1, "16: CL_PROTO_REV is 1 (the value the goldens were pinned at)\n");
	CHECK(DGBA_NET_EXP_DEFAULT == 5, "16: DGBA_NET_EXP_DEFAULT is 5 = state F (Celio local termination)\n");

	DgbaFprint g1; fp_base(&g1, "BPEE", 0);
	DgbaFprint g2; fp_base(&g2, "BPRE", 1);

	// --- canonical bytes: the struct IS the serialization, in declared order -------------------
	const uint8_t* b1 = (const uint8_t*)&g1;
	CHECK(b1[0]=='B' && b1[1]=='P' && b1[2]=='E' && b1[3]=='E', "16: bytes 0-3 = gameCode\n");
	CHECK(b1[4] == 0, "16: byte 4 = gameRev\n");
	CHECK(b1[5] == CL_PROTO_REV, "16: byte 5 = clProtoRev\n");
	CHECK(b1[6] == 1, "16: byte 6 = netProto\n");
	CHECK(b1[7] == DGBA_NET_EXP_DEFAULT, "16: byte 7 = modeFlags (link strategy nibble)\n");

	// --- the pinned digests -------------------------------------------------------------------
	// Both 32-bit lanes are pinned SEPARATELY as well as the fold, so a change that happens to
	// preserve one lane still fails visibly (lane independence).
	CHECK(dgba_fprint_lane(&g1, 0) == G1_LANE_A, "16: G1 lane A pinned (got %08X)\n", dgba_fprint_lane(&g1, 0));
	CHECK(dgba_fprint_lane(&g1, 1) == G1_LANE_B, "16: G1 lane B pinned (got %08X)\n", dgba_fprint_lane(&g1, 1));
	CHECK(dgba_fprint_hash(&g1)    == G1_HASH,   "16: G1 folded u64 pinned\n");
	CHECK(dgba_fprint_lane(&g2, 0) == G2_LANE_A, "16: G2 lane A pinned (got %08X)\n", dgba_fprint_lane(&g2, 0));
	CHECK(dgba_fprint_lane(&g2, 1) == G2_LANE_B, "16: G2 lane B pinned (got %08X)\n", dgba_fprint_lane(&g2, 1));
	CHECK(dgba_fprint_hash(&g2)    == G2_HASH,   "16: G2 folded u64 pinned\n");

	// --- the fold + the two-lane construction -------------------------------------------------
	CHECK(dgba_fprint_hash(&g1) == (((uint64_t)dgba_fprint_lane(&g1,0) << 32) | dgba_fprint_lane(&g1,1)),
	      "16: hash = laneA:laneB (gen1's fold)\n");
	CHECK(dgba_fprint_lane(&g1,0) != dgba_fprint_lane(&g1,1),
	      "16: the two lanes really do start from different bases (64 bits of signal, not 32 twice)\n");
	CHECK(dgba_fprint_hash(&g1) != dgba_fprint_hash(&g2), "16: the two goldens are distinct\n");

	// --- short / NULL game codes zero-fill (the 'no core loaded yet' lobby case) ----------------
	DgbaFprint sh; dgba_fprint_fill(&sh, "BP", 0, CL_PROTO_REV, 1);
	const uint8_t* bs = (const uint8_t*)&sh;
	CHECK(bs[0]=='B' && bs[1]=='P' && bs[2]==0 && bs[3]==0, "16: a short game code zero-fills\n");
	DgbaFprint nl; dgba_fprint_fill(&nl, NULL, 0, CL_PROTO_REV, 1);
	CHECK(((const uint8_t*)&nl)[0] == 0, "16: a NULL game code zero-fills (never reads off the end)\n");

	// --- the netlog line: format regression (tools grep this; D6.6) ----------------------------
	char line[224];
	int n = dgba_fprint_format(&g1, dgba_fprint_hash(&g1), &g2, dgba_fprint_hash(&g2), 1, line, (int)sizeof line);
	CHECK(n > 0, "16: format returns a length\n");
	CHECK(!strcmp(line,
	      "# fprint local=BPEE r0 cl1 np1 m5 h=0DC578702A5C56C7 peer=BPRE r1 cl1 np1 m5 h=8B048F76A51D3329"
	      " verdict=DIFF:gameCode(BPEE!=BPRE),gameRev(0!=1)"),
	      "16: DIFF netlog line is byte-exact — got '%s'\n", line);
	n = dgba_fprint_format(&g1, dgba_fprint_hash(&g1), &g1, dgba_fprint_hash(&g1), 1, line, (int)sizeof line);
	CHECK(strstr(line, "verdict=MATCH") != NULL, "16: identical surfaces read MATCH\n");
	// UNKNOWN DISCIPLINE: no peer surface is NEVER a match (absence of evidence is not a PASS).
	n = dgba_fprint_format(&g1, dgba_fprint_hash(&g1), &g2, 0, 0, line, (int)sizeof line);
	CHECK(!strcmp(line, "# fprint local=BPEE r0 cl1 np1 m5 h=0DC578702A5C56C7 peer=none verdict=unknown"),
	      "16: an absent peer logs verdict=unknown — got '%s'\n", line);
	CHECK(strstr(line, "MATCH") == NULL, "16: an absent peer NEVER reads as a match\n");
	CHECK(dgba_fprint_format(NULL, 0, NULL, 0, 0, line, (int)sizeof line) == -1, "16: format rejects a NULL local\n");

	// --- truncation safety (a HUD/netlog buffer is finite; a short buffer must not corrupt) -----
	char tiny[24]; memset(tiny, 0x7E, sizeof tiny);
	dgba_fprint_format(&g1, dgba_fprint_hash(&g1), &g2, dgba_fprint_hash(&g2), 1, tiny, 16);
	CHECK(tiny[15] == '\0', "16: format NUL-terminates inside the given max\n");
	CHECK(tiny[16] == 0x7E && tiny[23] == 0x7E, "16: format never writes past max\n");

	printf("  ok\n");
}

// TEST 17 — MUTATION: every field of the surface must move the digest, or the gate is decorative.
static void test_fingerprint_mutation(void)
{
	printf("TEST 17 (D6): fingerprint MUTATION — every surface field moves the digest\n");

	DgbaFprint base; fp_base(&base, "BPEE", 0);
	const uint64_t h0 = dgba_fprint_hash(&base);
	CHECK(h0 == G1_HASH, "17: the mutation base is the TEST-16 golden\n");

	// determinism within a process (gate_fingerprint.lua:112-124)
	CHECK(dgba_fprint_hash(&base) == h0, "17: recomputing the same surface gives the same digest\n");
	DgbaFprint copy = base;
	CHECK(dgba_fprint_hash(&copy) == h0, "17: a byte-identical copy hashes identically\n");

	// One mutation per FIELD (all four game-code bytes individually — a 4cc is one field but four
	// bytes, and a digest that only mixed the first byte would still pass a 1-field test).
	struct { const char* name; int off; uint8_t val; } mut[] = {
		{ "gameCode[0]", 0, (uint8_t)'C' }, { "gameCode[1]", 1, (uint8_t)'Q' },
		{ "gameCode[2]", 2, (uint8_t)'F' }, { "gameCode[3]", 3, (uint8_t)'D' },
		{ "gameRev",     4, 1            }, { "clProtoRev",  5, 2 },
		{ "netProto",    6, 2            }, { "modeFlags",   7, 3 },
	};
	const int nm = (int)(sizeof mut / sizeof mut[0]);
	uint64_t hs[8];

	for (int i = 0; i < nm; i++) {
		DgbaFprint m = base;
		uint8_t* mb = (uint8_t*)&m;
		CHECK(mb[mut[i].off] != mut[i].val, "17: %s mutation actually changes the byte\n", mut[i].name);
		mb[mut[i].off] = mut[i].val;
		hs[i] = dgba_fprint_hash(&m);
		CHECK(hs[i] != h0, "17: mutating %s MOVES the u64 digest\n", mut[i].name);
		// lane independence: a single byte flip must move BOTH lanes (FNV-1a's per-byte step is a
		// bijection on the 32-bit state, so anything less means the lane stopped reading the byte).
		CHECK(dgba_fprint_lane(&m, 0) != dgba_fprint_lane(&base, 0), "17: %s moves lane A\n", mut[i].name);
		CHECK(dgba_fprint_lane(&m, 1) != dgba_fprint_lane(&base, 1), "17: %s moves lane B\n", mut[i].name);
		// mutation ISOLATION: putting the byte back restores the golden exactly.
		mb[mut[i].off] = ((const uint8_t*)&base)[mut[i].off];
		CHECK(dgba_fprint_hash(&m) == h0, "17: restoring %s restores the golden\n", mut[i].name);
	}
	// no two single-field mutants collide (a digest that folded fields together could alias them)
	for (int i = 0; i < nm; i++)
		for (int j = i + 1; j < nm; j++)
			CHECK(hs[i] != hs[j], "17: mutants %s and %s do not collide\n", mut[i].name, mut[j].name);

	// --- dgba_fprint_diff names EXACTLY the mutated field (the modDiff lesson) ------------------
	{	struct { int off; uint8_t val; const char* tok; } dm[] = {
			{ 0, (uint8_t)'C', "gameCode(BPEE!=CPEE)" }, { 4, 1, "gameRev(0!=1)" },
			{ 5, 2, "clProtoRev(1!=2)" }, { 6, 2, "netProto(1!=2)" }, { 7, 3, "modeFlags(5!=3)" },
		};
		for (int i = 0; i < (int)(sizeof dm / sizeof dm[0]); i++) {
			DgbaFprint m = base; ((uint8_t*)&m)[dm[i].off] = dm[i].val;
			char d[96];
			int nd = dgba_fprint_diff(&base, &m, d, (int)sizeof d);
			CHECK(nd == 1, "17: one changed field => one named field (got %d for %s)\n", nd, dm[i].tok);
			CHECK(!strcmp(d, dm[i].tok), "17: diff names it exactly — expected '%s' got '%s'\n", dm[i].tok, d);
		}
	}
	// identical surfaces: 0 fields, empty string (the HUD prints MATCH off this)
	{	char d[32]; memset(d, 0x5A, sizeof d);
		CHECK(dgba_fprint_diff(&base, &base, d, (int)sizeof d) == 0, "17: identical surfaces diff to 0 fields\n");
		CHECK(d[0] == '\0', "17: a 0-field diff is the empty string\n");
	}
	// all five fields at once: five names, comma-separated, no spaces (one greppable netlog token)
	{	DgbaFprint m; dgba_fprint_fill(&m, "BPRE", 9, 7, 8); ((uint8_t*)&m)[7] = 2;
		char d[128];
		CHECK(dgba_fprint_diff(&base, &m, d, (int)sizeof d) == 5, "17: five differing fields are all named\n");
		CHECK(strchr(d, ' ') == NULL, "17: the diff string has no spaces (stays one token)\n");
		CHECK(strstr(d, "gameCode(") && strstr(d, "gameRev(") && strstr(d, "clProtoRev(")
		   && strstr(d, "netProto(") && strstr(d, "modeFlags("), "17: all five field names present\n");
	}
	// truncation safety of the diff itself (a 64-byte HUD buffer meets a 5-field diff)
	{	char d[80]; memset(d, 0x7E, sizeof d);
		DgbaFprint m; dgba_fprint_fill(&m, "BPRE", 9, 7, 8); ((uint8_t*)&m)[7] = 2;
		int nd = dgba_fprint_diff(&base, &m, d, 20);
		CHECK(nd == 5, "17: a truncated diff still COUNTS every differing field\n");
		CHECK(d[19] == '\0' || strlen(d) < 20, "17: a truncated diff stays NUL-terminated\n");
		CHECK(d[20] == 0x7E && d[79] == 0x7E, "17: a truncated diff never writes past outMax\n");
	}
	// NULL-safety (the lobby calls this before either side is known)
	{	char d[16]; memset(d, 0x11, sizeof d);
		CHECK(dgba_fprint_diff(NULL, &base, d, (int)sizeof d) == 0, "17: diff(NULL, b) is 0 fields\n");
		CHECK(d[0] == '\0', "17: diff(NULL, b) still terminates the buffer\n");
		CHECK(dgba_fprint_lane(NULL, 0) == 0 && dgba_fprint_hash(NULL) == 0, "17: hashing NULL is 0, not a crash\n");
		dgba_fprint_fill(NULL, "BPEE", 0, 1, 1);   // must not fault
		CHECK(1, "17: fill(NULL, ...) is a no-op\n");
	}
	printf("  ok\n");
}

int main(int argc, char** argv)
{
	// D7a: optional `[runs] [firstSeed]` — the randomized lag runs. Defaults 3 runs from 12345.
	int runs = (argc > 1) ? atoi(argv[1]) : 3;
	uint32_t firstSeed = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : 12345u;
	if (runs < 0) runs = 0;

	printf("==== celiolink T0 unit test ====\n");

	// sanity: struct sizes that the wire format depends on.
	printf("sizeof(ClLinkPlayerBlock) = %zu (expect 60)\n", sizeof(ClLinkPlayerBlock));
	CHECK(sizeof(ClLinkPlayerBlock) == 60, "LinkPlayerBlock is 60 bytes\n");

	test_handshake();
	test_crc();
	test_handlers();
	test_scripted_trade();
	test_two_instance_coordination(runs, firstSeed);   // D7a/D7b: zero-lag + the lag sweep
	test_master_drives_handshake();
	test_rehandshake();
	test_clock_pacing();
	test_rearm_preserves_partner();
	test_master_transmits_8fff();
	test_reset_session();
	test_room_keepalive();
	test_cancel_routes_to_lounge();
	test_capture_under_hold();
	test_capture_under_hold_lagged();   // D7a.3: the same hold, with the chunk delayed 0..4 pumps
	test_lounge_reentry();
	test_room_exit_close();
	test_trainer_card_exchange();
	test_fingerprint_golden();      // D6 (SPEC-suite-hardening §D6.7)
	test_fingerprint_mutation();    // D6

	printf("================================\n");
	printf("checks: %d   failures: %d\n", g_checks, g_fail);
	if (g_fail == 0) { printf("RESULT: PASS\n"); return 0; }
	printf("RESULT: FAIL\n");
	return 1;
}
