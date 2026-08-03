# Phase 13-prep — build log

Dated entry per slice (PHASE.md invariant 7). Newest first.

## 2026-08-03 — Slice D2: game-heartbeat hang catcher + auto ARM register dump (SPEC-firmware-diag §D2)

**Landed**
- `source/gamestate.{h,c}`: `GameProfile` += `vblankCtr` (gMain.vblankCounter1, the game's
  VBlank-ISR heartbeat — pret pokeemerald/pokefirered `src/main.c` `VBlankIntr():
  gMain.vblankCounter1++`). Values: BPEE **0x030022E0**, BPRE **0x03003110**, BPGE 0x03003110
  (FR-derived per the house rule). Derivation cited in both files: gMain base = the VERIFIED
  `mainCb2 - 4` anchor, offset 0x20 per pret `include/main.h` struct Main (callbacks 0x00-0x18,
  intrCheck u16 @0x1C +2 pad, vblankCounter1 @0x20), cross-checked by the hw-exercised
  newKeys = gMain+0x2E anchor (BPEE). All three marked **verify-on-hw-pending** (self-verifying
  once the D3 CSV shows the column ticking ~60/s).
- `source/diag.{h,c}`: `DIAG_D2_ENABLE` bisect gate; `DiagHangAction`/`DiagHang`/
  `diag_hang_step` (the freeze detector) with `DIAG_HANG_ARM_CORE/GAME` arming mask,
  `DIAG_HANG_FROZEN_FRAMES 180`, `DIAG_HANG_DUMP_CAP 24`; the pure `GbaCpuDump` mirror struct;
  `diag_hang_format` (the exact D2.5 dump text, truncation-safe would-be-length contract).
