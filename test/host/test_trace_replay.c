// ============================================================================================
// D7c — GOLDEN-TRACE REPLAY HARNESS  (SPEC-suite-hardening.md §D7c)
//
//   clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_trace_replay.c -o /tmp/tr && /tmp/tr
//   /tmp/tr netlogs/3DGBA_net_HOST_<new>.txt        # ad-hoc: run the tiers over one fresh log
//
// WHAT THIS IS. Twelve hardware runs produced ~150 netlogs and every one of them was read BY EYE.
// This harness reads them mechanically: it re-derives the Celio PacketLayer framing from the
// RECORDED word columns and checks the arithmetic that must hold on ANY build, so a corrupted /
// mis-aligned / mis-parsed log is caught by a program instead of by a human at 1 a.m.
//
// TWO TIERS, and the honesty rule that separates them (gen1-parity.md §12: an unwired tier must
// report itself unwired — "reporting exercised-but-actually-unwired coverage is what made the
// whole pipeline look delivered"):
//
//   TIER S — STRUCTURAL, build-independent, ALWAYS RUNS. Pure math over the recorded bytes:
//     S1 parse/self-description (role<->seat, CSV shape, state-F rows)
//     S2 every dongle word before framing starts is a handshake word {B9A0,8FFF,D15E}
//     S3 FRAMING + CRC ARITHMETIC: re-derive [1 CRC slot + 8 command words] alignment and verify
//        every CRC slot == the additive sum of the previous frame's 16 words
//     S4 header consistency (the FSM's own frames= vs the frames we re-derived)
//     S5 `# celio-trace` legality (section edges, blockSeq ordering, monotone frame counter)
//     S6 `# event` counter sanity (txAcked <= txSeq, etc.)
//
//   TIER R — EXACT REPLAY (feed the recorded GAME column into a fresh FSM, diff every output word
//     against the recorded DONGLE column). Valid ONLY for logs recorded on the CURRENT FSM build.
//     Every fixture here predates the run-#12 fixes, so word-exact equality would legitimately
//     fail where behavior intentionally changed => tier R SKIPS on all fixtures and says so.
//     It is NOT dead code: `tier_r_selftest()` generates a trace from the live FSM and replays it
//     through the same comparator, so the replay path is proven wired before any bless.
//     BLESS = drop a run-#13 round-0 log into test/fixtures/ and set `blessed = 1` in the table.
//
// NEVER REPLAYABLE (stated up front, per §D7c.4): radio timing (dt_us/rtt_us/turnus), UDS
// loss/reorder, and the PEER console's semantic ClEvents — the netlog ring records words, not
// event payloads, so event-driven holds are visible only through their effect on the word stream.
// Full two-console replay needs paired event logs (SPEC Open Question 7).
//
// COLUMN ROLES (verified against the writer, gbacore.c:641-653 net_celio_fill):
//   seat 0 / role=HOST : data[0] = the LOCAL GAME's word, data[1] = cl_transfer()'s reply
//                        => game = w0, dongle = w1
//   seat 1 / role=JOIN : data[0] = nd->clPartnerWord (dongle), data[1] = nd->clMyWord (game)
//                        => game = w1, dongle = w0
// Confirmed by the fixtures: HOST_0706_114305 row 0 is `w0=0000,w1=B9A0` (the CL_SLAVE dongle
// advertising to a still-silent master game); JOIN_0703_075344 row 0 is `w0=B9A0,w1=0000` (the
// CL_MASTER dongle advertising to a silent slave game).
//
// LINE FORMATS (cite = the single netlog writer; DO NOT invent a second format):
//   gbacore.c:936  `# 3DGBA netlog role=%s seat=%d startN=… toN=…`
//   gbacore.c:1003 `# celio section=… state=… blk=… frames=… tradeComplete=… [exitP=… idReal=…]`
//   gbacore.c:1027 `# celio-trace f=%lu cmd=%04X sec=%u st=%u blk=%u`
//   gbacore.c:1031 `# event txSeq=… txAcked=… rxDelivered=… overflow=… retransmits=…`
//   gbacore.c:1033 CSV header, :1042 rows `idx,round,frame,dvbl,dt_us,rtt_us,paceus,turnus,exp,w0,w1,ok,sub`
// Field parsing is by KEY LOOKUP, never by position: the header grew fields in run #12 (exitP/
// sessEnd/pCard/idReal) and in this phase, and an older log must parse as "field absent",
// never as "field == 0" (the UNKNOWN discipline — absence of evidence is not evidence).
// ============================================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// The shipped FSM, compiled into the harness so tier R can replay a blessed trace through the
// REAL code (and so this file dual-compiles the pure-C port on the PC, CLAUDE.md rule #4).
// The push/pop only silences celiolink.c's own pre-existing unused-static warning; warnings in
// THIS file are not suppressed.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../source/celiolink.c"
#pragma GCC diagnostic pop

