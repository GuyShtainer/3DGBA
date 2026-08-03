# 3DGBA — Celio LOCAL-TERMINATION Port Spec

> Implementation-ready spec for replacing 3DGBA's per-word SIO-over-UDS relay with a Celio-style
> **local-termination dongle**: each console's net SIO driver **is** the link partner — it answers
> **every** SIO transfer locally at full GBA speed, reconstructs the Gen-3 `[CRC][8 cmd]` framing
> on-device, and ships only **semantic 8-word commands** over UDS asynchronously.
>
> **Audience:** the project author + the specialist subagents (`n3ds-systems`, `gba-link-lockstep`,
> `mgba-core`, `devkitarm-3ds-build`). **Read first** (do not duplicate):
> `docs/kb/wireless-bridge-design.md` (the earlier game-agnostic bridge — its per-game Celio FSM was
> the *fallback*; **this spec promotes local termination to PRIMARY**), the `learn` skill
> `references/gen3-link-protocol.md` (the verified wire format — the single source of truth for every
> constant), and `docs/HANDOFF.md` (current status + the hard-won reverted dead-ends).
>
> **Why this supersedes the bridge doc's primary.** The bridge ("relay the game's own captured SIO
> words, idle-substitute when the peer hasn't arrived") was proven structurally incapable of meeting
> Gen-3 lock-step over UDS: the 2026-06-28 hardware runs (state E) aborted with
> `gRemoteLinkPlayersNotReceived=0x01010101 → CB2_LinkError` and **`gLinkStatus & 0x7F000 == 0`** (no
> checksum/lag/queue bit — see §5). Per-word relay desyncs because the two consoles idle-substitute on
> *different* rounds (the `sub` columns disagree), so each game reassembles the wrong words in the
> wrong per-player slots. Local termination removes the radio from the per-transfer critical path
> entirely: the game is **never** fed a mid-stream idle, so `RECEIVED_NOTHING` cannot arise under
> normal pacing.

---

## 0. Mental model — the dongle, transplanted

Celio is a USB dongle wedged between a real GBA and a host PC. On 3DGBA there is **no real GBA and no
USB**: each console runs its game in an embedded mGBA `mCore`, and UDS replaces both the USB cable and
the Celio server hop. So:

```
 Celio physical layout                      3DGBA transplant (per console)
 ─────────────────────                      ──────────────────────────────
 real GBA ──SIO──► dongle firmware          emulated mCore ──GBASIODriver──► NetDriver (the "dongle")
                     │ packetLayer                              │ packetLayer (ported, header-free)
                     │ sections/callbacks (LINKCMD FSM)         │ sections/callbacks (ported)
                     ▼ Transport (USB)                          ▼ NetTransport (UDS now / socket later)
                  host PC ──► Celio server ──► peer dongle ──► peer GBA      peer console NetDriver ──► peer mCore
```

**The one decision that defines this port** (corrected against the verified digests — see §8). Celio
ships **two mutually-exclusive modes**, and the verified source is unambiguous about which one actually
works in live online play:

- **`Mode::gbaTradeEmu` (the EMU sections: `TradeSetup`/`TradeConnection`/`TradeLounge`/`TradeDisconnect`)
  = LOCAL SYNTHESIS, and this is Celio's REAL online-trade path.** The dongle **fabricates the partner
  entirely on-device** from local structs (LinkPlayerBlock/party/trainerCard) and answers **every**
  Gen-3 LINKCMD locally (`trade-fsm.md §3`, `emu-module.md §3`). The ONLY thing that crosses the network
  is a **one-time party-file push** (per-mon 100-byte upload at session start, never a per-frame SIO
  stream); the two real GBAs are **not even talking to each other** — each talks only to its own dongle.
  This is why EMU is fast and robust: **the network is OUT of the per-word link loop** (`trade-fsm.md §4`).
- **`Mode::gbaLink` (the `UsbSection`/`usbLinkCommand` path) and `rawRelay` = TRANSPARENT RELAY of live
  SIO traffic to a real peer.** This puts the **network INSIDE the per-word GBA link loop** — the GBA's
  master clock cannot proceed until the partner word arrives over the wire → throughput is **RTT-bound**
  (`trade-fsm.md §4`, `server-relay.md §8`). This is the **~4-5 fps, ~23 ms/round** behavior — i.e. it is
  *structurally identical to 3DGBA's already-failing state-E bridge.* **Celio does NOT use this path for
  EMU trades, and neither should we.**

**Correction to the prior spec:** the earlier draft named the relay (`usbLinkCommand`) path "the 3DGBA
primary." That is backwards. **The 3DGBA primary is LOCAL SYNTHESIS** — the EMU-section dispatcher
answers every SIO frame locally, and only *semantic, latency-tolerant* data (the party block, completed
LinkPlayer blocks) crosses UDS, never per-frame raw SIO words. Adopting "relay primary" would re-import
the exact RTT-in-the-loop dependency that produced the state-E `RECEIVED_NOTHING` failure (§5). The
`usbLinkCommand` 8-word relay mechanics (split-64-into-4, resume-forever, the 0xFF0x→0x5FFF fixup) are
still **studied and partially reused** for the data-block transport (§3) and remain an **optional
fallback** for an activity whose synthesis proves intractable — but they are NOT the primary.

So 3DGBA's dongle = **PacketLayer (local SIO termination + additive CRC/framing) + the EMU-section
dispatcher (`switch(command[0])` → install the matching `cl_*` reply handler), synthesizing the partner
locally**, with only the one-shot party/identity blocks (and, for a live-peer activity, completed
semantic blocks) crossing UDS. Transparent per-frame relay is the fallback, not the primary.

---

## 1. ARCHITECTURE MAPPING

