# KNOWN DIFFERENCES — where 3DGBA's Celio port deliberately diverges

Seeded 2026-08-03 (phase-13-prep slice **D7e**, `docs/phase13-diagnostics/SPEC-suite-hardening.md`
§D7e). Discipline imported from gen1recomp's parity ledger (`docs/kb/external/gen1-parity.md` §3,
§13).

## What this file is — and the two files it is NOT

A port of somebody else's protocol implementation has exactly three kinds of code, and mixing them
is how a port rots into "it works, don't touch it":

| Kind | Where it lives |
|---|---|
| **Faithfully ported** — we do what Celio/pret does, with citations | `docs/kb/celio/PORT-SPEC.md` + the 2026-07-06 source audit in `docs/kb/celio/audit/` |
| **Known differences** — we deliberately do something else | **this file** |
| **New features** — no upstream analog at all (UDS transport, the 2-console event plane, the D1-D7 diagnostics) | `docs/phase13-diagnostics/`, `docs/kb/wireless-strategy.md`, the source headers |

Celio-Link's dongle terminates the link for **one** game against a **preloaded party file**. We
terminate for **two live consoles** joined by a lossy radio. Every entry below exists because of
that gap, or because real New-3DS hardware said so across twelve runs.

## Rules (they are the point of the file)

1. **Every entry names both sides with citations**: what Celio/pret does (file:line or a kb
   digest) and what we do (file:line), plus WHY the divergence exists.
2. **Every entry carries the literal label "intentional divergence"** — if it is not intentional,
   it is a bug and belongs in the HANDOFF's open-bug list, not here.
3. **Status is one of** `hw-validated (run #N)` / `PC-only (TEST N)` / `unmeasured`.
   **"unmeasured" means remaining work, not known-good** (gen1-parity.md §1: the delta column is
   only honest if it distinguishes "checked and equal" from "never checked").
4. **Entries are never deleted; status only moves forward.** A ledger you can empty is a ledger
   that rots into a permanent excuse (gen1-parity.md §13, the DEBT ratchet).
5. **Coupling rule (binds D6).** Any change to the FSM's wire-visible behavior takes THREE things
   in ONE commit: the entry here, a `CL_PROTO_REV` bump (`source/celiolink.h`), and a re-pinned
   fingerprint golden (`test/test_celiolink.c` TEST 16). Moving the fingerprint breaks linking
   between every existing build and every new one — that IS a parity change.