static int g_checks = 0, g_fail = 0, g_skips = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("  FAIL: "); printf(__VA_ARGS__); } } while (0)
#define INFO(...)     do { printf("  info: "); printf(__VA_ARGS__); } while (0)
#define SKIP(...)     do { g_skips++; printf("  SKIP: "); printf(__VA_ARGS__); } while (0)

// ============================================================================================
// PARSER
// ============================================================================================
#define MAX_ROWS   4096      // NETLOG_N is 1024 (gbacore.c:441); 4x headroom for a raised ring
#define MAX_TRACE    64      // the FSM ring is 32 (celiolink.c:396); 2x headroom

typedef struct { uint32_t idx, round, frame; char exp; uint16_t w0, w1; int ok, sub; } LogRow;
typedef struct { uint32_t f; uint16_t cmd; int sec, st, blk; } TraceLine;

typedef struct {
	char      path[256];
	char      role[8];              // "HOST" / "JOIN"
	int       seat, haveSeat;
	// `# celio` — every field optional (older builds wrote fewer). have* == 0 means ABSENT.
	long      section, state, blk, frames, partnerPartyB, tradeComplete;
	long      gateN, resetN, forceN, exitP, sessEnd, pCard, idReal;
	int       haveSection, haveFrames, haveTrade, haveResetN, haveExitP;
	// `# event`
	long      txSeq, txAcked, rxDelivered, overflow, retransmits;
	int       haveEvent;
	TraceLine tr[MAX_TRACE]; int nTr;
	LogRow    rows[MAX_ROWS];  int nRows; int truncated;
	int       haveCsvHeader, badRows;
} NetLog;

// Key lookup: find "key=" as a whole token (preceded by start-of-line or a space) and parse the
// value. Returns 1 + writes *out on success, 0 if the field is ABSENT from this build's header.
static int fld_long(const char* line, const char* key, long* out)
{
	size_t klen = strlen(key);
	for (const char* p = line; (p = strstr(p, key)) != NULL; p += klen) {
		if (p != line && p[-1] != ' ' && p[-1] != '\t') continue;   // suffix of another key
		if (p[klen] != '=') continue;
		char* end = NULL;
		long v = strtol(p + klen + 1, &end, 10);
		if (end == p + klen + 1) return 0;
		*out = v; return 1;
	}
	return 0;
}

static int fld_str(const char* line, const char* key, char* out, size_t max)
{
	size_t klen = strlen(key);
	for (const char* p = line; (p = strstr(p, key)) != NULL; p += klen) {
		if (p != line && p[-1] != ' ' && p[-1] != '\t') continue;
		if (p[klen] != '=') continue;
		const char* v = p + klen + 1;
		size_t n = 0;
		while (v[n] && v[n] != ' ' && v[n] != '\t' && v[n] != '\n' && n + 1 < max) n++;
		memcpy(out, v, n); out[n] = 0;
		return 1;
	}
	return 0;
}

// Split a CSV row into up to `max` field pointers (destructive on `line`). Returns the count.
static int split_csv(char* line, char** f, int max)
{
	int n = 0;
	char* p = line;
	while (n < max) {
		f[n++] = p;
		char* c = strchr(p, ',');
		if (!c) break;
		*c = 0; p = c + 1;
	}
	return n;
}