Each console's net SIO driver = **one Celio dongle** terminating its emulated game's link locally.
UDS replaces USB + the Celio server. "Reuse" = port the logic ~verbatim (it is header-free uint16/memcpy
— satisfies CLAUDE.md rule #4); "New" = written against the 3DGBA seam; "Drop" = hardware/Zephyr-only.

| Celio component | What it does | 3DGBA equivalent (file / function) | Reuse / New / Drop |
|---|---|---|---|
| **`packetLayer.*`** (RX/TX/8-word framing, additive CRC seed 0xB9A0 + per-block reset, handshake 8FFF/B9A0, `TransiveState` cycle, `m_handler` slot, `awaitTransiveResults`) | The local SIO terminator: turns a word stream into framed commands and back | **New** `source/celiolink.c` `cl_PacketLayer` — a pure-C struct driven by `gbacore.c`'s SIO callbacks. RX/TX/CRC/framing logic **ported verbatim** from packetLayer; the Zephyr `k_sem` (`awaitTransiveResults`) becomes a return-flag the per-transfer call sets when `m_commandIndex==8`. | **Reuse logic / New file.** Header-free (`<stdint.h>`/`<string.h>` only) → dual-compiles on PC. |
| **`callbacks/TransiveStruct` + emptyCommand / blockCommand / blockCommandRequestCommand / moveCommand / sendLinkTypeCommand / readyExitStandby / readyCloseLink / dummyCommand** | The canned LINKCMD_* reply generators (8-word frames) | **Port verbatim** into `source/celiolink.c` as `cl_*` handlers behind the same fn-pointer triple `{init, transive, transiveDone}`. `blockCommand`'s 14-byte chunking + advertised-size zero-pad, `moveCommand`'s 21× repeat, the `[OPCODE,ARG,0…]` index-switch pattern — all carried 1:1. | **Reuse.** Drop `blockRequest`/`dummyCommand` (unused in Celio's tree) unless a section needs them. |
| **`callbacks/usbLinkCommand` + `usbLink_receiveHandler`** (used only by `gbaLink`/`UsbSection`, **NOT by EMU**) | The transparent relay handler: replays network-arrived 8-word packets to the GBA; splits 64-B host frames into 4×8-word packets; `resume`-forever; idle = all-zero word when queue empty | **FALLBACK only** `cl_relayCommand` in `source/celiolink.c` feeding from a **local SPSC ring** filled by the UDS RX thread (replaces `K_MSGQ`). Keep for the fallback path: split-64-into-4, idle=0x00-on-empty, `resume` forever, the **0xFF02/06/07 → 0x5FFF reconnect fixup** (port it, log it as suspicious). NOTE: this is the **RTT-in-the-loop path Celio does NOT use for trades** (`trade-fsm.md §4`) — primary 3DGBA is synthesis, not this. | **Fallback only / New ring.** The `K_MSGQ` (msg=16B, depth **200**, `K_NO_WAIT`) → a sized SPSC ring (≥256, see §3 backpressure). |
| **`sections/section.hpp` connectAsMaster/Slave** (handshake drive, polarity 8FFF vs B9A0) | Master/slave handshake + the dispatcher loop scaffold | **New** `cl_section_step()` in `source/celiolink.c`, called once per completed frame from `gbacore.c`. The `switch(command[0])` dispatcher + "install handler, gate proactive sends on `idle()`" loop **ported**; `k_sleep`→nothing (we are driven by the emulator's transfer cadence, not a Zephyr scheduler). | **Reuse structure.** |
| **`sections/tradeConnection / tradeSetup / tradeLounge / tradeDisconnected`** (the EMU sections) | The Gen-3 trade conversation, **answered ENTIRELY locally**: per-section LINKTYPE announce → `connectAsMaster` → `switch(command[0])` LINKCMD loop → block exchange (LinkPlayer/party/trainerCard) → select/confirm/close → `NextSection` return (`trade-fsm.md §3-8`) | **PRIMARY (synthesis).** Port the section state machine + the `switch(command[0])` dispatcher **and** the local synthesis: `tradeConnection.cpp`'s LinkPlayer→PartyPart0/1/2→Mail→Ribbons block streaming, the `m_requestBlockSize` (1,1,1,3,4) peer-block pulls, `partnerPartyConstruct` capture, the negotiation LINKCMDs wrapped in `CONT_BLOCK` (real cmd in `command[1]`), `tradePkmnAtIndex`. The canned movement script `{UP,UP,LEFT,UP,RIGHT,READY}` drives menu nav. | **Reuse the whole EMU section machine — it IS the primary.** |
| **`payloads`** (LinkPlayerBlock **56B = magic16+LinkPlayer24+magic16**, party **600B = 6×100-byte mons**, trainerCard **96B**, mail **empty = 220B of 0xFF**, ribbons 40B) | The semantic block layouts the trade synthesizes | **New** `source/celiolink_payloads.h` mirroring the structs (primary, since synthesis is primary). Sizes per `payloads-usb.md`: each mon = **100B (0x64)** = 80B BoxPokemon + 20B battle stats; party shipped as **3×200B blocks** (each = two mons) via `blockCommandSetup(...,200)`; partner party captured in 200-byte chunks by `partnerPartyConstruct`. Cross-check vs pret (`/tmp/pret/poke{emerald,firered}`). | **Reuse — these ARE the hot path under synthesis.** |
| **`layers/linkLayer_pio.*` + `pio_master_mode.pio` / `pio_slave_mode.pio`** (RP2040/STM32 PIO that physically clocks SIO; pacing **~8.3 ms/round = 2 transfers/frame** (`timingPacketXg`), **~16.7 ms/round = 1/frame** for SYNC/INTERSYNC (`timingSync`); per-round ~500 µs missed-slave→dummy-word timeout) | Hardware bit-clocking + transfer timing (`pio-clocking.md`) | **Drop / replace.** mGBA owns the SIO clock. Master: keep `net_start` (gbacore.c:483-510) returning `true` so the engine schedules `completeEvent` at realistic `GBASIOTransferCycles`. Slave: keep the driver-self-schedule — capture `io[IO_SIOMLT_SEND]` (gbacore.c:938), set Busy `sio->siocnt|=0x80` (gbacore.c:945), then `mTimingDeschedule/Schedule(&completeEvent, cyc)` (gbacore.c:952-954) — the only way an mGBA slave advances. | **Drop firmware clock; reuse 3DGBA's existing local clock.** |
| **`control.hpp` / Transport (usb/serial dual)** | Registers `usbLink_receiveHandler`; `sendData(64B)` / `sendStatus(2B)` | **New** `NetTransport` vtable (§3, lifted from bridge-design §5) implemented by `uds_transport` over the existing `netlink.c` UDS code. `sendData(64B)`→one UDS datagram; `sendStatus(2B)`→a small control datagram. | **Reuse the seam idea / New impl.** |
| **Celio Server** (per-session rooms, relays frames host↔peer, provides ordering) | The internet relay | **Now:** UDS is peer-to-peer, no server. **Later (online):** a `sock_transport` to our own relay mirrors the Celio server (per-session rooms). Reliable-ordering stays **client-side** (§3) so any transport works. | **Defer (online milestone).** |
| **Celio Client `commandEmitter`** (abstracts USB-relay vs network-relay behind one emitter) | The swap point Celio uses to go from USB to network | **The `NetTransport` seam** (§3) is exactly this. All `cl_*` code calls `NetTransport` + the inject ring — **never** `netlink.c` internals. | **Reuse the abstraction.** |
| **`netlink.c` transport plane** (UDS host/scan/join/close, lobby, ping/RTT, the single RX thread owning `udsPullPacket` netlink.c:515-550, core pinning, 16-byte `DgbaLinkPkt` netlink.c:35-42 with `PK_PING 5/PK_PONG 6/PK_WORD 7`) | The proven-perfect byte transport (toN=0) | **KEEP verbatim** (`source/netlink.c`). Add only the §3 semantic-block send/recv (`PK_DATA`/`PK_DATA_ACK`/`PK_STATUS`/`PK_CMD`; fallback-only `PK_FRAMES`) + the inject ring drain. The per-word `net_transfer_*` / `net_round_wait` parking API is **retired from the hot path** (kept compilable behind the KEY_Y debug fork). NOTE: netlink carries **one SIO word per `PK_WORD`** — no CRC, no 9-word frame on the wire (Gen-3 framing lives inside the emulated link.c), so the port edits gbacore.c, not this layer. | **Reuse; extend with block-level API.** |
| **`gbacore.c` net SIO driver** (`NetDriver` struct gbacore.c:55-75, `net_start` gbacore.c:483-510, `net_finishMulti` gbacore.c:565-641, `gbacore_net_poll` gbacore.c:831-955, the netlog ring) | The current per-word relay (state-E `net_bridge_fill` gbacore.c:523-557) | **Rewire** to drive `cl_PacketLayer` per transfer instead of `net_transfer_collect`/`net_round_wait`. The precise edit seam is the `net_finishMulti` dispatch fork (gbacore.c:574-576) and `net_bridge_fill` (gbacore.c:523-557); the `peerCmd[8]`/`peerCmdIndex`/`peerCmdReal` fields (gbacore.c:68-74, currently declared+reset but UNUSED) are the latent 8-word-command cache. `net_start`/`net_finishMulti`/`gbacore_net_poll` change as in §2. **Keep all netlog/gs instrumentation verbatim** (the §5 probe set). | **Reuse the seam (`setPeripheral` at `gbacore.c:680`, inside `gbacore_net_attach` gbacore.c:646-681); rewrite the driver internals.** |
| **`emu` module** (Celio's GBA-side glue) | n/a on dongle side | **N/A** — mGBA is the emulated GBA. | **Drop.** |

---

## 2. THE LOCAL-TERMINATION DRIVER

The driver stays a `GBASIODriver` installed at `mPERIPH_GBA_LINK_PORT` via the **unchanged** seam
(`setPeripheral` at `gbacore.c:680`, inside `gbacore_net_attach` gbacore.c:646-681). The vtable is
unchanged: `{init, deinit, load, unload, writeRCNT, writeSIOCNT, start, finishMultiplayer}`. What
changes: **every SIO transfer is answered from `cl_PacketLayer`, never from the radio.** The radio only
ferries completed semantic blocks asynchronously.

### 2.1 The PacketLayer state machine (per console, `cl_PacketLayer`)

States are `enum TransiveState { crc, command, handshake }` (`packetLayer.hpp:37-42`), initial =
**`handshake`** (`packetLayer.hpp:253`); transitions in `onTransiveDone()` (`packetLayer.cpp:6-66`).
Advanced **once per SIO transfer** (one uint16 each way):

```
  HANDSHAKE  → exchange single words. The dongle's TX word is chosen by m_handshakeState
               (packetLayer.hpp:181-193): disabled→0xD15E, enabled→0xB9A0 (advertise as ready slave),
               connect→0x8FFF (force master). Leave handshake the instant EITHER side's word == 0x8FFF
               (LINK_MASTER_HANDSHAKE) — sent OR received (packetLayer.cpp:16-23). Then → CRC.
               (CRITICAL: single-word, NOT framed — see gen3-link-protocol.md; framing it drops B9A0
               and hangs before establishment. This was a real bug.)
  CRC        → exactly ONE word exchanged. transmitCrc() returns the dongle's running m_crc;
               the peer's received CRC word is IGNORED (receiveCrc is a no-op, packetLayer.hpp:197-200).
               On the crc→command transition m_crc is reset to 0x00 (packetLayer.cpp:32) for the
               upcoming block. Seed 0xB9A0 (LINK_SLAVE_HANDSHAKE) applies ONLY to the very first CRC
               word after handshake (packetLayer.hpp:239).
  COMMAND    → 8 transfers. Each transfer:
                 tx = m_handler.transive()           // the local reply word (canned/synthesized)
                 m_crc += tx; m_crc += rx            // ADDITIVE MUTUAL CRC, both directions
                 m_transmitted[i]=tx; m_received[i]=rx; ++i  (gate field = m_commandIndex)
               At i==8: onTransiveDone — set "frame complete" flag, flip state→CRC,
                 call m_handler.transiveDone(); if it returns 'done' install cl_emptyCommand.
```

**The CRC is RECOMPUTED locally, not copied from the peer (packetLayer.hpp:197-205, 239; confirmed in
packetLayer.md §3, trade-fsm.md §1).** Each side independently sums the previous frame's 16 words (its
own 8 TX + the 8 RX) and emits that as the next CRC word; `transmitCrc()` returns the dongle's own
accumulator and `receiveCrc()` discards the peer's. Because the scheme is mutual and additive, both ends
arrive at the same value if no word was lost — so the locally-recomputed CRC the dongle emits to its game
*must* equal what the game expects (the game's own TX words fed the same running sum). We never invent or
relay the CRC; we let the running additive sum produce it. Getting the seed (0xB9A0 once) or the
per-block reset (→0x00 each frame) wrong is the classic "reaches LinkPlayer then `CB2_LinkError`"
failure (digest gotcha).

### 2.2 What `net_start` / `net_finishMulti` / `gbacore_net_poll` become

The current code parks the emulated clock on the radio in two places — **both are deleted**:
`net_transfer_collect` inside `net_finishMulti` and `net_round_wait` inside `gbacore_net_poll`. Replace
with PacketLayer answers (O(1), never touch the radio).

**MASTER (HOST = seat 0):**

- `net_start` (`gbacore.c:483`) — **keep returning `true`** so the engine schedules `completeEvent` at
  the realistic `GBASIOTransferCycles` delay (do NOT use 0 cycles; games time their FSM off the
  transfer cadence). Where it used to call `net_transfer_send_word`, it now feeds the game's outgoing
  word into `cl_packet_rx(&pl, io[SIOMLT_SEND])` (the game's word is the *received* side from the
  dongle's POV) and stages the dongle's reply via `cl_packet_tx(&pl)`.
- `net_finishMulti` (`gbacore.c:565-641`) — at the dispatch fork (gbacore.c:574-576) replace the
  state-E `net_bridge_fill` branch; **delete the `net_transfer_collect` park** (gbacore.c:576) and the
  bridge's slow-path collect. Fill `data[]` **immediately, non-blocking**:
  ```c
  data[0] = io_seat0_word;                 // the local game's own word for this transfer
  data[1] = cl_packet_tx(&pl);             // the dongle's locally-SYNTHESIZED partner word
  // (for >2 players, data[2..3] likewise from the local dongle's partner model)
  bool ok = true;                          // NEVER park; the dongle always answers on time
  ```
  Keep the round-retire (gbacore.c:589-592, parent-only on `ok`) and the netlog append
  (gbacore.c:616-628). When a **frame** completes (PacketLayer hits `m_commandIndex==8`), the completed
  8-word command is handed to the dispatcher (`cl_section_step`), which **in the primary (synthesis)
  path** switches on `command[0]` and installs the matching `cl_*` reply handler locally (LinkPlayer /
  party / negotiation); only one-shot semantic blocks (the party file, completed LinkPlayer blocks)
  are sent over UDS asynchronously. (In the **fallback** relay path it would instead enqueue the frame
  for UDS send and pop the next peer frame from the inject ring, §3.) **Always advance** `s_netRound`
  (the per-transfer round is now purely a netlog stamp, not a rendezvous gate).

**SLAVE (JOIN = child = seat 1):**

- `gbacore_net_poll` (`gbacore.c:831-955`) — **delete the A/B/C/D pacing barrier + `net_round_wait`**
  (the per-experiment fork gbacore.c:881-898, the block at gbacore.c:901) and the strict round-lock.
  **KEEP**:
  - **The local clock** — capture at gbacore.c:938, set Busy `sio->siocnt|=0x80` (gbacore.c:945),
    `pendingRound=lastInjectedRound=round` (gbacore.c:946-947), `phase=NET_RECEIVING` (gbacore.c:948),
    then `mTimingDeschedule/Schedule(&completeEvent, cyc)` (gbacore.c:952-954) — the only way a slave
    advances in mGBA. The child clocks on **its own timer at the game's natural cadence**, never waiting
    on the host's word.
  - **The ISR-proof edge-strict capture gate** (`gbacore.c:908-935`) — capture `io[IO_SIOMLT_SEND]` only
    after the prior transfer's SIO ISR ran: `acked=(io[IO_IF]&SIO_IRQ_BIT)==0` (gbacore.c:920),
    `fired=!mTimingIsScheduled(irqEvent)` (gbacore.c:921), `guarded` (gbacore.c:922); the state-D
    edge-strict line `ceiling=(s_netExp!=3)&&…` (gbacore.c:928) DISABLES the time-ceiling so it always
    waits for the `(acked && fired)` edge (gbacore.c:929). **Non-negotiable, kept edge-strict (no
    time-ceiling force-capture)** — force-captures correlated with the checksum error (the reverted
    `f140f08` stale-word lesson). The captured word feeds `cl_packet_rx(&pl, w)`.
- `net_finishMulti` on the child fills `data[0] = cl_packet_tx(&pl)` (the dongle's locally-produced
  host word) and `data[1] = our captured child word`. **No collect park.**

**The local-answer rule (the heart of the fix).** The dongle answers **every** transfer locally. In the
**synthesis primary** the partner reply word is *always* available (it is produced on-device by the
active `cl_*` handler — `blockCommand`/`moveCommand`/etc.; Celio's `emptyCommand` is the steady-state
idle handler, `packetLayer.cpp:57-61`), so the question of an "absent peer word" never arises on the
per-transfer hot path. **The radio is out of the per-transfer loop entirely** — this alone removes the
state-E desync (§5).

*On the **fallback** relay path only:* when the inject ring has no next peer frame at a **frame
boundary** (the dispatcher is `idle()` and needs to relay-pop but the ring is empty), the dongle installs
`cl_emptyCommand` → emits a **whole all-zero 8-word frame** = `RECEIVED_NOTHING`, which the game **safely
skips before any CRC check** (gen3-link-protocol.md; matches `emptyCommand` returning `0x00` per word,
`emptyCommand.cpp:3-6`). **Idle substitution happens ONLY at frame boundaries, NEVER mid-frame** — a
mid-frame idle poisons the running additive CRC → `CB2_LinkError`. That whole-frame-not-word discipline
is the precise thing that distinguishes the relay fallback from the broken per-word state-E relay (which
substituted idle **words** mid-frame on differing rounds — see §5).

### 2.3 The capture-last / edge-strict gate to KEEP

From the proven state-D recipe (`gbacore.c:908-935`): Gen-3 writes `SIOMLT_SEND` in the **main loop,
after** the serial ISR. So the slave's word for transfer N is only valid once transfer N's ISR has
fired AND the game's main loop has run. Capture (gbacore.c:938) is gated on `acked && fired && guarded`
(gbacore.c:920-922, 929); the state-D `ceiling` disable (gbacore.c:928) means **no force-capture past a
time ceiling**. This stays exactly as-is — it is orthogonal to pacing and guarantees the dongle feeds
`cl_packet_rx` the **real post-ISR word**, never a stale 0x0000. (Pre-sending/early-capture shipped
stale words and tripped the host checksum — the reverted `f140f08`. Do not reintroduce.)

### 2.4 The new per-transfer state machine (summary)

```
 per SIO transfer (both seats, driven by mGBA's completeEvent):
   1. local game word  = io[SIOMLT_SEND]        (slave: only via the edge-strict gate)
   2. cl_packet_rx(&pl, local_word)             // feed running CRC + m_received[i]
   3. reply             = cl_packet_tx(&pl)      // canned/relayed/idle; feeds m_crc + m_transmitted[i]
   4. data[me]=local_word; data[peer]=reply; ok=true   // fill finishMultiplayer, NEVER park
   5. if (pl.m_commandIndex==8): frame done →
        cl_section_step(&sec, pl.received_frame): // dispatcher
          PRIMARY (synth): switch(command[0]) → install the matching cl_* handler (LinkPlayer/party/
                      negotiation); send only one-shot semantic blocks over UDS asynchronously
          FALLBACK (relay): enqueue received_frame for async UDS send;
                      next peer frame ← inject_ring_pop()  (else cl_emptyCommand → whole-idle frame)
   6. always advance s_netRound (netlog stamp only)
```

The **rate cap** (bridge-design §2.2, demoted state-D host-rate-follow): the joiner still must not
free-run so far that its emulated clock diverges or the slave VBlank watchdog (>10 VBlanks w/o serial
IRQ → `LAG_SLAVE`) trips. Keep a **non-blocking** governor — when the joiner is ahead of the host's
observed round rate, **skip clocking the next SIO round this slice** (yield emulated time) rather than
`svcSleepThread`-block a worker. This bounds divergence without ever freezing render (60 fps).

---

## 3. WHAT CROSSES UDS

**Corrected against Celio's real USB/network protocol (`payloads-usb.md §4`, `server-relay.md §2-3`).**
Celio does **not** define a "raw 8-word command frame" network message. Its wire protocol is **three
independent channels** (USB had four bulk endpoints = COMMAND, STATUS, DATA-in, DATA-out; the server
mirrors them as Socket.IO events):

1. **DATA** — the only bulk payload: a `DataPacket = { u32 sequence, u16 data[32] }` (= **64 bytes** =
   exactly 4×8-word packets). The server treats `data[32]` as **opaque** (`server-relay.md §3`) and the
   USB layer carries it with **no length/CRC/opcode header** — framing is *endpoint + transfer size*
   only (`payloads-usb.md §4`); the application reassembles logical blocks by counting bytes.
2. **STATUS** — a fixed **2-byte** message (`sendStatus`, `payloads-usb.md §4`): a `LinkStatus` u16
   (`server-relay.md §3` enum: `Empty 0xFFFF, AwaitMode 0xFF02, HandshakeReceived 0xFF03,
   HandshakeFinished 0xFF04, LinkConnected 0xFF05, LinkReconnecting 0xFF06, LinkClosed 0xFF07`).
3. **COMMAND** — host→device, a `CommandType` (`SetMode 0x00, SetModeMaster 0x10, SetModeSlave 0x11,
   StartHandshake 0x12, ConnectLink 0x13`) driving firmware mode/role (`server-relay.md §3, §5`).

**Crucially, in EMU mode (our primary) the DATA channel only ever carries the one-shot PARTY FILE** —
`party::usbReceivePkmFile` ingests **one 100-byte (0x64) mon per push** at session start
(`payloads-usb.md §2`, `trade-fsm.md §0/§3`). The per-frame Gen-3 SIO stream is **never** on the wire in
EMU mode. The 64-byte / 4-frame `DataPacket` shape is the *relay* (`gbaLink`) path's payload — kept here
only because our fallback path and our own block-transfer batching reuse the same 4-up batch geometry.

### 3.1 Message types (new/changed `DgbaLinkPkt` variants)

Existing 16-byte `DgbaLinkPkt` (netlink.c:35-42: `magic 'G', type, seat, mode, u32 round, union d`) and
its `PK_PING 5 / PK_PONG 6 / PK_WORD 7` (netlink.c:43-45) stay for the proven byte plane. Add, mapping
Celio's three channels onto UDS:

| Type | Payload | Celio analog | Meaning |
|---|---|---|---|
| `PK_DATA` | `u32 seq; u8 n; u8 bytes[n]` (n≤64, one datagram) | Celio `DataPacket {seq, u16[32]}` / USB DATA (≤64-B raw) | A chunk of a **semantic block** (party-file mon = 100B over 2 chunks, LinkPlayerBlock 56B, trainer card 96B, party part 200B, empty mail 220B). App reassembles by **running byte count** (no opcode/len header on Celio's wire). PRIMARY use = the one-shot party push. |
| `PK_DATA_ACK` | `u32 ackSeq` | server ack + `requestData` gap-repair | Highest **contiguous** seq received → drives re-send of the un-acked tail. |
| `PK_STATUS` | `u16 linkStatus` (2 B) | Celio `sendStatus` / `StatusPacket{uuid,linkStatus}` | The 2-byte `LinkStatus` control word (peer-present / link-phase). De-dup-able (Celio keys de-dup on a uuid; ping/RTT plane also covers liveness). |
| `PK_CMD` | `u16 command` | Celio `CommandPacket{uuid, CommandType}` | Mode/role arbitration (`SetModeMaster/Slave`, `StartHandshake`, `ConnectLink`). For UDS peer-to-peer this collapses to a tiny mode-negotiation; the server's "first-AwaitMode-wins master election" (`server-relay.md §5`) becomes the host=seat0 rule. |
| (**FALLBACK only**) `PK_FRAMES` | `u32 baseSeq; u8 n; u16 words[n*8]` (n≤4 → ≤66 B) | the relay (`gbaLink`) 64-B = 4×8-word batch | Only for the transparent-relay fallback (§0): up to 4 completed 8-word command frames batched. NOT used by the synthesis primary. |

**Field layout / endianness footgun (`client-emitter.md §1`):** Celio's USB/serial DATA path treats the
64 bytes as **native little-endian** `u16`, but its *local-WebSocket* bridge parses **big-endian** 32×u16
— a real BE-vs-LE mismatch in the source. For UDS we keep **little-endian** `u16` exactly as the GBA
produced them, and document it.

### 3.2 The reliable-ordered guarantee (P1 — non-negotiable)

The peer's frames must arrive **in order, exactly once**. A frame relayed out of order, dropped, or
duplicated mid-conversation desyncs the trade. Implement client-side (so any transport inherits it):

- **Per-seat sequence** `seq` stamped on every `PK_DATA` (and `PK_FRAMES` on the fallback path) — this
  is Celio's `DataPacket.sequence` (`server-relay.md §3,§6`); reuse the existing `round`/sequence
  machinery in `netlink.c`. NOTE Celio's server forwards in **send-order** (`concatMap`, one-ack-in-flight)
  and the `sequence` exists for the *receiver* to detect/repair gaps (`server-relay.md §7`) — keep that
  split.
- **Cumulative ack** (`PK_DATA_ACK` = highest contiguous seq); **re-send the un-acked tail.** Celio's
  exact scheme: app-level ack + retry (5×, 1 s timeout, linear backoff) plus a `requestData([gaps])`
  NACK that replays the cached packets (`server-relay.md §6`). Generalize over UDS.
- **The inject ring exposes blocks ONLY in contiguous order** — a gap blocks that seat's pop until the
  missing block's re-send arrives: **never reorder, only delay** (the synthesis primary mostly avoids
  this since blocks are one-shot, but the fallback relay needs it).

**Inbound ring (replaces Celio's relay `K_MSGQ`, which was msg=16 B, depth **200**, `K_NO_WAIT`,
`usbLinkCommand.cpp:13`):** an SPSC ring, filled by the **single RX thread** that owns `udsPullPacket`
(unchanged), drained at each block/frame boundary by the dongle. A **256-deep** ring is ample. On
overflow, **coalesce + raise `net_inject_overflow` diag** — never silently drop (Celio's `K_NO_WAIT`
silently dropped on overflow; we must not, or we lose data with no error → a phantom RECEIVED_NOTHING).

### 3.3 The transport seam (online-prep — lifted from bridge-design §5)

All `cl_*` code touches **only** this vtable + the inject ring, never `netlink.c` internals. This is
Celio's `CommandEmitterAbstract` seam (`client-emitter.md §1`): a transport-agnostic hub with
`receiveData`/`receiveStatus` outbound + `data$`/`command$`/`close$` inbound, swappable between the
Socket.IO (server) and local-WebSocket concretes. Our analog:

```c
// netlink_transport.h
typedef struct {
    bool (*start)(int seat, int rxCore);
    void (*stop)(void);
    void (*send_data)(int seat, u32 seq, const u8* bytes, int n);   // PK_DATA chunk, non-blocking (Celio receiveData)
    int  (*recv_data_nonblock)(int seat, u8* out, u32* seq, int maxBytes);
    void (*send_status)(u16 linkStatus);                            // PK_STATUS (Celio receiveStatus, 2-byte)
    void (*ack)(int seat, u32 ackSeq);                              // PK_DATA_ACK
    u32  (*rtt_us)(void);
    bool (*peer_present)(void);
    // (fallback relay path only) void (*send_frames)(int seat, u32 baseSeq, const u16* frames, int nFrames);
} NetTransport;
```

- **Now:** `uds_transport` wraps the existing `netlink.c` UDS code (rename current fns to the vtable;
  no behavior change to the proven byte plane).
- **Later (online):** `sock_transport` over `soc:U`/WebSocket to our own relay (per-session rooms,
  mirroring the Celio server) — the dongle, the inject ring, and **all of `gbacore.c` unchanged**.
  Reliable-ordering and idle-suppression live **above** the transport, so a lossy internet path gets
  ordering for free.

**Idle-suppression** (in the relay/`gbaLink` path Celio drops idle "no-button" packets to spare the
link + batches up to 4 packets, `usbSection.hpp:50-62,96` per `trade-fsm.md §4`; the relay's
`usbLinkTransive` also emits all-zero on an empty queue rather than re-sending the last frame,
`usbLinkCommand.cpp:30`): on our **fallback** relay path the dongle does **not** ship `cl_emptyCommand`
idle frames or unchanged held-key frames over UDS — only real semantic frames cross. (The synthesis
primary sidesteps this entirely: it answers idle locally and never puts per-frame traffic on the wire.)

---

## 4. MILESTONE LADDER (hardware-final, CLAUDE.md rule #6)

Testability tiers (cheapest first): **T0** PC host-harness (pure-C cores dual-compile per rule #4) +
simulated lossy/reordering transport · **T1** Azahar one console · **T2** 1 real console, loopback
(`net_link_set_loopback(true)`) · **T3** 2 real consoles. **First completing milestone = a real Gen-3
TRADE (FireRed + Emerald).** Then speed, then battle/record-mix.

| Milestone | Build | Tier | PASS gate | FAIL signal | Diagnostic that proves it |
|---|---|---|---|---|---|
| **T0 — PacketLayer unit** | `cl_PacketLayer` + the ported `cl_*` handlers on PC; feed a canned Gen-3 word script (handshake → SEND_LINK_TYPE → INIT/CONT_BLOCK → close) | T0 | Handshake terminates locally (0x8FFF seen), CRC matches the script's mutual sum every frame, every `cl_*` emits the exact `[OPCODE,ARG,0…]` frame; blockCommand 14-byte chunk + advertised-size zero-pad correct | CRC mismatch; wrong opcode/arg slot; chunk boundary off | A PC unit-test asserting frame-by-frame `m_transmitted[]` + running `m_crc` against the gen3-link-protocol.md golden values |
| **M-A — one real game, local termination, NO radio (synthesis)** | The new `net_finishMulti`/`gbacore_net_poll` driving `cl_PacketLayer`, partner = **local synthesis** (`tradeConnection`-ported canned LinkPlayer/party). One game. | T0 harness → **T2 loopback** | The single game **completes a one-sided trade** (handshake → LinkPlayer block → party exchange → select → confirm → close) at **60 fps**, emulated clock **never parked** (`paceus`≈0), `lnotrecv` reaches **0x00000000**, `lstat` never carries `RECEIVED_NOTHING (0x100)` at completion | Any park reappears; `LAG_SLAVE/MASTER`; `lnotrecv` stuck `0x01010101`; CRC/checksum bit (`lstat & 0x2000`) | netlog `paceus`/`turnus`≈0 + 60 fps render; **gs-log** `lerr`/`lstat`/`lnotrecv` reaching clean |
| **M1 — TWO consoles, real trade via SYNTHESIS + semantic block exchange (THE PAYOFF)** | **Synthesis primary** (NOT relay): each dongle answers SIO **locally** with the EMU sections; the two consoles cross only **semantic blocks** over **real UDS** — each console ships its **own 600-byte party + LinkPlayerBlock** (`PK_DATA` chunked + ack + inject ring), the peer's synthesis serves it as the partner. **FireRed (one console) ↔ Emerald (other).** (This is the architectural inverse of Celio: Celio synthesizes against a *one-shot uploaded* party file; we feed each side the *other live game's* party block — so both real parties actually swap.) | **T3** (2 real consoles — rule #6) | A **real cable-club trade COMPLETES + SAVES + EXITS** on both consoles (both parties actually swap), no `CB2_LinkError`, `lnotrecv→0` on both | `RECEIVED_NOTHING` returns (`lnotrecv=0x01010101`, `lstat&0x7F000==0`); a block arrives out of order/incomplete | Matched **4-file** capture (gs+net × HOST/JOIN, **same run, read gs first**): `lerr/lstat/lbuf0/lbuf1/lnotrecv` clean + the `seq`/ack columns show in-order block delivery |
| **M2 — speed** | Tune batching, idle-suppression, the rate-cap value; optionally pin the wifi channel (netbench result). Because synthesis keeps the network OUT of the per-word loop (`trade-fsm.md §4`), per-frame SIO is already local — speed work is about block-transfer overlap + render budget, not RTT-per-round | T3 | Trade completes **markedly faster than today's 4–5 fps** (target near-60 on the focused game; no per-round RTT dependency), **stable across repeated attempts** (fixes the intermittency) | fps still latency-bound (would indicate the relay fallback crept onto the hot path); divergence/`LAG` | netlog `paceus`≈0 + `turnus` small + `dvbl`/`vblMax` within watchdog |
| **M3 — battle + record-mix + cross-game + hardening** | Extend the EMU-section synthesis to `LINKTYPE_BATTLE/RECORD_MIX/BERRY_BLENDER`; FR↔FR, EM↔EM, cross-version; backpressure/overflow + link-loss recovery; the "can't HOST" oddity. Per-activity, fall back to the relay path only if synthesis proves intractable | T3, per game/activity matrix | Battle + record-mix complete on every supported permutation, 60 fps, no desync over long sessions; clean recovery on real link loss (peer gone → game's own watchdog errors cleanly, not a freeze) | any permutation desyncs; `net_inject_overflow` fires; freeze on peer-gone | the full matrix run + `net_inject_overflow` diag staying 0 + clean `lerr` on forced link-loss |

**Why this order:** T0 proves the framing/CRC math with zero hardware; M-A proves local termination
*on one game* (the non-park property + a completing one-sided trade) before any two-console variance;
M1 is the flagship (two real games actually trading); speed and battle follow only once correctness is
locked. Each gate names the **gs-log link-error decode** (ground truth) + the **netlog** (what crossed)
— never diagnose from the net log alone (HANDOFF lesson: the raw word log is easy to over-read).

---

## 5. RISKS + how the RECEIVED_NOTHING signature is specifically resolved

**The last-run signature (state E, 2026-06-28).** Run B reached `gLinkStatus=0x368`
(`CONN_ESTABLISHED|RECEIVED_NOTHING|…`, FR link-room cb2=08085E04) then aborted at frame 2401 with
`lerr=1, lstat=0x00000000, lbuf0=lbuf1=0, lnotrecv=0x01010101` — **identical on both consoles**.
`gLinkStatus & 0x7F000 == 0` in **every** row of all four files: **no** checksum (0x2000), lag
(0x10000/0x40000), HW (0x1000), queue-full (0x4000), or invalid-id (0x20000) bit ever set, and
`sLinkErrorBuffer` empty at the latch. The failure is **`gRemoteLinkPlayersNotReceived → CB2_LinkError`**
— "received nothing per player slot" — **not** a corrupt-word fault. The mechanism: the per-word bridge
idle-substituted (`sub=1`) on **different rounds** on the two consoles (the HOST and JOIN `sub` columns
disagree), so each game reassembled **the wrong words in the wrong per-player slots** — individually
well-formed words (no error bit, `lbuf=0`), just wrong position.

**How local termination resolves it — the causal chain, point by point:**

| Cause of RECEIVED_NOTHING (state E) | Local-termination removal |
|---|---|
| Idle word substituted **mid-frame** (peer word not on the wire in time) → wrong word at a significant block position | The dongle answers **every** SIO transfer **locally**, on time, from `cl_PacketLayer` — **the radio is never in the per-transfer critical path**, so a peer word is never "late." Idle is substituted **only as a whole 8-word frame at a frame boundary**, which the game **skips before any CRC/slot read** (`RECEIVED_NOTHING` is the *expected* benign idle, not a corruption). |
| HOST and JOIN substitute on **different rounds** → divergent streams → per-player block reassembly corrupts | There is no per-round wire dependency to diverge on: each console reconstructs the **full lock-stepped `[CRC][8 cmd]` framing locally**. Only *completed* semantic frames cross, **in contiguous order, exactly once** (P1). Reassembly sees complete, correctly-ordered blocks → `gRemoteLinkPlayersNotReceived` can actually reach **0**. |
| Latency budget (RTT 19–24 ms, 6–26 drops/run) **forced** idle substitution every late round | Latency now only **delays a frame's *arrival*** (absorbed by the inject ring + the game's 50-deep FIFO), never **delays an *SIO answer*** (always local). The wire is off the per-transfer hot path entirely. |

**Success criterion (re-confirm the signature is cleared):** `lnotrecv` must reach `0x00000000` and
`lstat` must NOT carry `RECEIVED_NOTHING (0x100)` once both sides hand the game complete remote player
blocks — verified on the same matched-4-file capture schema (gs+net × HOST/JOIN, same run).

**Residual risks (and mitigations):**

- **CRC seed / per-block-reset bug** → "reaches LinkPlayer then `CB2_LinkError`." Mitigation: unit-test
  the running `m_crc` against gen3-link golden values at T0; seed 0xB9A0 once at handshake, reset →0x00
  per block.
- **Mid-frame idle ever leaking onto the wire-to-game.** Mitigation: the dispatcher gates idle on
  `idle()` (frame boundary only); assert in debug that `cl_emptyCommand` is never installed with
  `pl.m_commandIndex!=0`. (Synthesis primary mostly avoids this — it's a fallback-relay concern.)
- **Inbound ring overflow under burst** silently dropping a frame → phantom RECEIVED_NOTHING.
  Mitigation: 256-deep ring + `net_inject_overflow` diag (never silent-drop, unlike Celio's K_MSGQ).
- **Frame arrives out of order/duplicated.** Mitigation: per-seat `seq` + cumulative ack + re-send the
  un-acked tail; the inject ring exposes frames only contiguously.
- **`moveCommand` held-key under-emission** — a single emission is silently ignored by the GBA;
  Celio repeats the frame 21×. Mitigation: port the 21× repeat exactly (digest gotcha).
- **`blockCommand` zero-pad gated on advertised size, not bytes copied** (e.g. `setup(0,0,220)` for
  empty mail). Mitigation: gate `done` on the advertised count, per Celio.
- **The 0xFF02/06/07→0x5FFF reconnect fixup** (relay handler, slave→master, index 0 only). Mitigation:
  port it but **log every occurrence** — Celio's author flags it as unexplained.
- **Per-game variance (honest ceiling).** Confidence is highest for Gen-3 trade (pret + Celio
  cross-verified). Battle/record-mix or cross-version may have tighter validation — M3's matrix
  surfaces which (if any) need the **synthesis fallback** for that activity.
- **Master-immediate-completion perturbation.** Keep the realistic `GBASIOTransferCycles` schedule (do
  not complete at 0 cycles) — games time their FSM off the transfer cadence.

---

## 6. LICENSING / CREDIT

- **Compatibility.** Celio (`Celio-Firmware`/`Celio-Server`/`Celio-Client`) is **GPL-3.0**; 3DGBA is
  **GPL-3.0**; mGBA is **MPL-2.0** (file-level copyleft). **All three are compatible** (MPL-2.0 §3.3
  permits combining MPL files into a GPL-3.0 larger work; Celio↔3DGBA are the same license). So we may
  either **direct-port Celio source with attribution** OR **clean-room-reimplement from these notes**.
- **Recommendation: clean-room-reimplement the technique from this spec's notes** (the design ideas —
  local termination, the TransiveStruct dispatch, additive-CRC framing, idle-at-frame-boundary — are
  **not copyrightable**; the code is). Reasons: (a) Celio is C++ + Zephyr (`k_sem`/`k_msgq`/PIO) and
  must be rewritten as header-free pure-C anyway to satisfy CLAUDE.md rule #4 → a near-total rewrite;
  (b) keeps the provenance clean. **If** any block is copied near-verbatim (e.g. a constants table), it
  is GPL-compatible, but mark that file with Celio's copyright header.
- **`mGBA` discipline (rule #5).** The dongle is a **new `GBASIODriver` peripheral**, NOT a fork of
  mGBA's lockstep. Do not modify mGBA files; if any modified mGBA file ships, it stays MPL-2.0 +
  source-available.
- **NOTICE file** `projects/3DGBA/NOTICE` — credit:
  - **Celio (Celio-Link)** for the local-termination architecture (PacketLayer SIO termination +
    TransiveStruct LINKCMD dispatch + additive-CRC `[CRC][8 cmd]` framing + the EMU-section LOCAL
    PARTNER SYNTHESIS that is the actual technique we port; plus its transparent 8-word relay as the
    studied fallback), with the upstream URL + GPL-3.0.
  - **pret/pokeemerald + pret/pokefirered** — the authoritative Gen-3 link protocol that validated the
    wire format + idle-tolerance.
  - Reference the specific files studied: `src/layers/packetLayer.*`, `src/callbacks/*` (TransiveStruct,
    blockCommand, blockCommandRequestCommand, moveCommand, sendLinkTypeCommand, readyExit/CloseLink,
    usbLinkCommand), `src/sections/tradeConnection.cpp`/`tradeSetup.cpp`, `src/link_defines.h`.
- **README "Credits / prior art"** section mirroring the NOTICE, in plain prose.

---

## 7. WHICH SPECIALIST AGENTS BUILD WHICH PART

| Part | Agent | Scope |
|---|---|---|
| The **link FSM** — `cl_PacketLayer` (handshake/CRC/framing), the `cl_*` LINKCMD handlers, the dispatcher (`cl_section_step`), the whole-idle-at-frame-boundary discipline, the per-seat seq/ack reliable-ordered layer, the relay-vs-synthesis decision | **`gba-link-lockstep`** (project-local) | Owns `source/celiolink.c`/`.h` + `celiolink_payloads.h`. The Gen-3 protocol correctness center of gravity. |
| **Driver wiring** — rewiring `net_start`/`net_finishMulti`/`gbacore_net_poll` to drive `cl_PacketLayer` per transfer; keeping the `setPeripheral` seam, the edge-strict capture gate, and ALL netlog/gs instrumentation; the inbound SPSC inject ring | **`mgba-core`** (project-local) | Owns `source/gbacore.c` net-driver internals + the mCore/SIO-callback boundary. |
| **Threading / pacing / frame-budget** — the non-blocking rate cap (skip-clock-when-ahead), keeping both worker threads + the RX thread on their pinned cores, never `svcSleepThread`-blocking a worker, the 60 fps + 804 MHz/L2 budget, the slave VBlank watchdog headroom | **`n3ds-systems`** (toolkit) | Owns the rate governor + the worker/RX-thread interaction; validates `dvbl`/`vblMax`. |
| **Transport** — the `NetTransport` vtable + `uds_transport`, `PK_DATA`/`PK_DATA_ACK`/`PK_STATUS`/`PK_CMD` (and the fallback-only `PK_FRAMES`), the chunked block send/ack/re-send, idle-suppression, `net_inject_overflow` diag; later `sock_transport` | **`mgba-core`** drives the `netlink.c` extension with **`n3ds-systems`** consulting on the RX-thread/core-pin + UDS specifics | Owns `source/netlink.c`/`.h` additions; keeps the proven byte plane untouched. |
| **Build / packaging** — new `.c` files auto-glob (no Makefile edit needed); confirm `.3dsx`+`.cia` build clean; the `app.rsf` grants (`CanAccessCore2`/804MHz/L2/`nwm::UDS`) unchanged; PC dual-compile of `celiolink.c` for the T0 harness | **`devkitarm-3ds-build`** (toolkit) | Owns the build/link/toolchain + the PC host-harness compile of the pure-C core. |
| **Hardware sign-off** — M1/M2/M3 on real New 3DS (the matched 4-file capture, the trade-completes/saves gate, battle, link-loss recovery); what Azahar can't prove | **`n3ds-hardware-testing`** (toolkit) | Owns the rule-#6 "done" gate; collects + reads the gs-first 4-file pairs. |

Spawn `gba-link-lockstep` (FSM) + `mgba-core` (driver) in parallel against the `cl_PacketLayer` API
contract (§2.4); `n3ds-systems` and `devkitarm-3ds-build` join once the seam compiles.

---

## 8. Verification log

Reconciled 2026-06-29 against the 10 verified source digests (8 of 10 readers had failed when this
spec was first synthesized). **CONFIRMED** = digest matches the prior spec; **CORRECTED** = the spec was
wrong/guessed and was edited.

### (a) PacketLayer state machine + CRC math

- **CONFIRMED** — additive **mutual** CRC: `m_crc += rx` (`receiveCommand`) **and** `m_crc += tx`
  (`transmitCommand`), 16-bit wrapping, no polynomial. (`packetLayer.md §3`, `trade-fsm.md §1`,
  `callbacks.md`)
- **CONFIRMED** — seed = `0xB9A0` (`LINK_SLAVE_HANDSHAKE`) applied only to the first CRC after handshake;
  reset to `0x00` per-frame in the `crc→command` transition (`packetLayer.cpp:32`). (`packetLayer.md §3`)
- **CONFIRMED** — 3-state machine `handshake → crc → command`, command gated on `m_commandIndex==8`,
  one CRC word per frame, frame = 1 CRC + 8 command = 9 words. (`packetLayer.md §1-2`)
- **CORRECTED** — the spec's bold claim *"The CRC is relayed, not recomputed"* was **wrong**. The
  digests show CRC is **recomputed locally**: `transmitCrc()` returns the dongle's own running `m_crc`,
  and `receiveCrc()` is a **no-op** (peer CRC discarded). Rewrote §2.1's CRC paragraph. (`packetLayer.md §3`)
- **CORRECTED** — the handshake-word mechanic. Spec said "master emits 0x8FFF, slave 0xB9A0"; the real
  TX word is chosen by `m_handshakeState` (disabled `0xD15E` / enabled `0xB9A0` / connect `0x8FFF`), and
  the trigger to leave handshake is **either** side's word == `0x8FFF`. Added to §2.1. (`packetLayer.md §2,§5`)

### (b) Relay-vs-synthesis architectural decision — THE BIG CORRECTION

- **CORRECTED (load-bearing)** — the prior spec named **transparent per-frame relay the "3DGBA primary."
  This is backwards.** The verified source is unambiguous: Celio's live online trade runs in
  `Mode::gbaTradeEmu`, which **SYNTHESIZES the partner entirely on-device**; the two real GBAs **never
  talk to each other** and only a **one-shot party file** crosses the network. The relay path
  (`usbLinkCommand`/`UsbSection`/`rawRelay`) is a *different* mode that puts the network **inside the
  per-word loop** → **RTT-bound ~4-5 fps** — i.e. structurally identical to 3DGBA's already-failing
  state-E bridge. (`trade-fsm.md §3-4`, `emu-module.md §3-4`, `server-relay.md §8`)
- Rewrote §0 (the defining decision), the §1 mapping rows (tradeConnection/payloads/usbLinkCommand),
  §2.2/§2.4 (synthesis primary, relay fallback), and reframed M1 (§4): the "two real games trade" payoff
  is achieved by **local synthesis fed each console's live party block over UDS**, not by relaying SIO
  frames. The whole-idle-at-frame-boundary discipline was demoted to a **fallback-relay** concern
  (synthesis always has a local answer). (`trade-fsm.md`, `emu-module.md`, `last-run-signature.md §4`)

### (c) The semantic message set over USB/UDS

- **CORRECTED** — §3 invented `PK_FRAME/PK_FRAMES` carrying raw 8-word SIO command frames as the wire
  protocol. Celio's **real** USB/network protocol is **three channels**: a 64-byte **DATA** `DataPacket
  {u32 seq, u16[32]}` (opaque, no len/CRC/opcode header — framing is endpoint+size only), a **2-byte
  STATUS** channel (`LinkStatus` enum `0xFF01..0xFF07`), and a **COMMAND** channel (`CommandType`
  `0x00/0x10/0x11/0x12/0x13`). Rewrote §3.1's message table to `PK_DATA / PK_DATA_ACK / PK_STATUS /
  PK_CMD`, with `PK_FRAMES` demoted to fallback-only. (`payloads-usb.md §4`, `server-relay.md §2-3`)
- **CORRECTED** — block sizes. Spec's §1 payload row had vague sizing; corrected to the verified layouts:
  party = **600 B = 6×100-byte mons** (not "3×200 storage"; 200 is the *transfer-chunk* size), each mon
  **100 B (0x64)** = 80 B BoxPokemon + 20 B stats, LinkPlayerBlock **56 B**, trainer card **96 B**,
  empty mail = **220 B of 0xFF**. (`payloads-usb.md §1-3`)
- **CONFIRMED** — Celio's reliability model that §3.2 leans on: per-sender `sequence`, send-order FIFO
  (`concatMap`, one-ack-in-flight), app-ack + 5× retry, `requestData` gap-repair. (`server-relay.md §6-7`)
- **CONFIRMED** — the transport seam idea = Celio's `CommandEmitterAbstract` (swap Socket.IO vs local
  bridge). Noted the BE-vs-LE endianness footgun in Celio's own two transports. (`client-emitter.md §1`)
- **CONFIRMED** — relay `K_MSGQ` was msg=16 B, depth **200**, `K_NO_WAIT` (silent drop). (`callbacks.md §10`)

### (d) Current 3DGBA driver line numbers + function shapes

All line numbers re-verified against `current-driver.md` (mapped against the live working tree). Every
corrected number below was a guess in the prior spec:

- **CORRECTED** — `setPeripheral` seam: spec said `gbacore.c:592` (twice); real = **gbacore.c:680**,
  inside `gbacore_net_attach` (**gbacore.c:646-681**). Spec's "seam at gbacore.c:560-592" was wrong.
- **CORRECTED** — slave self-clock: spec said `gbacore.c:840-849`; real = capture **gbacore.c:938**,
  Busy **:945**, `mTimingDeschedule/Schedule(&completeEvent)` **gbacore.c:952-954**.
- **CORRECTED** — ISR-proof edge-strict capture gate: spec said `gbacore.c:812-830`; real =
  **gbacore.c:908-935** (`acked` :920, `fired` :921, `guarded` :922, state-D `ceiling` :928, gate :929).
- **CONFIRMED** — `net_start` **gbacore.c:483-510** (parent-only, returns true), `net_finishMulti`
  **gbacore.c:565-641**, `gbacore_net_poll` **gbacore.c:831-955**, `net_round_wait` use at
  **gbacore.c:901**. (Spec's bare "483/565/831" were right.)
- **CORRECTED/ADDED** — the actual edit seam is the `net_finishMulti` **dispatch fork at
  gbacore.c:574-576** and `net_bridge_fill` (**gbacore.c:523-557**), with the latent
  `peerCmd[8]`/`peerCmdIndex`/`peerCmdReal` fields (**gbacore.c:68-74**, declared+reset but UNUSED) as
  the 8-word cache, and `net_cmd_collect` (**netlink.c:426-438**, declared at gbacore.c:300 but never
  called) as the ready-made atomic 8-word collector. Added throughout §1-§2.
- **CONFIRMED** — netlink.c carries **one SIO word per `PK_WORD`** keyed by `(seat,round)`; **no CRC, no
  9-word frame on the wire** — Gen-3 framing lives inside the emulated link.c. So the Celio port edits
  **gbacore.c (the SIO driver), not the netlink transport**. This validates the spec's "netlink stays a
  dumb word pipe" stance. (`current-driver.md §3`)

### Cross-cutting confirmations (unchanged in spec)

- **CONFIRMED** — `moveCommand` repeats the held-key frame **21 times** (`g_repeats != 20`).
  (`callbacks.md §8`)
- **CONFIRMED** — `blockCommand`: 14-byte chunks (`MAX_CHUNK`), termination gated on **advertised**
  `g_blockMaxSize` not bytes copied, final short chunk zero-padded. (`callbacks.md §9`, `trade-fsm.md §1`)
- **CONFIRMED** — `usbLinkCommand` 0xFF02/06/07→0x5FFF fixup is **word-0 only**, slave→master reconnect,
  flagged unexplained by Celio's author. (`callbacks.md §10`)
- **CONFIRMED** — state-E failure signature: `gRemoteLinkPlayersNotReceived=0x01010101 → CB2_LinkError`,
  `gLinkStatus & 0x7F000 == 0` (no checksum/lag/HW/queue/invalid-id bit) in every row, `lbuf=0`; HOST/JOIN
  `sub` columns disagree (the desync). The §5 causal chain is fully supported. (`last-run-signature.md`)
- **CONFIRMED** — PIO clocking numbers: ~8.3 ms/round (2/frame, `timingPacketXg`), ~16.7 ms/round
  (1/frame SYNC, `timingSync`), ~500 µs missed-slave timeout. (`pio-clocking.md`)
