// diag.c — Phase 13-prep slices D1+D2: crumb storage, the watchdog escalation state machine,
// and the game-heartbeat hang catcher + register-dump formatter.
// Pure C (no libctru/mGBA — see diag.h header note); dual-compiled by test/host/test_diag.c.
// Models: melonDS-PM detached watchdog + numbered breadcrumbs (docs/kb/external/
// pm-bridge-forensics.md §13; port item 6) and the PM always-on hang catcher (§6/§12; port
// item 1), per docs/phase13-diagnostics/SPEC-firmware-diag.md.
#include <stdio.h>    // snprintf/vsnprintf only
#include <stdarg.h>   // the diag_hang_format append helper
#include "diag.h"

// The crumb words (declared extern in diag.h; site table + writer threads documented there).
// DIAG_CACHELINE: one 32-byte ARM11 L1 line EACH — they are written from three different cores and
// packing them together turned every telemetry store into a coherency transaction on the frozen
// SIO path (review fix 2026-08-03; the reasoning + the measured pre-fix addresses are in diag.h).
volatile uint32_t DIAG_CACHELINE g_diagNetCrumb  = 0;
volatile uint32_t DIAG_CACHELINE g_diagSioCrumb  = 0;
volatile uint32_t DIAG_CACHELINE g_diagMainCrumb = 0;
volatile uint32_t DIAG_CACHELINE g_diagRxSeq     = 0;

// One '\n'-terminated STUCK line (SPEC D1.5 — exact format, decimal everywhere; %lu + casts so
// the format is identical on devkitARM newlib [uint32_t = unsigned long] and the PC host).
// The trailing `who=` names the EPISODE (worker vs radio) — see diag.h's two-episode note.
int diag_stuck_format(char* out, size_t cap, uint32_t ms, const DiagWdSample* s) {
	if (!out || !cap || !s) return -1;
	return snprintf(out, cap,
	    "STUCK ms=%lu rseq=%lu aF=%lu bF=%lu aVf=%lu bVf=%lu rx=%lu netC=%lu sioC=%lu gateN=%d forceN=%d wl=%d seat=%d who=%s\n",
	    (unsigned long)ms, (unsigned long)s->renderSeq,
	    (unsigned long)s->aFrame, (unsigned long)s->bFrame,
	    (unsigned long)s->aVf, (unsigned long)s->bVf,
	    (unsigned long)s->rxSeq,
	    (unsigned long)s->netCrumb, (unsigned long)s->sioCrumb,
	    s->gateN, s->forceN, s->wl, s->seat,
	    s->who == DIAG_WD_WHO_RX ? "rx" : "worker");
}

// SPEC D1.4. Compares the WATCHED seqs against the last progress edge; any change (!=, so
// wraparound still reads as progress) resets the episode. All watched frozen -> escalate at
// >=1000/4000/12000 ms since the first frozen sample, each threshold ONCE per episode, checked
// ascending (one line per call — a >200ms sampler gap emits 1s, then 4s, then 12s on successive
// calls instead of skipping straight to the highest).
int diag_wd_step(DiagWd* wd, uint32_t nowMs, const DiagWdSample* s, uint32_t watchMask,
                 char* lineOut, size_t cap) {
	if (!wd || !s) return 0;
	if (watchMask == 0) {                       // not armed (D1.7): clear any episode, emit nothing
		wd->last = *s;
		wd->stuckSinceMs = 0;
		wd->fired = 0;
		return 0;
	}
	int progress =
	    ((watchMask & DIAG_WD_AF)  && s->aFrame != wd->last.aFrame) ||
	    ((watchMask & DIAG_WD_BF)  && s->bFrame != wd->last.bFrame) ||
	    ((watchMask & DIAG_WD_AVF) && s->aVf    != wd->last.aVf)    ||
	    ((watchMask & DIAG_WD_BVF) && s->bVf    != wd->last.bVf)    ||
	    ((watchMask & DIAG_WD_RX)  && s->rxSeq  != wd->last.rxSeq);
	if (progress) {
		wd->last = *s;                          // new progress edge; unwatched fields refresh too
		wd->stuckSinceMs = 0;
		wd->fired = 0;
		return 0;
	}
	if (wd->stuckSinceMs == 0) {                // first frozen sample: episode starts NOW (granularity
		wd->stuckSinceMs = nowMs ? nowMs : 1u;  // = one sampler tick, ~200ms; nowMs==0 nudged to 1 so
		return 0;                               // 0 keeps meaning "progressing")
	}
	uint32_t elapsed = nowMs - wd->stuckSinceMs;
	uint32_t ms = 0;
	if      (elapsed >= 1000u  && !(wd->fired & 1)) { wd->fired |= 1; ms = 1000u;  }
	else if (elapsed >= 4000u  && !(wd->fired & 2)) { wd->fired |= 2; ms = 4000u;  }
	else if (elapsed >= 12000u && !(wd->fired & 4)) { wd->fired |= 4; ms = 12000u; }
	if (!ms) return 0;
	if (lineOut && cap) diag_stuck_format(lineOut, cap, ms, s);
	return 1;
}