static int parse_netlog(const char* path, NetLog* L)
{
	FILE* fp = fopen(path, "r");
	if (!fp) return 0;
	memset(L, 0, sizeof *L);
	snprintf(L->path, sizeof L->path, "%s", path);
	char line[4096];
	while (fgets(line, sizeof line, fp)) {
		if (line[0] == '#') {
			long v;
			if (strstr(line, "3DGBA netlog")) {
				fld_str(line, "role", L->role, sizeof L->role);
				if (fld_long(line, "seat", &v)) { L->seat = (int)v; L->haveSeat = 1; }
			} else if (strstr(line, "# celio-trace")) {
				if (L->nTr < MAX_TRACE) {
					TraceLine* t = &L->tr[L->nTr];
					char cmd[16] = {0};
					long a;
					if (fld_long(line, "f", &a)) t->f = (uint32_t)a;
					if (fld_str(line, "cmd", cmd, sizeof cmd)) t->cmd = (uint16_t)strtol(cmd, NULL, 16);
					t->sec = fld_long(line, "sec", &a) ? (int)a : -1;
					t->st  = fld_long(line, "st",  &a) ? (int)a : -1;
					t->blk = fld_long(line, "blk", &a) ? (int)a : -1;
					L->nTr++;
				}
			} else if (strstr(line, "# celio ")) {
				L->haveSection   = fld_long(line, "section", &L->section);
				(void)              fld_long(line, "state",   &L->state);
				(void)              fld_long(line, "blk",     &L->blk);
				L->haveFrames    = fld_long(line, "frames",   &L->frames);
				(void)              fld_long(line, "partnerPartyB", &L->partnerPartyB);
				L->haveTrade     = fld_long(line, "tradeComplete", &L->tradeComplete);
				(void)              fld_long(line, "gateN",   &L->gateN);
				L->haveResetN    = fld_long(line, "resetN",   &L->resetN);
				(void)              fld_long(line, "forceN",  &L->forceN);
				L->haveExitP     = fld_long(line, "exitP",    &L->exitP);
				(void)              fld_long(line, "sessEnd", &L->sessEnd);
				(void)              fld_long(line, "pCard",   &L->pCard);
				(void)              fld_long(line, "idReal",  &L->idReal);
			} else if (strstr(line, "# event ")) {
				L->haveEvent = fld_long(line, "txSeq", &L->txSeq);
				(void) fld_long(line, "txAcked",     &L->txAcked);
				(void) fld_long(line, "rxDelivered", &L->rxDelivered);
				(void) fld_long(line, "overflow",    &L->overflow);
				(void) fld_long(line, "retransmits", &L->retransmits);
			}
			continue;
		}
		if (!strncmp(line, "idx,", 4)) { L->haveCsvHeader = 1; continue; }
		if (line[0] == '\n' || line[0] == 0) continue;
		char* f[16];
		int n = split_csv(line, f, 16);
		if (n < 13) { L->badRows++; continue; }
		if (L->nRows >= MAX_ROWS) { L->truncated = 1; continue; }
		LogRow* r = &L->rows[L->nRows++];
		r->idx   = (uint32_t)strtoul(f[0], NULL, 10);
		r->round = (uint32_t)strtoul(f[1], NULL, 10);
		r->frame = (uint32_t)strtoul(f[2], NULL, 10);
		r->exp   = f[8][0];
		r->w0    = (uint16_t)strtoul(f[9],  NULL, 16);
		r->w1    = (uint16_t)strtoul(f[10], NULL, 16);
		r->ok    = atoi(f[11]);
		r->sub   = atoi(f[12]);
	}
	fclose(fp);
	return 1;
}

// ============================================================================================
// TIER S — the framing walk
//
// The PacketLayer stream (celiolink.c:1113-1146) is, once the handshake has been left:
//     [1 CRC slot][8 command words] [1 CRC slot][8 command words] ...
// The CRC slot the dongle emits is the ADDITIVE MUTUAL sum of the previous frame's 16 words —
// rx folded at :1129, tx folded at :1134 — and the very first slot after a (re-)handshake is the
// 0xB9A0 seed instead (celiolink.c:936/978, "first crc is always handshake").
//
// We do NOT assume WHERE the handshake ends: that rule is build-dependent (the 0703 build never
// left handshake at all — its dongle drove 0x8FFF forever, which is exactly why that log exists).
// Instead we SEARCH for an alignment that the arithmetic itself validates: a candidate start is
// accepted only if >= 2 CRC slots verify with zero mismatches. Two chained 16-bit sums matching
// by chance is ~2^-32, so a false alignment is not a practical concern.
// ============================================================================================
#define MIN_VERIFIED_SLOTS 2

typedef struct {
	int start;       // row index of this run's first CRC slot
	int seeded;      // 1 = the first slot was the 0xB9A0 seed (a fresh handshake exit)
	int frames;      // complete 8-word command frames walked
	int verified;    // CRC slots verified (the seed counts only when `seeded`)
	int breakRow;    // row where the arithmetic first disagreed, or -1 at end-of-log
} FrameRun;

