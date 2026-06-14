# Wireless multi-3DS GBA link — architecture & milestone plan

_Verified design study (2026-06-10). Source-checked against external/mgba + uds.h. Build from this._

All corrections confirmed against source:
- No `Error` bit writes anywhere in sio.c/lockstep.c (grep empty) — the timeout→Error path is **new code to build**, not "mirror lockstep."
- `SIOMLT_SEND = 0x12A` (io.h:134), not 0x128 — the study's literal was wrong.
- Cycle table: MULTI baud-0 = 31976–125829 ARM7 cycles (~2–7.5 ms emulated) — confirms "tens of thousands," not "a few hundred."

I have everything verified. Producing the final architecture document.

---

# Dual-GBA Wireless Multi-Console Link — Architecture & Build Plan

*Architect's plan, grounded in verified source. All file:line citations checked against this tree on 2026-06-10. Where the study findings were wrong, I state the correction and cite the real line.*

---

## 1. FEASIBILITY VERDICT

### **FEASIBLE — HARD on timing, not on API.**

The plan is buildable with **zero forks of mGBA** and **no edits to `sio.c` or `lockstep.c`**, protecting the MPL-2.0 rule (#5). It rests on three hook points I verified in this tree:

1. **The `GBASIODriver` vtable is the clean abstraction boundary.** `interface.h:114-129` defines 16 function pointers; `sio.c` knows only this contract and never references `GBASIOLockstepCoordinator`. We attach a driver via a single call already in our code: `g->core->setPeripheral(g->core, mPERIPH_GBA_LINK_PORT, &g->linkDriver.d)` (`gbacore.c:244`). A network driver is a peer of `GBASIOLockstepDriver`, attached the same way.

2. **`start()` returning `false` hands us completion ownership.** `sio.c:144-146`: *"if (!sio->driver->start(driver)) { // Transfer completion is handled internally to the driver"* — mGBA does **not** schedule `completeEvent`. This is the load-bearing seam: it lets us absorb multi-millisecond UDS latency by parking the core, then schedule the finish ourselves once remote words arrive.

3. **The CPU-park primitive already exists and our worker already honors it.** `GBASIOLockstepPlayerSleep` does `cpu->nextEvent = 0; GBAInterrupt(...)` (`lockstep.c:1090-1091`) to force `runLoop` to return at the transfer point; our worker then blocks on `LightEvent_Wait(&e->waitEv)` (`main.c:100`), un-parked by `link_cb_wake → LightEvent_Signal` (`main.c:70`). The stall machinery is reused verbatim; only the *wake source* changes from in-process ack to network RESULT arrival.

The GBA per-transfer payload is **≤8 bytes** (4×u16 `multiData`, or one u8/u32 for NORMAL), and the transfer set is tiny and known (`GBA_SIO_NORMAL_8=0, NORMAL_32=1, MULTI=2`; seats 0..3, `MAX_GBAS 4`). This is the ideal small-packet case for UDS (`UDS_DATAFRAME_MAXSIZE 0x5C6` = 1478 bytes — orders of magnitude of headroom). Because GBA multiplayer is **trade/handshake-deterministic, not lockstep-deterministic**, the two emulators do **not** need cycle-identical execution between transfers — they only agree on the words exchanged at each transfer. This kills the need for rollback netplay (which is wrong here anyway: link words are opaque serial data, unpredictable, and per-frame mGBA save-state is too heavy for two cores on ARM11).

### Why HARD, not trivial — the four corrections that scope the real work

The study's optimistic "a network SIO driver just slots in / it's a star with the host as player 0" framing is **wrong**, and the adversarial passes caught it. The verified reality:

- **A remote seat is NOT a coordinator player.** Every `GBASIOLockstepPlayer` is inseparable from a real local GBA core: `GBASIOLockstepTime` dereferences `player->driver->d.p->p->timing` (`lockstep.c:1019`), `_setData` reads `sio->p->memory.io[GBA_REG_SIOMLT_SEND]` (`lockstep.c:831`; note **SIOMLT_SEND = 0x12A**, `io.h:134`, not 0x128 as the study wrote). A network proxy has no GBA, no `mTiming`, no `memory.io` — attaching it as a player NULL-derefs. **You bridge at the driver vtable, not by adding a coordinator player.**

- **A lone local core won't even start a MULTI transfer.** `nAttached < 2` makes `Start` bail (`lockstep.c:551`), `WaitOnPlayers` early-return (`lockstep.c:1026`), and forces Slave (`lockstep.c:990`). So the **1-core-per-console topologies (4×1, and the single-core legs of 2+1+1) cannot use the in-process lockstep at all** — they need the network driver from day one (this is why the simplest real link, M2, is *not* "feed the existing coordinator one remote word").

- **mGBA never sets the SIOCNT.Error bit.** Verified: grep for `Error` writes in `sio.c` + `lockstep.c` is empty. On no-data, lockstep only does `memset(data, 0xFF, 8)` and warns "running behind" (`lockstep.c:587`). So the timeout/dropped-peer **Error path is new code we add**, not "mirror lockstep."

- **The mode/ready handshake must also cross the wire.** `_setReady` (`lockstep.c:856-860`) gates the game's "Ready" bit by aggregating every player's mode. Across consoles there is no shared mode table — the net layer must propagate mode-set so the remote game ever sees "Ready" and starts a transfer. It is not "only the transfer words."

**RSF correction (saves a milestone of work):** the study said to add `nwm::UDS` and base on a devkitPro UDS `.rsf`. **Already done / moot.** Our `app.rsf` already grants `nwm::UDS` (line **169**) and the `nwm: 0x0004013000002d02` dependency (line **212**). UDS is plain `nwm` IPC + a shared-memory block already covered by our `SystemCallAccess`/`CanShareDeviceMemory`. **No RSF edit is required.** (And there is no `.rsf` in `$DEVKITPRO/examples/3ds/network/uds/` to diff against — that example ships Makefile + source only.)

### The one real risk = latency (HARD, hardware-final)

UDS round-trip is multi-ms with jitter; every transfer parks the initiating core for a full RTT. Trade/battle protocols poll (they don't stream at 60 Hz), so the user sees brief hitches at transfer instants — acceptable. Continuous per-frame link titles (some real-time action/racing) are a documented limitation. This is gated by CLAUDE.md rule #6: **not done until it runs on real New 3DS** — Azahar models neither UDS latency nor core-2 contention.

---

## 2. LAYERED ARCHITECTURE

Translation-unit boundary stays clean: all mGBA includes in `gbacore.c`; all libctru/uds includes in a **new `netlink.c`** (peer of `audio.c`/`touch.c`). The network driver is a **new mGBA driver** in `gbacore.c` (it must touch mGBA headers), but it calls *out* to `netlink.c`'s pure-libctru transport via a tiny C API.

```
┌─────────────────────────────────────── main.c (core 0, main thread) ──────────────┐
│  Lobby UI + seat negotiation state in run_session;  link toggle (main.c:534-554)   │
│  owns C2D render (rule #2: GPU only on main thread)                                 │
└───────────────┬──────────────────────────────────────────┬────────────────────────┘
                │ go/done/waitEv (LightEvent)               │ netlink C API (no mGBA types)
   ┌────────────▼───────────┐  ┌──────────────▼───────────┐ │
   │ worker A  (core 0)     │  │ worker B  (core 2 N3DS)   │ │
   │ emuA.core (gbacore)    │  │ emuB.core (gbacore)       │ │
   │  runLoop→park@transfer │  │  runLoop→park@transfer    │ │
   └──────────┬─────────────┘  └──────────┬────────────────┘ │
              │ setPeripheral(LINK_PORT, driver)              │
   ┌──────────▼───────────────────────────▼──────────────┐   │
   │ gbacore.c LINK LAYER — per-console driver selection: │   │
   │  • LOCAL pair  → GBASIOLockstepDriver + coordinator  │   │
   │                  (gbacore.c:217-251, UNCHANGED)      │   │
   │  • REMOTE seat → NEW GBASIONetDriver (this plan)     │───┼──► netlink C API:
   │     start() sends our word + returns false (stall)   │   │     net_session_*
   │     finishMultiplayer() blocks → fills data[4]       │   │     net_transfer_*
   └──────────────────────────────────────────────────────┘   │     net_lobby_*
                                                                │
   ┌────────────────────────────────────────────────────────┐ │
   │ netlink.c  (UDS transport, core 1, RX/TX thread)        │◄┘
   │  udsInit/Create/Scan/Connect; bind+pullPacket (RX half) │
   │  udsSendTo (TX); seq+ack reliability; appData lobby      │
   │  runs alongside audio_thread (core 1), lower priority    │
   └─────────────────────────────────────────────────────────┘
```

### 2a. The UDS transport module — `netlink.c` / `netlink.h` (NEW)

Pure libctru, zero mGBA types. Owns the whole UDS session and a dedicated RX/TX thread.

**Lifecycle:** `udsInit(0x3000, NULL)` (0x1000-aligned; 0x3000 is convention-headroom — *verify on hardware*). Host: `udsGenerateDefaultNetworkStruct(&net, DGBA_WLANCOMMID, DGBA_ID8, max_nodes=4)` → `udsCreateNetwork(&net, NULL, 0, &bind, DGBA_DATACHAN /*non-zero*/, UDS_DEFAULT_RECVBUFSIZE /*0x2E30*/)`. Client: `udsScanBeacons(...DGBA_WLANCOMMID, DGBA_ID8...)` (array is malloc'd → `free`), then `udsConnectNetwork(&sel.network, NULL,0, &bind, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DGBA_DATACHAN, recvsize)`. The `wlancommID`+`id8` filter means the scan only ever sees other 3DGBA lobbies.

**Reliability (we build it — UDS has no reliable flag):** every packet carries a monotonic `u32 round` (the seq) owned by the host. Drop stale/early packets; resend our WORD until the expected RESULT arrives. Packet (assert `sizeof`, mark `__attribute__((packed))`; both ends ARM11 LE so memcpy is fine):

```c
typedef struct __attribute__((packed)) {
    u8  magic;   // 'G'
    u8  type;    // 0=WORD(client→host) 1=RESULT(host→all) 2=HELLO/seat 3=MODE 4=LOBBY/seatmap
    u8  seat;    // 0..3
    u8  mode;    // GBASIOMode 0=N8 1=N32 2=MULTI
    u32 round;
    union { u16 word[4]; u32 normal; u16 send; } d;
} DgbaLinkPkt;   // 16 bytes; MULTI WORD uses 12
```

**Threading:** the RX/TX thread lives on **core 1 alongside audio, lower priority** — it's I/O-bound (blocks in `udsWaitDataAvailable`), so it doesn't steal from audio's periodic mix. **Never inline `udsSendTo`/`udsPullPacket` on a worker** — they're blocking service calls; running them on an emulation core stalls it for the whole RTT. The worker only ever blocks on a lightweight `LightEvent`. Budget stays intact: **core 0** = main + worker A (`emu_start(&emuA,0,0,...)`, `main.c:334`), **core 2** = worker B (`isN3DS?2`, `main.c:335`), **core 1** = audio (`audio_thread_start`, `main.c:349`) + netlink, **core 3** = OS.

**C API exposed to gbacore.c** (no mGBA types cross this line):
```c
bool net_session_host(const DgbaLobby* adv);          // udsCreateNetwork + advertise
bool net_session_join(int sel);                        // udsConnectNetwork
void net_session_close(void);                          // unbind+destroy/disconnect+exit
int  net_lobby_scan(DgbaLobby* out, int max);          // udsScanBeacons + read appData
int  net_lobby_status(DgbaConn* out);                  // udsGetConnectionStatus + node info
// transfer plane (called from the net driver via gbacore, never from a worker directly):
void net_transfer_send_word(int seat, int mode, u32 round, u16 send);  // queue to TX
bool net_transfer_collect(u32 round, int mode, u16 out[4], u32 needMask, u64 deadline_ms);
void net_transfer_publish(u32 round, int mode, const u16 data[4]);     // host broadcasts RESULT
```

### 2b. The network SIO driver — in `gbacore.c` (NEW `GBASIONetDriver`)

A from-scratch `GBASIODriver` (sibling of `GBASIOLockstepDriver`), **not** a fork of lockstep.c. Implements the vtable (`interface.h:114-129`). Mandatory non-NULL pointers (verified: `handlesMode` is called unconditionally at `sio.c:167`, `connectedDevices` at `sio.c:152,172`, and `deinit` is called on `init`-failure without a NULL check at `sio.c:113-114`):

| Pointer | Logic |
|---|---|
| `init`/`deinit` | both non-NULL (deinit called on init-fail). init returns true. |
| `handlesMode` | `true` for `GBA_SIO_MULTI` (and N8/N32 in the NORMAL milestone). **Required.** |
| `connectedDevices` | **must return 0..3** (peers, not self) or the cycle-table assert zeroes the transfer (`sio.c:351-355`). Derive from the negotiated seat count, **folding in local cores** for 2+2/2+1+1, clamp ≤3. |
| `deviceId` | this seat (0=parent). **Must be stable from the moment MULTI is entered** — seats negotiated over UDS *before* the game enters multiplayer (`deviceId` read at `sio.c:60,170,243`). |
| `setMode`/`writeSIOCNT`/`writeRCNT`/`reset` | mostly pass-through; `setMode` also fires a MODE packet so peers can run `_setReady` equivalent. |
| `start` | **(a)** read our word `driver->p->p->memory.io[GBA_REG_SIOMLT_SEND]`; **(b)** `net_transfer_send_word(...)`; **(c)** park: `user->sleep` + replicate `cpu->nextEvent=0; GBAInterrupt(p)` (copied, not forked, from `lockstep.c:1090-1091`); **(d) return `false`** so we own completion. |
| `finishMultiplayer(data[4])` | the netlink RX, on RESULT arrival, has the agreed `data[4]`; this hook `memcpy`s it in. mGBA's `GBASIOMultiplayerFinishTransfer` (`sio.c:371-389`) then writes SIOMULTI0..3, clears Busy, raises `GBA_IRQ_SIO` **for free** — we never call `GBARaiseIRQ`. (Hook has **no** `cyclesLate` arg — `interface.h:127`; only the public `GBASIOMultiplayerFinishTransfer` does.) |
| `finishNormal8/32` | NORMAL path — one word each way; strictly simpler, the M2 starting point. |

**The wake/finish sequence:** when netlink has all seats' words for `round`, it stores `data[4]`, schedules `sio->completeEvent` (the thing the `start==true` path would have done, `sio.c:154-155`), and signals the worker's event so `runLoop` resumes; `completeEvent` then fires our `finishMultiplayer`. **The wake must be at the netlink-RX point inside/around our driver, NOT the lockstep coordinator's sleep callback** — for a from-scratch driver we reuse only the EmuInstance `waitEv` + `link_cb_sleep`/`link_cb_wake` frontend mechanism (`main.c:62-70`), **not** the `mLockstepUser`/`LinkUser` layer (which is lockstep-specific, `gbacore.c:32-39,202-215`).

**Timeout/Error (new code, mandatory):** since `start` returned false, *nothing else un-parks the core* if a peer drops. netlink enforces a deadline; on expiry it sets SIOCNT.Error (a write mGBA itself never does) and/or returns to standalone, **never** fabricating 0xFFFF into a live trade (a cancelled trade beats a corrupted one).

### 2c. Worker-loop integration — `main.c` (small additions)

The local 2-core path is **untouched** (rule #c). For a 2-core console where one or both seats are remote, the worker still does `gbacore_run_loop` then parks (`main.c:97-100`) — the *driver* attached to that core decides whether the park resolves locally (lockstep peer) or over the net (RESULT arrival). On 2+2/2+1+1, the local pair keeps the in-process coordinator and netlink reconciles only the *remote* seats of `data[4]` before `finishMultiplayer` returns. Drop the `FRAME_TICKS`/`paceDl` real-time pacer (`main.c:105-112`) for networked seats — network RTT is the pacer; pacing on top fights it. Keep pacing for purely-local consoles with no pending networked transfer.

### 2d. Lobby / seat negotiation — `main.c` (new state in `run_session`)

Replace the pause-menu "Link" item (`main.c:534-554`) with a **Lobby screen** reusing existing 2D button/list draw.

- **Seat = GBA `playerId` 0..3** (no separate mapping). Host-authoritative roster in **join order** (host first): walk consoles, hand out contiguous seats. `4×1` → host P1, clients P2/P3/P4; `2+2` → host P1,P2 / client P3,P4; `2+1+1` → host P1,P2 / P3 / P4. Optional manual seat request honored if free, else next-free (mirrors mGBA's own `requestedId` preference-with-packing, `lockstep.c:766-804`).
- **Each console attaches its local cores at the host-assigned `requestedId`** — `main.c:538-539`'s hardcoded `0`/`1` becomes "the seats the host gave me," so the in-process `_reconfigPlayers` places local cores at globally-correct ids.
- **Advertise** via `udsSetApplicationData` (≤0xC8): `{proto, gameCode[4], romCrc32, mode, seatsTotal, seatsOpen, seatNode[4]}`. Clients read it pre-join via `udsGetNetworkStructApplicationData`. **Final seat map and per-seat ROM identity come over the addressed type=4 packet**, not appData, so it's reliable.
- **Pre-START gate:** protocol version match; per-seat game identity — `gbacore_game_code` (reads 0x080000AC, `gbacore.c:269-272`) **+ a ROM CRC32** to catch region/revision; show per-seat ✓/✗. Refuse START until identity holds. After START, freeze (`udsSetNewConnectionsBlocked`).
- **Connection events** each frame via `udsWaitConnectionStatusEvent(false)` + `udsGetConnectionStatus` (`node_bitmask`, bit0=host); usernames via `udsGetNodeInformation` + `udsGetNodeInfoUsername` (UTF-16). **Don't conflate** UDS node id (1-based, host=0x1) with GBA seat id (0-based, parent=0).

---

## 3. MILESTONE PLAN — each independently hardware-testable

Each milestone is independently committable; the local 2-core link (current GREEN state) keeps working throughout. M1–M2.5 need **no second console or no wireless**, de-risking before the user must test on 2+ consoles.

### M1 — UDS lobby + seat negotiation (connect + show seats; NO emulation link)
**Build:** `netlink.c`/`.h` with `udsInit`/`net_session_host`/`net_session_join`/`net_lobby_scan`/`net_lobby_status`; appData advertise + read; connection-status polling. Lobby screen in `run_session` (`main.c`): host advertises, client scans/joins, both render the live seat map (host=P1, join-order packing), per-seat username + game-code/CRC ✓/✗, clean disconnect.
**Files:** NEW `source/netlink.c`, `source/netlink.h`; edit `source/main.c` (lobby state). **No RSF edit** (`nwm::UDS` already at app.rsf:169). No `gbacore.c`/mGBA changes.
**USER verifies on 2 consoles:** Console A "Host" → shows "1/4 seats." Console B "Join" → sees A's lobby in the scan list with A's username + game; joins → both screens show "2/4, P1=A P2=B," identity ✓ when same ROM / ✗ when different. Back out → both return to "0/1 seats" cleanly. *(Proves WiFi + RSF service grant + scan filter + seat algorithm.)*

### M2 — Echo ping / RTT (measure the latency before trusting the stall budget)
**Build:** `net_transfer_send_word`/`collect`/`publish` round-trip a counter packet each frame; on-screen ms RTT + loss counter; exercise broadcast vs unicast and the ignorable `0xC86113F0` retry (`UDS_CHECK_SENDTO_FATALERROR`).
**Files:** `source/netlink.c` (transfer plane), `source/main.c` (debug readout).
**USER verifies on 2 consoles:** both show a live "RTT: N ms, drops: M" readout; move consoles apart / add WiFi noise → watch RTT/jitter/loss. *(This produces the real latency number the stall budget depends on — UNCERTAIN until measured.)*

### M2.5 — Net driver against a LOCAL loopback (1 console, no radio)
**Build:** the full `GBASIONetDriver` in `gbacore.c` (all vtable pointers, `start`→send+park+return-false, `finishMultiplayer`→fill+IRQ-for-free), but back netlink's transport with an **in-memory echo** of this console's own cores. Debug the bridge (park/wake at the net point, `connectedDevices` 0..3, mode/ready handshake) with one device, comparing against the known-good in-process result.
**Files:** `source/gbacore.c` (new driver + a driver-selection switch at `gbacore.c:242-244`), `source/netlink.c` (loopback backend), `source/main.c` (`netEv` wiring).
**USER verifies on 1 console:** flip a "net link (loopback)" toggle; a two-game trade/handshake that works under the in-process link also completes under the loopback net driver — proving the driver path end-to-end with zero wireless and no second person.

### M3 — Simplest real 2-console / 1-core-each link (NORMAL or MULTI over the net)
**Build:** point M2.5's net driver at real UDS. Two single-core consoles (4×1 with N=2), `deviceId` 0 (host/parent) and 1 (client). NORMAL is the simplest cable; a Pokémon trade screen is the canonical test. seq+ack reliability + the timeout→link-lost/Error path (new code — mGBA never sets Error). Note: single-core consoles **must** use the net driver, since lockstep no-ops at `nAttached<2` (`lockstep.c:551`).
**Files:** `source/netlink.c`, `source/gbacore.c`, `source/main.c`.
**USER verifies on 2 consoles:** both load the same game, host+join, START; perform a Pokémon trade (or link battle). The trade completes on both. Pull WiFi mid-trade → both show "Link lost," keep their original Pokémon, drop to standalone (never a cloned/corrupt trade).

### M4 — 4 seats + topologies + integrate with the local 2-core link
**Build:** add the second local seat per console (2+2, 2+1+1), with the local pair on the in-process coordinator and netlink reconciling remote seats of `data[4]` before `finishMultiplayer`; MULTI 4-seat; host clock-owner (P0) election + re-election on drop; mode/ready aggregation over the wire; dropped-seat 0xFFFF-fill-and-keep-others (matching real-cable unplug); the full failure UX (suspend → tear down UDS so peers time out fast, `apt_hook` at `main.c:53-56`).
**Files:** `source/netlink.c`, `source/gbacore.c`, `source/main.c`.
**USER verifies on 3-4 consoles:** a 4-player link game with mixed topology (one 2-core console + two 1-core consoles = 2+1+1). All four seats see each other; a transfer round completes on all four. Kill one client → its seat goes to 0xFFFF/disconnected, the other three keep running. Also re-run the 2-core console standalone (link off) to confirm the local path is unregressed.

---

## 4. RISKS & OPEN QUESTIONS

**Latency (HARD — the gating risk).** UDS RTT is multi-ms with 15–30 ms jitter; every transfer parks a core for one RTT. Trade/battle poll (not 60 Hz streams), so the user sees brief hitches — acceptable. Continuous per-frame-link titles will drop framerate badly = **documented limitation**. *The actual RTT is UNCERTAIN until M2 measures it on hardware* (Azahar won't model it — rule #6). The emulated transfer itself costs **tens of thousands of ARM7 cycles** (MULTI baud-0 = 31976–125829, `sio.c:14-19`; ~2–7.5 ms emulated), which actually *helps hide* network latency — note the study's "a few hundred cycles" was wrong.

**Desync.** The enemy is the local core un-parking before all remote words arrived → `finishMultiplayer` returns 0xFFFF (`lockstep.c:587`). Rule: never finish a transfer without every seat's real word for *this* `round`; drop stale/early packets by seq; retransmit-don't-skip on loss; timeout → clean "link lost," **never fabricate 0xFFFF into a live trade.** Cross-console determinism is NOT required (stall-at-transfer never re-simulates).

**UDS reliability.** No reliable/sequenced flag exists (`UDS_SENDFLAG_Default`/`Broadcast` only) — we build seq+ack. `udsSendTo` can return ignorable `0xC86113F0` (retry next tick via `UDS_CHECK_SENDTO_FATALERROR`). UDS does **not** survive sleep/HOME-suspend — `apt_hook` (`main.c:53-56`) must additionally tear the link down and force reconnect on resume (socket state is gone).

**app.rsf / exheader.** **No work needed** — `nwm::UDS` already granted (app.rsf:169), nwm dependency present (app.rsf:212), shared-memory SVCs already covered. The study's RSF advice was wrong on specifics. (The `.cia` is still the target for rule #1's core-2 reasons, unrelated to UDS.)

**ROM identity.** Exchange `gbacore_game_code` (0x080000AC, `gbacore.c:269-272`) **+ a ROM CRC32** + protocol version pre-START; refuse mismatched links with a clear message rather than letting the games desync into a stuck handshake. SRAM/RTC need not match (per-cartridge save).

**Open questions to resolve on hardware (M2/M3):**
1. Real UDS RTT + jitter + loss rate under light/noisy WiFi (gates the stall budget).
2. Does the 0x3000 `udsInit` shmem size hold, or need more headroom? (Convention, not spec.)
3. Transfer frequency of real Pokémon trade vs link battle — confirm the "polls, not 60 Hz streams" assumption that makes hitches tolerable.
4. Whether the "dropped trade is automatically safe" claim holds for Pokémon's specific trade-commit protocol (engine guarantee is only "we never inject 0xFFFF"; the game-level safety is an assumption to validate).
5. Clock-owner re-election when the host drops mid-session — does a client cleanly fall back to standalone, or hang? (M4.)

---

**Files this plan creates/touches:** NEW `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/netlink.c` + `netlink.h` (UDS transport, pure libctru); `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/gbacore.c` (new `GBASIONetDriver` + driver-selection switch at the existing `setPeripheral`, line 244); `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/main.c` (lobby UI + `netEv` park-wake wiring + drop pacer for net seats). **`app.rsf` needs NO edit** (`nwm::UDS` already at line 169). The Makefile auto-globs `source/`, so the new `.c` needs no build change. Route any (unexpected) RSF/build work to `devkitarm-3ds-build`; the net-driver/lockstep work to the project-local `gba-link-lockstep` agent; UDS-thread core/priority validation to `n3ds-systems` and `n3ds-hardware-testing`.