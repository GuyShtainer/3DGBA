# Celio-Firmware: Gen-3 Link Local-Termination Layer

Documenting the **packet layer** (`PacketLayer`) which is the heart of Celio's local
termination: the dongle answers the GBA's SIO clock locally, frame by frame, computing
its own CRC and shipping 8-word commands. The serial/transport files are a **separate
host↔dongle USB/CDC path** (NOT the GBA wire) and are summarized last.

All paths under
`celio/Celio-Firmware/src/`.

---

## 0. Wire model recap (Gen-3 SIO)

A GBA multiplayer transfer clocks **one 16-bit word per node per transfer**. The master
(GBA) drives the clock; the dongle is wired as a peer. The link driver
(`linkLayer.h`/`.c`, not shown but its ABI is in `layers/linkLayer.h`) calls back into
`PacketLayer` three times per word:

- `link_setReceiveCallback` → `receiveCallback(uint16_t rx, void*)` — the word just
  shifted **in** from the GBA. (`linkLayer.h:18,24`)
- `link_setTransmitCallback` → `transmitCallback(void*) → struct NextTransmit {uint16_t
  value; uint32_t timingUs;}` — the word to shift **out** next, plus how long to wait
  before the next transfer. (`linkLayer.h:12-16,19,22`)
- `link_setTransiveDoneCallback` → `transiveDoneCallback(uint16_t rx, uint16_t tx,
  void*)` — fired after the word completes, carrying both directions. (`linkLayer.h:20,26`)

All three are registered in the `PacketLayer` ctor (`packetLayer.hpp:58-60`).

---

## 1. Per-transfer call — what runs per 16-bit SIO word

Per word the link driver invokes the **receive** then the **transmit** then (on
completion) the **transiveDone** callback. The dispatch is a 3-state switch on `m_state`:

`packetLayer.hpp:152-172`
```cpp
void onReceive(uint16_t rxBytes) {
    switch(m_state) {
        case TransiveState::crc: return receiveCrc(rxBytes);
        case TransiveState::command: return receiveCommand(rxBytes);
        case TransiveState::handshake: return receiveHandshake(rxBytes);
    };
}
struct NextTransmit onTransmit() {
    switch(m_state) {
        case TransiveState::crc: return {transmitCrc(), m_timingUs};
        case TransiveState::command: return {transmitCommand(), m_timingUs};
        case TransiveState::handshake: return {transmitHandshake(), m_timingUs};
        default: return {0x00, m_timingUs};
    };
}
```

**The command-word producer is `transmitCommand()`** (`packetLayer.hpp:216-223`):
```cpp
uint16_t transmitCommand() {
    uint16_t txBytes = (m_handler.transive != nullptr) ? m_handler.transive() : 0x00;
    m_crc += txBytes;
    m_transmittedCommand[m_transmitCommandIndex] = txBytes;
    m_transmitCommandIndex++;
    return txBytes;
}
```
It **returns the 16-bit word the dongle drives onto the wire** for this transfer: it
calls the active command handler's `transive()` (which returns the next of its 8 words),
adds it to the running CRC, records it, advances the index. This is the per-SIO-word call
the port must replicate.

The matching consumer is `receiveCommand()` (`packetLayer.hpp:209-214`):
```cpp
void receiveCommand(uint16_t rxBytes) {
    m_receivedCommand[m_commandIndex] = rxBytes;
    m_crc += rxBytes;
    m_commandIndex++;
}
```

---

## 2. The exact state machine

Three states (`packetLayer.hpp:37-42`):
```cpp
enum class TransiveState { crc, command, handshake };
```
Initial state = **`handshake`** (`packetLayer.hpp:253`).

All transitions happen in `onTransiveDone()` (`packetLayer.cpp:6-66`):

