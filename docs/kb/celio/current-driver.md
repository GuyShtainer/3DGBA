# 3DGBA net link driver — CURRENT state (with uncommitted state-E "bridge" changes)

Mapped 2026-06-29 against the working tree. All line numbers are REAL (verified by Read/Grep), correcting
the spec's guessed numbers. Four files:

- `projects/3DGBA/source/gbacore.c` (984 lines) — the mGBA-side `GBASIODriver` (the **net SIO driver**), pure stdint, NO libctru.
- `projects/3DGBA/source/gbacore.h` (98 lines) — plain-C interface; declares `gbacore_net_*`.
- `projects/3DGBA/source/netlink.c` (636 lines) — UDS transport plane (the `net_*` transfer ring + RX thread), pure libctru, NO mGBA.
- `projects/3DGBA/source/netlink.h` (97 lines) — transport API as libctru `u16/u32` types.

The two translation units never share headers; gbacore.c re-declares the transport API with `stdint` types
(ABI-identical) at **gbacore.c:296-308**. This TU boundary is the architectural invariant the Celio port must respect.

The live default experiment is **state D** (`s_netExp=3`, set in `gbacore_net_attach` at gbacore.c:659). The
uncommitted work is **state E = BRIDGE** (`s_netExp==4`), reachable via the HUD KEY_Y toggle. The Celio port replaces /
augments the **state-E fill path** (`net_bridge_fill`) with LOCAL TERMINATION (answer SIO locally, ship 8-word commands)
— so `net_bridge_fill` (gbacore.c:523), the `net_finishMulti` dispatch (gbacore.c:574-576), and the `NetDriver`
command-frame fields (gbacore.c:68-74) are the precise edit seam.

---

## (1) NetDriver struct + the GBASIODriver vtable

### `struct NetDriver` — gbacore.c:55-75
```c
struct NetDriver {
	struct GBASIODriver d;       // gbacore.c:56  MUST be first — mGBA holds &d; cast back to NetDriver
	int      seat;               // :57  our GBA playerId (0 = parent / clock owner)
	int      peers;              // :58  number of OTHER GBAs (1 for a 2-seat trade)
	uint32_t needMask;           // :59  bitmask of seats required for a complete round (0x3 for 2-seat)
	uint32_t pendingRound;       // :60  the round finishMultiplayer() must collect
	bool     roundOpen;          // :61  parent: round started, not yet retired by net_finishMulti
	uint32_t lastInjectedRound;  // :62  child: last self-scheduled round (sentinel 0xFFFFFFFF = none)
	enum NetChildPhase phase;    // :64  child only; NET_IDLE on the parent
	uint32_t isrWaitRound;       // :65  round whose SIO ISR must finish before next capture; 0xFFFFFFFF = none
	uint32_t irqArmTime;         // :66  local cycle stamp when RECEIVING-end armed the IRQ
	uint32_t lastActiveFrame;    // :67  child PACING: emulated frame counter at the last injected round
	// --- State E (bridge) command-framed peer assembly (gbacore.c:68-74) ---
	uint32_t peerCmdIndex;       // :72  peer command index (round>>3) cached; 0xFFFFFFFF = none
	uint16_t peerCmd[8];         // :73  cached peer command words (genuine, or zeros on idle-substitute)
	uint8_t  peerCmdReal;        // :74  diag: 1 = genuine command assembled, 0 = idle-substituted
};
```
`enum NetChildPhase { NET_IDLE=0, NET_SENDING, NET_RECEIVING }` — gbacore.c:54.
The driver lives embedded in `struct GbaCore` at gbacore.c:85 (`struct NetDriver netDriver;`), alongside the
mutually-exclusive lockstep `linkDriver` (gbacore.c:83). NOTE: `peerCmdIndex`/`peerCmd[8]` are **declared and
zeroed/reset** (net_reset gbacore.c:439, attach gbacore.c:677) but the CURRENT `net_bridge_fill` does NOT use them —
it went per-word, not per-command (see the "v1" comment gbacore.c:512-522). They are the latent slot the Celio
8-word-command path is expected to populate.