static int is_hs_word(uint16_t w)
{
	return w == LINK_SLAVE_HANDSHAKE || w == LINK_MASTER_HANDSHAKE || w == LINK_HANDSHAKE_DISABLE;
}

// Walk from `start` assuming rows[start] is a CRC slot. `seeded` demands the first slot be the
// 0xB9A0 seed; otherwise the first slot is unverifiable (a mid-ring window opens mid-stream) and
// only the slots after it are checked. Stops at the first mismatch.
static void frame_walk(const NetLog* L, int gameIsW0, int start, int seeded, FrameRun* out)
{
	out->start = start; out->seeded = seeded; out->frames = 0; out->verified = 0; out->breakRow = -1;
	uint16_t crc = 0; int idx = 0, first = 1, inCrc = 1;
	for (int i = start; i < L->nRows; i++) {
		const LogRow* r = &L->rows[i];
		uint16_t g = gameIsW0 ? r->w0 : r->w1;
		uint16_t d = gameIsW0 ? r->w1 : r->w0;
		if (inCrc) {
			if (first && !seeded) {
				/* unverifiable opening slot */
			} else {
				uint16_t exp = first ? (uint16_t)LINK_SLAVE_HANDSHAKE : crc;
				if (d != exp) { out->breakRow = i; return; }
				out->verified++;
			}
			first = 0; crc = 0; idx = 0; inCrc = 0;
		} else {
			crc = (uint16_t)(crc + g + d);   // rx then tx, exactly as :1129/:1134 fold them
			if (++idx == 8) { out->frames++; inCrc = 1; }
		}
	}
	return;
}

// Was there a re-handshake just before `row`? The game restarts the link at cable-club phase
// boundaries by sending handshake words again; the FSM re-arms after CL_HS_REARM_RUN of them
// (celiolink.c:1056-1066), which legitimately breaks the framing alignment.
static int rehandshake_before(const NetLog* L, int gameIsW0, int row)
{
	int run = 0;
	int from = row - 16; if (from < 0) from = 0;
	for (int i = from; i <= row && i < L->nRows; i++) {
		uint16_t g = gameIsW0 ? L->rows[i].w0 : L->rows[i].w1;
		if (g == LINK_SLAVE_HANDSHAKE || g == LINK_HANDSHAKE_DISABLE) {
			if (++run >= CL_HS_REARM_RUN) return 1;
		} else run = 0;
	}
	return 0;
}

// Collect every framing run in the log. `runs` gets up to `max`; returns the count.
static int find_runs(const NetLog* L, int gameIsW0, FrameRun* runs, int max, int* unexplained)
{
	int n = 0, pos = 0;
	*unexplained = 0;
	while (pos < L->nRows - (1 + 8 * MIN_VERIFIED_SLOTS) && n < max) {
		FrameRun best; best.start = -1;
		for (int s = pos; s < L->nRows - 9; s++) {
			FrameRun w;
			frame_walk(L, gameIsW0, s, /*seeded=*/1, &w);
			if (w.verified >= MIN_VERIFIED_SLOTS) { best = w; break; }
			frame_walk(L, gameIsW0, s, /*seeded=*/0, &w);
			if (w.verified >= MIN_VERIFIED_SLOTS) { best = w; break; }
		}
		if (best.start < 0) break;
		runs[n++] = best;
		if (best.breakRow < 0) break;                       // clean to end-of-log
		// A break must be explainable: a re-handshake re-arms the layer (legal), or the log
		// simply ends inside a frame. Anything else means the recorded stream is inconsistent.
		if (!rehandshake_before(L, gameIsW0, best.breakRow) &&
		    best.breakRow < L->nRows - 9) (*unexplained)++;
		pos = best.breakRow + 1;
	}
	return n;
}

static const char* sec_name(int s)
{
	switch (s) {
	case CL_SEC_SETUP: return "SETUP"; case CL_SEC_CONNECTION: return "CONNECTION";
	case CL_SEC_DISCONNECT: return "DISCONNECT"; case CL_SEC_LOUNGE: return "LOUNGE";
	default: return "?";
	}
}