### handshake → crc (`packetLayer.cpp:10-25`)
```cpp
case TransiveState::handshake:
    m_transmitedHandShake = txBytes;
    m_receivedHandshake = rxBytes;
    k_sem_give(&m_handshakeSemaphore);
    m_timingUs = timingHandshake;                 // 30097 us between handshakes
    if (rxBytes == LINK_MASTER_HANDSHAKE || txBytes == LINK_MASTER_HANDSHAKE) {
        m_state = TransiveState::crc;
        m_timingUs = timingCommandBytes;          // 1378 us
        if (m_mode == Mode::master) m_masterClock.startTransmissionSync();
    }
    break;
```
**Trigger to leave handshake:** either side emits `LINK_MASTER_HANDSHAKE` (`0x8FFF`).
Until then it loops in handshake, re-driving whatever `transmitHandshake()` returns
(below).

### crc → command (`packetLayer.cpp:27-33`)
```cpp
case TransiveState::crc:
    k_timer_start(&m_timeoutTimer, K_MSEC(14), K_NO_WAIT);  // STM32 only
    m_state = TransiveState::command;
    m_crc = 0x00;                                 // <-- CRC reset HERE, per frame
    break;
```
Exactly **one** word is exchanged in the `crc` state (the CRC word), then it flips to
`command` and **zeroes the CRC accumulator for the upcoming 8-word block**.

### command → (loops 8×) → crc (`packetLayer.cpp:35-62`)
```cpp
case TransiveState::command:
    if (m_commandIndex == 7) m_timingUs = timingBetweenCommands;   // 12953 us gap before frame end
    if (m_commandIndex != 8) return;              // <-- the 8-word counter gate
    m_commandIndex = 0;
    m_transmitCommandIndex = 0;
    m_state = TransiveState::crc;                 // back to crc for next frame
    m_timingUs = timingCommandBytes;
    k_timer_stop(&m_timeoutTimer);
    k_sem_give(&m_commandTransiveSemaphore);      // wake awaitTransiveResults()
    if (m_waitForDisable >= 1) { m_waitForDisable = 0; k_sem_give(&m_saveToDisableSemaphore); }
    if (m_handler.transiveDone != nullptr && m_handler.transiveDone() == CommandState::done) {
        m_idle = true;
        m_handler = emptyCommand();               // retire handler, go idle
    }
    break;
```

**The field that counts the 8 command words is `m_commandIndex`** — incremented in
`receiveCommand()` (rx side). It is the gate: the state machine stays in `command` and
returns early until `m_commandIndex == 8`, then both indices reset, state → `crc`, and
the per-frame `transiveDone()` handler hook fires once.

> Note the timing nuance: at `m_commandIndex == 7` (i.e. after the 7th word, before the
> 8th) the inter-word delay is bumped to `timingBetweenCommands` (12953 us) so there's a
> long gap framing the boundary.

**Full per-frame cycle:** `crc` (1 word) → `command` (8 words) → `crc` (1 word) →
`command` (8 words) → … Each frame is **1 CRC word + 8 command words = 9 words**, matching
the known Gen-3 `[CRC][8 cmd]` 9-word frame.

---

## 3. The additive CRC — exact semantics

Accumulator: `uint16_t m_crc` (`packetLayer.hpp:239`).

### Seed
```cpp
uint16_t m_crc = LINK_SLAVE_HANDSHAKE; // first crc is always handshake   (packetLayer.hpp:239)
```
i.e. seeded to **`0xB9A0`** (`LINK_SLAVE_HANDSHAKE`, `link_defines.h:62`). This seed only
matters for the **very first** CRC word emitted (the first `transmitCrc()` after the
handshake completes), because thereafter it is reset to 0 each frame.

### Which words are summed
**Both directions** — own TX and peer RX:
- RX side: `m_crc += rxBytes;` in `receiveCommand()` (`packetLayer.hpp:212`)
- TX side: `m_crc += txBytes;` in `transmitCommand()` (`packetLayer.hpp:219`)

So over one 8-word command block, `m_crc = Σ(all 8 received words) + Σ(all 8 transmitted
words)` — a **mutual additive checksum** (16-bit wrapping add, no carry folding, no
polynomial). The handshake and CRC states do NOT add to it (`receiveCrc()` is a no-op,
`packetLayer.hpp:197-200`; `transmitCrc()` only reads).