### The GBASIODriver vtable — wired in `gbacore_net_attach` at gbacore.c:664-670
| GBASIODriver slot | fn | def site | behavior |
|---|---|---|---|
| `init` | `net_init` | gbacore.c:430 | `return true;` (no-op) |
| `deinit` | `net_deinit` | gbacore.c:431 | no-op |
| `reset` | `net_reset` | gbacore.c:432-440 | zero pendingRound/roundOpen/phase, isrWaitRound=`0xFFFFFFFF`, peerCmdIndex=`0xFFFFFFFF` |
| `driverId` | `net_id` | gbacore.c:441 | `0x54454E47` ('GNET') |
| `loadState` | `net_load` | gbacore.c:442 | `return true;` (no-op) |
| `saveState` | `net_save` | gbacore.c:443 | writes NULL/0 |
| `setMode` | `net_setMode` | gbacore.c:444-460 | on `GBA_SIO_MULTI` entry: set **Ready** bit + RCNT.Sd; child re-baselines `lastActiveFrame` |
| `handlesMode` | `net_handles` | gbacore.c:461 | true only for `GBA_SIO_MULTI` |
| `connectedDevices` | `net_devices` | gbacore.c:462 | returns `peers` |
| `deviceId` | `net_devId` | gbacore.c:463 | returns `seat` |
| `writeSIOCNT` | `net_wSIOCNT` | gbacore.c:468-476 | captures baud (diag), forces **Ready=1**; **does NOT touch baud bits** (reverted — broke Gen-3 connect-verify) |
| `writeRCNT` | `net_wRCNT` | gbacore.c:477-479 | asserts RCNT.Sd line in MULTI |
| `start` | `net_start` | gbacore.c:483-510 | **PARENT only**; pushes our word, returns true |
| `finishMultiplayer` | `net_finishMulti` | gbacore.c:565-641 | both seats: collect the agreed words |
| `finishNormal8` | `net_finishN8` | gbacore.c:643 | `0xFF` |
| `finishNormal32` | `net_finishN32` | gbacore.c:644 | `0xFFFFFFFF` |

`setMode` seeds the MULTI Ready handshake at the mode transition (gbacore.c:448-451), mirroring lockstep `_setReady`,
because routing every SIOCNT write through `net_wSIOCNT` SKIPS mGBA's own FillReady fallback — WE must supply Ready
or the game waits forever.

---

## (2) Driver flow: net_start, net_finishMulti, net_bridge_fill, gbacore_net_poll, gbacore_net_attach

Key index constants (gbacore.c:310-312): `IO_SIOMLT_SEND = 0x95` (halfword index for 0x0400012A), `IO_IF = 0x101`,
`SIO_IRQ_BIT = (1<<7)` (GBA_IRQ_SIO==7).
Deadlines (gbacore.c:313-318): `NET_DEADLINE_MS=2000` (established link-lost), `NET_ESTABLISH_MS=100` (pre-establish poll).
Bridge tunables (gbacore.c:362-369): `NET_BRIDGE_WAIT_MS=30`, `NET_BRIDGE_LEAD=16`, `NET_BRIDGE_SAFE_VBL=6`.
Shared round counter: `static volatile uint32_t s_netRound` (gbacore.c:385), single writer = parent.

### `net_start` (PARENT seat-0 only) — gbacore.c:483-510
Reads OUR word from `io[IO_SIOMLT_SEND]` (gbacore.c:487). **One wire round per COMPLETED transfer** (FIX B): on a
re-Busy with `roundOpen` already set it RE-SENDS our word for the SAME `pendingRound` (gbacore.c:498-499); only when
no round is open does it claim a fresh round from `s_netRound` and set `roundOpen=true` (gbacore.c:500-503). Ships via:
```c
net_transfer_send_word(0, GBA_SIO_MULTI, round, w);   // gbacore.c:507
```
The round is RETIRED (s_netRound advanced) only in net_finishMulti, only on success. Secondaries `return false`
(gbacore.c:485) — must never self-start a MULTI they don't clock.

