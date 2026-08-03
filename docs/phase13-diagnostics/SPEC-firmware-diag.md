# SPEC — Firmware diagnostics slices D1 / D2 / D3 (phase 13-prep)

Status: **design spec, ready to implement** (authored 2026-08-03 from a read-only pass over the
actual sources — every claim below carries a file:line cite from today's working tree).

Binding contract: `docs/phase13-diagnostics/PHASE.md` — its **Invariants** section binds every
implementer of this spec, especially: the trade path is FROZEN (diagnostics are additive reads/
logs/counters only), suites stay green and grow, pure-C rule for parsers/formatters, `make`
green after every slice, and a dated `BUILDLOG.md` entry after each slice.

Source designs (models being ported, with their own cites):
- `docs/kb/external/pm-bridge-forensics.md` — §6/§12 (game-heartbeat hang catcher + register
  dump), §13 (freeze watchdog + numbered breadcrumbs), §3/§11 (per-frame CSV), PART 3 ranked
  checklist items 1, 4, 6.
- `docs/kb/external/pm-rom-abi.md` — §6 (the diag block: link-layer counters exported as CSV
  columns), §8 (test/telemetry patterns).

Key local sources inspected (line numbers valid for today's tree):
`source/main.c` (2430L), `source/gbacore.c` (1174L), `source/netlink.c` (949L),
`source/celiolink.c` (1310L), `source/celiolink.h` (407L), `source/gamestate.{c,h}`,
`source/netlink.h`, `external/mgba/include/mgba/internal/arm/arm.h`, `external/mgba/src/arm/arm.c`,
`external/mgba/include/mgba/core/core.h`, `external/mgba/include/mgba/internal/gba/gba.h`.

---

## 0. Shared infrastructure (build once, used by all three slices)

### S.1 — New pure-C module `source/diag.{c,h}`

- `diag.h` is **header-free pure C** (`<stdint.h>`/`<string.h>`/`<stdio.h>` for snprintf only —
  no libctru, no mGBA), so `diag.c` dual-compiles on the PC host harness (CLAUDE.md rule #4,
  PHASE invariant 4). It holds: the crumb site-ID table + encode macro (D1), the watchdog
  escalation state machine (D1), the hang-catcher state machine (D2), the register-dump text
  formatter (D2), and the CSV header/row formatters (D3). All of them are pure functions over
  plain structs — the 3DS-side glue (file I/O, `osGetTime`, LightEvent sampling) stays in
  `main.c`/`gbacore.c`.
- `gbacore.c` may include `diag.h` (it is pure); `celiolink.c` must NOT need it (celiolink has
  no blocking loops — see D1.2 — and stays exactly as it is; FROZEN path).

### S.2 — Netlog file helper (reuse the existing writer conventions, do not invent a new scheme)

- All new files land in **`sdmc:/cias/netlogs/`**, timestamped + role-tagged exactly like the
  existing writers: `wl_dump` (main.c:94-105) builds
  `sdmc:/cias/netlogs/3DGBA_net_<HOST|JOIN>_<MMDD>_<HHMMSS>.txt`; `gs_dump` (main.c:111-136)
  does the same for `_gs_`/`_touch_`. Factor that snprintf pattern into ONE static helper in
  main.c:

  ```c
  // main.c (static): build "sdmc:/cias/netlogs/3DGBA_<kind>_<ROLE>_<MMDD>_<HHMMSS>.<ext>"
  // role: seat 0 -> "HOST", 1 -> "JOIN", <0 -> no role tag (matches gs_dump's seat<0 branch).
  static void diag_log_path(char* out, size_t cap, const char* kind, const char* ext, int seat);
  ```

  and have the three new writers (watchdog D1, hang D2, csv D3) use it. `mkdir("sdmc:/cias")` +
  `mkdir("sdmc:/cias/netlogs")` before the first open, same as `gbacore_net_log_dump`
  (gbacore.c:834-835). Refactoring `wl_dump`/`gs_dump` onto the helper is OPTIONAL (no behavior
  change allowed if done).

### S.3 — Kill switch (flag/default policy per PHASE invariant 2)

- D1/D2/D3 are **passive logging → default ON**. One runtime kill switch disables all three:
  if the file **`sdmc:/cias/control/diag_off.txt`** exists, checked ONCE at wireless/net link
  start (the `wlOn = true; wlSeat = seat;` site, main.c:1812 — NOT per frame), all three slices
  stay dormant for that session. Additionally each slice has a compile-time gate
  `#define DIAG_D1_ENABLE 1` / `DIAG_D2_ENABLE` / `DIAG_D3_ENABLE` in `diag.h` so a reviewer
  can bisect a slice out of a build.
- Nothing in D1–D3 injects input or writes game RAM. Zero writes to any `gbacore_write*`.

### S.4 — Hot-path cost budget (binding)

- Worker-side (emu hot path) additions are **stores only, no new branches**: crumb stamps
  (one `volatile uint32_t` store, and only inside wait/gate paths that are already off the
  healthy fast path) and the already-existing `e->frame++` loop-seq (main.c:177/205 — no new
  code at all). The render-thread sampler is time-gated to ~200 ms and does a handful of loads
  + compares per sample — the render thread is not the emulation hot path.

---

## D1 — Breadcrumb globals + surviving-thread watchdog

Model: melonDS-PM detached watchdog + numbered wifi crumbs (pm-bridge-forensics.md §13,
EmuThread.cpp:568-602, Wifi.cpp:29-31/1236-1238/1846-1847, LAN.cpp:1208-1216; port item 6:
"the RENDER thread (which survives worker wedges — no extra thread needed on 3DS) samples every
200ms and appends STUCK lines at 1s/4s/12s").

### D1.1 — Inventory: every loop/gate in netlink.c / gbacore.c / celiolink.c that can block or spin

The crumb sites are exactly these. (Verified against today's sources; each is either a true
`for(;;)`-with-sleep loop, a thread loop, or a *cross-call spin* — a function that returns
"not yet" and is re-invoked forever by its caller, which wedges just as hard.)

**netlink.c — true blocking loops (1 ms poll loops, worker or main thread):**

| # | Site | Lines | Thread | Escapes | Crumb site-ID |
|---|---|---|---|---|---|
| 1 | `net_transfer_collect` `for(;;)` | netlink.c:410-423 | worker (via `net_finishMulti` gbacore.c:656, and state-E `net_bridge_fill` gbacore.c:565) | words arrived / `s_collectAbort` / `!s_up` / deadline | 1000 |
| 2 | `net_cmd_collect` `for(;;)` (state E only) | netlink.c:465-473 | worker | 8 words / abort / `!s_up` / deadline | 1100 |
| 3 | `net_round_wait` `for(;;)` (joiner pacing barrier) | netlink.c:505-511 | worker (gbacore_net_poll gbacore.c:1090) | round ready / abort / `!s_up` / deadline | 1200 |
| 4 | `net_rx_thread` outer `while(s_rxRun)` + drain `while(udsPullPacket)` | netlink.c:795-861 (drain at 797) | RX thread (core 2) | `s_rxRun=false` (0.5 ms poll, netlink.c:860) | 1300 (per-pass seq, D1.4) |
| 5 | `net_ping_update` lobby drain `while(udsPullPacket)` | netlink.c:270-285 | main thread (lobby only, `!s_rxRun`) | queue empty | 1400 |

**gbacore.c — cross-call spin gates (the worker re-polls these forever; the run-#4 wedge class):**

| # | Site | Lines | What wedges | Crumb site-ID |
|---|---|---|---|---|
| 6 | `net_poll_celio` link-ready gate (`mode!=MULTI \|\| !(siocnt&0x4000)` → return) | gbacore.c:942-947 | game never listens → partner never clocks (`s_celioGateN`/`nd->gateHeldN` already count it) | 2000 + (`gateHeldN` & 0xFF) |
| 7 | `net_poll_celio` ISR-edge wait (return until `(acked && fired && guarded)` or the wedge escape) | gbacore.c:967-988 | **the run-#4 silent-freeze site**; `s_celioForceN` counts escapes | 2100 |
| 8 | `net_poll_celio` Celio master-clock pacing gate (`nextClockCyc` not reached → return) | gbacore.c:964-965 | benign/expected; stamped anyway (it is the highest-frequency return, and a stuck `nextClockCyc` would look exactly like this) | 2200 |
| 9 | `gbacore_net_poll` (states A–E) ISR-proof gate | gbacore.c:1106-1124 | stale-word / edge-never-comes class | 2300 |
| 10 | `gbacore_net_poll` one-round-in-flight gate (`nd->phase != NET_IDLE` → return) | gbacore.c:1032 | completeEvent never fires (`finishN` X-ray, gbacore.c:645-647) | 2400 |
| 11 | `net_finishMulti` entry→exit bracket (the worker parks INSIDE `_sioFinish` for the whole collect) | gbacore.c:643-721 | wedged collect shows site 1000 (inner) + this bracket says which round | 3000 + (`pendingRound` & 0xFF) on entry; store 0 on exit |

**main.c — render/teardown blockers (stamped so a post-mortem names them, but NOT sampled —
the sampler runs on this very thread; see D1.8 limitation + Open Questions):**

| # | Site | Lines |
|---|---|---|
| 12 | `LightEvent_Wait(&emu*.done)` pause/pipeline waits | main.c:1420, 1428-1431 |
| 13 | teardown waits (`part->done` + `wl_dump`) | main.c:1594-1598, 1787-1791, 2156-2162 |
| 14 | `net_link_stop` `threadJoin` | netlink.c:898 |

**celiolink.c — NONE.** Verified: `cl_transfer` is O(1) per call (bounded switch; the only
loops are the fixed `for i<8`/`i<32` copies at celiolink.c:1141, 411-414); `cl_section_step`'s
loops are bounded (all-zero scan :443, chunk emit `c<3` :381-390, script index :796-805).
celiolink gets **no crumbs and no code change**. Its *logical* parks (the level-triggered
holds `partnerPartyHeld`/`selectHeld`/`confirmHeld`, celiolink.c:489-492/504-507/519-522,
594-597/613-617) are already exported via `ClStatus` (celiolink.h:208-229) and become D3
columns — a wedge with workers ALIVE but a hold flag pinned is diagnosed by CSV, not crumbs.

### D1.2 — Crumb globals + encoding

```c
// diag.h — pure C. One crumb per subsystem, written by exactly the threads named in D1.1.
extern volatile uint32_t g_diagNetCrumb;   // netlink.c wait loops   (sites 1000-1499)
extern volatile uint32_t g_diagSioCrumb;   // gbacore.c driver gates (sites 2000-3299)
// encode: site base + iteration low bits (PM model: "1600 + (gatherIter & 0xFF)", LAN.cpp:1214)
#define DIAG_CRUMB(var, site, iter) ((var) = (uint32_t)(site) + ((uint32_t)(iter) & 0xFFu))
```

- Site bases are the table values above, spaced ≥ 256 apart so `crumb/1000*1000` names the site
  and `crumb % 1000` is the iteration residue. Definitions (`uint32_t g_diagNetCrumb = 0;` etc.)
  live in `diag.c`.
- Each `for(;;)` loop body stamps ONCE per iteration with its running iteration count (a local
  `uint32_t it++`). Each cross-call gate stamps once per gated return (iteration = the existing
  counter where one exists: `nd->gateHeldN` for site 2000; `elapsed >> 10` for sites 2100/2300;
  0 otherwise). Cost: one volatile store, no branch (S.4). The stamps live INSIDE the
  wait/gate path — the healthy fast path (words already arrived, gate open) never executes them.
- netlink.c and gbacore.c both already declare cross-TU functions with plain stdint
  (gbacore.c:307-324); they include `diag.h` the same way — it is pure, no libctru leak.

### D1.3 — Per-frame loop-seq (already exists; do not duplicate)

- **Worker loop-seq = `EmuInstance.frame`** (`volatile u32`, main.c:48): incremented once per
  `gbacore_run_loop` slice in BOTH free-run loops — linked at main.c:177, netLinked at
  main.c:205. A worker parked inside collect (site 1000) stops bumping it; that is the signal.
  No new code. (In the plain unlinked path workers park between frames by design — go/done
  handshake, main.c:208-212 — so loop-seq freezing there is meaningless; see D1.7 arming.)
- **Emulated-video seq = `gbacore_frame_counter(core)`** (gbacore.c:1146-1148) — sampled
  alongside so "worker spinning but game not producing frames" is distinguishable from "worker
  parked".
- **Render loop-seq**: NEW `static volatile uint32_t g_renderSeq;` in main.c, incremented once
  per `while (aptMainLoop())` iteration of `run_session` (top of the loop, after main.c:1398).
  One store.
- **RX-thread loop-seq**: NEW `volatile uint32_t g_diagRxSeq` (declared in diag.h, defined in
  diag.c), incremented once per outer `net_rx_thread` pass (after netlink.c:795). The RX thread
  is the radio's heartbeat; `g_diagRxSeq` frozen while workers live = radio thread wedged (a
  distinct failure class from the close-hang fix).

### D1.4 — Watchdog state machine (pure C, host-tested)

```c
// diag.h
typedef struct {
    uint32_t renderSeq, aFrame, bFrame, aVf, bVf, rxSeq;   // sampled seqs
    uint32_t netCrumb, sioCrumb;                            // crumbs (pass-through to the line)
    int      gateN, forceN;                                 // celio quick-reads (pass-through)
} DiagWdSample;
typedef struct {
    DiagWdSample last;       // seqs at the last progress edge
    uint32_t stuckSinceMs;   // 0 = progressing
    uint8_t  fired;          // bitmask: bit0=1s bit1=4s bit2=12s emitted for this episode
} DiagWd;
// Step every ~200ms. Compares s->aFrame/bFrame/aVf/bVf/rxSeq against wd->last: ANY watched seq
// advancing = progress (reset). All frozen -> escalate at >=1000/4000/12000ms, each ONCE per
// episode. Returns 1 and fills lineOut (one '\n'-terminated STUCK line) when a line must be
// appended; else 0. Which seqs are "watched" is a caller-supplied mask so wlOn watches only the
// participant worker (see D1.7).
int diag_wd_step(DiagWd* wd, uint32_t nowMs, const DiagWdSample* s, uint32_t watchMask,
                 char* lineOut, size_t cap);
```

### D1.5 — Exact STUCK line format + file path

One line per escalation, append + `fflush` (crash-safe, PM: mp_bridge log discipline,
pm-bridge-forensics.md §1 "append + fflush per line"):

```
STUCK ms=<1000|4000|12000> rseq=<g_renderSeq> aF=<emuA.frame> bF=<emuB.frame> aVf=<vfA> bVf=<vfB> rx=<g_diagRxSeq> netC=<g_diagNetCrumb> sioC=<g_diagSioCrumb> gateN=<n> forceN=<n> wl=<0|1> seat=<-1|0|1>
```

- Decimal everywhere (crumbs decode by `/1000` + `%1000` per D1.2).
- File: **`sdmc:/cias/netlogs/3DGBA_wd_<HOST|JOIN>_<MMDD>_<HHMMSS>.txt`** via S.2's helper
  (`kind="wd"`), created **lazily at the first STUCK event** (a healthy run writes no file —
  and never fopens on the hot path; the first stuck moment is by definition not the healthy hot
  path). Kept open for the session, `fflush` after every line, closed at the same teardown
  sites as `wl_dump` (main.c:1594-1598/1787-1791/2156-2162).
- On session start (main.c:1812) reset the DiagWd struct and re-derive the filename timestamp.
- Ordering note: `gateN`/`forceN` come from `gbacore_net_counters` (spec'd in D3.2). Either
  pull that export forward into slice 1 (it is self-contained) or emit `gateN=0 forceN=0`
  until D3 lands — both acceptable; say which in the BUILDLOG.

### D1.6 — Render-thread sampling (the hook point)

- The sampler call lives in `run_session`'s frame loop **before the `if (!menuOpen)` split** —
  immediately after the HUD/nowMs block (main.c:1408-1411) — so it runs every frame *including
  menu-open frames* (during wlOn the workers keep free-running while the menu is open; the
  existing per-frame gs block main.c:1559-1579 is inside the non-menu branch and would go blind
  exactly when the user opens the menu mid-wedge).
- Cadence: `if (nowMs - lastWdMs >= 200)` → gather DiagWdSample (plain loads of
  `g_renderSeq`, `emuA.frame`, `emuB.frame`, `gbacore_frame_counter(emuA/B.core)`,
  `g_diagRxSeq`, both crumbs, plus `gateN`/`forceN` via the D3 counters export) → `diag_wd_step`
  → append the returned line if any.
- `aVf`/`bVf` reads are `gbacore_frame_counter` (a struct read, no bus access) — safe from the
  render thread while workers run (same class as the HUD's `gbacore_net_diag` reads).

### D1.7 — Arming

- Active while `linkOn || netOn || wlOn` (the free-run modes where `e->frame` is a true
  loop-seq and where all twelve runs died). `watchMask` selects the watched seqs: under `wlOn`
  only the participant worker (`g_netWorker`, main.c:69; the other core is paused —
  main.c:1603 comment) + rxSeq; under `linkOn`/`netOn` both workers. Outside those modes the
  sampler still ticks `g_renderSeq` housekeeping but emits nothing.

### D1.8 — Known blind spot (accepted, documented)

The sampler runs on the render thread; the render thread itself blocks at the D1.1 sites 12-14
(teardown waits). If THOSE wedge, no STUCK line is written — the crumbs are still stamped, but
nothing samples them. This matches the PHASE.md scope ("the RENDER thread samples") and the
teardown class is already fixed (memory: 3dgba-wireless-close-hang RESOLVED). Escalating to a
detached sampler thread (PM's actual design, EmuThread.cpp:568-602) is listed in Open Questions.

### D1.9 — Host-side tests (extend the suites; PHASE invariant 3)

New `test/host/test_diag.c`, run as
`clang -std=c11 -Wall -Wextra -O2 -I source test/host/test_diag.c -o /tmp/td && /tmp/td`:
1. Crumb encode/decode round-trip for every site ID in the D1.1 tables (site recovered by
   `/1000*1000`… exact: base + iter&0xFF, decode site = crumb - (crumb % 1000 < 256 ? crumb%1000 : 0)
   — test the documented decode rule).
2. Watchdog: healthy sequence (any seq advancing) → zero lines; full freeze → exactly three
   lines at ≥1000/4000/12000 ms with correct `ms=` values; progress mid-episode → reset;
   second freeze → three fresh lines; watchMask honored (unwatched seq frozen alone → nothing).
3. STUCK line golden-string compare (fixed sample in → exact expected bytes out) + cap
   truncation never overflows `lineOut`.

---

## D2 — Game-heartbeat hang catcher + auto register dump

Model: pm-bridge-forensics.md §6 (always-on crash catcher, mp_bridge.cpp:1133-1174) + §12
(melonDS flavour with IE/IF/IME, EmuThread.cpp:510-533) + port item 1; pm-rom-abi.md §8
("exported frameCounter frozen >180 frames while `wanted` ⇒ dump ARM9 regs + 32-word stack").

### D2.1 — WHICH counters to watch (two tiers, both required)

- **Tier A (no RAM read): `gbacore_frame_counter(participant core)`** (gbacore.c:1146-1148,
  mGBA's produced-video-frame counter). Frozen ⇒ the emulated core is not producing frames at
  all — the run-#4 class (driver gate parked the clock) and any worker park. Catches wedges
  even when game RAM is unreachable.
- **Tier B (game-side heartbeat): `gMain.vblankCounter1`**, read via
  `gbacore_read32(core, prof->vblankCtr)`. This is the Gen-3 analog of PM's exported overworld
  frameCounter: incremented unconditionally in `VBlankIntr` (pret pokeemerald `src/main.c`
  `VBlankIntr(): gMain.vblankCounter1++`; pokefirered same). Frozen while Tier A advances ⇒ the
  game's IRQ delivery is dead (IME/IE masked forever, or the CPU is halted in a bad state) —
  the exact "IRQ masked forever" wedge melonDS's IE/IF/IME columns exist to distinguish
  (pm-bridge-forensics.md §12).

**Addresses (new `GameProfile` field `vblankCtr`, gamestate.h:23-76 + gamestate.c PROFILES):**

| Game | gMain base | vblankCtr = gMain+0x20 | Status |
|---|---|---|---|
| BPEE (Emerald) | 0x030022C0 | **0x030022E0** | derived-from-verified-anchors; **verify-on-hw-pending** |
| BPRE (FireRed) | 0x030030F0 | **0x03003110** | derived-from-verified-anchors; **verify-on-hw-pending** |
| BPGE (LeafGreen) | 0x030030F0 | **0x03003110** | FR-derived (house rule: BPGE ROM addrs are FR-derived/unverified, HANDOFF Gotchas) |

Derivation (cite in the code comment, house style): gMain base = existing verified
`mainCb2 - 4` (gamestate.c:97-98 "gMain+0 = mainCb2-4"; BPEE mainCb2 0x030022C4,
BPRE/BPGE 0x030030F4 — gamestate.c PROFILES rows). Offset 0x20 = pret `include/main.h`
`struct Main`: callbacks 0x00-0x18 (7×4), `intrCheck` u16 @0x1C (+2 pad), `vblankCounter1` u32
@0x20, `vblankCounter2` u32 @0x24, `heldKeysRaw` @0x28 … `newKeys` u16 @**0x2E** — and the
existing PROFILES `newKeys` values (BPEE 0x030022EE, BPRE 0x0303011E = gMain+0x2E, already
hw-exercised) cross-check the layout through offset 0x20. NOTE: the `/tmp/pret` clones and
`/tmp/*.sym` maps cited by HANDOFF are **no longer present on this machine** — the implementer
re-clones pret (symbols branch) to re-verify, or trusts the two-anchor cross-check above;
either way the address is *self-verifying in the D3 CSV* (it must tick ~60/s in any healthy
frame — mark verified after run #13's CSV shows it advancing).

Known limitation (documented, not solved here): a main-loop-only wedge with VBlank IRQ alive
(game spinning in a callback) advances BOTH counters and is NOT caught by D2 — it is caught by
D3 (cb2 static + celio section pinned). See Open Questions for the optional third counter.

### D2.2 — Arm condition

Armed **only while a link session is wanted**: `wlOn` (and optionally `netOn` loopback, same
flag) AND the watched core is the participant (`g_netWorker->core`; under wlOn the OTHER core
is deliberately paused — main.c:1603 — and would false-trigger instantly). Menus/saves on the
participant don't false-trigger: vblankCounter1 ticks through menus, and flash-save IRQ-off
windows are milliseconds, far under the 180-frame threshold. Disarmed the moment the session
tears down (the wl teardown sites, main.c:1594/1787/2156).

### D2.3 — Trigger + cap + re-arm (pure C, host-tested)

```c
// diag.h
typedef enum { DIAG_HANG_NONE = 0, DIAG_HANG_DUMP_CORE, DIAG_HANG_DUMP_GAME } DiagHangAction;
typedef struct {
    uint32_t lastVf, lastVbl;     // last observed counters
    uint16_t frozenVf, frozenVbl; // consecutive armed render frames each has been static
    uint8_t  dumps;               // dumps emitted THIS episode (cap 24)
} DiagHang;
DiagHangAction diag_hang_step(DiagHang* h, int armed, uint32_t vf, uint32_t vbl);
```

- Threshold: **>180 consecutive armed render frames** (~3 s at the ~60 fps render loop) with
  the counter static (PM: "frozen >180 frames", mp_bridge.cpp:1135). Tier A frozen ⇒
  `DUMP_CORE`; Tier A advancing but Tier B frozen ⇒ `DUMP_GAME` (the IE/IF/IME lines make the
  difference readable).
- **Cap 24 dumps per freeze episode** (PM: "Cap 24 dumps per freeze episode",
  mp_bridge.cpp:1133-1174) — after the first trigger, one dump per ~200 ms sampler tick until
  24, then silent. **Re-arm rule:** the instant the watched counter advances again, `frozenN`
  and `dumps` reset — the episode is over; a later freeze is a fresh episode with its own 24.
- Hook point: the same pre-menu sampler block as D1.6 (the vbl read is ONE `gbacore_read32` per
  200 ms tick — same benign-race class as the gs reads at main.c:1559-1579, and it must keep
  running with the pause menu open).

### D2.4 — HOW to read the mGBA ARM registers (exact struct path, verified in-tree)

mGBA headers ARE readable in this repo at `external/mgba/include` (Makefile `MGBA_INC`,
Makefile:66: `-I$(TOPDIR)/external/mgba/include -I$(TOPDIR)/external/mgba/build-3ds/include`),
and `gbacore.c` is the one TU allowed to include them (gbacore.c:1-2). Verified paths:

- `struct mCore` has `void* cpu;` as its FIRST member (core.h:37-38). The existing
  `g->core` (`struct GbaCore`, gbacore.c:88-97) gives
  **`struct ARMCore* cpu = (struct ARMCore*)g->core->cpu;`**
  (equivalently `((struct GBA*)g->core->board)->cpu`, gba.h:65-68 — use `core->cpu`, it is the
  documented seam).
- `struct ARMCore` (arm.h:168-196) embeds `ARM_REGISTER_FILE` (arm.h:159-162):
  **`cpu->gprs[16]`** (`int32_t`; gprs[13]=SP, gprs[14]=LR, gprs[15]=PC),
  **`cpu->cpsr.packed`** / **`cpu->spsr.packed`** (`union PSR`, arm.h:71-111; `.packed` int32).
- Banked: **`cpu->bankedRegisters[6][7]`** + **`cpu->bankedSPSRs[6]`** (arm.h:178-179). Bank
  index = `enum RegisterBank` (arm.h:51-57): 0 NONE(usr/sys), 1 FIQ, 2 IRQ, 3 SVC, 4 ABT,
  5 UND. Slot semantics (arm.c:36-41): `bankedRegisters[bank][0]` = that mode's r13/SP,
  `[1]` = r14/LR (FIQ additionally banks r8-r12 in slots 2-6, arm.c:25-34);
  `bankedSPSRs[bank]` = that mode's SPSR. CAVEAT for the formatter comment: the CURRENT mode's
  SP/LR live in `gprs[13..14]` and its bank slots are stale — dump raw values and let offline
  analysis interpret via `cpsr.priv` (arm.h:84 low 5 bits).
- New accessor in gbacore.c (mGBA-free signature; struct defined in `diag.h` so the formatter
  stays pure):

  ```c
  // diag.h (plain stdint — no mGBA types)
  typedef struct {
      uint32_t gprs[16];            // r0-r15 (r13 SP, r14 LR, r15 PC)
      uint32_t cpsr, spsr;          // union PSR .packed
      uint32_t bankedR13[6], bankedR14[6], bankedSPSR[6];   // banks: NONE,FIQ,IRQ,SVC,ABT,UND
      uint16_t ie, if_, ime;        // GBA 0x4000200 / 0x4000202 / 0x4000208
      uint32_t sp;                  // = gprs[13] at capture
      uint32_t stack[32];           // 32 words read at sp & ~3 (see D2.5)
      uint8_t  stackValid;          // sp pointed into 0x02/0x03 RAM
  } GbaCpuDump;
  // gbacore.h
  bool gbacore_dump_cpu(GbaCore* g, GbaCpuDump* out);   // read-only snapshot; NULL-safe
  ```

  Implementation: copy regs from `(struct ARMCore*)g->core->cpu`; IE/IF/IME via the existing
  `gbacore_read16(g, 0x04000200/0x04000202/0x04000208)` (gbacore.c:1152 — bus reads, exactly
  the melonDS columns, EmuThread.cpp:525-528); stack: if `(sp >> 24) == 0x02 || == 0x03`
  (EWRAM/IWRAM — mirrors PM's "must point into main RAM" guard, mp_bridge.cpp:1157), read 32
  words `gbacore_read32(g, (sp & ~3u) + 4*i)` for i in [-8..23] (8 words below SP, 24 above —
  callee frames sit above), else `stackValid=0`.
- **Race disclosure (comment in the code):** the dump runs on the render thread while the
  wedged worker may still be executing; register values are a sampled instant, not a stopped
  core (PM's samplers accept the same). For a truly parked worker (collect) the values are
  stable. This is forensics, not a debugger.

### D2.5 — Dump format + file

Formatter `int diag_hang_format(char* buf, size_t cap, const GbaCpuDump* d, uint32_t vf,
uint32_t vbl, int kind /*DIAG_HANG_DUMP_**/, uint32_t nowMs, uint32_t netCrumb, uint32_t sioCrumb)`
in diag.c (pure). Output (one dump ≈ 12 lines, `%08X` words):

```
HANG kind=<CORE|GAME> ms=<nowMs> vf=<vf> vbl=<vbl> netC=<crumb> sioC=<crumb>
R0  xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx   (r0-r3)
R4  ... R8 ... R12 SP LR PC lines ...
CPSR xxxxxxxx SPSR xxxxxxxx
BANK r13: none=.. fiq=.. irq=.. svc=.. abt=.. und=..
BANK r14: ... / BANK spsr: ...
IE xxxx IF xxxx IME xxxx
STACK sp=xxxxxxxx valid=1
  xxxxxxxx*8   (4 lines x 8 words, or "STACK sp=... valid=0")
```

File: **`sdmc:/cias/netlogs/3DGBA_hang_<HOST|JOIN>_<MMDD>_<HHMMSS>.txt`** (S.2 helper,
`kind="hang"`), lazily opened at the first dump, append + `fflush` per dump, closed at
teardown. PC/LR resolve offline against the pret sym maps ("the exact hang site, resolvable
against the linker map" — EmuThread.cpp:531 comment, pm-bridge-forensics.md §12).

### D2.6 — Files/functions to touch

| File | Change |
|---|---|
| `source/gamestate.h` | `GameProfile` += `uint32_t vblankCtr;` (with the D2.1 citation comment) |
| `source/gamestate.c` | 3 PROFILES rows += the D2.1 addresses (comment: derivation + verify-on-hw-pending) |
| `source/gbacore.{c,h}` | `gbacore_dump_cpu` (D2.4) |
| `source/diag.{c,h}` | `DiagHang`, `diag_hang_step`, `diag_hang_format`, `GbaCpuDump` |
| `source/main.c` | sampler-block glue: arm/collect/trigger/write (D2.2-D2.3), file lifecycle |

### D2.7 — Host-side tests

In `test/host/test_diag.c`:
1. `diag_hang_step`: armed + static vf for 180 ticks → `DUMP_CORE` on tick 181; advancing vf +
   static vbl → `DUMP_GAME`; disarmed → never; 24-cap honored; counter advance resets cap +
   frozenN (re-arm); alternating advance/freeze below threshold → never.
2. `diag_hang_format` golden string from a synthetic `GbaCpuDump` (incl. `stackValid=0` branch)
   + cap truncation safety.
3. (PC-only) a compile-time `_Static_assert(sizeof(GbaCpuDump) == expected)` guard so the diag.h
   mirror can't silently drift.

---

## D3 — Per-frame CSV telemetry

Model: melonDS 31-column schema (pm-bridge-forensics.md §11, EmuThread.cpp:535-553), DeSmuME
apCsv flush-256 (§3, mp_bridge.cpp:1544-1559), the ROM diag-block-as-columns idea
(pm-rom-abi.md §6), port item 4 ("log own-export AND peer-import game state side by side, plus
the link layer's counters as CSV columns every frame — not occasional # celio lines").

### D3.1 — Where the numbers already live (grep-verified sources)

**`# celio` header line** (gbacore.c:896-899) is fed by worker-captured statics
(gbacore.c:582-588, filled by `net_celio_capture` gbacore.c:593-601 + gap/gate sites):
`s_celioSection, s_celioState, s_celioBlk, s_celioFrames, s_celioPartyB, s_celioTradeC,
s_celioGateN (gbacore.c:943), s_celioResetN (:610), s_celioForceN (:984), s_celioSioMode,
s_celioSiocnt (:599-600), s_celioExitP, s_celioSessEnd, s_celioPCard, s_celioIdReal (:597-598)`.
The richer per-FSM fields (holds, select slots, out-queue depth) live in `ClStatus`
(celiolink.h:208-229, filled by `cl_get_status` celiolink.c:1226-1246) — captured on the worker
in `net_celio_capture`, which currently keeps only a subset.

**`# rate` line** (gbacore.c:893-894) is *computed at dump time* from the `s_netLog` ring
(gbacore.c:882-895) — not a live counter. The live per-round facts are the ring tail
(`s_netLog[]`, `s_netLogN`, gbacore.c:438-441) and the driver counters:
`s_netStartN, s_netInjectN, s_netOkN, s_netToN (gbacore.c:404), s_netEdgeN, s_netForceN (:405),
s_netFinishN (:446), s_netRound (:403), s_netPWord/s_netCWord (:406), s_netRxP/s_netRxC (:409),
s_peakSent*/s_peakRx* (:413-414), s_netVblMax (:419), s_netPaceBlkN (:420)`.

**Transport counters** (netlink.c, already exported):
`net_link_get_rtt(&rtt,&drops)` (netlink.c:904-907), `net_link_get_stats(&rxWordN,&txFails,
&busyN,&peerUp,&maxSeat0)` (:914-920), `net_event_get_stats(&txSeq,&txAcked,&rxDelivered,
&overflow,&retransmits)` (:641-656, declared netlink.h:139). Missing from any export: the
EVENT-channel **outbound queue depth** (`tx->next - tx->base`, the run-#6 overflow X-ray,
netlink.c:699) — D3.3 adds it.

### D3.2 — New read-only counters export (resolves the charter's "export" question)

The charter suggested "a const pointer to a counters struct in celiolink.h". Resolution after
inspection: **celiolink.h needs NO new export** — `cl_get_status`/`ClStatus` already expose the
FSM read-only, and the live `CelioLink` lives inside the worker-owned `NetDriver`
(gbacore.c:78); handing the render thread a const pointer into it would race on multi-byte
fields mid-`cl_transfer`. The safe seam ALREADY EXISTS: `net_celio_capture` runs on the worker
at every transfer (gbacore.c:633, :998) and snapshots into statics the render thread reads
(exactly how the HUD reads `gbacore_net_diag` today). So:

- **Extend `net_celio_capture`** to store the FULL `ClStatus` into a new
  `static ClStatus s_celioStatus;` (plus one new int `s_celioOutQ` =
  `(cl->outTail - cl->outHead + CL_EVENT_QUEUE_DEPTH) % CL_EVENT_QUEUE_DEPTH`, computed
  worker-side) alongside the existing s_celio* ints (which stay — the netlog header keeps
  using them; FROZEN path untouched: capture is LOGGING ONLY, gbacore.c:581).
- **New export in gbacore.{c,h}** (mGBA-free, plain stdint — modeled on `gbacore_net_diag`
  gbacore.c:779-793):

  ```c
  // gbacore.h
  typedef struct {
      // celio FSM (worker-captured snapshot; single-word reads, benign race)
      int clSection, clState, clBlk;  unsigned clFrames, clPartyBytes;
      int clTradeC, clHeldParty, clHeldSel, clHeldConf, clSelLocal, clSelPeer;
      int clExitP, clSessEnd, clPCard, clIdReal, clOutQ;
      int celioGateN, celioForceN, celioResetN, celioSioMode; unsigned celioSiocnt;
      // SIO driver
      int startN, injectN, finishN, okN, toN, edgeN, forceN, paceBlkN;
      unsigned round, vblMax;
      unsigned pWord, cWord, rxP, rxC, peakSentP, peakSentC, peakRxP, peakRxC;
      // netlog ring tail (per-round facts without waiting for the dump)
      unsigned logN, lastRound, lastW0, lastW1; int lastOk;
  } GbaNetCounters;
  void gbacore_net_counters(GbaNetCounters* out);
  ```

  Pure copies of statics — no logic change, no lock (each field is a single aligned word;
  cross-field tearing is acceptable for telemetry and disclosed in the comment).
- **New export in netlink.{c,h}**:

  ```c
  int net_event_get_queue(void);   // max (next - base) across seats = un-acked outbound events
  ```

  (takes `s_evLock` like `net_event_get_stats`, netlink.c:641-656 — called at ~60 Hz from the
  render thread, negligible.)

### D3.3 — The exact CSV column list (52 columns, one row per render frame)

Header line 1 is a `#` comment naming build + role (mirrors the netlog header discipline,
gbacore.c:838); line 2 is the column header. Sources per column in parentheses.

```
# 3DGBA csv role=<HOST|JOIN> seat=<n> built=<__DATE__ __TIME__>
tms,rf,exp,
ctx,cb2,px,py,mapg,mapn,objx,objy,face,sb1,lstat,lerr,lnrecv,lbuf0,lbuf1,vbl,
clSec,clSt,clBlk,clFrm,clPB,clTC,clHP,clHS,clHC,clSelL,clSelP,clExitP,clSessEnd,clPCard,clIdReal,clOutQ,gateN,cForceN,resetN,sioMode,siocnt,
startN,injN,finN,okN,toN,edgeN,forceN,round,lastW0,lastW1,lastOk,
rtt,txSeq,txAcked,rxDel,evOvf,evRetx,evTxQ,rxWordN,txFails,busyN,peerUp
```

| Group | Columns | Source variable (file:line) |
|---|---|---|
| meta | `tms` wall ms (`nowMs`, main.c:1408); `rf` = `g_renderSeq` (D1.3); `exp` = `gbacore_net_get_exp()` (gbacore.c:827) | |
| own game (the participant core's `GameState` — the SAME `game_read` result the gs-logger block already computes at main.c:1559-1579; do NOT re-read) | `ctx` (gs.ctx int), `cb2` hex (gs.cb2), `px,py` (gs.px/py), `mapg,mapn` (gs.mapGroup/mapNum), `objx,objy,face` (gs.objX/objY/facing), `sb1` (gs.sb1Valid), `lstat` hex (gs.linkStatus), `lerr` (gs.linkErr), `lnrecv` hex (gs.linkNotRecv), `lbuf0,lbuf1` hex (gs.linkErrBuf0/1) — all from gamestate.h:79-114 | gamestate.h GameState |
| game heartbeat | `vbl` = `gbacore_read32(core, prof->vblankCtr)` (the D2.1 address — the CSV is its hw verification) | D2.1 |
| celio FSM (peer-presence: `clPB/clSelP/clPCard` ARE the peer's state as locally known — over wireless no fuller peer view exists on this console; the mechanical HOST-vs-JOIN file diff is the two-sided view, PM model §11) | `clSec..clOutQ,gateN,cForceN,resetN,sioMode,siocnt` | `gbacore_net_counters` (D3.2) ← s_celioStatus / s_celio* (gbacore.c:582-601) |
| SIO driver | `startN,injN,finN,okN,toN,edgeN,forceN,round,lastW0,lastW1,lastOk` | `gbacore_net_counters` ← gbacore.c:403-446 statics + ring tail |
| transport | `rtt` (`net_link_get_rtt`), `txSeq,txAcked,rxDel,evOvf,evRetx` (`net_event_get_stats`), `evTxQ` (`net_event_get_queue`, D3.2), `rxWordN,txFails,busyN,peerUp` (`net_link_get_stats`) | netlink.c:904-925, 641-656 |

Format rules (binding for the formatter): decimal for counters; `%X` hex (no 0x) for
`cb2,lstat,lnrecv,lbuf0,lbuf1,siocnt,lastW0,lastW1`; -1 where a source reports "none";
booleans 0/1. One row ≈ 200-260 bytes → ~15 KB/s at 60 fps, ~10-18 MB for a typical 10-20 min
run (fine for SD; decimation is an Open Question).

### D3.4 — Row cadence + hook point

One row per render-loop iteration while armed (same arming as D2.2: `wlOn`, participant core),
emitted from the gs-logger block (main.c:1559-1579) where the `GameState` for the participant
is already in hand (`gst` or `gsb` — pick by `g_netWorker == &emuA ? …` and `swapped`, same
mapping as main.c:1562-1563). Menu-open frames produce no row (the gs block is non-menu-branch
only) — a gap in `rf` in the file IS the "menu was open" marker; disclosed in the `#` header.

### D3.5 — File naming, open-once, flush cadence

- **`sdmc:/cias/netlogs/3DGBA_csv_<HOST|JOIN>_<MMDD>_<HHMMSS>.csv`** (S.2 helper,
  `kind="csv"`, ext `csv`) — one file per run per seat; successive runs never overwrite
  (matches `wl_dump`'s timestamp rationale, main.c:92-99).
- **Opened ONCE** at wireless link start (main.c:1812, next to `wlOn = true; wlSeat = seat;`)
  — never on the frame path (PHASE invariant 2). `setvbuf(f, NULL, _IOFBF, 8192)` so a row is
  a buffered memcpy; **`fflush` every 256 rows** (PM: "Flush every 256 frames",
  mp_bridge.cpp:1558; a hard crash loses ≤256 frames ≈ 4 s — acceptable, the wd/hang files
  carry the crash instant). Header written at open. `fclose` at every wl teardown site
  (main.c:1594-1598, 1787-1791, 2156-2162 — alongside `wl_dump`/`gs_dump`).
- No heap: the row is formatted into a `static char[512]` via the pure formatter, then one
  `fwrite`.

### D3.6 — Pure formatter (host-tested)

```c
// diag.h — plain struct mirroring the D3.3 columns; filled by main.c glue from the sources above
typedef struct { /* uint32_t tms, rf; int exp; ... one field per column ... */ } DiagCsvRow;
int diag_csv_header(char* buf, size_t cap, int seat);         // both header lines
int diag_csv_row(char* buf, size_t cap, const DiagCsvRow* r); // one '\n'-terminated row
```

### D3.7 — Loopback (`netOn`) note

The two-core one-console loopback also exercises the driver; D3 keys everything off the
wireless session and stays dormant under `netOn` in v1 (both cores would need two files/rows).
Enabling it is a one-line arming change; deferred (Open Questions).

### D3.8 — Host-side tests

In `test/host/test_diag.c`:
1. `diag_csv_header`/`diag_csv_row` golden strings (fixed synthetic row → exact bytes; hex
   columns hex, -1s propagated, field COUNT in the header equals fields in the row —
   count-commas assert so a future column add can't desync header vs row).
2. Truncation: cap smaller than a row → return <0 (or truncated len per chosen contract) and
   never overflow.
3. A tiny "diff smoke": two synthetic rows differing in one column → byte diff localizes to
   that column (guards the mechanical-diff promise).

---

## Implementation order + verification gates (per slice — PHASE invariants 3/5/7)

1. **S.1-S.4 + D1** → `make` green (`3DGBA.3dsx`), celiolink suite 417 PASS unchanged, netlink
   host suite 66 PASS unchanged, new `test_diag` ≥ ~12 checks → BUILDLOG entry.
2. **D2** (gamestate profile field + gbacore_dump_cpu + catcher) → same gates, test_diag grows
   (≥ ~10 more) → BUILDLOG entry.
3. **D3** (capture extension + exports + writer) → same gates + a manual Azahar boot proving a
   csv/wd file appears and the frame budget is unharmed → BUILDLOG entry; add the three new
   artifacts (`3DGBA_wd_*`, `3DGBA_hang_*`, `3DGBA_csv_*`) to the HANDOFF run-#13 checklist's
   "copy all netlogs after the run" step.

Every new game-RAM address and borrowed design carries its citation comment (PHASE invariant
6; house style per the D2.1/D2.4 tables). Reminder: mGBA files are never modified (MPL;
`gbacore_dump_cpu` only READS `cpu->…` through the public headers).

---

## Open Questions (implementer must decide / user may rule)

1. **Watchdog blind spot (D1.8):** keep the render-thread sampler per PHASE.md, or add a tiny
   detached sampler thread (PM's actual design) so teardown-wait wedges (D1.1 sites 12-14) also
   produce STUCK lines? A thread on core 1 (audio's core) is feasible but touches the
   thread-budget conventions (HANDOFF Gotchas: 3 usable lanes).
2. **Main-loop-alive-but-stuck wedges (D2.1 limitation):** vblankCounter1 misses a game
   spinning inside a callback with IRQs alive. Add a third watched counter (SaveBlock2
   playtime-frames — main-loop incremented — needs a NEW sb2 pointer profile field + pret
   verification), or accept that D3 (static cb2 + pinned celio section) covers it?
3. **pret re-verification:** `/tmp/pret/*` and `/tmp/EMERALD.sym`/`FIRERED.sym` are gone from
   this machine. Re-clone to independently confirm `struct Main`+0x20 for BPEE/BPRE **rev1**
   (user's FR is rev1 — memory `3dgba-run12-state`; gMain is IWRAM and matched rev0/rev1 for
   mainCb2/newKeys, but confirm), or ship on the two-anchor derivation + CSV self-verification?
4. **CSV size/cadence:** per-render-frame rows (~15 KB/s) vs on-change + 1 Hz heartbeat
   decimation. Spec'd per-frame (PM-faithful, runs are short); flip if run-#13 logs prove
   unwieldy.
5. **`GbaNetCounters` tearing:** accepted single-word-read races are disclosed; if a reviewer
   wants stronger guarantees, a seqlock around `net_celio_capture` is additive — but it adds
   worker-side cost to the FROZEN path's logging call and is probably not worth it.
6. **D3 under loopback `netOn` (D3.7):** enable now (two rows/files) or after run #13?
7. **wd/hang file lifecycle when a session never wedges:** spec'd lazy-create (no empty files).
   If the operator prefers "file exists = watchdog was armed" as a positive control, switch to
   eager-create + a single `ARMED` line at session start.