### When reset
**Per frame**, not per handshake: `m_crc = 0x00;` in the `crc→command` transition
(`packetLayer.cpp:32`). Reset happens the instant the CRC word for frame N has been
exchanged and we enter the command phase of frame N — so the accumulation that starts now
is the checksum that will be **emitted as the CRC word of frame N+1**.

### Recomputed or copied?
**Recomputed locally.** `transmitCrc()` simply returns the dongle's own running
accumulator:
```cpp
uint16_t transmitCrc() { return m_crc; }                 (packetLayer.hpp:202-205)
```
The peer's CRC word is **received but ignored**:
```cpp
void receiveCrc(uint16_t rxBytes) { /* don't care */ }   (packetLayer.hpp:197-200)
```
So the dongle never copies the GBA's CRC — it independently sums the previous frame's 16
words (its 8 + the GBA's 8) and emits that. (Both ends run the same additive scheme, so
the values match if no word was lost.)

**Port checklist:** seed 0xB9A0 once → emit as first CRC → reset to 0 → for each of 8
command words add both the word you send and the word you receive → emit that sum as the
next frame's CRC → reset to 0 → repeat.

---

## 4. Queue depth + skip-on-all-zero (RECEIVED_NOTHING)

### Queue depth
The "queue" is a fixed pair of **8-entry arrays**, one frame deep, no ring:
```cpp
int m_commandIndex = 0;
std::array<uint16_t, 8> m_receivedCommand = {};
int m_transmitCommandIndex = 0;
std::array<uint16_t, 8> m_transmittedCommand = {};        (packetLayer.hpp:247-250)
```
Depth = **8 words = one command block**. Filled during the `command` state, snapshot
handed off (by value via `std::span`) when the block completes:
```cpp
TransiveResult awaitTransiveResults() {
    k_sem_take(&m_commandTransiveSemaphore, K_FOREVER);
    return TransiveResult{ std::span(m_receivedCommand), std::span(m_transmittedCommand) };
}                                                          (packetLayer.hpp:83-91)
```
`m_commandTransiveSemaphore` is binary (`k_sem_init(...,0,1)`, `packetLayer.hpp:62`), so a
consumer gets woken exactly once per completed 8-word frame.

### Skip-on-all-zero / RECEIVED_NOTHING
There is **no all-zero skip inside PacketLayer itself** — every word, including 0x0000, is
stored and summed. The "RECEIVED_NOTHING / idle frame" semantics live one level up in the
**command handlers** via the `transive()` content and the `transiveDone()` return:

- `emptyCommand()` transmits all zeros and reports `done` immediately
  (`emptyCommand.cpp:3-6,14`):
  ```cpp
  uint16_t emptyCommandTransive() { return 0x00; }
  // .transiveDone = [](){ return CommandState::done; }
  ```
  When the active handler is `emptyCommand`, the dongle ships an 8-word all-zero frame
  (the on-wire idle / RECEIVED_NOTHING) and goes idle.
- After any handler returns `CommandState::done`, PacketLayer sets `m_idle = true` and
  installs `emptyCommand()` (`packetLayer.cpp:57-61`) — so the **steady-state idle frame
  is 8× 0x0000**, with a recomputed CRC over it.

`LINKCMD_EMPTY 0x0000` (`link_defines.h:35`) is the command value for "nothing".

---

## 5. Handshake words + switch to command frames

Handshake values (`link_defines.h:61-63`):
```c
#define LINK_MASTER_HANDSHAKE   0x8FFF
#define LINK_SLAVE_HANDSHAKE    0xB9A0
#define LINK_HANDSHAKE_DISABLE  0xD15E
```