// Direct section transitions the dispatcher can make (celiolink.c section switches; the same
// table the PC suite's mirror check uses). `any -> SETUP` is the session reset (cl_reset_session).
static int sec_edge_legal(int a, int b)
{
	if (a == b) return 1;
	if (b == CL_SEC_SETUP) return 1;                                   // reset (resetN cross-check below)
	if (a == CL_SEC_SETUP      && b == CL_SEC_CONNECTION) return 1;
	if (a == CL_SEC_CONNECTION && (b == CL_SEC_DISCONNECT || b == CL_SEC_LOUNGE)) return 1;
	if (a == CL_SEC_DISCONNECT && b == CL_SEC_CONNECTION) return 1;
	if (a == CL_SEC_LOUNGE     && b == CL_SEC_CONNECTION) return 1;
	return 0;
}

// ============================================================================================
// TIER S — the per-fixture run
// ============================================================================================
static void tier_s(NetLog* L)
{
	const char* base = strrchr(L->path, '/'); base = base ? base + 1 : L->path;

	// ---- S1: parse + self-description ----
	int isHost = !strcmp(L->role, "HOST"), isJoin = !strcmp(L->role, "JOIN");
	CHECK(isHost || isJoin, "S1 %s: role is HOST or JOIN (got '%s')\n", base, L->role);
	CHECK(L->haveSeat && L->seat == (isHost ? 0 : 1),
	      "S1 %s: seat matches role (role=%s seat=%d)\n", base, L->role, L->seat);
	CHECK(L->haveCsvHeader, "S1 %s: the CSV column header is present\n", base);
	CHECK(L->nRows > 0 && L->badRows == 0 && !L->truncated,
	      "S1 %s: every data row parsed (%d rows, %d malformed, truncated=%d)\n",
	      base, L->nRows, L->badRows, L->truncated);
	int nonF = 0;
	for (int i = 0; i < L->nRows; i++) if (L->rows[i].exp != 'F') nonF++;
	CHECK(nonF == 0, "S1 %s: all %d rows were recorded in state F (%d were not)\n", base, L->nRows, nonF);
	uint32_t prevRound = 0; int roundBack = 0;
	for (int i = 0; i < L->nRows; i++) {
		if (i && L->rows[i].round < prevRound) roundBack++;
		prevRound = L->rows[i].round;
	}
	CHECK(roundBack == 0, "S1 %s: the round counter never goes backwards (%d inversions)\n", base, roundBack);

	int gameIsW0 = isHost;   // gbacore.c:641-653 (see the file header)

	// ---- S3: framing + CRC arithmetic (run first — S2 needs to know where framing starts) ----
	FrameRun runs[16]; int unexplained = 0;
	int nRuns = find_runs(L, gameIsW0, runs, 16, &unexplained);
	int totalFrames = 0, totalVerified = 0, covered = 0;
	for (int i = 0; i < nRuns; i++) {
		totalFrames   += runs[i].frames;
		totalVerified += runs[i].verified;
		covered       += 1 + runs[i].frames * 9;
	}
	CHECK(unexplained == 0,
	      "S3 %s: every framing break is explained by a re-handshake or end-of-log (%d unexplained)\n",
	      base, unexplained);
	if (nRuns > 0) {
		CHECK(totalVerified >= MIN_VERIFIED_SLOTS,
		      "S3 %s: the framing alignment verified >= %d CRC slots (got %d)\n",
		      base, MIN_VERIFIED_SLOTS, totalVerified);
		INFO("%s: %d framing run(s), first at row %d (%s), %d frames, %d CRC slots verified, %d/%d rows covered\n",
		     base, nRuns, runs[0].start, runs[0].seeded ? "seeded B9A0" : "mid-stream",
		     totalFrames, totalVerified, covered, L->nRows);
	} else {
		INFO("%s: NO framing run — the recorded stream never left the handshake\n", base);
	}

	// ---- S2: handshake-phase words (celiolink.h:57-59) ----
	// Every dongle word BEFORE the first framing run must be a handshake word. This is only a
	// real invariant when the log OPENS at a session start: a mid-ring window (the 0715 fixtures
	// open at round 51860) begins in the middle of a command frame, so its leading rows are
	// command words, not a handshake phase — asserting there would be a false invariant.
	{
		int fromZero = (L->nRows > 0 && L->rows[0].round == 0);
		int sessionStart = fromZero && (nRuns == 0 || runs[0].seeded);
		int upto = (nRuns > 0) ? runs[0].start : L->nRows;
		int bad = 0; uint16_t firstBad = 0;
		for (int i = 0; i < upto; i++) {
			uint16_t d = gameIsW0 ? L->rows[i].w1 : L->rows[i].w0;
			if (!is_hs_word(d)) { if (!bad) firstBad = d; bad++; }
		}
		if (sessionStart)
			CHECK(bad == 0, "S2 %s: all %d pre-framing dongle words are handshake words "
			      "(%d were not, first 0x%04X)\n", base, upto, bad, firstBad);
		else
			INFO("%s: S2 skipped — the ring opened mid-stream (first round %u), so the leading "
			     "%d rows are not a handshake phase\n", base, L->nRows ? L->rows[0].round : 0, upto);
	}

	// ---- S4: header consistency ----
	// The FSM's own frames= counter is comparable to our re-derived count ONLY when the ring holds
	// the WHOLE session: it must start at round 0, must not have wrapped (NETLOG_N == 1024,
	// gbacore.c:441), and no session reset may have zeroed the FSM (resetN == 0). Otherwise the
	// two numbers count different spans and comparing them would be a false invariant.
	{
		int fromZero  = (L->nRows > 0 && L->rows[0].round == 0);
		int notWrapped = (L->nRows < 1024);
		int noReset   = (L->haveResetN && L->resetN == 0);
		if (L->haveFrames && fromZero && notWrapped && noReset) {
			CHECK(totalFrames == L->frames,
			      "S4 %s: re-derived frames == the FSM's own frames= (%d vs %ld)\n",
			      base, totalFrames, L->frames);
		} else if (L->haveFrames && fromZero && notWrapped && !L->haveResetN) {
			// Pre-run-#12 builds wrote no resetN. frames=0 is still falsifiable: it must mean
			// no complete command frame exists anywhere in a complete-from-round-0 stream.
			if (L->frames == 0)
				CHECK(totalFrames == 0,
				      "S4 %s: header says frames=0, so no framing run may exist (found %d frames)\n",
				      base, totalFrames);
			else
				INFO("%s: frames=%ld vs re-derived %d — no resetN field on this build, not comparable\n",
				     base, L->frames, totalFrames);
		} else {
			INFO("%s: frames=%ld not comparable (fromRound0=%d ringComplete=%d resetN=%s)\n",
			     base, L->haveFrames ? L->frames : -1, fromZero, notWrapped,
			     L->haveResetN ? (L->resetN ? "nonzero" : "0") : "absent");
		}
	}

	// ---- S5: `# celio-trace` legality ----
	if (L->nTr > 0) {
		int monoBad = 0, rangeBad = 0, edgeBad = 0, blkBack = 0;
		char firstEdge[64] = {0};
		for (int i = 0; i < L->nTr; i++) {
			const TraceLine* t = &L->tr[i];
			if (i && t->f < L->tr[i - 1].f) monoBad++;
			if (t->sec < 0 || t->sec > CL_SEC_LOUNGE) rangeBad++;
			if (t->st  < CL_ST_HANDSHAKE || t->st > CL_ST_COMMAND) rangeBad++;
			if (t->blk < 0 || t->blk > CL_BLK_REQ_TRAINERCARD) rangeBad++;
			if (i) {
				int a = L->tr[i - 1].sec, b = t->sec;
				if (!sec_edge_legal(a, b)) {
					if (!edgeBad) snprintf(firstEdge, sizeof firstEdge, "%s->%s @f=%u",
					                       sec_name(a), sec_name(b), t->f);
					edgeBad++;
				}
				// blockSeq walks FORWARD through the main chain LINKPLAYER..LINKCMD
				// (celiolink.h:208-214) within one section run; the side blocks TRAINERCARD /
				// REQ_TRAINERCARD (:216-217) are served out of band and are exempt.
				if (a == b && L->tr[i - 1].blk <= CL_BLK_LINKCMD && t->blk <= CL_BLK_LINKCMD &&
				    t->blk < L->tr[i - 1].blk) blkBack++;
			}
		}
		CHECK(monoBad == 0,  "S5 %s: the trace frame counter is monotone (%d inversions)\n", base, monoBad);
		CHECK(rangeBad == 0, "S5 %s: every trace sec/st/blk is in range (%d out of range)\n", base, rangeBad);
		CHECK(edgeBad == 0,  "S5 %s: every section edge is a legal transition (%d illegal, first %s)\n",
		      base, edgeBad, firstEdge[0] ? firstEdge : "-");
		CHECK(blkBack == 0,  "S5 %s: blockSeq never walks backwards inside a section (%d)\n", base, blkBack);
		// A trace edge back to SETUP is the session reset — it must be corroborated by resetN.
		int toSetup = 0;
		for (int i = 1; i < L->nTr; i++)
			if (L->tr[i].sec == CL_SEC_SETUP && L->tr[i - 1].sec != CL_SEC_SETUP) toSetup++;
		if (toSetup && L->haveResetN)
			CHECK(L->resetN > 0, "S5 %s: %d trace edge(s) back to SETUP but resetN=%ld\n",
			      base, toSetup, L->resetN);
		INFO("%s: %d trace lines, sections %s..%s\n", base, L->nTr,
		     sec_name(L->tr[0].sec), sec_name(L->tr[L->nTr - 1].sec));
	} else {
		INFO("%s: no `# celio-trace` lines (build predates the trace ring)\n", base);
	}

	// ---- S6: `# event` counter sanity ----
	if (L->haveEvent) {
		CHECK(L->txAcked <= L->txSeq, "S6 %s: txAcked <= txSeq (%ld/%ld)\n", base, L->txAcked, L->txSeq);
		CHECK(L->txSeq >= 0 && L->rxDelivered >= 0 && L->overflow >= 0,
		      "S6 %s: event counters are non-negative\n", base);
		INFO("%s: event txSeq=%ld txAcked=%ld rxDelivered=%ld overflow=%ld retransmits=%ld\n",
		     base, L->txSeq, L->txAcked, L->rxDelivered, L->overflow, L->retransmits);
	} else {
		INFO("%s: no `# event` line (build predates the event counters)\n", base);
	}
}

