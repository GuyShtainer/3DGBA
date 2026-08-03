# 3DGBA Wireless GBA Link — Bridge Rebuild: Implementation Design Doc

> Definitive design for replacing 3DGBA's blocking lockstep wireless link with a non-parking
> **bridge** driver. Audience: the project author + the engineer (you) who codes it next.
> Goal: wireless **trade AND battle**, every game/console, seamless, **60 fps**, 100% stable,
> with the transport factored so a later **online-via-own-server** mode is a swap, not a rewrite.
>
> Every load-bearing claim below cites the evidence that backs it. Where the evidence said a claim
> is only **partial**, that is called out explicitly with the cheapest experiment to resolve it.
> Companion docs: `wireless-link-architecture.md` (the current design), `HANDOFF.md` (the lessons),
> `link-cable-lockstep.md` (the in-process link).

---

## 0. TL;DR

Today every 16-bit SIO word is **one synchronous UDS round-trip** (HANDOFF.md:12). The two parks
that cause this are `net_transfer_collect` inside `net_finishMulti` (gbacore.c:491, netlink.c:375-388)
and `net_round_wait` inside `gbacore_net_poll` (gbacore.c:796, netlink.c:408-418). The result is the
proven **latency-bound ~4-5 fps** trade (HANDOFF.md:12).

We replace this with the **bridge model**: never park the emulated clock on the radio. Each console
**answers its local game's every SIO round in microseconds** from a ring/queue (idle-substitute when
the peer's word hasn't arrived), **captures** the local word, and **ships it async** for the peer to
inject into a *future* round. This is exactly Celio's RawRelay substrate
(rawRelaySection.cpp:87-95 local-answer, :12-23 async-inject), and mGBA's SIO layer fully permits it
(sio.c:143-156, :371-389; **confirmed** by the adversarial verdict and by mGBA's own lockstep driver
shipping the placeholder-then-correct pattern, lockstep.c:585-595).

**The catch, stated up front (this is the whole risk):** idle substitution is safe only *between*
Gen-3 commands or when the partner genuinely has nothing staged. Substituting an idle/stale word
*inside* a checksummed 8-word command is **unrecoverably desync-fatal** (refuted verdict:
link.c:2280-2313 checksum, :1562-1578 CB2_LinkError teardown; HANDOFF.md:58 stale-word lesson). The
bridge therefore is **not** "blindly relay bytes." It is "answer locally **with the real captured
word, reliably ordered**, and only substitute idle when the local game itself would have sent idle
(0x0000)." The async ring must deliver **every real payload word, in order, exactly once** — the
decoupling buys us *latency hiding*, not *permission to drop words mid-command*.

---

## 1. Decision

### Primary: the game-agnostic BRIDGE model

**Adopt local-answer + async idle-suppressed reliable-ordered word injection, never parking the
emulated core.** Both software GBA cores (and, over the radio, both consoles) run at full 60 fps;
each SIO round is answered immediately from a per-seat ordered queue, the local word is captured and
queued for async send, and received words are injected into later rounds.

**Why this is the right primary, from evidence:**

1. **mGBA permits it for master AND slave — confirmed.** `driver->start()` returning false hands the
   driver full ownership of completion (the engine schedules nothing); returning true lets the engine
   schedule `completeEvent` at an emulated-cycle delay, never an RTT (sio.c:143-156). On completion the
   engine fetches words via `finishMultiplayer(data[4])` then writes SIOMULTI0..3, clears Busy, sets
   Id/SC, and raises the SIO IRQ — **with no correctness gate on the word values** (sio.c:371-389).
   mGBA's own lockstep driver pre-fills 0xFF and returns 0xFFFF on a miss with only a non-fatal WARN
   (lockstep.c:560-561, :585-595). The adversarial lens **confirmed** every sub-assertion. A slave
   cannot self-clock in mGBA (the `id!=0` Busy branch is a literal TODO no-op, sio.c:201-203) — so a
   driver-owned local clock is not just allowed, it's the *only* way a slave advances. **3DGBA already
   does this** in `gbacore_net_poll`: `sio->siocnt |= 0x80` + `mTimingSchedule(&sio->completeEvent, …)`
   on the child's own wheel (gbacore.c:840-849).