- `source/gbacore.{h,c}`: `gbacore_dump_cpu` — read-only ARM snapshot via
  `(struct ARMCore*)g->core->cpu` (mGBA headers ARE readable at `external/mgba/include`;
  verified against today's tree: `mCore.cpu` first member in core.h; `gprs[16]`,
  `cpsr/spsr.packed`, `bankedRegisters[6][7]` slot0=r13/slot1=r14, `bankedSPSRs[6]`, bank order
  NONE/FIQ/IRQ/SVC/ABT/UND in arm.h — so NO TODO-verify gate was needed); IE/IF/IME via the
  existing `gbacore_read16` bus reads (0x4000200/02/08); 32-word stack window at `(sp&~3)-32
  .. +92` guarded to EWRAM/IWRAM (`stackValid`). Race disclosure comment (sampled instant, not
  a stopped core). mGBA files untouched (MPL). `gbacore.h` now includes the pure `diag.h` for
  the `GbaCpuDump` type (recompiles touch.c/netlink.c — surfaced 5 PRE-EXISTING
  misleading-indentation warnings from those untouched files, incl. the netlink one already
  noted in the D1 entry; my files' warning sets are byte-identical to baseline).
- `source/main.c`: the D1 ~200 ms sampler block is now gated `#if DIAG_D1_ENABLE ||
  DIAG_D2_ENABLE` with each slice's body under its own gate; the D2 catcher runs on the same
  tick — armed ONLY under `wlOn` on the participant core (`g_netWorker`), Tier B armed only
  when the profile maps `vblankCtr`; on trigger, `gbacore_dump_cpu` + `diag_hang_format` append
  to the LAZY **`sdmc:/cias/netlogs/3DGBA_hang_<HOST|JOIN>_<MMDD>_<HHMMSS>.txt`** (S.2 helper,
  fflush per dump). Hang state + file share the wd lifecycle (`diag_wd_session_reset`/
  `diag_wd_close` — zero new call sites on the frozen teardown paths); same
  `diag_off.txt` kill switch.
- `test/host/test_diag.c`: TEST 4 (trigger edges: disarmed-never, 180-static-ticks→NONE /
  tick-181→DUMP_CORE, cap exactly 24, advance-resets-cap+frozenN then a fresh episode,
  vf-advancing+vbl-static→DUMP_GAME, unarmed-Tier-B placeholder never fires, sub-threshold
  alternation never fires + the independent-tier corollary at exactly call 181, the 200ms
  tick cadence with renderSeq+=12 firing on the 16th static tick = 192 frames, NULL safety)
  + TEST 5 (full golden-bytes dump compare, GAME/valid=0 branch ends at the STACK line,
  truncation returns the would-be length + never overflows, degenerate args refused) +
  `_Static_assert(sizeof(GbaCpuDump) == 288)` (D2.7 item 3).

**Gates** — celiolink **417 PASS** (unchanged), netlink **66 PASS** (unchanged), test_diag
**284 → 321 PASS**; `make` → `3DGBA.3dsx` green; per-file warning counts for main.c/gbacore.c/
gamestate.c/diag.c identical to the pre-slice baseline (15/0/0/0, all pre-existing).

**Decisions / deviations (recorded per the phase contract)**
1. **`diag_hang_step` gained a `renderSeq` parameter (+ `lastRseq`/`init` fields in
   `DiagHang`).** The spec pins BOTH a ">180 consecutive armed render frames" threshold and
   the 200 ms sampler hook (and its own D2.7 test speaks in per-tick units) — counting freeze
   time as the g_renderSeq DELTA keeps the 180-render-frame threshold literal at any call
   cadence, and the one-action-per-call return yields exactly the PM "one dump per ~200 ms
   sampler tick until 24" at the D1.6 hook. Host-tested at both cadences (TEST 4).
2. **`armed` is a 2-bit mask (`DIAG_HANG_ARM_CORE/GAME`), not a plain flag.** A game without a
   mapped `vblankCtr` (unknown ROM) feeds a constant placeholder; with Tier B blindly armed
   that would false-DUMP_GAME after the threshold. Tier B arms only when the profile maps the
   address (TEST 4 covers the placeholder case).
3. **Armed under `wlOn` only.** D2.2's "(and optionally netOn loopback, same flag)" deferred:
   under netOn BOTH cores participate and the single-participant state machine would need
   doubling — same call as the spec's own D3.7 loopback deferral. One-line change later.
4. **Hang file/episode lifecycle folded into the existing `diag_wd_close`/
   `diag_wd_session_reset`** instead of new teardown call sites — the wd helpers are already
   invoked at every wl teardown + session end, so the frozen paths gain zero new calls.
5. **D2.4's "if the headers genuinely aren't readable" fallback NOT taken** — the mGBA headers
   are in-tree and the struct paths were re-verified today (see above), so the full register
   dump shipped.
6. **Open Q3 (pret re-verification): shipped on the two-anchor derivation** (mainCb2-4 base +
   struct-Main offset table + the BPEE newKeys cross-check); the addresses stay
   verify-on-hw-pending until a run-#13 CSV/dump shows vbl ticking. NOTE while cross-checking:
   the BPRE `newKeys` literal in PROFILES (0x0303011E) looks like a digit transposition of
   gMain+0x2E = 0x0300311E (it IWRAM-mirrors to 0x0300011E, not gMain) — the field is UNUSED
   (touch injects via the returned key mask, gamestate.h comment), so per the house
   verify-on-hw rule it was flagged here, NOT silently changed. The FR vblankCtr derivation
   leans on the VERIFIED mainCb2 anchor (proven by the run-#12 cb2-fingerprint decodes), not
   on newKeys.
7. **Open Q2 (main-loop-alive wedge): accepted as spec'd** — vblankCounter1 misses a
   callback-spin with IRQs alive; that class is D3's (static cb2 + pinned celio section). No
   third counter added this slice.

**For the HW run-#13 checklist**: a `sdmc:/cias/netlogs/3DGBA_hang_*.txt` file only exists if
the participant froze ≥ ~3 s during a wireless session — `kind=CORE` = the emulated core
stopped producing frames (worker/driver wedge; read PC/LR against the pret sym maps),
`kind=GAME` = the core ran but the game's VBlank IRQ died (read the IE/IF/IME line). Up to 24
dumps per episode, ~200 ms apart — successive PCs tell spin from park. (The HANDOFF checklist
itself gains the new artifacts when D3 lands, per SPEC step 3.)

## 2026-08-03 — Slice D1: breadcrumbs + surviving-thread watchdog (SPEC-firmware-diag §D1 + S.1-S.4)

**Landed**
- `source/diag.{c,h}` (NEW, pure C, dual-compiles on the PC): crumb globals
  (`g_diagNetCrumb`/`g_diagSioCrumb`/`g_diagMainCrumb`), RX loop-seq `g_diagRxSeq`, the
  `DIAG_CRUMB` encode + `DIAG_CRUMB_SITE/ITER` decode macros, the site-ID table (D1.1),
  `DiagWd`/`DiagWdSample`/`diag_wd_step` escalation machine (1s/4s/12s, once per episode,
  ascending one-line-per-call), and the exact `diag_stuck_format` STUCK line (D1.5).
- `source/netlink.c`: stamps at sites 1000 (`net_transfer_collect`), 1100 (`net_cmd_collect`),
  1200 (`net_round_wait`) — each inside the wait path only, one volatile store per pass;
  1300 + `g_diagRxSeq++` per `net_rx_thread` outer pass; 1400 in the lobby drain
  (`net_ping_update`); site-4200 bracket around `net_link_stop`'s `threadJoin`.
- `source/gbacore.c`: stamps at sites 2000 (celio link-ready gate, iter=`gateHeldN`),
  2100 (celio ISR-edge wait — the run-#4 freeze site, iter=`elapsed>>10`), 2200 (celio pacing
  gate), 2300 (A–E ISR-proof gate), 2400 (one-round-in-flight gate), and the 3000+round
  entry/exit bracket in `net_finishMulti`. NEW minimal export `gbacore_net_wd_counters(gateN,
  forceN)` (declared in `gbacore.h`) for the STUCK line's celio quick-reads.
- `source/main.c`: `g_renderSeq` (per-frame render loop-seq), the ~200 ms watchdog sampler
  BEFORE the `menuOpen` split (D1.6), D1.7 arming/watchMask (wlOn → participant worker + RX;
  linkOn/netOn → both workers), the S.2 path helper `diag_log_path`
  (`3DGBA_wd_<ROLE>_<MMDD>_<HHMMSS>.txt`, lazy-created at the FIRST STUCK line, fflush per
  line, closed at every wl teardown site + session end), the S.3 kill switch
  (`sdmc:/cias/control/diag_off.txt`, checked at run_session entry + the `wlOn = true` site),
  and site-4000/4100 brackets on the pause/pipeline + wl-teardown waits.
- `test/host/test_diag.c` (NEW; run line in its header:
  `clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c -o /tmp/td && /tmp/td`):
  crumb encode/decode for every D1.1 site, watchdog healthy/freeze×3/reset/re-freeze/
  sampler-gap/watchMask/mask==0 scenarios, STUCK golden bytes + truncation safety.
- `test/host/test_netlink_reliability.c`: now includes `source/diag.c` before `netlink.c`
  (the stamps reference the crumb globals); same run line, same 66 checks.

**Gates** — celiolink suite **417 PASS** (unchanged), netlink suite **66 PASS** (unchanged),
NEW test_diag **284 PASS**; `make` → `3DGBA.3dsx` green, and the warning set of
main.c/netlink.c/gbacore.c is byte-identical to the pre-slice baseline (verified by stashing
the slice, rebuilding, and diffing the normalized warning lists — all 16 are pre-existing
UI-redesign leftovers + one old netlink misleading-indentation).

**Decisions / deviations (recorded per the phase contract)**
1. **Crumb iteration mask 0x3F, not the spec's 0xFF.** The D1.1 site bases are spaced 100
   apart (they are load-bearing across the spec/D3 docs, so they stay), and a 0xFF residue
   aliases neighbouring sites (1000+150 == 1100+50; likewise 2000+200.. vs 2200) — the
   spec's own "spaced ≥ 256" premise doesn't hold for its table. 0x3F keeps every crumb
   uniquely decodable: `site = crumb - crumb%100`, `iter = crumb%100`. Host-tested for every
   site (test_diag TEST 1).
2. **celiolink.c: zero changes.** The task charter mentioned celiolink stamps via a
   callback/extern; the binding SPEC's D1.1 inventory verifies celiolink has NO blocking
   loops (`cl_transfer` O(1), all loops bounded) and mandates "no crumbs and no code change"
   — its logical holds are already exported via `ClStatus` and become D3 CSV columns.
   Followed the SPEC.
3. **STUCK gateN/forceN** (the D1.5 ordering note): neither "pull the full D3
   `gbacore_net_counters` forward" (that drags D3.2's `net_celio_capture` extension into
   this slice and would collide with the D3 implementer) nor "emit zeros" — added the
   minimal read-only `gbacore_net_wd_counters(gateN, forceN)` (pure copies of
   `s_celioGateN`/`s_celioForceN`, modeled on `gbacore_net_diag`). D3 may fold it into the
   full export.
4. **Third crumb word `g_diagMainCrumb` (sites 4000-4299).** D1.2 declared only net/sio
   crumbs but D1.1 requires sites 12-14 (render-thread blockers) to be "stamped so a
   post-mortem names them". A separate word keeps render-thread brackets from clobbering the
   worker/RX crumbs. It is NOT in the STUCK line (D1.5's format is binding, and those sites
   can't be sampled anyway — D1.8); it is bracket-style (0 = not parked) for a debugger /
   future detached sampler. Site 14 lives in netlink.c code but runs on the render thread,
   so it stamps the main crumb.
5. **DiagWdSample gained `wl`/`seat`.** The spec's struct lacked them but its line format
   requires them; pass-through fields, host-tested.
6. **Kill switch checked at run_session entry AND the `wlOn = true` site.** S.3 names only
   the wl site, but D1.7 arms the watchdog under linkOn/netOn too — the session-entry check
   covers those; both are one-time `stat()` calls, never per-frame.
7. **`DIAG_D1_ENABLE` bisect gate**: when 0, `DIAG_CRUMB` compiles to argument-evaluation
   only (no store — keeps e.g. `wdIt++` semantics and avoids unused warnings) and the main.c
   sampler block compiles out. `g_diagRxSeq++`/`g_renderSeq++` and the bracket clears remain
   (inert single stores).
8. **Open Questions**: #1 (watchdog blind spot) — kept the render-thread sampler per
   PHASE.md scope; the teardown-wait class is already fixed (memory: close-hang RESOLVED)
   and sites 12-14 get post-mortem brackets. #7 (wd file lifecycle) — kept lazy-create as
   spec'd (healthy run writes no file). The rest (#2-#6) belong to D2/D3.

**For the HW run-#13 checklist**: if a freeze happens, `sdmc:/cias/netlogs/3DGBA_wd_*.txt`
now names the parked loop — decode `netC`/`sioC` with `site = crumb - crumb%100`. No file =
the watchdog saw no ≥1 s freeze. (SPEC step 3 says the HANDOFF checklist gains the new
artifacts when D3 lands; noting the wd file here so an earlier run doesn't discard it.)

## 2026-08-03 — Slice D3: PARTIAL — session stopped mid-slice (model switch)

The D3 agent was killed ~10 min in. What exists in the tree: `diag_csv_header()` /
`diag_csv_row()` formatters in `source/diag.c` (+ their decls/struct). Wiring state
(celiolink/netlink counter exports, gamestate writer, main.c hookup) NOT complete —
the resuming D3 implementer must diff the tree against SPEC-firmware-diag §D3 and
finish, not restart. Tree verified green at checkpoint: celiolink 417 PASS,
netlink 66 PASS, test_diag 321 PASS, `make` → 3DGBA.3dsx clean.
