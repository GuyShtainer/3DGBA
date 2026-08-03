# Celio Firmware — GBA-TRADE-EMU mode, end to end

Scope: how `Mode::gbaTradeEmu` is orchestrated, and the decisive architectural
question — in a live online trade between two real GBAs, does each dongle **relay**
semantic frames between peers, or **synthesize** the partner locally? The answer is
unambiguous from the code: **EMU mode SYNTHESIZES the partner entirely on-device.**
The only thing that crosses the network is the *party file* (a static blob), not the
per-frame Gen-3 link SIO stream. Relaying the live SIO stream is a *different mode*
(`gbaLink` via `UsbSection`/`usbLinkCommand`, and the raw `rawRelay`), and that is the
latency-bound one.

---

## 0. Layer stack and how a command flows (serial/usb ↔ packetLayer ↔ section ↔ usb)

Two host transports exist behind one `Transport` facade:
`Transport` (transport.hpp:9-28) routes status/data to whichever transport the host
last spoke on (`setActive`). WebUSB = `UsbLayer`; WebSerial/CDC-ACM = `SerialLayer`.

`SerialLayer` framing on the wire (serialLayer.hpp:17-31):

    | 0x47 0x42 | channel:1 | len:2 LE | payload[len] |
      sync 'GB'    0=cmd, 1=data, 2=status

`processIncomingByte` is a byte-level state machine (serialLayer.cpp:72-114); a full
frame is handed to `dispatchFrame` (serialLayer.cpp:116-131), which routes
`channelCommand`→command handler, `channelData`→data handler, and flips the active
transport. **Which data handler is registered is the whole ballgame** (see §4).

The GBA-facing side is the `link*` driver (PIO/bitbang) → `PacketLayer`. `PacketLayer`
registers three callbacks into the link driver (packetLayer.hpp:57-60):
`transmitCallback` (what word to clock out next), `receiveCallback` (a word came in),
`transiveDoneCallback` (a unit completed). Every Gen-3 "command" is **8 × uint16 words**
framed by a single CRC word.

PacketLayer's transive state machine (packetLayer.hpp:152-223, packetLayer.cpp:6-66):

- `handshake`: exchanges handshake words; on seeing `LINK_MASTER_HANDSHAKE` it advances
  to `crc` (packetLayer.cpp:16-24).
- `crc`: transmits the running CRC, resets `m_crc=0` for the next command
  (packetLayer.cpp:27-33; transmitCrc at packetLayer.hpp:202-205).
- `command`: 8 words. **CRC = additive sum of every word, both directions**:
  - RX: `m_receivedCommand[m_commandIndex]=rxBytes; m_crc += rxBytes;` (packetLayer.hpp:209-214)
  - TX: `txBytes = m_handler.transive(); m_crc += txBytes;` (packetLayer.hpp:216-223)
  - Seed: `m_crc = LINK_SLAVE_HANDSHAKE; //first crc is always handshake` (packetLayer.hpp:239)
  - After word index 8, it gives `m_commandTransiveSemaphore`, then if the active
    handler's `transiveDone()` returns `CommandState::done` it goes idle and installs
    `emptyCommand()` (packetLayer.cpp:35-62).

A **section** is the consumer. It blocks on `awaitTransiveResults()`
(packetLayer.hpp:83-91) which returns `{received[8], transmitted[8]}`, inspects
`received[0]` (the Gen-3 LINKCMD), and installs the **next** transmit handler via
`setTransiveHandler(...)`. So the pattern everywhere is:

    auto result = m_packetLayer.awaitTransiveResults();   // wait one 8-word command
    switch (result.received[0]) { ... m_packetLayer.setTransiveHandler(<next reply>); }

The handler is a `TransiveStruct{ init, transive(), transiveDone() }`
(TransiveStruct.hpp:11-22): `transive()` is pumped once per word to produce TX words;
`transiveDone()` runs after each 8-word unit and returns `resume`/`done`.

---

## 1. EmuModule main loop / tick — section orchestration

`Control::executeMode()` selects mode (control.hpp:46-129). For `Mode::gbaTradeEmu`
(control.hpp:58-72) it:

1. `applyLedForSlot(LED_SLOT_GBA)`, `link_detectCableType()`.
2. `party::partyInit()` — initializes the **local** party model.
3. **`Transport::registerDataHandler(party::usbReceivePkmFile, nullptr);`**
   (control.hpp:64) — the data channel feeds the *party file*, NOT a SIO relay. (Contrast
   `gbaLink`, control.hpp:79, which registers `usbLink_receiveHandler` instead.)
4. constructs `EmuModule`, calls `emuModule.execute()`.
5. on return, `sendLinkStatus(LinkStatus::EmuTradeSessionFinished)`.