### `net_finishMulti` (BOTH seats — the collect rendezvous) — gbacore.c:565-641
This is the **single dispatch seam** Celio edits. Key lines:
- gbacore.c:567 `s_netFinishN++` (joiner-stall X-ray).
- gbacore.c:573 deadline = `s_netEstablished ? NET_DEADLINE_MS : NET_ESTABLISH_MS`.
- **THE FORK (gbacore.c:574-576):**
```c
bool ok = (s_netExp == 4)
          ? net_bridge_fill(nd, data)            // STATE E (bridge)
          : net_transfer_collect(nd->pendingRound, GBA_SIO_MULTI, data, nd->needMask, deadline);
```
  State E -> `net_bridge_fill`; every other state -> the proven blocking `net_transfer_collect` that PARKS the worker.
- gbacore.c:577-588 ok/timeout bookkeeping. On timeout (`ok==false`) it `memset(data, 0xFF, 8)` (gbacore.c:587) so the
  game errors cleanly, does NOT retry (a phantom transfer poisons the checksum).
- **Round retire (gbacore.c:589-592):** parent only, ONLY on `ok`:
  `__atomic_store_n(&s_netRound, nd->pendingRound + 1, RELEASE); nd->roundOpen = false;`. On timeout it KEEPS the same
  round + `roundOpen` (host re-clocks an absent slave — keeps host/joiner round-locked).
- gbacore.c:600-603 RX-word diag + peak latch.
- gbacore.c:604-629 **netlog append** (see §4): dedups a repeated timeout row for the same round.
- gbacore.c:635-640 **child RECEIVING-end bookkeeping**: stamps `isrWaitRound = pendingRound`,
  `irqArmTime = mTimingCurrentTime`, `phase = NET_IDLE`. This is the post-collect handoff back to the poll's ISR gate.
- IMPORTANT (gbacore.c:559-564 comment): the CHILD does NOT re-read `io[SIOMLT_SEND]` here — its word was already
  captured+sent at the START of the round in `gbacore_net_poll` (the VBA-M one-transfer latency). Both seats only
  RENDEZVOUS on the collect's full `needMask`.

### `net_bridge_fill` (STATE E — the Celio edit target) — gbacore.c:523-557
Current behavior is **per-word free-run** (NOT 8-word-command framed — the v1 comment gbacore.c:512-522 explains the
prior 8-word pre-framing was the bug that cached an idle "command" before real words arrived, so realCmds stayed 0).
Flow:
- `me = nd->seat, peer = me ^ 1; r = nd->pendingRound;` (gbacore.c:524-525).
- Lead bound (gbacore.c:531-536): `net_seat_max_round(peer, &peerMax)` -> `lead = r - peerMax`;
  `farAhead = lead > NET_BRIDGE_LEAD`. If the peer's progress is invisible (no peer word in ring), treat as far-ahead.
- Our own word: `data[me] = 0; net_round_peek_word(r, me, &data[me]);` (gbacore.c:538).
- **FAST PATH (gbacore.c:540-541):** `if (net_round_peek_word(r, peer, &peerW))` -> peer word already pipelined ->
  `peerCmdReal=1; s_netBridgeRealN++`.
- **SLOW PATH (gbacore.c:542-553):** `dl = farAhead ? NET_DEADLINE_MS : NET_BRIDGE_WAIT_MS;`
  `net_transfer_collect(r, GBA_SIO_MULTI, data, nd->needMask, dl)` — if it fills, `return true`. Else
  `peerW = 0x0000` (Gen-3 idle, game retries; NEVER 0xFFFF), `peerCmdReal=0; s_netBridgeIdleN++`.
- gbacore.c:554-556: `data[peer] = peerW; data[2]=data[3]=0xFFFF; return true;` (free-run advance).

This is where the **Celio LOCAL TERMINATION** lands: instead of fetching the peer's word over the wire round-by-round
and idle-substituting on miss, the port answers SIO LOCALLY (the Celio link state machine produces the partner's
response from the local command) and ships 8-word command frames. The `peerCmd[8]`/`peerCmdIndex` fields (gbacore.c:72-73)
and `net_cmd_collect` (netlink.c:426) are the unused scaffolding intended for that 8-word atomic delivery.

### `gbacore_net_poll` (CHILD per-slice hook) — gbacore.c:831-955
Called ONLY from the core's own worker thread. Parent returns immediately (gbacore.c:834). Behavior:
- **STRICTLY ONE round in flight** (gbacore.c:844): `if (nd->phase != NET_IDLE) return;` — lets run_loop fire the
  in-flight round's completeEvent + SIO IRQ before touching the next round (the pace-block below sleeps ON THIS WORKER,
  so reaching it mid-round would starve run_loop — the 0616 okN-stall).