// ============================================================================================
// D2 — game-heartbeat hang catcher (SPEC D2.3) + register-dump formatter (SPEC D2.5).
// ============================================================================================

// Saturating freeze accounting for one tier: static counter -> frozenN += render-frame delta;
// advanced (or tier unarmed) -> reset + track. != (not <) so counter wraparound reads as progress
// (same rule as diag_wd_step).
static void hang_tier(uint16_t* frozenN, uint32_t* lastV, uint32_t v, int tierArmed, uint32_t delta) {
	if (tierArmed && v == *lastV) {
		uint32_t n = (uint32_t)*frozenN + delta;
		*frozenN = (n > 0xFFFFu) ? 0xFFFFu : (uint16_t)n;
	} else {
		*frozenN = 0;
		*lastV   = v;
	}
}

// SPEC D2.3. Freeze time is measured in RENDER FRAMES via the renderSeq delta (recorded design
// decision, BUILDLOG 2026-08-03 D2): the spec pins BOTH ">180 consecutive armed render frames"
// AND the 200 ms sampler hook — counting seq deltas keeps the 180-frame threshold literal at any
// call cadence, and the one-action-per-CALL return gives exactly the PM "one dump per ~200 ms
// sampler tick until 24" at the D1.6 hook.
DiagHangAction diag_hang_step(DiagHang* h, int armed, uint32_t renderSeq, uint32_t vf, uint32_t vbl) {
	if (!h) return DIAG_HANG_NONE;
	if (!armed) {                        // disarmed (D2.2: session tore down): clear the episode
		h->lastVf = vf; h->lastVbl = vbl; h->lastRseq = renderSeq;
		h->frozenVf = h->frozenVbl = 0;
		h->dumps = 0;
		h->init  = 0;
		return DIAG_HANG_NONE;
	}
	if (!h->init) {                      // first armed sample: baseline only, no freeze credit
		h->init = 1;
		h->lastVf = vf; h->lastVbl = vbl; h->lastRseq = renderSeq;
		h->frozenVf = h->frozenVbl = 0;
		return DIAG_HANG_NONE;
	}
	uint32_t delta = renderSeq - h->lastRseq;   // render frames since the last step (wrap-safe)
	h->lastRseq = renderSeq;
	if (delta > 0xFFFFu) delta = 0xFFFFu;       // clamp a pathological gap below the saturation cap
	hang_tier(&h->frozenVf,  &h->lastVf,  vf,  armed & DIAG_HANG_ARM_CORE, delta);
	hang_tier(&h->frozenVbl, &h->lastVbl, vbl, armed & DIAG_HANG_ARM_GAME, delta);
	// Tier A first (a parked core freezes BOTH counters — CORE is the truthful verdict there);
	// Tier B fires only when the core advances but the game's VBlank heartbeat is dead.
	DiagHangAction act = DIAG_HANG_NONE;
	if      ((armed & DIAG_HANG_ARM_CORE) && h->frozenVf  > DIAG_HANG_FROZEN_FRAMES) act = DIAG_HANG_DUMP_CORE;
	else if ((armed & DIAG_HANG_ARM_GAME) && h->frozenVbl > DIAG_HANG_FROZEN_FRAMES) act = DIAG_HANG_DUMP_GAME;
	if (act == DIAG_HANG_NONE) {
		h->dumps = 0;                    // re-arm (PM: "counter resets when FC moves again"):
		return DIAG_HANG_NONE;           // episode over -> a later freeze gets a fresh 24-cap
	}
	if (h->dumps >= DIAG_HANG_DUMP_CAP) return DIAG_HANG_NONE;   // capped: stay silent this episode
	h->dumps++;
	return act;
}

// Truncation-safe append: tot = the running would-be length; writes land at min(tot, cap-1) with
// the remaining space, so a truncated buffer stays NUL-terminated while tot keeps counting the
// full length (snprintf contract, same as diag_stuck_format's return).
static int hang_append(char* buf, size_t cap, int tot, const char* fmt, ...) {
	size_t off = ((size_t)tot < cap) ? (size_t)tot : cap - 1;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf + off, cap - off, fmt, ap);
	va_end(ap);
	return (n < 0) ? tot : tot + n;
}