2. **Gen-3 is structurally built to tolerate a silent partner — confirmed for trade AND battle.**
   When a Gen-3 game has nothing to send it writes `REG_SIOMLT_SEND = 0` (link.c:2301-2308); an
   all-zero received command is **never enqueued** and sets `receivedNothing` (link.c:2056-2067,
   :2272-2276); the receiving game then **skips its own frame update** (link.c:1791-1794,
   main.c:171-175). Reliability is a 50-deep FIFO send/recv queue + a running checksum, so inter-frame
   jitter **only delays, never drops/reorders** (link.h:7, link.c:2020-2041). Battle rides the **same**
   `SendBlock`/`GetBlockReceivedStatus`/`IsLinkTaskFinished` primitives (battle_main.c:1037-1045,
   battle_controllers.c:819-848); the adversarial lens **confirmed battle is equally idle-tolerant at
   the protocol level** (battle_main.c:3566-3588, battle_controller_link_opponent.c:514-527).
   App-level timeouts are coarse (~600 frames ≈ 10 s; cable_club.c:559-568, link.c:886-889) — there is
   no tight real-time deadline the bridge must hit.

3. **It preserves protocol-agnosticism and covers trade + battle + any turn/poll game.** Because the
   bridge relays the game's own captured SIO words (not a per-game reimplementation), the *same* driver
   handles trade, battle, and any other cable activity, for FireRed/LeafGreen/Emerald (and any GBA
   title whose link is poll/turn-based). The Gen-3 idle word is **0x0000** (link.c:2301-2308) — note
   this is the *correct* idle, unlike Celio's RawRelay comment which mislabels 0x7FFF as the Multi idle;
   in real Gen-3, 0x7FFF is `LINKCMD_COUNTDOWN` and 0xEFFF is `LINKCMD_NONE` (refuted verdict:
   link.h:66,84). **Use 0x0000 as the substitute** for "local game wrote nothing," and otherwise never
   substitute at all (see §6).

### The bridge feasibility is PARTIAL — exactly what M-A must prove

The mechanism is confirmed; the **end-to-end "wrong-then-corrected mid-command word survives"** claim
was **refuted** for the data-transfer phase. The reconciliation is: **the bridge must never present an
idle/stale word *as if it were* a real command word.** It hides latency by *deferring* a round's
completion to its real word's arrival **only when the local game is mid-command**, and substitutes
idle **only when the local game itself is idle.** Concretely, M-A must prove the following before we
commit the radio path:

