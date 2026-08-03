# Phase 13-prep — Diagnostics & Reliability Infrastructure

Decided 2026-08-03 after the external-projects teardown (`docs/kb/external-projects-teardown.md`
§Round 2). Goal: stop debugging hardware runs blind. Twelve runs root-caused bugs at a cost of
one whole run (sometimes several) per defect; the Project PM forks prove a diagnostics layer
that finds the same class of defect in ONE run. Build it cleanly and completely, then run #13.

## Scope — IN

| # | System | Source model (cites in docs/kb/external/) |
|---|---|---|
| D1 | Breadcrumb globals + watchdog: numbered site IDs stamped in every blocking/retry loop of netlink/celiolink; per-frame loop-seq; the RENDER thread samples ~200ms and appends `STUCK` lines (1s/4s/12s) to sdmc netlogs | pm-bridge-forensics.md (watchdog + crumbs) |
| D2 | Game-heartbeat hang catcher: watch the game's own advancing frame counter during a link session; frozen >180 frames → auto-dump mGBA ARM regs (PC/LR/SP/CPSR, banked) + GBA IE/IF/IME + 32-word stack to sdmc | pm-bridge-forensics.md (hang catcher) |
| D3 | Per-frame CSV telemetry: own + peer game state + celio/netlink counters as COLUMNS (section, gateN, forceN, resetN, txSeq/txAcked, rxDelivered, sioMode/siocnt, queue depths); 256-frame flush; host/join CSVs diff mechanically | pm-bridge-forensics.md (CSV), pm-rom-abi.md (diag page) |
| D4 | File-driven tile-exact movement: `sdmc:/cias/control/move_p1.txt` / `move_p2.txt`, grammar `L1 U20 R3 D2` closed-loop on gamestate coords (hold until the live tile hits target), wall timeout, bare-dir = tap, buttons, `W<n>` wait, leading `!` abort; consumed on pickup; go-file one-shots | pm-bridge-forensics.md (move/go files) |
| D5 | Input record/replay: on-change key masks anchored at overworld entry, record to sdmc; replay armed by a control file; never overrides live input while recording | pm-bridge-forensics.md (patterns 10/11) |
| D6 | Link-surface fingerprint at UDS connect: game code + revision + protocol/FSM revs + mode flags; mismatch = loud lobby-stage warning + netlog line (narrow surface — over-hashing rejects compatible peers, gen1recomp #511) | gen1-link.md (Fingerprint), gen1-parity.md (golden+mutation) |
| D7 | PC-suite hardening: laggyPair delay shim (~23ms UDS model) on the two-instance tests; per-round hash agreement across both synthesized sides; golden-trace replay harness fed from archived hw netlogs; post-run netlog→verdict grep tool; `KNOWN-DIFFERENCES.md` ledger for the celio port | gen1-parity.md |

## Scope — OUT (deliberate)

- **Tier-D async-stretch + pre-push** — touches the SIO/transport next to the working trade
  path; frozen until HW run #13 rules on the built `.cia`. Design banked in melonds-async.md.
- **mGBA write-watch tripwire** — requires patching vendored mGBA (`external/`, not in this
  repo, MPL patch-tracking obligations). Deferred to its own phase.
- **Tier-3 two-core PC lab, presence overlay M0+** — next phases, after #13.

## Invariants (every implementer)

1. **The trade path is FROZEN.** No semantic change to celiolink FSM transitions, pacing
   constants, netlink round/ack logic, or gbacore SIO fill. Diagnostics are additive:
   reads, logs, counters, and flag-gated input injection at the existing injection point.
2. **Flag-gated, default-off** for anything active (D4/D5 injection, D2 dumps armed only
   in-session); passive logging may default on but must not add per-frame heap churn or
   fopen on the hot path (open once, buffered, flush on cadence).
3. **Suites stay green and GROW**: `clang -std=c11 -Wall -Wextra -O0 -g -I source
   test/test_celiolink.c -o /tmp/t && /tmp/t` (417+) and the netlink host test (66+).
   New pure-C logic (script parser, fingerprint, CSV formatting) gets host-side checks.
4. **Pure-C rule** (CLAUDE.md #4): parsers/formatters header-free, dual-compiled on PC.
5. **Build proof**: `make` must produce `3DGBA.3dsx` cleanly after every slice
   (`export DEVKITPRO=/opt/devkitpro DEVKITARM=$DEVKITPRO/devkitARM`).
6. **Citations in comments** for every game-RAM address (pret symbol map + kb doc) and
   every borrowed design (the external/ teardown file).
7. **Bank progress**: append a dated entry to `docs/phase13-diagnostics/BUILDLOG.md` after
   each slice (what landed, files, check counts) — a killed session must lose nothing.
8. Hardware-final: nothing here is "done" until run #13+ exercises it; PC green + build
   green is this phase's exit gate, and the HANDOFF run-#13 checklist gains the new tools.
