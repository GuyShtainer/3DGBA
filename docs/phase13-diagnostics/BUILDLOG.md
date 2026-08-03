# Phase 13-prep — build log

Dated entry per slice (PHASE.md invariant 7). Newest first.

## 2026-08-03 — FINAL GATE (PHASE.md invariant 8): all suites + both build targets + HANDOFF

The phase's exit gate, run from a clean shell (`export DEVKITPRO=/opt/devkitpro
DEVKITARM=$DEVKITPRO/devkitARM`) on the post-fix-pass tree. Nothing was edited in `source/` by this
step — it is a verification pass plus the two docs the gate owns (this entry + `docs/HANDOFF.md`).

**Suites — all five, all PASS, every count ≥ the fix pass's:**

| Suite | Compile line (from the file's own header) | Result |
|---|---|---|
| celiolink | `clang -std=c11 -Wall -Wextra -O0 -g -I source test/test_celiolink.c` | **1259 checks, 0 failures** |
| netlink reliability | `… -O2 -I test/host -I source test/host/test_netlink_reliability.c` | **66 checks, 0 failures** |
| diag (D1/D2/D3) | `… -O2 -I source test/host/test_diag.c` | **369 checks, 0 failures** |
| control (D4/D5) | `… -O0 -g -I source test/host/test_control.c` | **6897 checks, 0 failures** |
| trace replay (D7c) | `… -O2 -I source test/host/test_trace_replay.c` | **58 checks, 0 failures, 4 loud SKIPs** |

Total **8649 checks**. The 4 skips are the deliberate tier-R "no blessed golden yet" SKIPs on the
four pre-run-#12 fixtures (they are loud by design and become real replays when run #13 blesses one).

**Builds — both targets, from `make clean`:**

- `make -j8` → **`3DGBA.3dsx`** (3,778,184 B). Full from-scratch compile, 19 s.
- `make cia` → **`3DGBA.cia`** (1,907,648 B) + `banner.bnr`, exit 0. `makerom`/`bannertool` were
  auto-discovered from the toolkit's `tools/bin/` with no PATH help, exactly as CLAUDE.md documents;
  **no cia failure to report.**

**Warning discipline, verified mechanically on the clean build** (`grep -c warning:` over the whole
log, then the non-`external/mgba` subset): 35 total, of which **every one is pre-existing** —
15 in `main.c`, 1 in `netlink.c`, 1 in `celiolink.c` (the known `cl_partner_party`), plus
`rompicker.c`/`touch.c`/`theme.c`/`ui.c`/`wireless.c` and the LTO serial-compile note. The four
files this phase CREATED — `diag.c`, `control.c`, `fingerprint.c` and their headers — emit
**zero warnings** under the devkitARM build and zero under `-Wall -Wextra` on the host.

**Slice coverage check (invariant 7):** all seven scoped systems have a dated entry above —
D1, D2, D3 (partial + RESUMED/COMPLETE), D4, D5, D6, D7 (partial + the D7b-e completion entry,
whose "Correction to the previous entry" section is where D7a's landing is recorded, verified by
reading the code rather than trusting the killed session's note). Plus the fix-pass entry.

**HANDOFF updated** (`docs/HANDOFF.md`): Current status gained a diagnostics-phase paragraph
(what shipped, the five suite counts, everything flag-gated/additive, trade path untouched);
Next steps #2 (the run-#13 checklist) gained the setup line (wipe `sdmc:/cias/netlogs/` **and**
`mkdir sdmc:/cias/control`), **both example move scripts inlined verbatim** from
`SPEC-control-replay.md` Appendix, the collect-afterwards artifact list, and the `tools/verdict.sh
<folder>` grading step; the session log gained a dated entry. Header date → 2026-08-03.

**Nothing here is "done" (invariant 8):** PC-green + build-green is this phase's exit gate only.
Every one of D1-D7 is hardware-unproven until run #13 exercises it.

## 2026-08-03 — FIX PASS on the adversarial reviews (diff vs `a8764c0`)

Twelve findings arrived (1 blocker, 5 major, 6 minor; two pairs were the same defect reported
twice). **Every one was verified against the code before being touched — all twelve are real**;
none was rejected. Outcome per finding below, then the deviations they forced.

**Files changed:** `source/diag.h`, `source/diag.c`, `source/main.c`, `source/netlink.c`,
`source/netlink.h`, `source/gbacore.c`, `test/host/test_diag.c`, `tools/verdict.sh`.

### Findings and outcomes

1. **BLOCKER — `main.c:1772` D1 watchdog blind under `wlOn`** (also reported as a major).
   CONFIRMED by reading `diag.c:45-50` (progress = OR across every watched seq) against
   `netlink.c:921` + `:987` (`g_diagRxSeq++` per RX pass, 0.5 ms sleep ⇒ ~2 kHz on the other
   core): ORing `DIAG_WD_RX` into the worker mask meant the radio reset the episode ~400× between
   two 200 ms samples, so a wedged worker could never escalate — and symmetrically a live worker
   masked an RX wedge, so *neither* class D1.3 names was detectable. **FIXED with two independent
   episodes**: `s_wd` (participant worker: `AF|AVF` or `BF|BVF`, RX removed) and the new `s_wdRx`
   (`DIAG_WD_RX` alone, armed only under `wlOn`), each with its own escalation clock, both stepped
   every tick and both writing the same wd file. `DiagWdSample.who` + the line's new `who=` token
   say which episode spoke. New host test **TEST 2b** pins all four cases, including the reviewer's
   exact scenario (a WATCHED seq frozen while another WATCHED seq advances — the case the old
   suite never covered, which is why it stayed green through the bug).
2. **MAJOR — `diag.c:12` all four crumb globals share one 32-byte cache line.** CONFIRMED with
   `arm-none-eabi-nm` on the pre-fix ELF: `0049b628/62c/630/634`, i.e. 16 contiguous bytes inside
   the line at `0x49b620` (ARM11 L1 line = 32 B), written from three different cores with the
   hottest writer on the frozen SIO poll path. **FIXED** with `DIAG_CACHELINE`
   (`__attribute__((aligned(32)))`) on each of the four; post-fix nm: `0049b640 / 660 / 680 / 6a0`
   — one line each, so a store no longer invalidates any other crumb's line in another core.
   *Partial by choice:* the reviewer also suggested deleting the site-2200 PACE crumb. **Kept**,
   with reason: once each word owns its line the store is a private L1 hit (~1 cycle) and carries
   no coherency traffic, and the site is not signal-free — if the emulated clock stops advancing
   the pacing gate is exactly where the driver last was, and `sioC=2200` names it.
3. **MAJOR — `main.c:2127` D3 CSV unbounded.** CONFIRMED: `s_csvFile` is opened for every wireless
   link unless `diag_off.txt` exists, one row per non-menu render frame, no cap anywhere (SPEC D3.5
   specifies open-once + flush-256 and no bound — a design gap, not a deviation). **FIXED**:
   `DIAG_CSV_MAX_ROWS` (108000 ≈ 30 min at 60 fps ≈ 24 MB); on reaching it the writer appends one
   `# csv capped …` line, closes the file and goes dormant (`s_csvFile == NULL` is already the
   dormant state everywhere else). The reviewer's second point — that the 8 KB `setvbuf` means the
   real FS write happens every ~36 rows, not every 256 — was also correct; **the misleading comment
   is fixed** (flush-256 now documented as bounding what a hard crash loses, not the write cadence).