// SPEC D2.5: one whole hang dump. %08lX words (+casts: uint32_t is unsigned long on devkitARM
// newlib, unsigned int on the PC host — the casts keep one format string correct on both).
// Header numbers decimal (crumbs decode via DIAG_CRUMB_SITE/ITER like the STUCK line).
int diag_hang_format(char* buf, size_t cap, const GbaCpuDump* d, uint32_t vf, uint32_t vbl,
                     int kind, uint32_t nowMs, uint32_t netCrumb, uint32_t sioCrumb) {
	if (!buf || !cap || !d) return -1;
	int t = 0;
	t = hang_append(buf, cap, t, "HANG kind=%s ms=%lu vf=%lu vbl=%lu netC=%lu sioC=%lu\n",
	                kind == DIAG_HANG_DUMP_GAME ? "GAME" : "CORE",
	                (unsigned long)nowMs, (unsigned long)vf, (unsigned long)vbl,
	                (unsigned long)netCrumb, (unsigned long)sioCrumb);
	t = hang_append(buf, cap, t, "R0  %08lX %08lX %08lX %08lX\n",
	                (unsigned long)d->gprs[0], (unsigned long)d->gprs[1],
	                (unsigned long)d->gprs[2], (unsigned long)d->gprs[3]);
	t = hang_append(buf, cap, t, "R4  %08lX %08lX %08lX %08lX\n",
	                (unsigned long)d->gprs[4], (unsigned long)d->gprs[5],
	                (unsigned long)d->gprs[6], (unsigned long)d->gprs[7]);
	t = hang_append(buf, cap, t, "R8  %08lX %08lX %08lX %08lX\n",
	                (unsigned long)d->gprs[8], (unsigned long)d->gprs[9],
	                (unsigned long)d->gprs[10], (unsigned long)d->gprs[11]);
	t = hang_append(buf, cap, t, "R12 %08lX SP %08lX LR %08lX PC %08lX\n",
	                (unsigned long)d->gprs[12], (unsigned long)d->gprs[13],
	                (unsigned long)d->gprs[14], (unsigned long)d->gprs[15]);
	t = hang_append(buf, cap, t, "CPSR %08lX SPSR %08lX\n",
	                (unsigned long)d->cpsr, (unsigned long)d->spsr);
	// Bank order matches enum RegisterBank (arm.h): NONE FIQ IRQ SVC ABT UND. Raw values —
	// offline analysis picks the live vs stale slots via CPSR's mode bits (diag.h CAVEAT).
	t = hang_append(buf, cap, t, "BANK r13: none=%08lX fiq=%08lX irq=%08lX svc=%08lX abt=%08lX und=%08lX\n",
	                (unsigned long)d->bankedR13[0], (unsigned long)d->bankedR13[1],
	                (unsigned long)d->bankedR13[2], (unsigned long)d->bankedR13[3],
	                (unsigned long)d->bankedR13[4], (unsigned long)d->bankedR13[5]);
	t = hang_append(buf, cap, t, "BANK r14: none=%08lX fiq=%08lX irq=%08lX svc=%08lX abt=%08lX und=%08lX\n",
	                (unsigned long)d->bankedR14[0], (unsigned long)d->bankedR14[1],
	                (unsigned long)d->bankedR14[2], (unsigned long)d->bankedR14[3],
	                (unsigned long)d->bankedR14[4], (unsigned long)d->bankedR14[5]);
	t = hang_append(buf, cap, t, "BANK spsr: none=%08lX fiq=%08lX irq=%08lX svc=%08lX abt=%08lX und=%08lX\n",
	                (unsigned long)d->bankedSPSR[0], (unsigned long)d->bankedSPSR[1],
	                (unsigned long)d->bankedSPSR[2], (unsigned long)d->bankedSPSR[3],
	                (unsigned long)d->bankedSPSR[4], (unsigned long)d->bankedSPSR[5]);
	t = hang_append(buf, cap, t, "IE %04X IF %04X IME %04X\n",
	                (unsigned)d->ie, (unsigned)d->if_, (unsigned)d->ime);
	t = hang_append(buf, cap, t, "STACK sp=%08lX valid=%d\n",
	                (unsigned long)d->sp, d->stackValid ? 1 : 0);
	if (d->stackValid) {
		for (int r = 0; r < 4; r++)      // 4 lines x 8 words: stack[0] = sp-32 ... stack[31] = sp+92
			t = hang_append(buf, cap, t, "  %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\n",
			                (unsigned long)d->stack[r*8 + 0], (unsigned long)d->stack[r*8 + 1],
			                (unsigned long)d->stack[r*8 + 2], (unsigned long)d->stack[r*8 + 3],
			                (unsigned long)d->stack[r*8 + 4], (unsigned long)d->stack[r*8 + 5],
			                (unsigned long)d->stack[r*8 + 6], (unsigned long)d->stack[r*8 + 7]);
	}
	return t;
}