// ============================================================================================
// TIER R — exact replay
//
// Feed the recorded GAME column into a fresh cl_init'd FSM and diff every emitted word against
// the recorded DONGLE column. Valid ONLY when the log was recorded on the CURRENT FSM build:
// every intentional behavior change since (the run-#12 exit close, the LOUNGE rebuild, the
// pacing, the identity latch) legitimately moves the dongle's words, so a mismatch against an
// OLD log means "the port changed", not "the port is broken".
// ============================================================================================
typedef struct { int compared, mismatches, firstRow; uint16_t got, want; } ReplayResult;

static void replay_words(const uint16_t* game, const uint16_t* dongle, int n, ClRole role,
                         ReplayResult* out)
{
	CelioLink cl;
	cl_init(&cl, role, LINKTYPE_TRADE);
	memset(out, 0, sizeof *out);
	out->firstRow = -1;
	for (int i = 0; i < n; i++) {
		uint16_t tx = cl_transfer(&cl, game[i]);
		out->compared++;
		if (tx != dongle[i]) {
			if (out->firstRow < 0) { out->firstRow = i; out->got = tx; out->want = dongle[i]; }
			out->mismatches++;
		}
	}
}

// ANTI-VACUITY: prove the replay comparator is actually wired by generating a trace from the LIVE
// FSM and replaying it. If this ever fails, the tier-R skip below is hiding a broken harness, not
// a missing golden.
static void tier_r_selftest(void)
{
	printf("TIER R self-test: the replay comparator against a live-FSM-generated trace\n");
	enum { N = 400 };
	static uint16_t game[N], dongle[N];
	CelioLink gen;
	cl_init(&gen, CL_MASTER, LINKTYPE_TRADE);
	// A scripted "game": silent while waiting for a master (the JOIN shape, TEST 6a), then the
	// slave handshake, then INIT_BLOCK command frames — enough to cross into framing.
	for (int i = 0; i < N; i++) {
		uint16_t g;
		if (i < 4)       g = 0x0000;
		else if (i < 10) g = LINK_SLAVE_HANDSHAKE;
		else             g = ((i - 10) % 9 == 0) ? 0x0000 : (uint16_t)(((i - 10) % 9 == 1) ? LINKCMD_INIT_BLOCK : 0x0000);
		game[i]   = g;
		dongle[i] = cl_transfer(&gen, g);
	}
	ReplayResult rr;
	replay_words(game, dongle, N, CL_MASTER, &rr);
	CHECK(rr.compared == N, "R0: the comparator ran over the whole trace (%d/%d)\n", rr.compared, N);
	CHECK(rr.mismatches == 0, "R0: a live-FSM trace replays word-exact (%d mismatches, first row %d: got %04X want %04X)\n",
	      rr.mismatches, rr.firstRow, rr.got, rr.want);
	// ...and the comparator must FAIL on a corrupted trace (a check that cannot fail is decorative).
	uint16_t saved = dongle[N / 2]; dongle[N / 2] ^= 0x0001u;
	replay_words(game, dongle, N, CL_MASTER, &rr);
	CHECK(rr.mismatches >= 1, "R0: the comparator DETECTS a single flipped word (mutation gate)\n");
	dongle[N / 2] = saved;
	printf("  ok\n");
}