6. **Status upgrades are evidence-driven.** After a hardware run, `tools/verdict.sh <run-folder>`
   prints the PASS/FAIL/UNKNOWN per checklist item; copy its PASS lines into the Status column.
   An UNKNOWN never upgrades anything (the run-#11 lost-log lesson).

---

## The ledger

### 1. Proactive room heartbeat — the partner sends key frames UNPROMPTED
**Intentional divergence.**
- **Celio/pret:** Celio's dongle is the SIO partner of a real game and answers what it is asked;
  a Gen-3 SLAVE game self-heartbeats through its own echo. pret `link.c:1793`: a whole-idle frame
  is `RECEIVED_NOTHING`, and with `IsSendingKeysOverCable` the game **skips the entire frame** —
  so a frozen slave can never send the frame that would wake it (audit: `docs/kb/celio/audit/`).
- **3DGBA:** the `roomKeys` flag (`source/celiolink.h:273`) arms an unconditional idle key-frame
  heartbeat while the room phase is live (`source/celiolink.c:754-757`, `:791-796`, LOUNGE
  `:877-880`).
- **Why:** runs #4/#5 were a chicken-and-egg — a reactive keepalive can never start, because the
  frozen slave is the one who would have to speak first. The MASTER partner *is* the slave's
  heartbeat.
- **Status:** `hw-validated (run #6: JOIN room live key exchange; runs #8-#10 completed trades)`.
  PC gate: TEST 10 (`test/test_celiolink.c:1237`).

### 2. Whole-idle holds, only on data-exchange waits
**Intentional divergence.**
- **Celio/pret:** Celio synthesizes from a ONE-SHOT uploaded party file and never waits on a live
  peer at all (`docs/kb/celio/trade-fsm.md`; port note `source/celiolink.h` CL_EV_* block).
- **3DGBA:** the M1 hold gates (`partnerPartyHeld` / `selectHeld` / `confirmHeld`,
  `source/celiolink.h:232-234`, surfaced in `ClStatus` `:336-338`) answer EMPTY frames and refuse
  to advance until the peer's event lands — and the holds are **LEVEL-triggered every idle frame**,
  not edge-triggered (the run-#8 fix; a Gen-3 game sends its select/confirm exactly once).
- **Why:** the partner must never offer canned filler where the peer's real party/slot belongs.
  The held state is the game's own "waiting for your friend" screen, which has no link timeout.
- **Status:** `hw-validated (run #2 proved a whole-idle hold benign on hw; runs #8-#10 trades)`.
  PC gates: TEST 5 (`:1073`, now also under the D7a radio-lag sweep), TEST 12 (`:1352`).

### 3. Wedge-escape ceiling on the edge-strict capture gate
**Intentional divergence.**
- **Celio/pret:** no analog — the gate is a 3DGBA/mGBA construct (Celio talks to real silicon over
  a real PIO, with no emulated-ISR ordering to prove).
- **3DGBA:** `source/gbacore.c:1101-1114` — the state-F edge gate escapes when the SIO ISR
  *cannot* run (IE.SIO masked, `gba.c:585`) or after `NET_CELIO_GATE_CEIL` = 60000 cycles
  (`:338`); each escape bumps `s_celioForceN` (`:595`), reported as `forceN=` in the `# celio`
  netlog header (`:1003`).
- **Why:** the run-#4/#6 WEDGE — a gate with no escape parks the clock forever, producing a silent
  freeze in which even `CB2_LinkError` cannot render.
- **Status:** `hw-validated (run #6: forceN=21 with a healthy session)`. `tools/verdict.sh` grades
  a nonzero `forceN` as WARN, not FAIL, on that precedent.

### 4. Over-drain master-clock pacing (WORD 350 µs / FRAME 2500 µs)
**Intentional divergence.**
- **Celio:** `packetLayer.hpp:51-53` = 30097 / 1378 / 12953 µs in emulated cycles
  (`docs/kb/celio/packetLayer.md:351-359`); the real-master nominal is ≈ 752 / 7637 µs.
- **3DGBA:** `CL_TIMING_HANDSHAKE_US` 30097 / `CL_TIMING_WORD_US` 350 / `CL_TIMING_FRAME_US` 2500
  (`source/celiolink.h:166-169`), consumed by `cl_next_delay_us` (`source/celiolink.c:1036-1041`).
- **Why:** our per-word ISR-ack wait adds the rest of the nominal gap, so the *effective* rate lands
  ≈1.2-1.5× nominal. Run #6 proved the asymmetry: **under-drain is fatal** (the send queue filled
  at 1.0× and drained at 0.5×, 98 frames of key 0x1C = HANDLE_SEND_QUEUE, then a FireRed link
  error), while **over-drain is absorbed** by the game's own catch-up, since the heartbeat keeps
  the extra bursts nonzero.
- **Status:** `hw-validated (run #7+: the send-queue overflow never returned)`. PC gate: TEST 7
  (`test/test_celiolink.c:1140`) pins the constants.

### 5. LOUNGE is a full room re-establishment, not a passive lounge
**Intentional divergence.**
- **Celio:** `tradeLounge.cpp` is a thin idle section (`docs/kb/celio/trade-fsm.md`) — enough when
  the dongle's game is the link master.
- **3DGBA:** `source/celiolink.c:863-900` answers `READY_EXIT_STANDBY`, re-ships LinkPlayer, and
  gates the heartbeat on `roomKeys` (cleared on the CONNECTION→LOUNGE edge, `:649`).
- **Why:** run #10 — the JOIN console's game is the SLAVE, and a slave that re-enters the room
  without the LinkPlayer/standby exchange black-screens. The HOST (master game) free-runs and
  masked it, the same slave-gating asymmetry as entries 1 and 2.
- **Status:** `partially hw-validated` — re-entry proven (run #12); the EXIT half is entry 6.
  PC gate: TEST 13 (`test/test_celiolink.c:1306`).

### 6. Exit-fade silence + 5FFF session end + `CL_EV_EXIT_ROOM` relay (both games leave together)
**Intentional divergence.**
- **pret:** `link.c:1319` — `LinkCB_ReadyCloseLink` is hard-gated on `gLastRecvQueueCount == 0`; a
  real exiting partner simply goes silent. Celio has no cross-console "both leave" concept at all
  (there is only one game).
- **3DGBA:** `exitPending` / `exitZeroRun` / `exitEmitted` / `sessionEnded`
  (`source/celiolink.h:278-282`, `CL_EXIT_ZERO_RUN` `:253`), the heartbeat silencer
  (`source/celiolink.c:441-447`), and `CL_EV_EXIT_ROOM` (`source/celiolink.h:132`) relayed to the
  peer console.
- **Why:** run #12's JOIN black screen — the always-on heartbeat held the game's close-gate shut
  forever; and with two consoles, one player walking out must take the other with them.
- **Status:** **`PC-only (TEST 14, test/test_celiolink.c:1440) — awaiting HW run #13`**
  (checklist item 13c; `tools/verdict.sh` grades it from `exitP=`/`sessEnd=`).

### 7. The CRC word after a fresh handshake gets the frame gap, not Celio's word gap
**Intentional divergence.**
- **Celio:** the PacketLayer's per-word timing distinguishes the post-handshake CRC word
  (`docs/kb/celio/packetLayer.md:98-101`, ~1.4 ms).
- **3DGBA:** `source/celiolink.h:411` (the comment says it outright: "Divergence from Celio … the
  CRC word after a fresh handshake gets the frame gap") — ~13 ms, i.e. slower.
- **Why:** slower is safe here; the game is waiting, not timing out. Not worth a special case.
- **Status:** `unmeasured` — never isolated on hardware. **Remaining work, not known-good.**

### 8. MASTER partner escalates to driving 0x8FFF; re-handshake detector re-arms the layer
**Intentional divergence.**
- **Celio:** the EMU master drives per its own thread timing and never misses the link start, so
  there is no escalate counter and no re-arm-on-run detector.
- **3DGBA:** `CL_HS_ESCALATE_AFTER` 8 / `CL_HS_REARM_RUN` 4 (`source/celiolink.h:163-164`),
  implemented at `source/celiolink.c:1056-1066` (re-arm) and `:1086-1093` (escalate).
- **Why:** run #2/#3 — an emulated game "waiting for connection" writes 0x0000 and will not
  advertise until it SEES a master, so the partner must drive 0x8FFF (deadlock breaker); and the
  game tears the link down and re-handshakes at each cable-club phase boundary (sitting at the
  trade machine), which our single persistent PacketLayer must notice.
- **Status:** `hw-validated (runs #2/#3 fixes; every later trade)`. PC gates: TESTs 6a/6b/6c
  (`test/test_celiolink.c:1098` / `:1118` / `:1190`).

### 9. Capture-under-hold pipeline (pull the local party DURING the hold; blockSeq-derived windows)
**Intentional divergence.**
- **Celio:** the party is preloaded from a file — there is nothing to capture, and no reason to
  pull while holding.
- **3DGBA:** `cl_capture_feed_init` / `cl_capture_feed_cont` (`source/celiolink.c:312-369`), the
  window derived from `blockSeq` (PARTY0→0 / PARTY1→200 / else 400), per-chunk serve holds, and a
  restored per-block `reqDelay` so the pull runs under the hold.
- **Why:** run #6's `txSeq=0` mutual deadlock — both consoles held while suppressing the very pull
  that would have made their own game stream its party. Run #7 then exposed re-stream mis-filing
  (a held game re-streams the SAME stage; a "lowest unemitted window" rule filed duplicates as
  later chunks), hence the blockSeq-derived window.
- **Status:** `hw-validated (run #8: both parties crossed; runs #9/#10 completed trades)`. PC gate:
  TEST 12 (`test/test_celiolink.c:1352`, incl. the 12b2 re-stream case).

### 10. Identity latch + real trainer-card serve, with a version-consistency guard
**Intentional divergence.**
- **Celio:** the partner's identity IS canned — its use case has no live peer.
- **3DGBA:** `identityLatched` / `shippedIdentity` / `partnerCard`
  (`source/celiolink.h:290-299`), fed by `CL_EV_TRAINERCARD`; the canned card is a **well-formed**
  fallback (`cl_make_trainer_card`), never the all-zero card that black-screened run #12.
- **Why:** `CheckLinkPlayersMatchSaved` freezes the partner identity for a club visit, so the
  first-shipped block must be latched and cannot be swapped mid-visit; and the receiver parses the
  card by the identity's VERSION field, so a real card may only be served alongside a real identity.
- **Status:** identity latch `hw-validated (runs #9-#12)`; the card path is
  **`PC-only (TEST 15, test/test_celiolink.c:1549) — awaiting HW run #13`** (checklist item 13b —
  and note the RENDER itself is not log-provable, so `tools/verdict.sh` prints `needs-eyes`).

---

## Open items this ledger is watching

| # | Item | Where it will be settled |
|---|---|---|
| 6 | room-exit close | HW run #13 item (c) — `verdict.sh` 13c |
| 10 | trainer-card serve | HW run #13 item (b) — `verdict.sh` 13b + eyes on the card |
| 7 | post-handshake CRC word timing | never isolated; would need a timing-instrumented run |
| — | mid-link `KEY_Y` state changes are not re-exchanged (D6 `modeFlags` is fixed at lobby time) | forbidden by convention today (HANDOFF run-#11 note); revisit if the A-E fallback ever becomes a lobby choice |
| — | 3-4 player seats | Celio and this port are both strictly 2-seat (`CL_MASTER`/`CL_SLAVE`); HANDOFF Next steps #3 |
| — | battle synthesis | Celio has none; battles belong to the input-sync tier (`docs/kb/wireless-strategy.md`) |