- Next round = `nd->lastInjectedRound + 1` (gbacore.c:851), in-order, never skip (UDS reorders/drops).
- **PACING (gbacore.c:852-905):** if `!net_round_ready(round, 1u<<0)`:
  - Free-run when NOT actively transferring (`s_netLastInjectTick==0 || net_older_than_ms(.., NET_ACTIVE_MS)`)
    (gbacore.c:860-869) — also invalidates state-D baseline.
  - Per-experiment fork (gbacore.c:881-898): A=block; B(=1)=never block; C(=2)=cap then block;
    **E(=4)**: `if (vbl < NET_BRIDGE_SAFE_VBL) return;` else fall through to block (gbacore.c:882-888) — caps the
    SLAVE's VBlanks-without-IRQ so it can't trip LAG_SLAVE; D(=3)=host-rate follow (gbacore.c:891-898).
  - The block: `net_round_wait(round, 1u<<0, NET_PACE_WAIT_MS)` (gbacore.c:901). `if (!got) return;`.
- **ISR-PROOF GATE (gbacore.c:908-935):** do not capture round R's reply until (R-1)'s SIO ISR ran+acked. Three LOCAL
  signals: `acked = (io[IO_IF] & SIO_IRQ_BIT)==0` (gbacore.c:920), `fired = !mTimingIsScheduled(irqEvent)`
  (gbacore.c:921), `guarded = elapsed >= NET_ISR_GUARD_CYCLES` (gbacore.c:922).
  **EDGE-STRICT (state D, gbacore.c:928):** `ceiling = (s_netExp != 3) && (elapsed >= NET_ISR_GUARD_CEIL)` — D
  DISABLES the time-ceiling so it ALWAYS waits for the proven `(acked && fired)` edge (force-captures correlated
  with the host CHECKSUM error). gbacore.c:929 `if (!ceiling && !((acked&&fired)&&guarded)) return;`.
- **SENDING (gbacore.c:937-954):** capture `w = gba->memory.io[IO_SIOMLT_SEND]` (gbacore.c:938) — the post-ISR-armed
  reply — then `net_transfer_send_word(nd->seat, GBA_SIO_MULTI, round, w)` (gbacore.c:941). Set Busy
  `sio->siocnt |= 0x80` (gbacore.c:945), `pendingRound = lastInjectedRound = round` (gbacore.c:946-947),
  `phase = NET_RECEIVING` (gbacore.c:948), then SCHEDULE the completeEvent:
```c
int32_t cyc = GBASIOTransferCycles(GBA_SIO_MULTI, sio->siocnt, nd->peers);   // gbacore.c:952
mTimingDeschedule(&gba->timing, &sio->completeEvent);                         // :953
mTimingSchedule(&gba->timing, &sio->completeEvent, cyc);                      // :954
```
  (The parent's completeEvent is scheduled by mGBA's stock sio.c after `net_start` returns true.)

### `gbacore_net_attach` — gbacore.c:646-681
`needMask = (1u << (peers+1)) - 1u` (gbacore.c:652) = 0x3 for a 2-seat trade. Resets all diag + the NetDriver fields,
clears `peerCmdIndex` (gbacore.c:677), sets **default `s_netExp = 3` (state D)** (gbacore.c:659), `s_netRound=0` on
seat 0 (gbacore.c:679), wires the vtable (gbacore.c:664-670), and installs via
`g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, &nd->d)` (gbacore.c:680). `gbacore_net_detach` clears the
peripheral (gbacore.c:683-686).

---

## (3) netlink.c transport API

### Packet + types — netlink.c:35-45
```c
typedef struct __attribute__((packed)) {   // netlink.c:35-42  16 bytes
	u8  magic;   // 'G'
	u8  type;    // 5=PING, 6=PONG, 7=WORD
	u8  seat;
	u8  mode;
	u32 round;
	union { u16 word[4]; u32 normal; u16 send; } d;
} DgbaLinkPkt;
#define PK_PING 5   // :43
#define PK_PONG 6   // :44
#define PK_WORD 7   // :45   one seat's SIO word: pk->seat / pk->round / pk->d.send
```
There is **no CRC and no 8-word frame on the wire** — the transport carries ONE SIO word per `PK_WORD` packet, keyed
by `(seat, round)`. All Gen-3 CRC / 9-word framing lives ABOVE this, inside the emulated GBA's own link.c (the
`gen3-link-protocol.md` reference). So the Celio port operates entirely in gbacore.c (the SIO driver), NOT in this
transport layer — netlink stays a dumb word pipe.