- **(P1) The async ring delivers every real payload word in order, exactly once**, under simulated loss
  /reorder — i.e. the reliable-ordered layer (Celio's sequence/queue idea over UDS) holds. If a real
  command word is ever replaced by an idle/stale value, Gen-3's checksum (link.c:2280-2313) trips
  `CB2_LinkError` (link.c:1562-1578) — the HANDOFF.md:58 lesson and the reverted f140f08 pre-send bug.
- **(P2) The slave VBlank watchdog does not trip when the joiner free-runs between rounds.** The whole
  state-A/B/C/D apparatus exists because a free-running joiner desynced (gbacore.c:772-777; the >10
  VBlank-without-serial-IRQ `LAG_SLAVE`, link.c:2119-2133). The bridge keeps a **rate cap**, not a
  per-round park (see §3).
- **(P3) "Complete immediately with a substitute, correct next round" is acceptable to the game** — the
  open question the engine sidesteps today (mGBA has no "amend a past SIOMULTI" path; the correction
  must surface as a *future* seat word, which only works if the game re-reads/re-requests). For Gen-3
  the FIFO queue + skip-on-nothing makes this true **as long as we only substitute idle when the local
  side is idle.** M-A and M-B prove this empirically via the netlog w0/w1 divergence-by-round columns.

**Cheapest experiment to resolve the partial:** M-A and M-B (below) — a PC host-harness and an
in-process two-core bridge — answer P1/P2/P3 with **zero radio** and full instrumentation, before any
hardware run. If the in-process bridge completes trade **and** battle with `force=0` and no checksum
error across 10k+ rounds, the bridge is proven at the protocol level; only radio jitter remains.

### Fallback: the per-game trade FSM (only on residual desync)

If, after M-B/M-D, the bridge leaves **residual desync** on a specific game/activity (the refuted-case
risk materializes — e.g. a title with a tighter link watchdog, or cross-version battles), fall back to
a **per-game deterministic FSM** for that activity, modeled on Celio's `gbaTradeEmu`
(packetLayer + the 8-word command framing + additive sum-of-words checksum seeded 0xB9A0,
packetLayer.hpp:209-239; INIT_BLOCK 0xBBBB / CONT_BLOCK 0x8888 14-byte chunking, blockCommand.cpp).
This is **strictly more code and strictly per-game** (Celio's README:140 itself says the technique is
"not a game-agnostic solution"), so it is the fallback, not the default. The bridge keeps it optional:
the FSM, if needed, sits behind the same transport interface (§5) and is selected per detected game +
activity. **Do not build the FSM until the bridge demonstrably fails a specific case** — and even then,
only for that case.

---

## 2. Target architecture

### 2.1 What is KEPT verbatim (no edits)

All of these are proven (HANDOFF.md:57 "byte transport proven perfect, toN=0"):

- **`source/netlink.c` transport**: session host/scan/join/close/active, lobby, ping/RTT plane
  (`net_ping_update`), the single RX thread that owns `udsPullPacket` (netlink.c:457+), core pinning
  (`net_link_start(seat,rxCore)`, netlink.c:494), `net_resolve_peer`, `net_send_locked`, the
  `DgbaLinkPkt` + `PK_WORD` packet, `net_link_get_rtt/loss/stats`, the per-round merge ring
  (`net_round_merge`), the "re-send our current word until the peer advances" reliability
  (`net_resend_current` + `s_curPacked`, netlink.c:354,360-363).
- **All netlog instrumentation** in gbacore.c: the 1024-entry `NetLogEntry` ring with
  round/frame/tick/rtt/paceus/turnus/w0/w1/ok/exp columns (gbacore.c:531-543), `gbacore_net_log_dump`
  rate/dt_us/rtt/turnus DEMAND-vs-SUPPLY summary, `net_mono_ticks`/`net_ticks_to_us` (netlink.c:424-425).
- **`source/main.c` wiring**: focused-game participant pick (bfde410, main.c:1492-1502), pause-the-other
  -for-the-RX-core, `net_link_start(seat,rxCore)`, the HUD, `wl_dump`/`gs_dump`, the `apt_hook` teardown.
- **The seam**: `core->setPeripheral(core, mPERIPH_GBA_LINK_PORT, &nd->d)` (gbacore.c:592). The bridge
  is still a `GBASIODriver` installed at exactly this port; we only change the driver's *internals*.

### 2.2 The new bridge GBASIODriver (per console)

The driver is still `struct NetDriver { struct GBASIODriver d; int seat; … }` attached via
`gbacore_net_attach` (gbacore.c:560-592). The vtable stays `{ init, deinit, load, unload, writeRCNT,
writeSIOCNT, start, finishMultiplayer }`. The two parks are removed; everything else is reshaped around
a **per-seat ordered inject queue** filled by the RX thread and drained one-word-per-round.

#### MASTER side (HOST = seat 0) — *local-answer*

`net_start` (gbacore.c:447-474) — keep returning `true` so the engine schedules `completeEvent` at the
realistic `GBASIOTransferCycles` delay (do **not** use 0 cycles — games time their link FSM off the
transfer cadence; the OPEN note on master timing and the cycle table sio.c:14-19 say keep it plausible).
Keep `net_transfer_send_word(0, …, round, w)` (merge our word + async unicast). Keep `s_netRound`
round-stamping.

`net_finishMulti` (gbacore.c:482-555) — **DELETE the `net_transfer_collect` park** (gbacore.c:491). Fill
`data[]` immediately and non-blocking:

```
data[0] = our captured seat-0 word for this round (already merged locally);
data[1] = net_inject_pop(seat=1, round)  // O(1) next ordered peer word, else IDLE
          where IDLE = 0x0000 (Gen-3 idle) — NEVER 0xFFFF for the peer seat in a
          checksummed command (0xFFFF/stale would poison the sum; HANDOFF.md:58)
```

The host **always advances `s_netRound`** now (no "advance only on real exchange" — that was the
lockstep invariant, gbacore.c:504-507). Correctness moves to the inject queue: the peer's real word for
round R, if not yet arrived, is delivered on a **later** round (the game's FIFO/skip-on-nothing absorbs
the deferral, link.c:2056-2067). Keep all netlog stamping.

#### SLAVE side (JOIN = child = seat 1) — *local-clock*

`gbacore_net_poll` (gbacore.c:733-850) — **DELETE the A/B/C/D pacing barrier + `net_round_wait`**
(gbacore.c:754-798) and the strict round-lock (`round = lastInjected+1`, "wait for exactly this round",
gbacore.c:748-754). **KEEP**:

- The local clock: `sio->siocnt |= 0x80` + `mTimingDeschedule/Schedule(&sio->completeEvent, cyc)`
  (gbacore.c:840-849) — this is the only way a slave advances (sio.c:201-203). The child now clocks on
  **its own timer at the game's natural cadence**, regardless of whether the host's word has arrived.
- The **ISR-proof capture gate** (gbacore.c:812-830) — capture `io[SIOMLT_SEND]` only after the prior
  round's SIO ISR has run (`acked && fired && guarded`). This is what prevents shipping a **stale** word
  (the f140f08 / HANDOFF.md:58 lesson) and is **non-negotiable** — keep it edge-strict (no time-ceiling
  force-capture; force-captures correlated with the checksum error, gbacore.c:818-826).
- `net_transfer_send_word(1, …, round, w)` — ship the captured word async.

`net_finishMulti` on the child fills `data[0] = net_inject_pop(seat=0, round)` (host word from queue,
else 0x0000 idle) and `data[1] = our captured child word`. Drop the collect park here too.

#### Replace the round-lock with a RATE CAP, not a per-round park

The joiner free-runs frames, but to keep the slave VBlank watchdog happy **and** keep the two emulated
clocks from diverging (the reverted 26df69a VBlank-cap-free-run lesson, gbacore.c:772-777), retain a
**lightweight rate governor** derived from state-D's host-rate-follow logic (gbacore.c:786-792): cap the
joiner's emulated frame advance to the host's observed round rate (`round/NET_HOSTRATE_DIV`), but
**never `svcSleepThread`-block a worker thread** — instead, when ahead, simply *skip clocking the next
SIO round this slice* (yield emulated time without freezing the thread). This keeps 60 fps render while
bounding divergence. (This is the cheapest answer to P2; M-B validates it in-process.)

### 2.3 New netlink API (batched, non-blocking, reliable-ordered)

Add to `netlink.c`/`netlink.h`, mirroring Celio's K_NO_WAIT queue + sequence/ordering ideas
(rawRelaySection.cpp:12-23,97-101) but over UDS:

- `int  net_inject_pop(int seat, u32 round, u16* out)` — O(1) non-blocking pop of the **next in-order**
  real word for `seat` (the answer-locally read). Returns 0 (and leaves `*out` untouched, caller uses
  idle 0x0000) if the next ordered word hasn't arrived. **Strict FIFO per seat** — this is the
  reliable-ordered guarantee P1 hinges on.
- `void net_send_words(int seat, u32 baseRound, const u16* words, int n)` — batch several rounds' words
  into **one** UDS send, amortizing the per-round round-trip that is the latency-bound killer
  (HANDOFF.md:12). The DgbaLinkPkt grows a small payload array (or a new `PK_WORDS` type).
- `int  net_recv_words_nonblock(int seat, u16* out, u32* outRound, int max)` — drain newest merged peer
  words without blocking (replaces collect's blocking copy); feeds the per-seat ordered inject queue.
- `bool net_latest_word(int seat, u16* out, u32* outRound)` — O(1) most-recent peer word (diagnostic /
  last-good).
- **Idle-suppression**: `bool net_word_changed(int seat, u16 w)` guard so `net_word_tx` skips
  re-shipping an unchanged idle (0x0000) word — cuts the radio idle-flood (Celio drops zero words on
  inject and empty USB frames, rawRelaySection.cpp:49-67). The RX re-send already de-dups via
  `s_curPacked`; add "don't ship if equal-to-last-shipped AND link idle."
- **Backpressure** (the OPEN concern: TX queue overflow under sustained burst): cap the per-seat inject
  queue depth; if the network delivers faster than rounds drain, **coalesce** (Gen-3 needs ~9
  transfers/frame, ~540/s — well within a 256-deep ring) and surface a `net_inject_overflow` diag so we
  detect it rather than silently reorder.

### 2.4 Reliable-ordered layer (the P1 substrate)

Today reliability is "re-send current word until peer advances" (netlink.c:360,486) — correct for
strict lockstep, insufficient for async because words now flow ahead of the partner. Add a thin
**sequence number per (seat)** stamped on each `PK_WORD`/`PK_WORDS`, an ack of the highest contiguous
sequence received, and re-send of the un-acked tail (Celio's sequence/queue, generalized). The merge
ring already keys on `round`; reuse `round` as the sequence (the host stamps sequential rounds,
gbacore.c:465-467; the child adopts via `net_round_next_parent`, netlink.c:437-452). The inject queue
exposes words **only in contiguous order** — a gap blocks **that seat's** pop (returns idle) until the
missing word's re-send arrives, exactly as Gen-3's FIFO would stall, never reorder.

---

## 3. Master/child + the two-console roles

| Role | Maps to | Clock | Local word | Peer word in `finishMultiplayer` |
|---|---|---|---|---|
| **HOST (parent, seat 0)** | local-**answer** master | engine schedules `completeEvent` (realistic cycles) in `net_start` | captured in `net_start` from `io[SIOMLT_SEND]` (gbacore.c:451) | `net_inject_pop(1, round)` else 0x0000 |
| **JOIN (child, seat 1)** | local-**clock** slave | driver schedules `completeEvent` itself in `net_poll` (gbacore.c:847-849) | captured post-ISR-edge in `net_poll` (gbacore.c:833) | `net_inject_pop(0, round)` else 0x0000 |

Mapping to evidence: the HOST initiating rounds with `start()=>true` and the child with `start()=>false`
is exactly the current split (gbacore.c:447-449) and matches both 3DGBA's design and mGBA lockstep
(parent uses engine schedule, slave self-schedules, lockstep.c:964-970). Celio's MASTER PIO likewise
locally clocks a child at ~8.3 ms/round with a firmware timing word (pio_master_mode.pio:16-27); our
"emulated parent runs at its own rate, child answers what's staged" is the software analog the reader
findings prescribe.

**What state D becomes.** State D (host-rate-follow + edge-strict, the current default, gbacore.c:573,
786-792) is **demoted from a per-round blocking barrier to a non-blocking rate cap** (§2.2). Its
host-rate-follow math (`round/DIV` proxy vs joiner frame delta) is retained as the *governor*; its
`net_round_wait` **block** is deleted. The KEY_Y A/B/C/D experiment toggle (main.c) can stay during the
bridge bring-up as a debug fork (e.g. A = old blocking lockstep for A/B comparison, D = bridge), but the
shipped default becomes the bridge. The edge-strict capture gate (af43230) is **kept and remains
mandatory** — it is orthogonal to pacing and is what guarantees we ship the real post-ISR word, not a
stale one.

---

## 4. Milestone ladder (with testability tiers)

Testability tiers, cheapest first:
**T0** PC host-harness: two mgba cores compiled on PC + a simulated lossy/reordering transport (pure-C
cores are header-free per CLAUDE.md rule #4, so they dual-compile). **T1** Azahar, one console.
**T2** 1 real console, loopback (`net_link_set_loopback(true)`, netlink.c:296). **T3** 2 real consoles.

### M-A — non-park bridge driver vs ONE local game (no radio)
- **Build:** the new `net_finishMulti` (no collect) + `net_poll` (no `net_round_wait`, local-clock kept)
  for **one** real game, with the partner being a **synthetic/loopback** word source (a stub that
  answers the trade/battle handshake from a canned word script, or a loopback echo).
- **Where testable:** T0 (PC harness, fastest) → T2 (1 console loopback).
- **Pass/fail gate:** the single game reaches and holds the link **idle/handshake loop at 60 fps** with
  the emulated clock **never parked** (netlog `paceus`≈0, `turnus` small, render stays 60 fps). Proves
  the non-park driver and answers **P3** for the handshake phase. **Fail** = any park reappears, or the
  game trips `LAG_SLAVE`/`LAG_MASTER` (gLinkStatus read via gamestate.c link-error fields).

### M-B — two local cores bridged in-process (the de-risking step)
- **Build:** attach the bridge to **both** `emuA`/`emuB` (seat 0/1) in-process (main.c:1526-1527 already
  attaches both for the on-device link), but route words through the **bridge inject queues** with a
  **simulated lossy/reordered** in-process transport (not real UDS). Run a **full trade AND a full
  battle** between the two screens.
- **Where testable:** T0 (PC harness, two cores + simulated transport) → T1/T2 (on device, both cores).
- **Pass/fail gate:** trade **completes + saves + exits** AND a battle **runs to completion**, both at
  **60 fps**, with `force=0` (no force-captures) and **zero checksum errors** across **10k+ rounds**,
  under injected loss/reorder. This is the decisive **P1/P2/P3** proof at the protocol level with no
  radio variance. **This step is redundant with M-C's payoff but de-risks the refuted-case fear cheaply**
  — if the bridge desyncs, it desyncs here, where the netlog w0/w1-by-round pinpoints the exact round.
  **Fail here → invoke the §1 fallback FSM** for the failing activity before touching radio.

### M-C — blocks/words over UDS between two consoles (the payoff)
- **Build:** wire the bridge to the **real** netlink transport with the §2.3 batched/ordered API +
  idle-suppression. Keep ping/RTT/lobby.
- **Where testable:** T3 (2 real consoles) — per CLAUDE.md rule #6, timing-sensitive ⇒ hardware-final.
- **Pass/fail gate:** trade completes over UDS at a **markedly higher fps than today's 4-5** (target:
  near-60 on the focused game; the joiner tracks host rate without divergence) and **stably across
  repeated attempts** (fixes the HANDOFF.md:32 intermittency). Netlog DEMAND≈SUPPLY (latency hidden).

### M-D — hardening + every-game / cross-game + battle sign-off
- **Build:** every loaded-game permutation (FireRed↔FireRed, Emerald↔Emerald, and the cross-version
  handshake path if mixed ROMs are in scope — `LINKCMD_SEND_LINK_TYPE`/`GameFreak inc.` magic,
  link.c:539-549); battle sign-off on hardware; backpressure/overflow + link-loss recovery; the
  "this game can't HOST" hiccup (HANDOFF.md:33).
- **Where testable:** T3 (2 real consoles), per game/activity matrix.
- **Pass/fail gate:** trade **and** battle complete on every supported permutation, 60 fps, no desync
  across long sessions; clean recovery on real link loss (peer gone ⇒ game's own watchdog errors
  cleanly, not a freeze). Sign-off is on **real New 3DS** (rule #6). **OPEN to resolve here:** does
  collapsing/keeping the 8-IRQ-per-frame structure trip `LAG_MASTER/LAG_SLAVE`? — measured on hardware
  with the netlog `dvbl`/`vblMax` columns.

---

## 5. Online-prep seam — transport as an interface

Make the transport an **interface**, mirroring Celio's `commandEmitter` abstraction (Celio swaps the
USB relay for a network relay behind one emitter). Today `netlink.c` is already a clean C-API seam
(HANDOFF.md:30,66 — "the netlink.c C-API seam was isolated for exactly this"). Formalize it:

```c
// netlink_transport.h — the swappable interface (UDS now, Socket.IO/WebSocket-to-own-server later)
typedef struct {
    bool (*start)(int seat, int rxCore);
    void (*stop)(void);
    void (*send_words)(int seat, u32 baseRound, const u16* w, int n);  // batched, non-blocking
    int  (*recv_words_nonblock)(int seat, u16* out, u32* round, int max);
    u32  (*rtt_us)(void);
    bool (*peer_present)(void);
} NetTransport;
```

- **Now:** `uds_transport` implements it over the existing `netlink.c` UDS code (rename the current
  functions to the vtable, no behavior change).
- **Later (online):** a `sock_transport` implements the same vtable over `soc:U` UDP / a
  WebSocket/Socket.IO client to **our own relay server** — exactly the HANDOFF.md:30 "netbench v2 UDP
  transport … also the wanted online-play foundation" path. Peer discovery moves from the UDS lobby to a
  server-side lobby; the bridge driver, the inject queues, and **all** of gbacore.c are **unchanged**.

**What to factor now so online is a swap, not a rewrite:**
1. The bridge driver must call **only** `NetTransport` methods + the inject queues — never `netlink.c`
   internals directly.
2. **Word-level batching + reliable-ordered sequence** (§2.3/2.4) must live **above** the transport, so
   a lossy UDP/internet transport gets ordering for free (Celio's server provides ordering at the relay;
   we keep it client-side so any transport works).
3. **Idle-suppression** belongs above the transport too (don't flood a metered/internet link).
4. Reference: **Celio client** (`commandEmitter` swap) + **Celio server** as the online-relay model — a
   thin word-relay with per-session rooms, which our own server mirrors.

---

## 6. Risks & the desync question

- **The HANDOFF.md:58 stale-word lesson (the central risk).** Gen-3 writes `SIOMLT_SEND` in the main
  loop *after* the serial ISR; capturing/pre-sending early ships a **stale 0x0000**, which poisons the
  checksum (the reverted f140f08). The bridge's **idle-substitution differs fundamentally from
  pre-send**: pre-send shipped a stale value *in place of a real word the game was about to produce*;
  the bridge substitutes idle **only when the local game genuinely produced nothing** (its own
  `SIOMLT_SEND == 0`), and otherwise ships the **real post-ISR-captured word** via the kept edge-strict
  gate (gbacore.c:812-830). The async ring **defers** a real word; it never **fabricates** one
  mid-command. This is the difference between the **confirmed** mechanism (idle when idle) and the
  **refuted** overreach (idle in place of payload).
- **The checksum-desync failure mode (refuted verdict).** A wrong/idle/stale word inside an 8-word
  command ⇒ `badChecksum` ⇒ `TrySetLinkErrorBuffer` ⇒ `CB2_LinkError` + `CloseLink`
  (link.c:2243-2245,1562-1578) — **unrecoverable**. Mitigation is entirely **P1**: the reliable-ordered
  inject queue must deliver every command word in order, exactly once. The idle word is only ever
  presented for a seat that is **between commands**. Instrumentation that proves correctness: the netlog
  **w0/w1 columns by round** (gbacore.c:537-538) — overlay the host's sent word vs the joiner's received
  word per round; a divergence at a non-idle round is the bug, localized to the round. Reuse the
  link-error decode (`lerr`/`lstat`/`lbuf`, gamestate.c) to catch `badChecksum` the instant it sets.
- **The slave VBlank watchdog (P2).** Free-run ⇒ >10 VBlanks-without-serial-IRQ ⇒ `LAG_SLAVE`
  (link.c:2119-2133). Mitigation: the **non-blocking rate cap** (§2.2/§3) keeps the joiner clocking SIO
  rounds frequently enough; M-B validates the cap value against `dvbl`/`vblMax`.
- **Per-game variance (the honest ceiling).** Celio supports only Gen-3 trade + Advance Wars and calls
  the technique non-agnostic (README:140; partial verdict). Our confidence is **highest for Gen-3 trade
  + battle** (pret-confirmed idle-tolerance). Other titles (or cross-version battles) may have tighter
  validation — that's exactly where the §1 **fallback FSM** applies, per game/activity. M-D's matrix
  surfaces which titles need it.
- **Master-immediate-completion perturbation (OPEN).** Completing the master at 0 cycles may perturb a
  game timing its FSM off transfer cadence (cycle table sio.c:14-19). Mitigation: keep the realistic
  `GBASIOTransferCycles` schedule (§2.2) — the slave already reuses it for plausibility (gbacore.c:847).

---

## 7. Crediting Celio

Celio firmware is the architectural source for the local-answer + async-inject substrate and the
fallback trade FSM. Concrete obligations:

- **License compliance.** 3DGBA is **GPL-3.0** (repo `LICENSE`). mGBA is **MPL-2.0** (file-level
  copyleft — modified mGBA files stay open; CLAUDE.md rule #5). Check Celio's license before vendoring
  **any** Celio source: if Celio is GPL-compatible we may port code with attribution; if not, we
  **reimplement the technique clean-room from these notes** (the design ideas are not copyrightable; the
  code is). **Do not copy Celio source files** into 3DGBA until its license is confirmed compatible with
  GPL-3.0; default to a clean-room reimplementation.
- **NOTICE file.** Add `projects/3DGBA/NOTICE` crediting Celio for the bridge architecture
  (local-answer + K_NO_WAIT async-inject + idle substitution + master-locally-clocks-child) and the
  Gen-3 trade FSM reference, with the upstream URL and its license.
- **README attribution.** A "Credits / prior art" section: Celio (Celio-Firmware) — the wireless GBA
  bridge substrate and Gen-3 trade-emu FSM that inspired this design; pret/pokeemerald + pret/pokefirered
  — the authoritative Gen-3 link protocol that validated idle-tolerance.
- **Which ideas/files were referenced:** `src/sections/rawRelaySection.cpp` (local-answer +
  async-inject), `src/layers/linkLayer_pio.c` + `pio_master_mode.pio`/`pio_slave_mode.pio` (master/slave
  clocking), `src/sections/tradeConnection.cpp` + `blockCommand.cpp` + `packetLayer.hpp` +
  `link_defines.h` (the fallback FSM). Cite these in the NOTICE.

---

## 8. First coding step (M-A scaffold)

The smallest change that starts M-A and is testable fastest:

1. **Add the inject-queue primitive** to `netlink.c`/`netlink.h`:
   `int net_inject_pop(int seat, u32 round, u16* out);` — back it with the existing merge ring
   (`s_rounds`, netlink.c:373) reading the next contiguous round for `seat` non-blocking. Default
   `*out` unchanged ⇒ caller substitutes **0x0000**.
2. **Gut the host park** in `net_finishMulti` (gbacore.c:482-491): replace the
   `net_transfer_collect(...)` call with:
   ```c
   data[0] = our_seat0_word_for_round(nd->pendingRound);   // already merged in net_start
   u16 peer; data[1] = net_inject_pop(1, nd->pendingRound, &peer) ? peer : 0x0000;
   bool ok = true;   // never park; correctness is via the ordered queue
   ```
   and **always** advance `s_netRound` (remove the `&& ok` gate, gbacore.c:504). Keep every netlog line.
3. **Gut the joiner park** in `gbacore_net_poll` (gbacore.c:754-798): delete the `net_round_ready`
   gate + the A/B/C/D `net_round_wait` block; keep the **edge-strict ISR capture gate**
   (gbacore.c:812-830), the `net_transfer_send_word`, and the local-clock schedule
   (gbacore.c:840-849). Add the §2.2 non-blocking rate cap (skip-clock-when-ahead).
4. **Test fastest (T0 → T2):**
   - **T0 (preferred, no hardware):** in the PC host-harness, instantiate one core + a loopback word
     source; assert the emulated clock never parks (no `svcSleepThread` path is reached — stub it to
     `abort()` under the harness) and the handshake loop holds.
   - **T2 (on device):** `net_link_set_loopback(true)` (netlink.c:296) so the driver answers its own
     words with no radio; flash, confirm the focused game holds the link idle loop at **60 fps** with
     netlog `paceus`≈0. That is the M-A pass gate.

**Why this order:** it removes both parks behind the *existing* attach/seam (gbacore.c:592) with the
smallest diff, keeps every diagnostic, and proves the non-park property at 60 fps **before** any radio
or two-core complexity — so if the bridge is going to desync a game, M-B catches it in-process with the
w0/w1 netlog, and the §1 fallback FSM is invoked only for the specific failing case.