`EmuModule::execute()` (emu.cpp:4-47) is a **section state machine** over `NextSection`:

    NextSection nextSection = NextSection::setup;
    while (!m_cancel) switch (nextSection) {
      setup:      TradeSetup(LINKTYPE_TRADE_SETUP)  -> exit/connection/cancel
      connection: TradeConnection()                 -> lounge/disconnect/cancel
      disconnect: TradeDisconnect()                 -> connection/cancel
      lounge:     TradeLounge()                      -> exit/connection/cancel
      cancel/exit: return;
    }

Each case news a Section, stores `m_currentSection`, calls `.process()` (which blocks
until that phase of the GBA conversation ends), and uses its `NextSection` return to
pick the next. `EmuModule::receiveCommand` is **empty** (emu.hpp:15) and
`canHandle` matches `0x2x` — i.e. the host sends essentially no per-frame commands in
emu mode; the firmware drives the GBA autonomously. `cancel()` forwards to the current
section (emu.hpp:19-23).

Note the menu navigation is **canned**: `TradeSetup`/`TradeLounge` replay a fixed
movement script `{UP,UP,LEFT,UP,RIGHT,READY}` (tradeSetup.hpp:39, tradeLounge.hpp:38)
to walk the avatar to the trade table — more evidence the dongle *plays* the partner
console rather than mirroring a remote human.

---

## 2. How a section runs + how NextSection transitions

`Section` base (section.hpp:8-54) owns a `PacketLayer m_packetLayer` and provides
`connectAsMaster()`/`connectAsSlave()` handshake helpers (section.hpp:24-50). EMU is
slave/master per `CONFIG_SECTIONS_USE_MASTER_MODE`. `cancel()` cancels the packet layer
and sets `m_cancel`.

**TradeSetup** (tradeSetup.cpp:13-108): connects, then loops on `awaitTransiveResults()`
answering Gen-3 link commands:
- `LINKCMD_INIT_BLOCK` → installs `blockCommand()` and serves, in order, a
  `LinkPlayerBlock` (from `linkPLayer(m_linkType)`) then a `TrainerCard`
  (`trainerCardPlaceholder()`) (tradeSetup.cpp:43-72).
- `LINKCMD_READY_EXIT_STANDBY` → `readyExitStandbyCommand()` (tradeSetup.cpp:74-76).
- `LINKCMD_SEND_HELD_KEYS` → replays the canned movement script via `moveCommand()`,
  or on `LINK_KEY_CODE_EXIT_ROOM` sets `nextSection = exit` (tradeSetup.cpp:78-96).
- `LINKCMD_READY_CLOSE_LINK` → `readyCloseLinkCommand()`, then `return nextSection`
  (tradeSetup.cpp:98-101).

This is the universal section idiom: receive a LINKCMD, install the matching reply
handler, and on `READY_CLOSE_LINK` return a `NextSection`. `cancel` returns
`NextSection::cancel`. `TradeLounge` (tradeLounge.cpp) and `TradeDisconnect`
(tradeDisconnected.cpp) follow the same shape with different LINKCMD tables.

`NextSection` (nextSectionState.hpp:3-11): `setup, connection, disconnect, lounge, exit,
cancel`. `emu.cpp` is the dispatcher; sections only *return* the next state.

---

## 3. THE KEY QUESTION — relay vs synthesize, traced exactly

### EMU mode = local SYNTHESIS. Definitive.

The partner GBA is **fully fabricated inside the dongle from a static party file**. The
proof is in `TradeConnection` (tradeConnection.cpp), which is the only EMU section that
exchanges actual mons:

**Partner OUTGOING party (what "the other player" offers) comes from local memory**,
served as canned block payloads — `handleInitialDataExchange` (tradeConnection.cpp:13-129):

    case TradeConnectionState::PartyPart0:
        const auto party = std::as_bytes(party::getParty().subspan<0,200>());
        blockCommandSetup(party.data(), party.size(), 200);   // tradeConnection.cpp:52-59

`party::getParty()` (pokemon.hpp:144) returns the **local** party blob (loaded earlier
by `party::usbReceivePkmFile`, pokemon.hpp:142 — a one-shot file push at session start,
NOT a per-frame stream). Mail (`getEmptyMailPayload`, tradeConnection.cpp:82-90) and
Ribbons (tradeConnection.cpp:92-97) are **stubbed empty**. There is no point at which a
remote peer's live SIO words are inserted into the GBA TX stream.

**Partner INCOMING (the mon the real GBA is giving away) is captured locally**, not
forwarded to a peer — `LINKCMD_CONT_BLOCK` (tradeConnection.cpp:106-116):

    party::partnerPartyConstruct(std::span(...command.data()..., 16).subspan(2));

i.e. the real player's offered party is reassembled into the dongle's own
`partnerParty` model. It is *consumed locally*, not relayed.