### NetRound ring — netlink.c:83-96
`#define NET_ROUNDS 32` (netlink.c:83). Each slot:
```c
typedef struct {           // netlink.c:86-93
	LightLock  lock;
	LightEvent ev;         // RESET_STICKY
	u32  round;
	u32  arrivedMask;
	u16  words[DGBA_MAX_SEATS];
	bool used;
} NetRound;
static NetRound s_rounds[NET_ROUNDS];   // :94
```
`net_round_merge(seat, round, word)` (netlink.c:313-332) is the single merge point (local send AND UDS RX): ignores a
late word for an OLDER round (netlink.c:317-318), (re)claims the slot for a same/newer round filling absent seats with
`0xFFFF` (netlink.c:320-326), sets `arrivedMask`, tracks `s_rxMaxSeat0` host-progress (netlink.c:329), signals `ev`.

### Session / lifecycle
- `net_session_host(gameCode, romCrc, seatsTotal)` — netlink.c:153-168. `udsGenerateDefaultNetworkStruct` +
  `udsCreateNetwork`, advertises `DgbaAdv` (netlink.c:18-24), sets `s_up=s_host=true`.
- `net_session_join(sel)` — netlink.c:204-211. `udsConnectNetwork` to the sel'th scanned net, `s_host=false`.
- `net_lobby_scan` netlink.c:170-202, `net_session_close` netlink.c:213-225, `net_lobby_status` netlink.c:614-636.

### net_link_start / RX thread / transfer API
- `net_link_start(seat, rxCore)` — netlink.c:552-570. `net_rounds_init` + `net_transfer_reset`, `s_loopback=false`,
  **REFUSES** without a unicast peer (`net_resolve_peer`, netlink.c:558), spins the RX thread pinned to `rxCore`
  (netlink.c:565-566, falls back to default core -2).
- `net_rx_thread` — netlink.c:515-550. **THE ONE udsPullPacket owner.** Drains to empty under `s_rxLock`
  (netlink.c:521-538): PING->pong, PONG->RTT, **PK_WORD-> `net_round_merge`** (netlink.c:534-536, `s_rxWordN++`).
  Every ~4ms (`resendTick>=8`) `net_resend_current()` (netlink.c:544) RE-SENDS this console's current word
  (`s_curPacked`, netlink.c:75) so dropped/reordered words eventually land. Polls at ~2 kHz
  (`svcSleepThread(500us)`, netlink.c:548) instead of blocking on the bind event (close-hang fix).
- `net_transfer_send_word(seat, mode, round, send)` — netlink.c:351-356: `net_round_merge` OUR seat first, store
  `s_curPacked` for RX re-send, then `net_word_tx` (single non-blocking `udsSendTo`, netlink.c:341-349 — busy-retry
  REMOVED, it starved run_loop).
- `net_transfer_collect(round, mode, out[4], needMask, deadline_ms)` — **netlink.c:370-388.** The PARK point: polls
  the ring slot, returns true when `(arrivedMask & needMask)==needMask` (netlink.c:378-381); else
  `svcSleepThread(1ms)` (netlink.c:386) and loops until `s_collectAbort`/`!s_up`/deadline (netlink.c:384-385). NEVER
  fabricates a word. This is what `net_finishMulti` blocks in (non-bridge), and what `net_bridge_fill`'s slow path uses.
- `net_round_wait(round, needMask, deadline_ms)` — **netlink.c:466-476.** The JOINER PACING BARRIER park: same escapes,
  copies NO words, just gates the joiner's emulated clock. Used by `gbacore_net_poll` at gbacore.c:901.
- `net_round_ready` netlink.c:392-399, `net_transfer_abort` netlink.c:572-580, `net_link_stop` netlink.c:582-589
  (stop+join RX thread, then abort).