4. **MAJOR/minor (same defect, twice) — `netlink.c:922` site-1300 crumb starves `g_diagNetCrumb`.**
   CONFIRMED: the RX pass stamped it unconditionally at ~2 kHz while the parked-worker sites stamp
   at ~1 kHz, so the sampled word named the HEALTHY thread ~2 samples out of 3 and sites
   1000/1100/1200/1400 — the entire reason the word exists — were unreadable. **FIXED by deleting
   the stamp**: the RX heartbeat is already carried losslessly by `g_diagRxSeq`, which the watchdog
   samples separately, and the crumb's iteration added nothing. Site ID 1300 stays allocated
   (decode tables, TEST 1) and now documents why it is deliberately never stamped.
5. **MINOR (twice) — `main.c:2117` per-frame `s_evLock` acquisitions on the render thread.**
   CONFIRMED: `net_event_get_stats` + `net_event_get_queue` each take `s_evLock` (`netlink.c:749`/
   `:770`), and the RX thread holds that lock across `udsSendTo` bursts (ACK under lock,
   `netlink.c:927-932`; whole un-ACKed-tail re-send every ~16 ms, `:972-982`) — a new coupling
   between the render loop and the frozen event plane, and a third contender for the worker.
   **FIXED**: new `net_event_get_stats_fast()` returns all six columns in ONE lock-free call
   (`__atomic_load_n` relaxed reads of aligned u32s — the `gbacore_net_counters` benign-race
   pattern the CSV already uses for every other counter column). The render thread now takes no
   transport lock at all; the locked getters remain for the once-per-session netlog dump.
