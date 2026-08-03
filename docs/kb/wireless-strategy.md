# Wireless strategy — the three tiers (reflection checkpoint, 2026-06-30)

> Written at the user's "stop and reflect" checkpoint, after HW run #1 of the Celio local-termination
> path (state F) and before run #2. Goal restated by the user: **clean, seamless, stable wireless play
> between GBA games — Pokémon now, but ultimately ALL games (Kirby, Sonic, Mario, …), with online play
> later.** This doc fixes the strategic architecture so per-session work always lands inside it.

## Where the evidence stands

- **State D (universal word relay):** works for ANY game, proven to complete a Gen-3 trade on hardware —
  but physically capped at ~4–5 fps (radio RTT ~21 ms × 9 transfers/emulated-VBlank; proven by the
  A/B/C sweep + turnus/pureNetRtt instrumentation; no software tuning escapes it). KEY_Y fallback.
- **State F (Celio local termination, Gen-3 trade):** run #1 proved the architecture on hardware — the
  synthesized partner establishes, walks the cable-club room, sits at the trade machine. Two precise
  bugs found (JOIN handshake deadlock; no re-handshake at the room→trade phase boundary), both fixed +
  PC-verified (test/test_celiolink.c, 176 checks incl. TEST 6a/6b). Run #2 pending.
- **In-process dual link (the product core):** two GBA games at 60 fps on one New 3DS, linked in-process
  by the lockstep coordinator — long proven. This is the load-bearing asset for tier 3 below.

## The three tiers