The word the dongle drives during handshake is chosen by `m_handshakeState`
(`packetLayer.hpp:181-193`):
```cpp
uint16_t transmitHandshake() {
    switch(m_handshakeState) {
        case HandShakeState::disabled: return LINK_HANDSHAKE_DISABLE;   // 0xD15E
        case HandShakeState::enabled:  return LINK_SLAVE_HANDSHAKE;     // 0xB9A0
        case HandShakeState::connect:  return LINK_MASTER_HANDSHAKE;    // 0x8FFF
    }
    return 0xDEAD;
}
```
`m_handshakeState` starts `disabled` (`packetLayer.hpp:254`). External control:
- `enableHandshake()` → `enabled` → dongle answers the GBA with `0xB9A0` (advertises as a
  ready slave) (`packetLayer.hpp:116`).
- `connectHandshake()` → `connect` → dongle drives `0x8FFF` (becomes master / forces the
  link open) (`packetLayer.hpp:118`).
- `isHandshakeEnabled()` checks `m_transmitedHandShake == LINK_SLAVE_HANDSHAKE`
  (`packetLayer.hpp:120`).

**Switch to command frames:** in `onTransiveDone` handshake case — as soon as **either**
the received or transmitted word equals `LINK_MASTER_HANDSHAKE` (0x8FFF), `m_state` flips
to `crc` (`packetLayer.cpp:16-23`). So the GBA sending 0x8FFF (it became master) OR the
dongle driving 0x8FFF (connect mode) both kick off the command phase. The first thing sent
in the command phase is the CRC word seeded at 0xB9A0.

---

## 6. The `m_handler` {init, transive, transiveDone} fn-pointer mechanism

`TransiveStruct` (`callbacks/TransiveStruct.hpp:11-22`):
```cpp
enum class CommandState { resume, done };          // line 5-9
struct TransiveStruct {
    using InitCallback        = void(*)(std::span<const uint16_t>);
    using TransiveCallback    = uint16_t(*)();
    using TransiveDoneCallback= CommandState(*)();
    std::span<const uint16_t> userData;
    InitCallback        init;        // called once when handler installed
    TransiveCallback    transive;    // returns next 16-bit word, called per command word
    TransiveDoneCallback transiveDone; // called once per completed 8-word frame
};
```

Installed via `setTransiveHandler` (`packetLayer.hpp:76-81`):
```cpp
void setTransiveHandler(struct TransiveStruct handler) {
    m_handler = handler;
    m_idle = false;
    if (handler.init != nullptr) handler.init(handler.userData);
}
```

Lifecycle of the three pointers:
- **`init(userData)`** — fired exactly once at install (`packetLayer.hpp:80`). Used to
  stage the payload (e.g. `blockCommandSetup`). Many handlers leave it `nullptr`.
- **`transive()`** — called inside `transmitCommand()` **once per command word**
  (`packetLayer.hpp:218`); returns the word to drive. Handlers walk their own 8-entry
  array and self-wrap (see block example below).
- **`transiveDone()`** — called inside `onTransiveDone` **once per completed 8-word frame**
  (`packetLayer.cpp:57`). Return `CommandState::resume` to keep the handler for another
  frame, or `CommandState::done` to retire it → PacketLayer goes idle and reinstalls
  `emptyCommand()` (`packetLayer.cpp:57-61`).

### Worked example — `blockCommand` (`callbacks/blockCommand.cpp`)
Shows exactly how a handler queues its 8 words and signals multi-frame continuation:
```cpp
uint16_t blockCommandTransive() {                          // lines 57-69
    uint16_t ret = g_initSend ? g_blockCommandContent[g_index] : g_blockCommandInit[g_index];
    g_index++;
    if (g_index == 8) { g_index = 0; g_initSend = true; }   // wrap every 8 words
    return ret;
}
CommandState blockCommandChunk() {                          // transiveDone, lines 24-44
    if (g_blockMaxSize == 0) return CommandState::done;      // finished -> retire
    if (!g_initSend) return CommandState::resume;            // first frame = the INIT header
    // copy up to MAX_CHUNK(14) payload bytes into g_blockCommandContent+2, advance
    ...
    return CommandState::resume;                             // more frames to send
}
```
- First 8-word frame = `g_blockCommandInit` = `{LINKCMD_INIT_BLOCK(0xBBBB), size, 0x80,
  0,0,0,0,0}` (`blockCommand.cpp:17-19,54`).