// ============================================================================================
// D3 — per-frame CSV telemetry formatters (SPEC D3.3 / D3.6). Model: melonDS-PM per-frame CSV
// (pm-bridge-forensics.md §11) + DeSmuME-PM flush-256 (§3); the writer glue (open-once file,
// 256-row fflush cadence, arming) lives in main.c — these two functions are pure snprintf.
// ============================================================================================

// The exact SPEC D3.3 column list, one name per DiagCsvRow field, in struct order. Kept as ONE
// string so the header/row comma counts are host-test-comparable (test_diag TEST 6/7).
#define DIAG_CSV_COLUMNS \
	"tms,rf,exp," \
	"ctx,cb2,px,py,mapg,mapn,objx,objy,face,sb1,lstat,lerr,lnrecv,lbuf0,lbuf1,vbl," \
	"clSec,clSt,clBlk,clFrm,clPB,clTC,clHP,clHS,clHC,clSelL,clSelP,clExitP,clSessEnd,clPCard,clIdReal,clOutQ,gateN,cForceN,resetN,sioMode,siocnt," \
	"startN,injN,finN,okN,toN,edgeN,forceN,round,lastW0,lastW1,lastOk," \
	"rtt,txSeq,txAcked,rxDel,evOvf,evRetx,evTxQ,rxWordN,txFails,busyN,peerUp"

// SPEC D3.3 header: line 1 = build + role comment (mirrors the netlog header discipline,
// gbacore.c gbacore_net_log_dump) + the D3.4 disclosure that a GAP in rf marks menu-open frames
// (the gs-logger block is non-menu-branch only, so menu frames simply produce no row); line 2 =
// the column names. built= is the diag.c translation-unit build stamp (__DATE__/__TIME__ are
// TU-wide constants, so the host golden test can reproduce the exact bytes).
int diag_csv_header(char* buf, size_t cap, int seat) {
	if (!buf || !cap) return -1;
	return snprintf(buf, cap,
	    "# 3DGBA csv role=%s seat=%d built=%s | rf gaps = menu-open frames\n"
	    DIAG_CSV_COLUMNS "\n",
	    seat == 0 ? "HOST" : "JOIN", seat, __DATE__ " " __TIME__);
}

// One row (SPEC D3.6). ONE snprintf so a row is a single bounded format pass; %lu/%lX + casts keep
// the format identical on devkitARM newlib (uint32_t = unsigned long) and the PC host. HEX columns
// (no 0x, no zero-pad — the binding D3.3 "%X" rule): cb2, lstat, lnrecv, lbuf0, lbuf1, siocnt,
// lastW0, lastW1. Everything else decimal; -1 sentinels print as -1; booleans 0/1.
int diag_csv_row(char* buf, size_t cap, const DiagCsvRow* r) {
	if (!buf || !cap || !r) return -1;
	return snprintf(buf, cap,
	    "%lu,%lu,%d,"                                                        // tms,rf,exp
	    "%d,%lX,%d,%d,%d,%d,%d,%d,%d,%d,%lX,%d,%lX,%lX,%lX,%lu,"            // ctx..vbl
	    "%d,%d,%d,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lX," // clSec..siocnt
	    "%d,%d,%d,%d,%d,%d,%d,%lu,%lX,%lX,%d,"                              // startN..lastOk
	    "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",                               // rtt..peerUp
	    (unsigned long)r->tms, (unsigned long)r->rf, r->exp,
	    r->ctx, (unsigned long)r->cb2, r->px, r->py, r->mapg, r->mapn,
	    r->objx, r->objy, r->face, r->sb1,
	    (unsigned long)r->lstat, r->lerr, (unsigned long)r->lnrecv,
	    (unsigned long)r->lbuf0, (unsigned long)r->lbuf1, (unsigned long)r->vbl,
	    r->clSec, r->clSt, r->clBlk, (unsigned long)r->clFrm, (unsigned long)r->clPB,
	    r->clTC, r->clHP, r->clHS, r->clHC, r->clSelL, r->clSelP,
	    r->clExitP, r->clSessEnd, r->clPCard, r->clIdReal, r->clOutQ,
	    r->gateN, r->cForceN, r->resetN, r->sioMode, (unsigned long)r->siocnt,
	    r->startN, r->injN, r->finN, r->okN, r->toN, r->edgeN, r->forceN,
	    (unsigned long)r->round, (unsigned long)r->lastW0, (unsigned long)r->lastW1, r->lastOk,
	    r->rtt, r->txSeq, r->txAcked, r->rxDel, r->evOvf, r->evRetx, r->evTxQ,
	    r->rxWordN, r->txFails, r->busyN, r->peerUp);
}