### STATE-E bridge transport helpers (the Celio 8-word-command scaffolding)
- `net_round_peek_word(round, seat, *out)` — **netlink.c:405-417.** Non-blocking single-seat read; true only if the
  slot holds `round` and `seat`'s `arrivedMask` bit is set. Used by `net_bridge_fill` (gbacore.c:538,540).
- `net_cmd_collect(seat, base, out[8], deadline_ms)` — **netlink.c:426-438.** ATOMIC 8-word command wait: loops over
  `net_round_peek_word(base+i, seat, &out[i])` for i in 0..7; returns true only when all 8 present, false on
  abort/deadline (caller idle-substitutes 8 zeros). **Currently UNUSED by gbacore.c** — this is the function the Celio
  local-termination port is meant to drive (8-word command granularity).
- `net_seat_max_round(seat, *out)` — **netlink.c:443-457.** Highest round with this seat's word present (lead bound).
  Used by `net_bridge_fill` (gbacore.c:532).
- `net_round_next_parent(afterRound, *out)` — netlink.c:495-510 (child adopts the parent's lowest unstamped wire round).

---

## (4) The netlog ring + columns

### Entry — gbacore.c:420-423
```c
typedef struct {
	uint32_t round; uint32_t frame; uint64_t tick;
	uint32_t rtt; uint32_t paceus; uint32_t turnus;
	uint16_t w0, w1; uint8_t ok; uint8_t exp; uint8_t sub;
} NetLogEntry;                              // gbacore.c:420
static NetLogEntry s_netLog[NETLOG_N];      // gbacore.c:421  NETLOG_N = 1024 (gbacore.c:414)
static uint32_t s_netLogN = 0;              // gbacore.c:422  total appended (ring idx = % NETLOG_N)
```
Appended in `net_finishMulti` (gbacore.c:616-628), dedups a repeated same-round timeout row (gbacore.c:608-609).
`sub` = `(s_netExp==4 && !nd->peerCmdReal) ? 1 : 0` (gbacore.c:626) — STATE-E peer word was idle-substituted (prime
desync suspect).

### Dump — `gbacore_net_log_dump(path, seat)` gbacore.c:744-823 -> `sdmc:/cias/netlogs/`
Header lines: role/seat/startN/injectN/finishN/okN/toN/edge/force/stallO (gbacore.c:749-750); peak words (:751);
established/vblMax/paceN/activeMs/baudGame/expSeen/capVbl (:763); **bridge realCmds/idleCmds/leadStalls/leadMax/leadCap/waitMs**
(:772-774); transport rxWords/txFails/busy/peerUp/maxSeat0Round/hostRound (:780); ping pureNetRtt (:786); rate
SUPPLY/DEMAND (:804). Per-row columns (gbacore.c:808):
```
idx,round,frame,dvbl,dt_us,rtt_us,paceus,turnus,exp,w0,w1,ok,sub
```
where `dvbl` = emulated VBlanks since prev logged round, `dt_us` = wall-clock between rounds, `rtt_us` = HOST
send->collect round-trip, `paceus` = JOINER barrier-block this round, `turnus` = role turnaround, `exp` = letter
`'A'+exp` (A..E), `sub` = idle-substituted flag (gbacore.c:812-821).

---

## Where the Celio port edits (the precise seam)

1. **gbacore.c:574-576** — the `net_finishMulti` dispatch fork: add/replace the state-E branch with a LOCAL-TERMINATION
   fill that answers SIO from the local Celio state machine and ships/collects 8-word command frames.
2. **gbacore.c:523-557** — `net_bridge_fill` is the function to rewrite (or replace) for command-framed local termination.
3. **gbacore.c:68-74** — `peerCmd[8]`/`peerCmdIndex`/`peerCmdReal` NetDriver fields are the (currently unused) cache the
   8-word path populates; reset sites net_reset gbacore.c:439 + attach gbacore.c:677.
4. **netlink.c:426-438** `net_cmd_collect` — the ready-made atomic 8-word collector to wire in (it is declared in
   gbacore.c:300 but never called yet).
5. No transport (netlink.c wire format) change needed: PK_WORD/(seat,round) word pipe + RX re-send stay as-is; CRC and
   9-word framing remain inside the emulated link.c, NOT this layer.