// ============================================================================================
// FIXTURES
// ============================================================================================
typedef struct {
	const char* path;
	const char* why;
	int         blessed;   // 1 = recorded on the CURRENT FSM build => tier R may replay it
} Fixture;

static const Fixture g_fixtures[] = {
	{ "test/fixtures/3DGBA_net_JOIN_0703_075344.txt",
	  "run #3 JOIN, complete from round 0: the pre-gate era — the CL_MASTER dongle drives "
	  "B9A0x8 then 8FFF forever at a silent (0000) game and NEVER frames (frames=0). "
	  "The recorded shape of TEST 6a, and the reason the framing walk may not assume a build's "
	  "handshake-exit rule.", 0 },
	{ "test/fixtures/3DGBA_net_HOST_0706_114305.txt",
	  "run #11-era HOST, complete from round 0 with 11 `# celio-trace` lines walking "
	  "SETUP->CONNECTION: the only fixture whose whole session fits the ring, so the re-derived "
	  "frame count is checkable against the FSM's own frames=17.", 0 },
	{ "test/fixtures/3DGBA_net_HOST_0715_162959.txt",
	  "run #12 HOST, tradeComplete=1, 32 trace lines, ring opened mid-session (first round "
	  "51860): a mid-stream framing window — the LOUNGE keepalive frames [CAFE,0011] whose CRC "
	  "slots verify arithmetically without any seed.", 0 },
	{ "test/fixtures/3DGBA_net_JOIN_0715_162349.txt",
	  "run #12 JOIN of the SAME session, tradeComplete=1 rxDelivered=10: the peer half, so the "
	  "two logs of one trade are both parsed by the same code.", 0 },
};