**Trade negotiation LINKCMDs are answered locally** by the dongle, not bridged —
`handleTradeNegotiations` (tradeConnection.cpp:131-204): on `LINKCMD_READY_TO_TRADE` it
replies `LINKCMD_SET_MONS_TO_TRADE` and locally records `party::tradePkmnAtIndex(...)`
(tradeConnection.cpp:165-169); it manufactures `LINKCMD_START_TRADE`,
`LINKCMD_PLAYER_CANCEL_TRADE`, `LINKCMD_BOTH_CANCEL_TRADE`, etc.
(tradeConnection.cpp:157-188) entirely from local state via `sendLinkCommand()`
(tradeConnection.cpp:212-219). The dongle **is** the second GBA. No second console is
in the loop during the trade conversation.

So for live online play the path that runs is:
`EmuModule → TradeSetup/TradeConnection/... → blockCommand/moveCommand handlers →
PacketLayer → GBA`. The remote network only ever delivered the *party file* once.

### Where relay-style re-injection of network frames lives (and is NOT used by EMU)

`usbLinkCommand()` (usbLinkCommand.cpp:49-67) is the handler that **re-injects
network/USB-arrived words back into the GBA SIO stream**: `usbLink_receiveHandler`
splits a 64-byte host packet into four 16-byte (8-word) messages into a msgq
(usbLinkCommand.cpp:15-21); `usbLinkTransive` pops the queued word and clocks it out to
the GBA (usbLinkCommand.cpp:28-47). This is the genuine bidirectional bridge. **But it
is installed only by `UsbSection` (usbSection.hpp:17: `m_packetLayer.setTransiveHandler(
usbLinkCommand())`), which is used by `LinkModule` (gbaLink mode), NOT by EmuModule.**
EMU's data handler is `party::usbReceivePkmFile`, and EMU's sections install
`blockCommand`/`moveCommand`/`sendLinkTypeCommand`, never `usbLinkCommand`. So no live
peer frame ever re-enters the GBA SIO stream in EMU mode.

(One quirk in the bridge: `usbLinkTransive` rewrites a leading `0xFF02/0xFF06/0xFF07`
status word to `0x5FFF` on reconnect, usbLinkCommand.cpp:33-38 — a relay-only kludge,
irrelevant to EMU.)

**Verdict:** In a real online EMU trade, each dongle **synthesizes its partner locally**
from a one-time party-file transfer. It does **not** relay semantic Gen-3 frames between
the two real GBAs. (The two real GBAs are not even talking to each other in EMU mode —
each talks only to its own dongle.)

---

## 4. Why EMU is fast and rawRelay/usbLink is latency-bound

The two relay paths require a **network round-trip per link cycle**, so end-to-end
latency = GBA bit period + 2× network RTT, every command:

- **`gbaLink` / `UsbSection`** (usbSection.cpp:41-83): each `awaitTransiveResults()`
  yields one received command which is buffered and `Transport::sendData`'d to the host
  (usbSection.cpp:50-52, usbSection.hpp:73-94), while the *reply* the GBA needs must come
  back over the network through `usbLinkCommand`'s queue (usbLinkCommand.cpp). The GBA's
  master clock cannot proceed until the partner word arrives → throughput is RTT-bound.
  It even drops idle "no button" packets to spare the link (usbSection.hpp:50-62) and
  batches up to 4 packets (usbSection.hpp:96) — both are latency/bandwidth mitigations.

- **`rawRelay` / `RawRelaySection`** (rawRelay.cpp, rawRelaySection.cpp): a dumb word
  pump. `rawRelay_receiveHandler` enqueues network words into `g_rawRelayTxQueue`
  (rawRelaySection.cpp:12-23); `transmitCallback` pops one per GBA clock, defaulting to
  `0x7FFF` (CMD_NONE) when the queue is empty (rawRelaySection.cpp:87-95); RX words go
  back to USB in 32-word flushes with a 10 ms poll (rawRelaySection.cpp:42-85). Because
  the GBA clocks continuously and the partner word must traverse the network each cycle,
  any RTT > one bit period starves the queue → it clocks `0x7FFF` idles, stalling/
  desyncing the link. This is the ~4–5 fps, ~23 ms/round behavior the project saw.

- **EMU** has **no per-cycle network dependency**: every reply word is produced
  synchronously on-device by a `TransiveStruct.transive()` (blockCommand etc.). The GBA
  never waits on the network, so it runs at full link speed. The cost is fidelity —
  mail/ribbons are stubbed and the partner is a fabricated model — but timing is local.

**One-line contrast:** rawRelay/usbLink put the network *inside* the per-word GBA link
loop (correct semantics, RTT-bound, fragile); EMU takes the network *out* of the loop by
synthesizing the partner from a one-shot party file (fast, robust, lower fidelity).