6. **MINOR — `netlink.c:371` D6 send failures folded into `s_pingSendFails` + doubled lobby TX.**
   CONFIRMED on both counts (same counter as the ping's own failure path and the one
   `net_ping_update` returns as `sendFails`; and the extra send was issued on the same tick with
   the same dst/flags every period until the peer's surface arrived). **FIXED**: dedicated
   `s_fprintSendFails` (reset in `net_session_close`, exposed as `net_fprint_fails()` and printed
   as `txFails=` on the netlog's `# fprint` line), and the fingerprint moved to its own tick half a
   period off the ping — same total rate, no doubled instantaneous pressure.
7. **MAJOR — `tools/verdict.sh:266` fails every legitimate EM↔FR run.** CONFIRMED against
   `fingerprint.h:17-20/41-46` (no ROM CRC in the surface *by design*; `gameCode`/`gameRev` diffs
   are explicitly never refuse-grade) and the allow-with-warning implementations in
   `wireless.c:145-162` / `gbacore.c:1007-1012` — and the user's own run-#13 setup is Emerald ↔
   FireRed rev1, so the tool would have scored the successful trade a FAIL and exited 1. **FIXED**:
   FAIL now keys on the refuse-grade fields only (`clProtoRev|netProto|modeFlags`); a game-side-only
   DIFF is PASS with the reason spelled out. Smoke-tested both ways with synthetic `# fprint` lines
   (EM↔FR ⇒ PASS, exit 0; `clProtoRev(1!=2)` ⇒ FAIL, exit 1).
8. **MINOR — `CTL_D4_ENABLE 0` does not build.** CONFIRMED (the D5 glue was defined inside the D4
   `#if` but called from D5-only blocks). **FIXED properly rather than by documenting a dependency**:
   `ctl_path` moved to `#if CTL_D4_ENABLE || CTL_D5_ENABLE`, the D5 glue block lifted to its own
   top-level `#if CTL_D5_ENABLE`, the arming stat and the shared per-frame tick gated on the OR of
   the two, and the D4-only bits (`ctl_poll_seat`/`ctl_tick`) gated inside it. **All five bisect
   gates now build**: verified `CTL_D4_ENABLE=0`, `CTL_D5_ENABLE=0`, both off, `DIAG_D1=0`,
   `DIAG_D2=0`, `DIAG_D3=0` — each produces `3DGBA.3dsx`.

### Deviations forced by the fixes (recorded per PHASE.md invariant 1/7)

- **SPEC D1.7 mask composition** — the spec pins ONE `wlOn` mask of "participant worker + rxSeq".
  That composition is the blocker; it is replaced by two independent episodes. The spec text is now
  wrong on this point and `diag.h` says so at the definition.
- **SPEC D1.5 STUCK line** — one field APPENDED at the end (`who=worker|rx`). Required: both
  episodes append to the same file, so without it the artifact cannot name the frozen subsystem.
  Appended last, so every prefix-parse (and `verdict.sh`, which only counts wd files) is unaffected.
  Golden updated in `test_diag` TEST 3.
- **SPEC D3.5** — gains a row cap it never specified (finding 3). Passive-logging invariant is
  unchanged: still open-once, buffered, no per-frame heap.
- **Trade path** — untouched. Every edit is a delete of a store, an alignment attribute, a
  lock-free read replacing a locked one, a counter split, preprocessor structure, or render-thread
  bookkeeping. No FSM transition, pacing constant, round/ack rule or SIO fill was modified.

### Gate

Suites (all PASS, all ≥ their previous counts): celiolink **1259**, netlink **66**, diag **369**
(was 360 — TEST 2b adds 9), control **6897**, trace_replay **58** (4 skips, unchanged).
`make -j8` → `3DGBA.3dsx` built. Warning discipline verified mechanically: the warning set for
`main.c`/`netlink.c`/`diag.c`/`gbacore.c` was captured before (via `git stash` of just these files)
and after — **identical, 16 warnings, all pre-existing**; zero new warnings in the edited files.
`make cia` still not run (the phase's final gate owns it).

## 2026-08-03 — Slice D7b-e (COMPLETE): mirror asserts, replay harness, verdict tool, ledger

Completes D7 (SPEC-suite-hardening §D7a-e). **D7a and, as it turns out, most of D7b were already
in the tree** from the killed session — see "Correction to the previous entry" below.

**Landed**

- **D7b (completed)** — `test/test_celiolink.c`. The per-pump mirror machinery (`Pair`,
  `mirror_sample`, `section_pair_legal`, the violation accumulators, the byte-exact PARTY_CHUNK
  check in `lag_deliver`) was ALREADY present and correct; what was missing were the two
  **convergence-point** rows of the SPEC §D7b.2 mirror table:
  - `tradeComplete` **converges equal on both sides** (a one-sided complete is the "one console
    kept my Pokémon" failure) + **all three holds clear** at convergence, per lag combo;
  - both sides hold the peer's **FULL 600-byte party** — sampled **before the close**, because the
    post-trade re-arm deliberately zeroes `partnerPartyBytes` (`source/celiolink.c:854`); asserting
    it after the close was a false invariant and failed 12× on the first run (fixed by moving the
    sample, not by weakening the check).
  - **Not implemented, deliberately (SPEC §D7b.1):** cross-instance CRC equality. Under local
    termination the two consoles run different game↔dongle conversations by design, so equal CRCs
    is a FALSE invariant; per-instance CRC correctness is already asserted every frame by
    `CrcTracker`. This is the charter's "per-round hash agreement" resolved against the source.
- **TEST 12L (NEW)** — the one genuine D7a gap: SPEC §D7a.3 mandates a lagged TEST-12 variant and
  the tree had none. Sweeps the peer's `CL_EV_PARTY_CHUNK` through the lag shim at k=0..4 pumps
  while driving a **real INIT_BLOCK evaluation every single frame**, and asserts the PARTY0 hold
  stands for exactly the in-flight frames and releases on the frame the chunk lands. Measured:
  in-flight frames 0/0/1/2/3 for k=0..4, release delay **0 frames** every time — so the release
  bound is pinned TIGHT at ≤2. This is the run-#8 edge-vs-level defect class at its smallest scale
  (an edge-triggered hold would consume its only edges while the event was still in the air).
- **D7c (NEW)** — `test/host/test_trace_replay.c` (+ `test/fixtures/`, 4 real hardware netlogs,
  names kept). **Tier S (structural, build-independent, always runs):** parse/self-description
  (role↔seat, CSV shape, all-state-F rows, monotone rounds) · pre-framing dongle words ∈
  {B9A0,8FFF,D15E} · **framing + CRC arithmetic** re-derived from the recorded columns · header
  consistency · `# celio-trace` legality (monotone f, sec/st/blk range, legal section edges,
  blockSeq never walks backwards, a SETUP edge must be corroborated by `resetN`) · `# event`
  counter sanity. **Tier R (exact replay): SKIPS loudly on all four fixtures** — every one predates
  the run-#12 FSM fixes, so word-exact equality would legitimately fail where behavior intentionally
  changed. It is **not** dead code: `tier_r_selftest()` generates a trace from the LIVE FSM, replays
  it through the same comparator (0 mismatches), then flips one word and requires the comparator to
  catch it. Bless = drop a run-#13 round-0 log into `test/fixtures/` and set `blessed=1`.
  - **Framing walk design (the honest part).** The handshake-exit rule is BUILD-DEPENDENT — the
    0703 fixture's dongle drove 0x8FFF forever and never framed — so the walk never assumes where
    framing starts. It **searches for an alignment the arithmetic itself validates** (≥2 CRC slots
    verified, zero mismatches; two chained 16-bit sums agreeing by chance is ~2⁻³²), allows an
    unverifiable opening slot for a mid-ring window, and requires every framing break to be
    explained by a re-handshake run or end-of-log.
  - **Result on the corpus:** JOIN_0703 = no framing run ⇔ header `frames=0` ✓; HOST_0706_114305 =
    17 frames, 18 CRC slots, **equals the FSM's own `frames=17`** ✓; both 0715 logs = a mid-stream
    window of 113 frames with **113 CRC slots verified and zero mismatches** across 1018/1024 rows.
  - **Deviation (in the harness's favour):** SPEC §D7c.2 tiered the two 0715 fixtures as "header +
    trace-legality (ring not replayable)". They ARE framing-checkable — the LOUNGE keepalive frames
    `[CAFE,0011]` verify arithmetically without any seed — so the harness checks them too. Verified
    self-falsifying: flipping ONE word in a copy of HOST_0706 produces `1 unexplained break` + a
    frames mismatch (16 vs 17) and exits 1.
- **D7d (NEW)** — `tools/verdict.sh <run-folder>`: one line per run-#13 checklist item (13a trade,
  13b card, 13c room-exit, 13d re-link) plus health lines (event-channel overflow, wedge-escape
  `forceN`, the GAME's own `lerr`/red-screen `cb2` from the gs log, the D6 link-surface verdict, and
  the mere existence of a D1 watchdog / D2 hang dump). POSIX sh + grep/awk, macOS-native, **never
  writes**. Every pattern was read out of the actual writer (`gbacore.c:936/977/1003/1027/1031`,
  `gamestate.c:320-321`, `main.c:106-138`) — **note the SPEC's line cites had drifted by ~95 lines**
  from the D1-D6 work; the tool and the harness both cite the CURRENT lines. gs columns are located
  **by name** from the CSV header so a future column cannot shift the check.
  - **UNKNOWN discipline, mechanised:** a missing log, an absent field, or a feature that postdates
    the log's build ⇒ **UNKNOWN, never PASS** (the run-#11 lost-log lesson). 13b never auto-passes
    the "card renders" half — it prints `needs-eyes`. 13d cannot distinguish "not attempted" from
    "failed" and says so (SPEC Open Question 6 — answered in the tool's own output rather than by
    adding a `# session` counter to the frozen-adjacent TU).
  - **Smoke tests:** `netlogs/` (the mixed run-#9..#12 folder) ⇒ 13a PASS, 4 UNKNOWNs where the
    fields postdate those builds, exit 0. An EMPTY folder ⇒ 8 UNKNOWN, exit 0. A synthetic broken
    run ⇒ 6 FAIL, exit 1.
- **D7e (NEW)** — `docs/kb/celio/KNOWN-DIFFERENCES.md`: the tri-ledger's middle file, seeded with
  the SPEC's 10 entries, each with a Celio/pret cite, OUR file:line, the why, and a status ∈
  {hw-validated (run #N) / PC-only (TEST N) / unmeasured}. **All line cites re-verified against the
  current tree** (the SPEC's had drifted). Entry 6 (room-exit close) and entry 10 (trainer card) are
  the two `PC-only — awaiting HW run #13` rows; entry 7 (post-handshake CRC word timing) is the one
  honest `unmeasured`. Header states the coupling rule (wire-visible change ⇒ ledger entry +
  `CL_PROTO_REV` bump + re-pinned golden, one commit) and the ratchet (entries are never deleted).
- `.gitignore`: `netlogs/` + `3DGBA_net_*.txt` were silently swallowing the new fixtures (verified
  with `git check-ignore`); added a negation for `test/fixtures/3DGBA_net_*.txt` only. The fixtures
  are recorded link words + counters — **user gameplay data, no ROM code**; flagged here for the
  `release-legal-audit` gate, same class as the netlogs the repo already carried.
- Removed three now-dead test helpers (`relay_events`, `relay_until_event`, `supply_peer_party`) —
  the D7a lag shim superseded them and they were emitting `-Wunused-function` under the suite's
  `-Wall -Wextra`. The suite now compiles with **only** the one pre-existing `celiolink.c`
  (`cl_partner_party`) warning.

**Suites** (all PASS): celiolink **1259** (was 1171: +40 from TEST 12L, +6 convergence-mirror
checks per the 8 TEST-5 lag combos) · netlink **66** (byte-untouched) · diag **360** · control
**6897** · **NEW** trace-replay **58 checks, 0 failures, 4 loud SKIPs**.

**Build:** `make -j8` → `3DGBA.3dsx` (20:56). **`source/` is byte-untouched by this slice** —
`git status` shows only `.gitignore`, `test/`, `tools/`, `docs/`. Acceptance gate 5 ("git diff
source/celiolink.c is EMPTY") holds trivially: the whole frozen path is untouched.

**Correction to the previous entry.** The killed session's BUILDLOG note said "D7b — TODO". That
was wrong: the D7a agent landed the mirror machinery together with the shim (the code is labelled
`D7a/D7b`) and died before writing its entry. Verified by reading the code, not by trusting the
note — and D7a itself was re-read against SPEC §D7a and found complete except for the TEST-12
variant, which is now TEST 12L. Nothing was re-done.

**Open questions resolved by this slice** (SPEC Open Questions): **#4** (tier-R bless mechanics) —
the harness takes any log path as argv and the fixture table has a `blessed` flag, so blessing is a
copy + a one-line edit; raising `NETLOG_N` is NOT needed for a short run-#13 session (HOST_0706
proves a whole 412-round session fits the 1024-entry ring). **#6** (13d ambiguity) — answered in the
tool's output rather than by touching the frozen-adjacent TU. **#7** (paired event logs) stays
deferred: tier R's "never replayable" list names it explicitly.

**Not done here** (belongs to later steps): the two adversarial reviews, the fix pass, `make cia`,
and the full HANDOFF rewrite. One spec-mandated HANDOFF line WAS added (§D7e.3: run `verdict.sh` on
run #13's folder and copy its PASS lines into the ledger's status column).

## 2026-08-03 — Slice D6: link-surface fingerprint at connect (SPEC-suite-hardening §D6)

**Landed**
- `source/fingerprint.{h,c}` (NEW, PURE C — `<stdint.h>`/`<string.h>`/`<stdio.h>` only, no libctru /
  no mGBA / no celiolink, dual-compiled by two host suites): the 8-byte packed `DgbaFprint` surface
  (`gameCode[4]`, `gameRev`, `clProtoRev`, `netProto`, `modeFlags`) whose **struct layout IS the wire
  format**, `dgba_fprint_fill`, the **two-lane FNV-1a** (`dgba_fprint_lane` A/B on different offset
  bases → `dgba_fprint_hash` fold, gen1's construction, Fingerprint.lua:21-63), `dgba_fprint_diff`
  (names the differing fields — the modDiff lesson, Handshake.lua:215-236 — comma-separated so a
  netlog line stays one greppable token), and `dgba_fprint_format` (the `# fprint` line, incl. the
  UNKNOWN-never-MATCH rule). NO ROM CRC, no trainer identity, no build id — the gen1recomp #511
  over-coverage lesson is quoted at the top of the header next to the exclusion list.
- `source/celiolink.h`: **the one `#define CL_PROTO_REV 1`** + its bless-discipline comment. Zero
  logic; `celiolink.c` diff is EMPTY (acceptance gate 5).
- `source/gbacore.{h,c}`: `gbacore_game_rev()` — one `busRead8(0x080000BC)` (GBATEK header:
  0xAC game code, **0xBC software version**); `s_netExp = DGBA_NET_EXP_DEFAULT` /
  `s_netExpSeen = (1 << DGBA_NET_EXP_DEFAULT)` (same values, now a single source of truth shared
  with the fingerprint's `modeFlags`); the `# fprint` netlog line next to `# transport`, emitted
  through a `net_fprint_log` **forward declaration** so the TU stays libctru-free (the
  `net_mono_ticks` precedent).
- `source/netlink.{h,c}`: `DGBA_PROTO` moved netlink.c → netlink.h (value unchanged) so the
  fingerprint fill reads the same symbol the wire uses; **`PK_FPRINT` (type 10) + the 20-byte
  `DgbaFprintPkt`** (shares the `{magic,type}` prefix, so it passes the lobby drain's
  `got >= sizeof(DgbaLinkPkt)` gate — the `DgbaEventPkt` precedent) with two `_Static_assert`s on
  its size; the RX branch in the **lobby pump** `net_ping_update` and the piggyback send on the same
  ~6 Hz ping tick, **both gated on `!s_rxRun`** so the exchange is strictly pre-`net_link_start` and
  the in-link path never sees the type; `net_fprint_set_local` / `net_fprint_peer` /
  `net_fprint_log`; peer state cleared in `net_session_close`.
- `source/wireless.{h,c}` + `source/main.c`: the lobby builds its own surface once
  (`dgba_fprint_fill(&myFp, myCode, myGameRev, CL_PROTO_REV, DGBA_PROTO)`), publishes it with
  `net_fprint_set_local` **before** `net_session_host` / `net_session_join`, and each frame diffs
  the peer's → a top-screen line `link-surface: MATCH` (dim) / `link-surface DIFF: <fields>`
  (`THEME_QUIT_TEXT` red, y209) plus a one-shot echo into `status[]`. `wireless_lobby_run` gained a
  `uint8_t myGameRev` parameter; main.c reads it from the focused core beside the game code.
- `test/test_celiolink.c`: `#include "../source/fingerprint.c"` + **TEST 16 (GOLDEN)** — both 32-bit
  lanes AND the folded u64 pinned for `{"BPEE",0,1,1,5}` (`0DC57870`/`2A5C56C7`) and the user's real
  `{"BPRE",1,1,1,5}` (`8B048F76`/`A51D3329`), the canonical byte order, `CL_PROTO_REV`/
  `DGBA_NET_EXP_DEFAULT` pinned, byte-exact `# fprint` DIFF/MATCH/unknown lines, short/NULL game
  codes, truncation guard-bytes — and **TEST 17 (MUTATION)**: all 8 surface bytes moved one at a
  time, each must move the u64 **and both lanes**, mutation isolation (restore ⇒ golden), no
  pairwise collisions, `dgba_fprint_diff` naming exactly the mutated field, the 5-field diff, diff
  truncation/NULL safety.
- `test/host/test_netlink_reliability.c`: ONE added include line (`fingerprint.c`) — netlink.c's
  `net_fprint_log` calls the formatter, so the TU no longer links without it. No check changed.
- `docs/HANDOFF.md`: the run-#13 checklist (Next steps #2) gained the D6 paragraph — what the lobby
  line means and the "**confirm both consoles agree before running**" pre-flight.

**Gates** — celiolink suite **538 PASS** (was 417, +121), netlink **66 PASS** (unchanged, as gate 2
requires), test_diag **360 PASS** and test_control **6897 PASS** (both untouched); `make -j8` →
`3DGBA.3dsx` green with **fingerprint.c 0 warnings, gbacore.c 0, netlink.c unchanged at its 1
pre-existing misleading-indentation, wireless.c unchanged at its 1 pre-existing scan-card
format-truncation** (verified by rebuilding all of `source/` and diffing the warning inventory
against the D5 baseline: main.c still 15, every other file identical). `git diff source/celiolink.c`
is EMPTY; `git diff source/celiolink.h` is the single `#define` block.

**Decisions / deviations (recorded per the phase contract)**
1. **Charter vs SPEC on the transport — the SPEC wins, and it is the only reading that satisfies
   "no new radio round-trips inside the link".** The task charter said "reuse the existing
   connect-time event machinery (trainer-card/identity precedent)"; SPEC §D6.4 examined exactly that
   and **rejected** it with evidence: `net_event_*` only exists after `net_link_start` spins the RX
   thread (netlink.h) and `net_event_reset` runs at link start, so using it would move the exchange
   INSIDE the link window — next to the frozen path. The lobby pump (`net_ping_update`) is the sole
   packet path while `!s_rxRun`, already owns pull/echo/RTT pre-link, and never runs concurrently
   with the trade path. Implemented on the lobby pump; gbacore.c/celiolink.c gain no exchange code
   at all. What IS reused from the trainer-card precedent is its *shape*: an opaque blob the
   transport ships without interpreting.
2. **Convergence rule made explicit** (SPEC said "3 extra sends after it has [arrived]"): we send on
   every ping tick while the peer's surface is missing, and after it arrives send
   `FPRINT_POST_SENDS`=3 more. The budget is re-armed **only when the peer's bytes actually CHANGE**
   — re-arming on every received packet would make two consoles ping-pong forever at 6 Hz.
3. **`net_session_close` clears the PEER surface but KEEPS ours.** SPEC said "all state cleared"; a
   stale peer would be a lie next session, but our own surface is re-published before every
   host/join anyway, and keeping it means a netlog dumped after teardown still names our own side
   (`wl_dump` runs before `net_session_close` today, but that ordering shouldn't be load-bearing).
4. **The packet's `seat` field is filled from `s_host`**, not from a `myNode==1` status call — the
   two are the same fact (the host IS node 1) and `s_host` needs no UDS round-trip. It is carried,
   unread, for the future 3-4 player lobby (HANDOFF Next steps #3), exactly as SPEC intended.
5. **`net_fprint_log` interprets the surface via `fingerprint.h`** rather than netlink.c growing a
   second formatter. The *wire* stays opaque (the packet field is `u8 surface[8]`, never parsed on
   the RX path); the include exists only so the netlog line is rendered by the HOST-TESTED formatter.
   This is why `test_netlink_reliability.c` needed its one added include.
6. **No rompicker file-read fallback for the pre-boot case.** SPEC mentioned falling back to
   `rom_game_code()` "when no core is loaded yet"; the existing lobby has no such fallback for the
   advertisement game code either (main.c leaves it empty when `!fg->core`), so rev falls back to 0
   the same way. Adding a new file-read path would have been new untested behavior on a path the
   lobby cannot currently reach with a link.
7. **Open Questions resolved as spec'd, none blocking**: (1) `netProto`/`clProtoRev` stay
   **allow-with-warning** in v0 — PHASE.md's D6 row is binding and a refusal path is new behavior
   next to a frozen transport; revisit at first public release. (2) The **beacon is byte-identical**
   — `udsGetNetworkStructApplicationData`'s truncation semantics for a larger-than-buffer appdata
   are unverified, and getting them wrong breaks old↔new scanning entirely. (3) **No re-exchange on
   a mid-link KEY_Y** — mid-link KEY_Y is already forbidden (HANDOFF run-#11); if the A–E fallback
   ever becomes a lobby-stage choice, the send must re-arm on that change (one call to
   `net_fprint_set_local`).
8. **Two lanes with the same prime and byte order, different bases** — SPEC's wording followed
   exactly. TEST 17 asserts a single byte flip moves BOTH lanes, which is guaranteed rather than
   lucky: FNV-1a's per-byte step (XOR then multiply by an odd prime mod 2³²) is a bijection on the
   32-bit state, so distinct intermediate states can never re-converge.

**For the HW run-#13 checklist**: the lobby now answers "are these two consoles even running the
same build?" BEFORE the link — `link-surface: MATCH` / red `link-surface DIFF: <field>` on the top
screen, and a `# fprint local=… peer=… verdict=…` line in every `3DGBA_net_*` header. Two habits
worth keeping: (a) glance at the lobby line before pressing Start-link — a `DIFF:clProtoRev` means
one console has a stale `.cia` and the run's forensics would be worthless; (b) the header's
`local=BPRE r1` now RECORDS the ROM revision of each side, so no future session has to re-derive it
from callback fingerprints. `verdict=unknown` = the exchange never completed (short lobby / lossy
radio) — it is not a match.

## 2026-08-03 — Slice D5: input record / replay (SPEC-control-replay §D5 + §C)

**Landed**
- `source/control.{h,c}` (EXTENDED, still PURE C — no libctru, no mGBA, no file I/O, no clock):
  - **`CtlRec` (record, PASSIVE)** — `ctl_rec_init/arm/armed/anchored/anchor_frame/lines/stop/
    tick/header/status`. `ctl_rec_tick` takes the seat's FINAL assembled mask and returns a
    `"<frameOffset> <mask>\n"` line ON CHANGE (PM's `apRecLastMask`, mp_bridge.cpp:1591-1615),
    0 when silent, **-1 exactly once** at the `CTL_REC_MAX_LINES` cap. It returns TEXT, never a
    key mask, so "record never overrides live input" (PM's early return at mp_bridge.cpp:1614)
    holds by construction. `ctl_rec_header` formats the `#` header (seat/game/anchored/date, the
    GBA KEYINPUT bit table, the determinism rules) — pure formatting, host-tested, and skipped by
    the loader so a recording is a valid replay table verbatim.
  - **`CtlRep` (replay)** — `ctl_rep_init/load/load_begin/load_feed/load_end/active/abort/tick/
    status`. The table is `CTL_REP_MAX`=8192 **parallel arrays** (`uint32 f[]` + `uint16 mask[]`
    = 6 B/entry = 48 KB/seat, the spec's sizing; a `{u32,u16}` struct would pad to 8). Playback
    is PM's catch-up loop verbatim (mp_bridge.cpp:1616-1647): consume every entry already due,
    inject only the LATEST, so a slow render frame or the wireless ~4-5 emu-fps loses nothing.
  - **`CtlAnchor` — the ONE field-entry EDGE detector both halves share** (D5.2): `fieldValid`
    (the existing D4.9 predicate) held for `CTL_ANCHOR_FRAMES`=3 consecutive ticks **after a
    non-field tick**. Same code, same debounce on both sides, so the constant delay cancels and
    the offsets mean the same thing on the replaying console.
  - `CtlStat` gained `recLines/recState/repState/repIdx/repN` + `ctl_publish_rr`.
  - Internal-only refactor: the status-ring push/drain became `ctl_ring_vpush/push/drain(buffer,
    head, count, seat, …)` so the two new halves get their OWN rings. **`CtlSched`'s layout and
    every D4 entry point are unchanged** (the D4 tests still poke `cs.ring`/`cs.rCount`).
- `source/main.c` glue (all sdmc I/O, ~150 lines, inside the existing `#if CTL_D4_ENABLE`
  region so D5 can never build without its host): `s_ctlRec[2]`/`s_ctlRep[2]`/`s_recFile[2]` in
  **static** storage; `ctl_rr_poll_seat` — the record/replay polls staggered **half a period**
  from that seat's move/go poll (`g_renderSeq%20 == seat*10+5`), so no render frame ever does two
  poll sites; `record_p<N>.txt` marker → `ctl_rec_arm`; `replay_go_p<N>.txt` → **streams**
  `replay_p<N>.txt` through a 512-byte static buffer into the chunked parser (an 8192-entry table
  is ~80 KB of text — never materialised); `ctl_rec_feed` writes the lazily-created
  `sdmc:/cias/netlogs/3DGBA_rec_p<N>_<MMDD>_<HHMMSS>.txt` (header at the anchor, `fflush` per
  line = the PM crash-safety rule / the run-#11 lost-log lesson); the replay mask ORs into the
  SAME `ckA/ckB` term D4 already injects (one seam, strictly additive); the recorder is fed the
  FINAL `emuA.keys`/`emuB.keys` immediately after the existing assembly (D5.4); `!` abort files
  now also `ctl_rep_abort` + `ctl_rec_stop` + close the file (D5.5/D5.8); session init/teardown
  reset/stop/close both halves next to the D4 calls.
- `source/gbacore.c`: one added `fprintf` — `# control-rr p1 rec=..(N lines) rep=..(i/n) | p2 …`
  next to the D4 `# control` line, read from the same mirror. Zero logic change.
- `test/host/test_control.c`: TESTs 14-20 added (same run line) — record anchor/debounce/
  on-change/offset-across-a-frozen-clock, ON-FIELD arming waiting loudly for a real edge,
  stop/re-arm, the cap returning -1 once, the header (every line `#`, loads as a table), the
  loader's 9 loud-failure modes + the exact cap, chunked==whole-buffer for chunk sizes 1..7,
  playback (edge wait, catch-up, done, non-zero tail release, both abort paths), the D5 mirror,
  two independent seats — and **TEST 19, the RECORD → REPLAY ROUND TRIP**: a 22-tick mask
  timeline recorded through `ctl_rec_tick`, written as a real header+body file, reloaded through
  `ctl_rep_load` and replayed through `ctl_rep_tick` reproduces the mask **exactly, tick for
  tick** (from a different absolute start frame), plus a coarse-clock (wireless-speed) pass.

**Gates** — celiolink **417 PASS**, netlink **66 PASS**, test_diag **360 PASS** (all unchanged),
test_control **6897 PASS** (was 6287, +610); `make -j8` → `3DGBA.3dsx` green with **control.c 0
warnings, gbacore.c 0**, and main.c still at its pre-slice **15** (the same pre-existing
UI-redesign leftovers — full list diffed against the D4 baseline).

**Decisions / deviations (recorded per the phase contract)**
1. **The anchor requires a real EDGE, and arming on the field says so LOUDLY.** D5.2's "edge =
   first frame after a non-field state" is taken literally: a recorder/replayer armed while the
   player is already standing in the field waits, and pushes one `record|replay armed ON-FIELD:
   waiting for a field-entry EDGE` line naming the cheapest way to make one (open+close the START
   menu — `GCTX_FIELDMENU` is a non-field context). Rationale: an anchor that fires "wherever we
   happen to be standing" is not reproducible on the second console, and a silent no-op would be
   the exact quiet-failure class this project refuses. The operator discipline (Appendix R2) is
   unchanged; the difference is that a mis-armed session now explains itself in the log.
2. **The anchor latches on the CONFIRMING tick** (the 3rd consecutive field tick), not on the
   first. Both halves run the identical detector, so the 2-tick delay cancels between record and
   replay, and "the first line sits at offset 0" stays exactly true.
3. **Replay's real-input abort applies only while RUNNING**, not while it hunts the anchor —
   the same scope call D4 made for the go-gate (BUILDLOG D4 §3), and for the same reason: the
   operator has to walk/press into the field BY HAND to create the anchor edge, so aborting on
   that input would make an armed replay impossible to start. Recording never aborts on real
   input at all (it is *recording* it). Both halves pinned by TEST 18.
4. **A chunked loader was added** (`ctl_rep_load_begin/feed/end`); `ctl_rep_load(text, …)` is
   kept with the spec's exact signature and implemented on top of it. Reason: a full 8192-entry
   table is ~80 KB of TEXT, and a static 80 KB scratch buffer next to two mGBA cores (on top of
   the 48 KB table itself) is not worth it — the glue streams the file in 512-byte reads. TEST 17
   proves every chunk size 1..7 parses identically to the whole-buffer path.
5. **Over-long `#` comment lines are legal; over-long DATA lines are a loud error.** The partial-
   line buffer is `CTL_REP_LINE_MAX`=63; a header line explaining the format (or a hand-written
   `# save:` note) is longer than that, so the loader swallows over-long comments and only fails
   on over-long data. Found by TEST 15/19 the moment the real header met the real loader.
6. **Backwards offsets are rejected** (`line N: offset X goes backwards`). The playback loop only
   walks forward, so such an entry could never be played — rejecting the table is honest;
   skipping the entry would silently replay something else.
7. **On a load failure the table file is REMOVED** (with the go file) so a broken table cannot
   re-fire on the next trigger; on SUCCESS `replay_p<N>.txt` is LEFT in place, so the same table
   can be re-armed by touching the go file again (the go file is the one-shot, per D5.6).
8. **Recording file name reuses the existing writer helper** — `diag_log_path(…, "rec_p<N>", …)`
   → `sdmc:/cias/netlogs/3DGBA_rec_p<N>_<MMDD>_<HHMMSS>.txt` rather than the spec's literal
   `rec_p<N>_<MMDD>_<HHMMSS>.txt`: same folder, same information, and the `3DGBA_` prefix keeps
   it consistent with every other netlog (the charter's "reuse the existing writer helpers").
9. **API additions beyond the spec's listing** (all additive, none renamed): `ctl_rec_stop`,
   `ctl_rec_anchored`, `ctl_rec_anchor_frame`, `ctl_rec_lines`, `ctl_rec_header`,
   `ctl_rec_status`, `ctl_publish_rr`, the chunked loader. `ctl_rec_status` mirrors the spec's
   own `ctl_rep_status` (the anchor/cap/stop events are detected INSIDE the module, so it needs a
   way to say so); the rest are the glue's read-only questions.
10. **SPEC Open Question 3 (does the 3DS START+SELECT menu chord leak GBA START/SELECT into a
    recording?): resolved — NO leak on the chord frame.** The menu trigger and the whole input
    block are the two halves of one `if (combo) {…} else {…}` (main.c), so the frame that opens
    the menu skips the input block entirely and records nothing. The frames where START alone was
    genuinely delivered to the game before the chord completed ARE recorded — correctly: the game
    really received them, and replaying them reproduces the same game behaviour (the 3DS-level
    pause menu is not part of the emulated game).
11. **SPEC Open Question 5 (sdmc `stat` cost): held at the spec's cadence** — D5 adds 2 more
    polled files per seat, but on a DIFFERENT tick (offset `CTL_POLL_FRAMES/2`), so the worst
    frame still does at most one poll site (≤2 `stat`s) and `fopen` still only happens when a
    file is actually there. The one genuinely new hot-ish cost is the spec-mandated `fflush` per
    recorded line (D5.3, PM crash-safety); recordings are on-change, so a walking player produces
    a few lines a second. Both are on the run-#13 worst-frame-ms watch list.
12. **SPEC Open Question 7 (replay cap 8192): shipped as spec'd** — the loud `table too large`
    failure names the fix if a real recording ever overflows it.
13. **Not exercised on hardware** (invariant 8): PC suites + a clean `make` are this slice's
    gate. The HANDOFF run-#13 checklist (Next steps #2) now documents the record/replay files,
    the field-entry anchor recipe, the artifacts and the determinism rules.

## 2026-08-03 — Slice D4: file-driven tile-exact movement + go-files (SPEC-control-replay §D4 + §C)

**Landed**
- `source/control.{h,c}` (NEW, PURE C — no libctru, no mGBA, **no file I/O and no clock**, so it
  dual-compiles on the PC and the whole feature is testable with a fake coordinate feed):
  the token grammar (`L<n>/R<n>/U<n>/D<n>` walks, lowercase = sprint (+B), bare dirs = taps,
  `a b s c x y` → A/B/START/SELECT/**GBA L/R** (the GBA has no X/Y), `W<n>` waits, leading `G`
  go-gate, leading `!` abort), the **closed-loop walk scheduler** (latch start tile → target →
  hold the d-pad until the GAME's own coordinate reaches it; warp = map change completes the
  token; cross-axis drift completes + logs; `n*60+240` emulated-frame wall timeout aborts the
  WHOLE queue; `CTL_NOFIELD_DL`=600 field-validity guard), the real-input abort rule, a 16×96
  status ring, per-seat counters and the `g_ctlStat[2]` netlog mirror. Every timer is a uint32
  delta of the scripted core's EMULATED frame counter, so a frozen clock freezes the script.
- `source/main.c` glue (all sdmc I/O; ~130 lines): `s_ctl[2]` schedulers in **static** storage
  (off run_session's stack), the D4.1 arm-iff-`sdmc:/cias/control`-exists check (ONE stat per
  session), `ctl_poll_seat` (staggered 10-frame polls — seat 0 on `g_renderSeq%20==0`, seat 1 on
  `==10`, so at most one sdmc stat per frame; `stat` is the cheap common case, `fopen` only when
  a file is really there; consume-on-pickup; `!` honoured mid-script; a non-abort file dropped
  mid-script is LEFT IN PLACE; `go_p<N>.txt` polled only while gated), the per-frame tick at the
  EXISTING parked-window read site (reuses the `gst`/`gsb` GameStates the gs logger already read
  — **no new game-RAM reads, no new addresses**), and the injection: `emuA.keys |= ckA` /
  `emuB.keys |= ckB` — strictly additive at the same emulated-keypad seam touch uses. Status
  lines go to a LAZY `sdmc:/cias/netlogs/3DGBA_control_<MMDD>_<HHMMSS>.txt` (reuses
  `diag_log_path` + the existing mkdir preamble; fflush per line, PM crash-safety). Script keys
  also ride the gs log's existing `injKeys` column (free correlation). A `_Static_assert` pins
  `CTL_KEY_*` to `GBAKEY_*` so the hand-mirrored key table can never drift silently.
- `source/gbacore.c`: one `fprintf` in `gbacore_net_log_dump` next to `# celio` — the
  `# control p1 tok=/abort=/timeout=/pick=/st=` summary, read from the `g_ctlStat` mirror
  (pure copies; the dump never touches the schedulers). Zero logic change.
- `test/host/test_control.c` (NEW; run line in its header:
  `clang -std=c11 -Wall -Wextra -O0 -g -I source test/host/test_control.c -o /tmp/tcl && /tmp/tcl`):
  13 tests / **6287 checks** — full grammar incl. both Appendix run-#13 recipes verbatim and the
  64-token / 512-byte caps; 14 loud parse-error cases; the `!` file + `ctl_abort` idempotence;
  closed-loop convergence on a fake coordinate feed (lag-immune hold, sprint mask, multi-token
  boundaries, cross-drift); warp completion (and "map became known" is NOT a warp); the
  `n*60+240` timeout aborting the whole queue + deadline-armed-at-real-start; the no-field guard
  (and taps/waits staying menu-legal); frame-exact tap press/slot windows and `W<n>`; the go-gate
  (incl. hand-staging not aborting it); real-input abort naming the live token; **frozen-clock
  behaviour** (no progress, no timeout, resume fires it); status-ring FIFO/overflow/truncation;
  counters + mirror; and two independent seats.

**Gates** — celiolink **417 PASS**, netlink **66 PASS**, test_diag **360 PASS** (all unchanged),
NEW test_control **6287 PASS**; `make -j8` → `3DGBA.3dsx` green with **control.c 0 warnings,
gbacore.c 0**, and main.c still at its pre-slice **15** (all pre-existing UI-redesign leftovers).

**Decisions / deviations (recorded per the phase contract)**
1. **Test lives at `test/host/test_control.c`** (the charter's path) rather than the spec's
   `test/test_control.c` — it belongs with the other pure-host suites (`test_diag.c`,
   `test_netlink_reliability.c`); `test/test_celiolink.c` is the one legacy exception.
2. **D5 (record/replay) is NOT in this slice** — the charter scopes D4 only. `control.h` is
   written so `CtlRec`/`CtlRep` slot in additively (the shared `CtlIn` snapshot and the status
   ring already carry them); nothing here needs changing when D5 lands.
3. **Real-input abort applies only while RUNNING, not while a `G` script is gated (SPEC D4.11
   scope call).** D4.11 says "any real input aborts", but D4.10/Appendix R2 explicitly design
   the go-gate for the operator to **stage the scene by hand** and then touch the file — if the
   staging input aborted the waiting script, the whole go-file workflow (the two-console sync for
   run #13) would be unusable. Aborting a *running* script is unchanged, and TEST 9 pins both
   halves.
4. **One token completion per tick** (the completing tick injects 0). This gives every token
   boundary a one-frame key RELEASE — a Gen-3 menu needs the `newKeys` edge and a walk needs the
   d-pad let go before the next direction. Documented in `ctl_next`, pinned by TEST 4/8.
5. **Buttons are LOWERCASE ONLY** (strict spec grammar). An uppercase `A` is a loud parse error
   that names the token and says so, rather than a silently-accepted synonym — the "loud not
   silent" discipline; a superset would have fossilized into recipes untested.
6. **Open Question 2 (script picked up on a PAUSED seat): accept + say so.** The pickup succeeds
   (the spec's default) but the glue emits one extra status line naming the stopped emulated
   clock — otherwise the log shows a queued script that never moves with nothing explaining why.
7. **Open Question 1 (tap frame constants 4/16 and 12/40): shipped at the spec's values,
   verify-on-hw-pending**, all four in one `#define` block in `control.h` with the reason
   (PM's numbers are DS-era; the Gen-3 turn-vs-step threshold was never frame-counted here).
   Run #13 tunes them in one place.
8. **Open Question 5 (sdmc `stat` cost at ~6 Hz): shipped at the spec's 10-frame cadence**
   (`CTL_POLL_FRAMES`), staggered so no frame ever does two stats, and the `fopen` only happens
   on a tick where a file actually exists. If the HUD's worst-frame-ms shows it, the cadence is
   one constant.
9. **Abort files are read, not just stat'd, while a script runs.** D4.4 requires honouring `!`
   mid-script, which needs the first byte — so the poll opens the file only when `stat` says one
   exists (rare, operator-created), and a non-abort file found mid-script is left untouched.
10. **`control.c` uses `<stdarg.h>` + `vsnprintf`** for the status ring (the spec's include list
    named only `snprintf`). One variadic helper replaced ~14 hand-rolled snprintf sites; still
    pure C, still no file I/O.
11. **Open Question 6 (auto-go from a celio FSM transition): deferred as spec'd** — the go file
    stays manual; wiring it to `s_celioSection` would couple the control glue to celiolink state
    and run #13 does not need it.
12. **Not exercised on hardware** (invariant 8): PC suites + a clean `make` are this slice's
    gate. The HANDOFF run-#13 checklist (Next steps #2) now documents the grammar, the arm-by-
    `mkdir` opt-in, the `3DGBA_control_*.txt` artifact and the `# control` netlog line.

## 2026-08-03 — Slice D3 (RESUMED + COMPLETE): per-frame CSV telemetry (SPEC-firmware-diag §D3)

Resumed the interrupted D3 attempt. **Audit first**: the killed session's work was committed
(2b20149) and turned out to be much further along than its own BUILDLOG note claimed — a diff of
the tree against SPEC §D3 found the capture extension, BOTH exports and the whole main.c writer
already landed and green. This entry records what was already there (verified line-by-line
against the spec, nothing rewritten) plus what this session finished.

**Already landed by the interrupted attempt (verified against the SPEC, kept as-is)**
- `source/diag.{h,c}`: `DiagCsvRow` (one field per D3.3 column, in column order),
  `DIAG_CSV_COLUMNS`, `diag_csv_header` (2 lines: build/role comment + column names),
  `diag_csv_row` (ONE bounded snprintf; `%lu/%lX` + casts so devkitARM newlib and the PC host
  format identically), `DIAG_D3_ENABLE` bisect gate.
- `source/gbacore.c`: `net_celio_capture` (LOGGING ONLY seam, unchanged call sites) additionally
  snapshots the FULL `ClStatus` into `s_celioStatus` + the outgoing-ClEvent queue depth
  `s_celioOutQ = (outTail - outHead + CL_EVENT_QUEUE_DEPTH) % CL_EVENT_QUEUE_DEPTH` (ring indices
  — verified against celiolink.c:24-34/1170-1172). New `gbacore_net_counters(GbaNetCounters*)`:
  pure copies of the worker-captured statics + the netlog ring tail, no lock, tearing disclosed.
- `source/netlink.{c,h}`: `net_event_get_queue()` — max un-ACKed outbound backlog across seats
  under the existing `s_evLock` (the live form of the run-#6 send-queue-overflow X-ray).
- `source/main.c`: `s_csvFile`/`s_csvRows` writer state; CSV **opened once** at the `wlOn = true`
  site (after `diag_wd_session_reset` re-reads the `diag_off.txt` kill switch) with
  `setvbuf(_IOFBF, 8192)` + header + one `fflush`; the row emitted from the gs-logger block
  REUSING the `GameState` it already read (SPEC D3.4 "do NOT re-read"), `fwrite` from a
  `static char[512]`, **fflush every 256 rows**; `fclose` folded into `diag_wd_close` so all
  three wl teardown sites + session end already close it (zero new call sites on the frozen path).

**Finished this session**
- `test/host/test_diag.c`: **TEST 6** (CSV header) — golden bytes for both lines (the column-name
  line spelled out INDEPENDENTLY of `DIAG_CSV_COLUMNS`, so a silent rename/reorder fails),
  HOST/JOIN role line, the D3.8 **header-vs-row field-count parity assert** (comma counts equal;
  62 columns; no empty name or value), truncation returns the would-be length and never overflows,
  degenerate args refused. **TEST 7** (CSV row) — golden bytes over a sample exercising every
  format class (hex columns hex + un-padded, `-1` sentinels, 0/1 booleans, large decimals); the
  all-zero row; an all-sentinel row proving no `-1` prints as `4294967295`/`FFFFFFFF`; truncation
  + degenerate args; and the **one-column diff smoke** (D3.8 item 3): flipping `clHS` changes
  exactly ONE byte and the comma count before it equals `clHS`'s column index — repeated for a
  HEX column (`lastW0`) so hex formatting can't smear into neighbours.
- **D1 deviation #3 FOLD (charter directive)**: `gbacore_net_wd_counters(gateN, forceN)` REMOVED
  from `gbacore.{c,h}`; the D1 watchdog sampler now takes `celioGateN`/`celioForceN` out of
  `gbacore_net_counters` — one read-only counters seam in gbacore.c instead of two (one struct
  fill per 200 ms tick; all pure copies, no logic change).
- `docs/HANDOFF.md` Next steps #2 (SPEC step 3): the run-#13 checklist now names the three new
  artifacts (`3DGBA_csv_*.csv`, `3DGBA_wd_*.txt`, `3DGBA_hang_*.txt`), how to read them, the
  "check `vbl` ticks ~60/s to promote the D2.1 address to verified" instruction, and the
  `diag_off.txt` kill switch.

**Gates** — celiolink **417 PASS** (unchanged), netlink **66 PASS** (unchanged), test_diag
**321 → 360 PASS**; `make -j8` → `3DGBA.3dsx` green; per-file warning counts unchanged from the
D2 baseline (main.c 15, gbacore.c 0, diag.c 0, gamestate.c 0 — all pre-existing UI-redesign
leftovers; touch.c's 4 are likewise pre-existing).

**Decisions / deviations (recorded per the phase contract)**
1. **Charter said "counters export from celiolink.h (const struct pointer)"; the binding SPEC
   D3.2 explicitly RESOLVES that differently and was followed**: celiolink.h gets NO new export
   (the FROZEN FSM already exposes itself read-only via `cl_get_status`/`ClStatus`, and the live
   `CelioLink` lives inside the worker-owned `NetDriver` — a const pointer into it would hand the
   render thread a struct being mutated mid-`cl_transfer`). The safe seam is the existing
   worker-side `net_celio_capture` snapshot + `gbacore_net_counters`. Zero celiolink.c/h changes.
2. **Charter said "the CSV writer in gamestate.c (or a new csvlog.c)"; the SPEC pins main.c**
   (D3.4 hook = the gs-logger block where the participant's `GameState` is already in hand;
   D3.5 open site = the `wlOn = true` line; the teardown closes are main.c's). Followed the SPEC —
   a writer in gamestate.c would have to re-read the game state (explicitly forbidden by D3.4)
   and would need its own teardown call sites on the frozen path.
3. **D3.3's prose says "52 columns"; the column LIST it pins has 62.** The list is binding and
   was implemented verbatim; the test asserts 62 (61 commas) and header/row parity, so the count
   can't drift silently. Noted in the test comment.
4. **`built=` in the CSV header line 1 is diag.c's `__DATE__ " " __TIME__`.** Because the host
   test `#include`s diag.c, both expand in the SAME translation unit — the golden test reproduces
   the exact bytes without hardcoding a date.
5. **Open Q4 (CSV size/cadence): shipped per-frame as spec'd** (~15 KB/s; runs are minutes).
   Decimation is a one-line change if run #13's files prove unwieldy — the `rf` column already
   makes gaps explicit.
6. **Open Q6 (D3 under loopback `netOn`): deferred as spec'd (D3.7)** — armed on `wlOn` +
   participant only; enabling loopback needs two files/two rows per frame. One-line arming change.
7. **Open Q5 (`GbaNetCounters` tearing): accepted + disclosed**, no seqlock — it would add
   worker-side cost inside the FROZEN path's logging call (PHASE invariant 1).
8. **SPEC step 3's "manual Azahar boot proving a csv file appears" was NOT performed** and cannot
   be, solo: the CSV opens only at wireless link start, which requires a resolved UDS peer (two
   consoles). PC suites + `make` green are this slice's gate; the file's first real proof is the
   run-#13 checklist item added to HANDOFF (invariant 8: hardware-final).

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

*(Superseded by the completed D3 entry at the top of this file. Kept for the record — and as a
caution: this note UNDERSTATED what the killed session had actually committed. The resuming
implementer's first action, diffing the tree against the SPEC, found the exports and the whole
main.c writer already present and green; only the host tests and the D1 fold were missing.)*

The D3 agent was killed ~10 min in. What exists in the tree: `diag_csv_header()` /
`diag_csv_row()` formatters in `source/diag.c` (+ their decls/struct). Wiring state
(celiolink/netlink counter exports, gamestate writer, main.c hookup) NOT complete —
the resuming D3 implementer must diff the tree against SPEC-firmware-diag §D3 and
finish, not restart. Tree verified green at checkpoint: celiolink 417 PASS,
netlink 66 PASS, test_diag 321 PASS, `make` → 3DGBA.3dsx clean.

## 2026-08-03 — Slice D7: PARTIAL — session stopped at the usage limit

D7a (laggyPair delay shim) LANDED and is green: `test/test_celiolink.c` gained the
delay-queue shim hooked into every `frame_drive`, running the two-instance TESTs under
modelled radio lag — suite grew 417 -> **1171 checks, 0 failures**. The D7 agent was
killed before returning, so BUILDLOG has no D7 entry of its own and the remaining
sub-slices are NOT started:

- **D7b** per-round CRC/hash agreement assertions across both synthesized sides — TODO
- **D7c** `test/host/test_trace_replay.c` + `test/fixtures/` from
  `netlogs-archive-2026-07-06.tar.gz` — TODO (files absent)
- **D7d** `tools/verdict.sh` netlog-folder -> per-checklist PASS/FAIL/UNKNOWN — TODO (absent)
- **D7e** `docs/kb/celio/KNOWN-DIFFERENCES.md` tri-ledger — TODO (absent)

Verified green at this checkpoint: celiolink **1171**, netlink **66**, diag **360**,
control **6897** — all PASS; `make` -> `3DGBA.3dsx` clean, warning count identical to the
pre-phase baseline (26, all pre-existing in main.c) and **zero** warnings in diag.c /
control.c / fingerprint.c. `make cia` NOT yet run (final gate never executed).

NOT started after D7: the two adversarial reviews, the fix pass, and the final gate
(which also owns the HANDOFF run-#13 checklist update). `docs/HANDOFF.md` shows as
modified — that is a slice-local edit, not the final gate's rewrite.