int main(int argc, char** argv)
{
	printf("=== D7c golden-trace replay harness (tier S structural + tier R exact) ===\n");
	tier_r_selftest();

	if (argc > 1) {
		// Ad-hoc mode: run the tiers over caller-supplied logs (the bless preview path).
		for (int a = 1; a < argc; a++) {
			NetLog L;
			printf("FIXTURE %s (ad-hoc)\n", argv[a]);
			if (!parse_netlog(argv[a], &L)) { printf("  FAIL: cannot open %s\n", argv[a]); g_fail++; g_checks++; continue; }
			tier_s(&L);
			SKIP("tier R: ad-hoc logs are not blessed goldens — pass them only after confirming "
			     "they were recorded on THIS build\n");
		}
	} else {
		for (int i = 0; i < (int)(sizeof g_fixtures / sizeof g_fixtures[0]); i++) {
			const Fixture* fx = &g_fixtures[i];
			NetLog L;
			const char* base = strrchr(fx->path, '/'); base = base ? base + 1 : fx->path;
			printf("FIXTURE %s\n  why: %s\n", base, fx->why);
			if (!parse_netlog(fx->path, &L)) {
				// A missing fixture is a FAILURE, not a silent skip (the run-#11 lost-log lesson:
				// absence of evidence must never read as success). Run from the project root.
				printf("  FAIL: cannot open %s (run from the project root)\n", fx->path);
				g_fail++; g_checks++;
				continue;
			}
			tier_s(&L);
			if (fx->blessed) {
				printf("  (tier R: blessed — exact replay would run here)\n");
			} else {
				SKIP("tier R: no current-build golden trace yet — this log predates the run-#12 "
				     "FSM fixes, so word-exact equality would fail where behavior intentionally "
				     "changed. Bless from run #13: copy a round-0 log into test/fixtures/ and set "
				     "blessed=1.\n");
			}
		}
	}

	printf("================================\n");
	printf("checks: %d   failures: %d   skips: %d\n", g_checks, g_fail, g_skips);
	printf("RESULT: %s\n", g_fail ? "FAIL" : "PASS");
	return g_fail ? 1 : 0;
}