### Tier 1 — Per-protocol LOCAL TERMINATION (state F) — Pokémon, ✅ TRADE WORKS (hw-confirmed run #10)
Each console's SIO driver *is* the link partner (Celio's dongle model): answers every transfer locally,
ships only semantic events (party block ~600 B, select/confirm) over the reliable event channel
(`net_event_*`). Full speed for the local game; latency-invisible (waits happen on the game's own
"waiting for friend" screens). **Per-protocol by nature** (Celio README: "not game-agnostic"; their
Advance Wars needed its own `awProtocol` module). Scope: Gen-3 trade first; battle/record-mix sections
are possible follow-ups on the same FSM infra, but see tier 3 — input-sync may deliver battles cheaper.
Long-term this tier is also the right model for **online trades with strangers** (no shared ROMs, tiny
data, server-relay friendly — Celio-Server's exact use case).

**Two known limitations of the local-termination model (deferred — user-flagged 2026-07-06):**
- **The room "other player" is FAKED, not real.** Because the partner is synthesized locally, the room
  shows a canned LinkPlayer identity (wrong trainer NAME) + a scripted walk. The real Celio online demo
  shows the REAL remote player. Root cause: `cl_ship_linkplayer` prefers the peer's real block but the
  first ship (room SETUP) predates the peer identity crossing UDS → it latches a canned block (frozen
  per club visit via `CheckLinkPlayersMatchSaved`, so it can't swap mid-visit). **Fix:** eagerly cross
  the real LinkPlayer identity at connect-time, before room setup latches, → correct names in the room
  AND the trade menu. Do this only once the trade flow (incl. the room exit) is fully clean.
- **2 players only.** Celio's code (and our port) is strictly 2-seat (`CL_MASTER`/`CL_SLAVE`, seat 0/1).
  Gen-3 supports up to 4 (mpId 0–3, 4-way round-robin SIO). Enabling 3–4 = a new UDS topology + the
  multiplayer SIO protocol. Explicitly deferred until 2-player is truly done.

### Tier 2 — Universal word relay (state D) — the fallback, done
Keep behind KEY_Y. Works for anything, slowly (~5 fps floor). Good for patient turn-based exchanges and
as the bisection baseline. No further investment.

### Tier 3 — MIRRORED-PAIR INPUT-SYNC — the real "ALL games" answer (the next big milestone)
**Each console runs BOTH games locally, linked by the already-working in-process link; only controller
inputs cross the radio** (classic lockstep/input-delay netplay, ~2 B/player/frame, fixed input delay
~2–3 frames covers the ~20 ms UDS RTT). Both consoles compute the identical simulation; each keeps the
authoritative save for its own game (identical sims ⇒ identical saves). Why this wins:
- **Universal + full speed:** the link cable never touches the network — Kirby/Sonic/Mario Kart/anything.
- **Mostly built:** the dual-core 60 fps pair, the UDS lobby/session (romCrc already advertised), and the
  reliable-ordered event channel (for the input stream + the one-shot `.sav` exchange at session start).
- **The online foundation:** input-delay netplay over a socket to our own relay = internet play for all
  games, riding the same `NetTransport` seam.

New work it actually needs (honest list):
1. Input sequencing + fixed delay + stall-on-late (the netplay core; traffic is trivial).
2. **Determinism**: identical config both sides (same .cia), RTC virtualized to a synced epoch
   (Pokémon uses RTC), initial-state sync (.sav exchange or checksum-verify), idle-loop setting identical.
3. **Desync detection**: periodic state checksum; resync via savestate transfer on mismatch.
4. Core budget: both games + the RX thread simultaneously (today the wireless path pauses one game to
   free a core for the radio). Input-sync traffic is ~10× lighter than the word relay — likely shareable;
   `n3ds-systems` question for that milestone, verify-on-hw.

Known trade-offs: both ROMs must be on both SDs (already how 3DGBA is used); the peer game's state is
locally simulated (fine for co-op/trades; optionally don't render the peer screen in competitive play).

## Celio: learn vs use (settled)
Licenses compatible (both GPL-3.0) — direct use allowed. We ported the **logic 1:1 clean-room into pure
C** (framing, additive CRC, 14-byte chunking, section flow, TransiveStruct dispatch) because rule #4
(header-free, PC-dual-compile) is what enables the PC proof gates; direct C++/Zephyr copies wouldn't
compile here anyway. Credit formalized in `NOTICE` (+ mirror in README at release). Still unmined:
`awProtocol` (module pattern for a 2nd game family), Celio-Server (the online relay model).

## Method (what run #1 vindicated — keep doing this)
- **PC-first proof gates**: FSM changes land only with unit tests (182→176 checks); the transport proved
  exactly-once/in-order under 40% loss + 40% dup + reorder (66 checks) before ever touching hardware.
  Result: hardware failures are now *specific bugs*, not fog.
- **gs-log first, net-log second**; the raw word log is easy to over-read (three past wrong diagnoses).
- **Never lose the working thing**: every new mode is additive behind KEY_Y; state D stays reachable.
- **Verify against source, not memory**: the relay-vs-synthesis inversion, the seat→CL_role inversion,
  and the 56→60-byte LinkPlayerBlock were all caught by reading/compiling the actual code.

## Logging (current coverage)
- Wireless: per-round `w0/w1/ok/sub` + `# celio` (FSM section/state/blk/partnerPartyB/tradeComplete) +
  `# event` (txSeq/txAcked/rxDelivered/overflow/retransmits) + the gs-log link-error decode
  (`lerr/lstat/lbuf/lnotrecv`). Gap (deliberate, post-run-#2): an FSM *transition* ring — the per-round
  words already reconstruct the trajectory by hand.
- Touch: every event logs ctx + cursor **before→after** + injected keys + the `cb2`/task fingerprint for
  undetected screens (map/PokéNav/PC/move-learn/intro/frontier) → `3DGBA_touch_*.txt`.
- 3D: per-frame disparity stats (feet/head min/max, tall-is-taller pass/fail, front-is-front ordering)
  in the gs log. One play session feeds all three investigations.

## Milestones from here
1. ✅ **M1 DONE (hw-confirmed run #10): a FireRed↔Emerald trade COMPLETES + SAVES in state F.**
2. **M1.5 (in flight): trade polish** — the post-trade ROOM EXIT (fixed run #10, awaiting run #11);
   then real room players + correct partner NAME (eager identity exchange, Tier-1 limitation above);
   then repeat trades / cancel paths / cross-version matrix / error recovery. 3–4 players is a later
   topology change (Tier-1 limitation above), only after 2-player is fully clean.
3. **M2: mirrored-pair input-sync prototype** — one real-time game (e.g. a Kirby minigame) at 60 fps over
   UDS; determinism + desync-checksum harness. The "all games" unlock.
4. M3: Pokémon battle — via input-sync (the linkType is TRADE-only today, so battle correctly refuses;
   input-sync likely delivers it "free" rather than a bespoke battle FSM section).
5. M4: online — `sock_transport` + relay server (Celio-Server model); termination-trades for strangers,
   input-sync for friends.