- Subsequent frames = `g_blockCommandContent` = `{LINKCMD_CONT_BLOCK(0x8888), 14 bytes of
  payload...}` (`blockCommand.cpp:21,30-43`); 14 payload bytes per frame because words
  1..7 hold 14 bytes (word 0 is the command id).
- When the block is fully sent (`g_blockMaxSize==0`), `transiveDone` returns `done` and
  PacketLayer retires the handler.

This is the template a 3DS port must follow for **every** Gen-3 command: a per-word
`transive()` that walks an 8-word buffer wrapping at 8, plus a `transiveDone()` returning
resume/done to control how many frames the command spans.

---

## 7. Timing constants (microseconds)

`packetLayer.hpp:51-53`, applied as `NextTransmit.timingUs`:
```cpp
static constexpr uint32_t timingHandshake       = 30097;  // gap between handshake words
static constexpr uint32_t timingCommandBytes     = 1378;  // gap between command/crc words
static constexpr uint32_t timingBetweenCommands  = 12953; // gap before the 8th word / frame boundary
```
- Default during handshake loop: 30097 us.
- During command/crc words: 1378 us.
- At `m_commandIndex == 7`: bumped to 12953 us (`packetLayer.cpp:38`) — the long
  inter-frame gap.

STM32-only safety net: a 14 ms `m_timeoutTimer` is armed entering `command`
(`packetLayer.cpp:29`) and stopped on frame completion (`packetLayer.cpp:49`); on expiry
`packetTimeout → reset()` (`packetLayer.hpp:283-288`) — i.e. if a frame stalls mid-block
it resets state. (A port must implement an equivalent watchdog so a dropped word doesn't
wedge the state machine forever.)

### MasterClock (master-mode clock source, `masterClock.hpp`)
Only used when the dongle is **master** (drives the clock itself rather than following the
GBA). Two hardware counters:
- periodic `m_syncCounter` top=50700 ticks → `onPeriodicCounter()` calls
  `link_startTransive()` to kick a transfer (`masterClock.hpp:46-58,72-78`).
- `m_transmissionCounter` top=58000 → `onTransmissionCounter()` fires `link_startTransive()`
  up to **8 times** then stops (`masterClock.hpp:60-68,80-86`) — i.e. clocks out exactly
  the 8 command words of a frame once `startTransmissionSync()` is called from the
  handshake→crc transition (`packetLayer.cpp:22`).
For the 3DS port the GBA is the master, so the dongle is the **slave/follower** — MasterClock
is not the path to replicate; the GBA's SIO clock drives `onReceive/onTransmit`.

---

## 8. Serial / Transport layers — NOT the GBA wire

`serialLayer.*`, `transport.*` are the **host(PC/browser)↔dongle** control channel
(WebUSB or WebSerial/CDC-ACM), independent of the GBA link. Included only so an
implementer doesn't confuse them with link framing:

- `Transport` (`transport.hpp/.cpp`) multiplexes two host transports (`Id::Usb`,
  `Id::Serial`); `setActive()` picks the one the host last spoke on so replies go back the
  same way (`transport.cpp:13,28-38`).
- `SerialLayer` framing on the CDC byte stream (`serialLayer.hpp:14-20`):
  `| 0x47 'G' | 0x42 'B' | channel:1 | len:2 LE | payload[len] |`, channels
  0=command/1=data/2=status, `maxPayload=64` (`serialLayer.hpp:24-31`). RX is a 6-state
  byte parser `sync1→sync2→channel→lenLo→lenHi→payload` (`serialLayer.cpp:72-114`); TX is a
  512-byte ring buffer drained in the UART IRQ (`serialLayer.cpp:44-70,133-165`).

**This framing/CRC has nothing to do with the GBA link**; the GBA-link framing is entirely
in PacketLayer (sections 1-7). Do not port section 8 for the link cable.
