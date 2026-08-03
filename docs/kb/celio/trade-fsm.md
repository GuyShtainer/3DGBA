# Celio-Firmware — Gen-3 TRADE state machine, reconstructed section-by-section

Source root: `Celio-Firmware/src/`. All line cites are into that tree.

Celio is a **local-termination** trade partner: it is a microcontroller that pretends to
be the *other* Pokemon game on the GBA link cable. It answers the SIO transfers itself
and ships its own party blocks; **it does NOT route to a relay** and **it does NOT bridge
to a second real game** — there is no networking/relay code anywhere in the trade path.
The "partner party" it trades back is built from data the *connected* game sent it during
the block exchange (`partnerPartyConstruct`), or, before that arrives, from a hardcoded
filler Pokemon. This is the key contrast with the 3DGBA state-E BRIDGE (which relayed
words to a real peer and stalled at `LinkPlayer`).

---

## 0. Constants (the wire vocabulary)

### Handshakes — `link_defines.h:61-63`
```
#define LINK_MASTER_HANDSHAKE       0x8FFF
#define LINK_SLAVE_HANDSHAKE        0xB9A0
#define LINK_HANDSHAKE_DISABLE      0xD15E
```
Note: in 3DGBA notes the single-word handshake was described as host=`B9A0` / device=`8FFF`.
Celio names them MASTER=`0x8FFF`, SLAVE=`0xB9A0`. The firmware connecting *as master* (the
common path here) transmits `0x8FFF`; the GBA it talks to provides `0xB9A0`.

### LINKCMD command words (first word of a 8-word frame) — `link_defines.h`
| Const | Value | Meaning in trade |
|---|---|---|
| `LINKCMD_SEND_LINK_TYPE` | `0x2222` | announce link-type (followed by the LINKTYPE word) |
| `LINKCMD_READY_EXIT_STANDBY` | `0x2FFE` | leave standby |
| `LINKCMD_READY_CLOSE_LINK` | `0x5FFF` | close the link (ends every section) |
| `LINKCMD_CONT_BLOCK` | `0x8888` | block-continuation / negotiation-carrier (word[1] = real cmd) |
| `LINKCMD_READY_TO_TRADE` | `0xAABB` | (carried inside CONT_BLOCK) peer ready to trade slot N |
| `LINKCMD_READY_FINISH_TRADE` | `0xABCD` | finish-trade handshake (disconnect section) |
| `LINKCMD_INIT_BLOCK` | `0xBBBB` | start a block transfer (request to send a block) |
| `LINKCMD_READY_CANCEL_TRADE` | `0xBBCC` | (in CONT_BLOCK) cancel handshake |
| `LINKCMD_SEND_HELD_KEYS` | `0xCAFE` | poll for held keys (drives menu navigation) |
| `LINKCMD_SEND_BLOCK_REQ` | `0xCCCC` | request a block of size N from peer |
| `LINKCMD_START_TRADE` | `0xCCDD` | begin the trade |
| `LINKCMD_CONFIRM_FINISH_TRADE` | `0xDCBA` | confirm finish |
| `LINKCMD_SET_MONS_TO_TRADE` | `0xDDDD` | commit which slot is being traded |
| `LINKCMD_PLAYER_CANCEL_TRADE` | `0xDDEE` | player cancelled |
| `LINKCMD_REQUEST_CANCEL` | `0xEEAA` | request cancel |
| `LINKCMD_BOTH_CANCEL_TRADE` | `0xEEBB` | both cancel |

### LINKTYPE words (second word of a SEND_LINK_TYPE frame) — `link_defines.h:37-40`
```
#define LINKTYPE_TRADE               0x1111   // lounge / trade-room idle
#define LINKTYPE_TRADE_CONNECTING    0x1122   // active trade data-exchange + negotiation
#define LINKTYPE_TRADE_SETUP         0x1133   // initial setup / room entry
#define LINKTYPE_TRADE_DISCONNECTED  0x1144   // post-trade reconnect / finish
```

### Key-codes (menu nav) — `link_defines.h:65-75`
`DPAD_UP 0x13`, `DPAD_LEFT 0x14`, `DPAD_RIGHT 0x15`, `READY 0x16`, `EXIT_ROOM 0x17`.

---

## 1. Frame format, CRC math, frame assembly — `packetLayer.{hpp,cpp}`, `blockCommand.cpp`

A "command" / frame is **8 words of 16 bits** (`std::array<uint16_t,8>`,
`packetLayer.hpp:248,250`). The transfer cycle has three states
(`packetLayer.hpp:37-42`): `handshake` → `crc` → `command`, repeating `crc`↔`command`.

### CRC = additive 16-bit sum of every command word (RX + TX), seeded with the handshake
`packetLayer.hpp:239`:
```cpp
uint16_t m_crc = LINK_SLAVE_HANDSHAKE; //first crc is always handshake
```
Each received word is summed (`packetLayer.hpp:209-214`):
```cpp
void receiveCommand(uint16_t rxBytes) {
    m_receivedCommand[m_commandIndex] = rxBytes;
    m_crc += rxBytes;
    m_commandIndex++;
}
```
Each transmitted word is *also* summed into the same accumulator
(`packetLayer.hpp:216-223`):
```cpp
uint16_t transmitCommand() {
    uint16_t txBytes = (m_handler.transive != nullptr) ? m_handler.transive() : 0x00;
    m_crc += txBytes;          // <-- mutual/additive CRC: own TX folded in too
    m_transmittedCommand[m_transmitCommandIndex] = txBytes;
    m_transmitCommandIndex++;
    return txBytes;
}
```
So the CRC is a **mutual additive checksum**: `0xB9A0 + Σ(all 8 RX words) + Σ(all 8 TX
words)`. It is transmitted in the `crc` state (`transmitCrc()` returns `m_crc`,
`packetLayer.hpp:202-205`). The received CRC is ignored (`receiveCrc` is a no-op,
`packetLayer.hpp:197-200`). After the 8th command word the state flips back to `crc` and
`m_crc` is reset to 0 at the start of the next command run (`packetLayer.cpp:27-33`: in the
`crc` done-state `m_crc = 0x00`). **Net: CRC word sent before a frame covers that frame's
8 RX+TX words, seeded 0 (the very first one is seeded with the handshake value).**

### Frame indexing / "command done" — `packetLayer.cpp:35-62`
A frame completes when `m_commandIndex == 8`. On the 7th word (`==7`) the inter-word timing
switches to `timingBetweenCommands` (12953 µs) so the GBA's between-block gap is honored.
On completion it gives `m_commandTransiveSemaphore` (unblocking the section's
`awaitTransiveResults()`), and if the handler's `transiveDone()` returns `CommandState::done`
the layer goes idle and reverts to `emptyCommand()`.

### How a TX frame is produced — the `TransiveStruct` handler
`TransiveStruct` (`callbacks/TransiveStruct.hpp`) = `{userData, init, transive(), transiveDone()}`.
`transive()` is called once per word and returns the next of 8 words. `transiveDone()` returns
`resume` (keep this handler for another 8-word frame) or `done` (one-shot).

Single-word command frames (link-type, block-req, move, close, exit-standby) all share the
same skeleton: word0 = the LINKCMD, word1 = an argument, words 2..6 = 0, word7 = 0 then reset
index. E.g. `sendLinkTypeCommand` (`callbacks/sendLinkTypeCommand.cpp:7-24`): word0=`0x2222`,
word1=`g_type` (the LINKTYPE). `sendBlockCommandRequestCommand`
(`callbacks/blockCommandRequestCommand.cpp`): word0=`0xCCCC`, word1=block size. `moveCommand`
(`callbacks/moveCommand.cpp`): word0=`0xCAFE`, word1=keycode, and `transiveDone` **repeats the
same key 21 times** (`g_repeats != 20`) so a single keypress is held long enough to register.
`readyCloseLinkCommand`=`0x5FFF`, `readyExitStandbyCommand`=`0x2FFE`, `emptyCommand`=all-zero.

### Block transfer assembly — `callbacks/blockCommand.cpp`
This is how `LinkPlayer`, `trainerCard`, and the 600-byte party get shipped. Two 8-word
templates:
```cpp
static uint16_t g_blockCommandInit[8]    = { LINKCMD_INIT_BLOCK /*0xBBBB*/, 0x00, 0x80, 0,0,0,0,0 };
static uint16_t g_blockCommandContent[8] = { LINKCMD_CONT_BLOCK /*0x8888*/, 0,0,0,0,0,0,0 };
```
- `blockCommandSetup(src,size,blockMaxSize)` (`blockCommand.cpp:46-55`) stores the source
  pointer/size and **writes `blockMaxSize` into word[1] of the INIT frame**
  (`g_blockCommandInit[BLOCK_SIZE_INDEX]=blockMaxSize`, index 1). So an INIT_BLOCK frame
  is `[0xBBBB][totalBlockSize][0x80][0…]`.
- `blockCommandTransive` (`blockCommand.cpp:57-69`) sends the 8-word INIT frame first
  (`g_initSend==false`), then flips to repeatedly sending CONT frames.
- **CONT frame payload = 14 bytes per frame** (`#define MAX_CHUNK 14`, `blockCommand.cpp:6`).
  Each CONT frame copies up to 14 bytes of `src` into bytes 2.. of the 16-byte frame
  (word0 stays `0x8888`, then 14 payload bytes = 7 words):
  ```cpp
  memset((uint8_t*)g_blockCommandContent + 2, 0x00, MAX_CHUNK);
  uint16_t chunkSize = g_srcSize < MAX_CHUNK ? g_srcSize : MAX_CHUNK;
  memcpy((uint8_t*)g_blockCommandContent + 2, (uint8_t*)g_src + g_pos, chunkSize);
  ```
  `g_blockMaxSize` (the declared block size, padded) drives termination, NOT the real data
  size — when `g_blockMaxSize==0` `blockCommandChunk` returns `done` (`blockCommand.cpp:26`).
  So a 200-byte party part = `ceil(200/14)=15` CONT frames; a 600-byte party = 3×200.
- **RX block reassembly**: when the peer is the one sending a block, the section watches for
  `LINKCMD_CONT_BLOCK` (`0x8888`) frames and feeds bytes 2.. (14 bytes) into
  `party::partnerPartyConstruct` (`tradeConnection.cpp:106-115`): it takes
  `std::span(command.data(),16).subspan(2)` = the 14 payload bytes.

---

## 2. Connect primitives — `section.hpp`

Every section begins by establishing the GBA-cable handshake. `Section`
(`section.hpp:8-54`) owns the `PacketLayer m_packetLayer`.

- `connectAsMaster()` (`section.hpp:24-37`): sets master mode, spins until the received
  handshake == `LINK_SLAVE_HANDSHAKE` (`0xB9A0`), then `enableHandshake()` (start sending
  `0xB9A0`), 500 ms, then `connectHandshake()` (start sending `LINK_MASTER_HANDSHAKE`
  `0x8FFF`, which is what flips the transfer state into `crc`/`command` —
  `packetLayer.cpp:16-24`).
- `connectAsSlave()` (`section.hpp:39-50`): slave mode, waits for `0xB9A0`, `enableHandshake()`.

`CONFIG_SECTIONS_USE_MASTER_MODE` selects which one TradeSetup uses; Lounge / Connection /
Disconnect always `connectAsMaster()`.

---

## 3. Section flow / dispatcher — `module/emu.cpp`

The whole trade is a loop over `NextSection` (`sections/nextSectionState.hpp`:
`setup, connection, disconnect, lounge, exit, cancel`). `EmuModule::execute()`
(`module/emu.cpp:4-47`) starts at `setup`:

```
setup (TradeSetup)        -> connection | exit | cancel
connection (TradeConnection) -> lounge | disconnect | cancel
disconnect (TradeDisconnect) -> connection | cancel
lounge (TradeLounge)      -> connection | exit | cancel
exit / cancel             -> return (done)
```

Ordered happy path: **setup → connection → disconnect → connection → … → exit**. The trade
itself happens inside `connection`; `disconnect` is the post-trade re-handshake that loops
back to `connection` for another trade; `lounge` is the trade-room idle that you return to
on cancel-trade.

---

## 4. `TradeSetup` — LINKTYPE_TRADE_SETUP (0x1133) — `tradeSetup.cpp`

Phase / entry: first section. Announces `LINKTYPE_TRADE_SETUP`. This is "I just sat down at
the cable / entering the trade room." Block-state machine `LinkPlayer → TrainerCard`
(`tradeSetup.hpp:14-19,33`).

Master path sets the initial TX handler to `sendLinkTypeCommand(m_linkType)`
(`tradeSetup.cpp:21-23`), m_linkType=`0x1133`. Then it loops on `awaitTransiveResults()` and
switches on `command[0]` (the peer's LINKCMD):

- **`LINKCMD_INIT_BLOCK` (0xBBBB)** — peer is asking to exchange a block; Celio answers by
  *sending its own* block (`tradeSetup.cpp:43-72`):
  - state `LinkPlayer`: ship `linkPLayer(0x1133)` — a `LinkPlayerBlock` (16-byte magic
    "GameFreak inc." + `LinkPlayer` struct + 16-byte magic2), size = `sizeof(block)`
    (`blockCommandSetup(linkPlayerBlock, sizeof, sizeof)`). Master then advances to
    `RequestTrainerCard`; slave advances to `TrainerCard`.
  - state `TrainerCard`: ship `trainerCardPlaceholder()`, declared block size `0x64`
    (`blockCommandSetup(trainerCard, sizeof(*trainerCard), 0x64)`).
- Master-only pre-switch (`tradeSetup.cpp:31-38`): when state==`RequestTrainerCard` and the
  layer is idle, send `sendBlockCommandRequestCommand(2)` (`0xCCCC` size 2 — "send me your
  trainer card") and advance to `TrainerCard`. So master ACTIVELY pulls the peer's card.
- **`LINKCMD_READY_EXIT_STANDBY` (0x2FFE)** → answer `readyExitStandbyCommand()`.
- **`LINKCMD_SEND_HELD_KEYS` (0xCAFE)** (`tradeSetup.cpp:78-96`) — peer polls keys; Celio
  drives the menu by replaying a scripted path
  `m_movementData = {UP, UP, LEFT, UP, RIGHT, READY}` (`tradeSetup.hpp:39`), one keypress
  per poll via `moveCommand()`. If `command[1]==EXIT_ROOM (0x17)` it sends EXIT_ROOM and sets
  `nextSection = exit`.
- **`LINKCMD_READY_CLOSE_LINK` (0x5FFF)** → answer `readyCloseLinkCommand()`, sleep 300 ms,
  **return `nextSection`** (default `NextSection::connection`).

**Transition out:** normally → `connection`; → `exit` if EXIT_ROOM seen; → `cancel` on web
cancel. **Synthesizes its own LinkPlayer + TrainerCard — no relay.**

---

## 5. `TradeConnection` — LINKTYPE_TRADE_CONNECTING (0x1122) — `tradeConnection.cpp`

This is the heart of the trade. `process()` = `handleInitialDataExchange()` then
`handleTradeNegotiations()` (`tradeConnection.cpp:206-210`).

### 5a. `handleInitialDataExchange()` (`tradeConnection.cpp:13-129`)
`connectAsMaster()`, TX handler `sendLinkTypeCommand(LINKTYPE_TRADE_CONNECTING)` (0x1122).
State machine `m_blockState` (`tradeConnection.hpp:51-62`):
`LinkPlayer → PartyPart0 → PartyPart1 → PartyPart2 → Mail → Ribbons → LinkCMD`.

A `k_timer m_commandRequestTimer` fires `requestBlockCommand` after a delay; when it fires,
`m_requestBlock=true` and at the top of the loop Celio sends
`sendBlockCommandRequestCommand(m_requestBlockSize)` (`0xCCCC` + size) to **pull the next
block from the peer** (`tradeConnection.cpp:23-30`). This is how Celio *receives* the peer's
party while also *sending* its own.

On `LINKCMD_INIT_BLOCK` (peer wants to send / we send a block), per state
(`tradeConnection.cpp:35-104`):
- `LinkPlayer`: send `linkPLayer(0x1122)` (full LinkPlayerBlock). Then
  `m_requestBlockSize=1`, **`party::partnerPartyInit()`** (zero the 600-byte partner buffer),
  start the 2000 ms request timer, advance → `PartyPart0`.
- `PartyPart0`: send `party::getParty().subspan<0,200>()` — **first 200 bytes of own party**,
  declared size 200. `m_requestBlockSize=1`, timer, → `PartyPart1`.
- `PartyPart1`: send `subspan<200,200>()` (bytes 200–399). → `PartyPart2`.
- `PartyPart2`: send `subspan<400,200>()` (bytes 400–599). `m_requestBlockSize=3`, → `Mail`.
- `Mail`: send an empty mail block (`blockCommandSetup(0,0,220)` — declared 220 bytes, zero
  data). `m_requestBlockSize=4`, → `Ribbons`.
- `Ribbons`: `blockCommandSetup(nullptr,0,40)` (40 bytes, zero data). → `LinkCMD`.

So **own party = 600 bytes = 3 × 200-byte blocks**, each 200-byte block shipped as ~15
CONT frames of 14 bytes (`MAX_CHUNK`). The `m_requestBlockSize` values (1,1,1,3,4) are the
sizes Celio asks the peer to send back next.

On `LINKCMD_CONT_BLOCK` (peer is streaming *its* block to us): during states
`PartyPart1/PartyPart2/Mail` Celio captures the inbound party via
`party::partnerPartyConstruct(subspan(2))` — 14 payload bytes per frame into the partner
buffer (`tradeConnection.cpp:106-116`). (The offset-by-one-state is because the peer's part-N
arrives while we're shipping our part-(N+1).)

Exit of initial exchange: when `m_blockState==LinkCMD && m_packetLayer.idle()` it `return`s
(`tradeConnection.cpp:120-123`).

### 5b. `handleTradeNegotiations()` (`tradeConnection.cpp:131-204`)
The select/confirm phase. Default `nextSection = disconnect`. A `followupCmd`/`cmd` pair lets
it send a second command on the next idle tick (`tradeConnection.cpp:143-149`).

Negotiation commands all arrive **wrapped in `LINKCMD_CONT_BLOCK` (0x8888)** with the real
command in `command[1]` (note the source comment "WTF were they thinking?"):
- `command[1]==LINKCMD_INIT_BLOCK (0xBBBB)` → reply `sendLinkCommand(LINKCMD_INIT_BLOCK)`,
  then followup `LINKCMD_START_TRADE (0xCCDD)` (`tradeConnection.cpp:157-162`).
- `command[1]==LINKCMD_READY_TO_TRADE (0xAABB)` → reply
  `sendLinkCommand(LINKCMD_SET_MONS_TO_TRADE, command[2])` and
  **`party::tradePkmnAtIndex(command[2])`** — i.e. commit to trading the mon in slot
  `command[2]`; `tradePkmnAtIndex` copies the partner's slot N into our own party slot N
  (`tradeConnection.cpp:165-169`, `pokemon.cpp:97-100`). **command[2] = the chosen slot index.**
- `command[1]==LINKCMD_REQUEST_CANCEL (0xEEAA)` → reply `REQUEST_CANCEL`, followup
  `LINKCMD_BOTH_CANCEL_TRADE (0xEEBB)`, set `nextSection = lounge`
  (`tradeConnection.cpp:172-180`).
- `command[1]==LINKCMD_READY_CANCEL_TRADE (0xBBCC)` → reply `INIT_BLOCK`, followup
  `LINKCMD_PLAYER_CANCEL_TRADE (0xDDEE)` (`tradeConnection.cpp:182-188`).

`sendLinkCommand(cmd,arg)` (`tradeConnection.cpp:212-219`) builds a 2-word payload
`{cmd,arg}` and ships it as a **block** (`blockCommandSetup(data, 4 bytes, 20)` →
INIT_BLOCK with declared size 20, then CONT frames carrying the 2 words). I.e. negotiation
"commands" are themselves tiny 20-byte blocks.

**`LINKCMD_READY_CLOSE_LINK (0x5FFF)`** → `readyCloseLinkCommand()`, 400 ms, return
`nextSection` (`disconnect` normally; `lounge` if a cancel happened).

**Transition out:** → `disconnect` (trade went through) | `lounge` (cancelled) | `cancel`.
**Synthesizes party/mail/ribbons + answers negotiation locally — no relay.**

---

## 6. `TradeDisconnect` — LINKTYPE_TRADE_DISCONNECTED (0x1144) — `tradeDisconnected.cpp`

Post-trade reconnect / "finish the trade animation" handshake. `process()` =
`exchangeTrainerData()` then `handleDisconnect()` (`tradeDisconnected.cpp:91-95`).

### 6a. `exchangeTrainerData()` (`tradeDisconnected.cpp:11-37`)
`connectAsMaster()`, announce `LINKTYPE_TRADE_DISCONNECTED (0x1144)`. State `None →
LinkPlayer`. On `LINKCMD_INIT_BLOCK` while state==`None`: send `linkPLayer(0x1144)` LinkPlayer
block, advance → `LinkPlayer`. When state==`LinkPlayer && idle()` → `return`. (Re-announces
identity after the trade so the games re-sync.)

### 6b. `handleDisconnect()` (`tradeDisconnected.cpp:39-88`)
- `LINKCMD_READY_EXIT_STANDBY (0x2FFE)` → `readyExitStandbyCommand()`.
- `LINKCMD_CONT_BLOCK` with `command[1]==LINKCMD_READY_FINISH_TRADE (0xABCD)`
  (`tradeDisconnected.cpp:64-74`): ship a `{READY_FINISH_TRADE}` 2-word block
  (`blockCommandSetup(...,20)`), set state `FinishTrade`. On the next iteration
  (state==`FinishTrade`, `tradeDisconnected.cpp:46-56`) it ships a `{CONFIRM_FINISH_TRADE
  (0xDCBA)}` block and resets state to `None`. So the finish handshake is
  **READY_FINISH_TRADE → CONFIRM_FINISH_TRADE**.
- `LINKCMD_READY_CLOSE_LINK (0x5FFF)` → close, 400 ms, **return `NextSection::connection`**
  (loop back for the next trade).

**Transition out:** → `connection` (always, on close) | `cancel`. **No relay.**

---

## 7. `TradeLounge` — LINKTYPE_TRADE (0x1111) — `tradeLounge.cpp`

The trade-room idle state (you're standing in the union room, no active trade). Reached from
`connection` when a trade is cancelled. `connectAsMaster()`, announce `LINKTYPE_TRADE
(0x1111)`. Default `nextSection = connection`. Block-state declared but only `LinkPlayer`
used.

- `LINKCMD_INIT_BLOCK (0xBBBB)` → send `linkPLayer(LINKTYPE_TRADE)` LinkPlayer block
  (`tradeLounge.cpp:23-30`).
- `LINKCMD_SEND_HELD_KEYS (0xCAFE)` (`tradeLounge.cpp:32-47`):
  - `command[1]==EXIT_ROOM (0x17)` → send EXIT_ROOM move, `nextSection = exit`.
  - `command[1]==READY (0x16)` → send READY move, `nextSection = connection` (start a trade).
- `LINKCMD_READY_CLOSE_LINK (0x5FFF)` → close, 200 ms, return `nextSection`.

It carries the same scripted `m_movementData` array but in practice only echoes the peer's
EXIT_ROOM / READY. **Transition out:** → `connection` (READY) | `exit` (EXIT_ROOM) | `cancel`.
**No relay.**

---

## 8. Full ordered happy-path handshake (the exact LINKCMD at each step)

```
[per-section preamble, every section]
  cable handshake:  recv 0xB9A0 (SLAVE) ; we send 0xB9A0 (enable) ; +500ms ;
                    we send 0x8FFF (MASTER) -> transfer enters crc/command mode
  every frame:      CRC word (additive sum of prev frame's RX+TX, seed 0/handshake)
                    then 8 command words

SETUP  (announce LINKTYPE_TRADE_SETUP 0x1133)
  TX  0x2222 0x1133                      SEND_LINK_TYPE = setup
  RX  0xBBBB ...   -> TX LinkPlayer block (0xBBBB init [size] then 0x8888 CONT*N, 14B/frame)
  (master) TX 0xCCCC 0x0002              SEND_BLOCK_REQ -> pull peer TrainerCard
  RX  0xBBBB ...   -> TX TrainerCard block (declared size 0x64)
  RX  0xCAFE k     SEND_HELD_KEYS -> TX 0xCAFE move (UP,UP,LEFT,UP,RIGHT,READY scripted)
  RX  0x2FFE       READY_EXIT_STANDBY -> TX 0x2FFE
  RX  0x5FFF       READY_CLOSE_LINK -> TX 0x5FFF ; +300ms ; -> CONNECTION

CONNECTION / initial exchange (announce LINKTYPE_TRADE_CONNECTING 0x1122)
  TX  0x2222 0x1122                      SEND_LINK_TYPE = connecting
  RX  0xBBBB -> TX LinkPlayer block ; partnerPartyInit() ; req timer
  TX  0xCCCC 0x0001 (timer) ; RX 0xBBBB -> TX party[0:200]   ; RX 0x8888.. capture partner pt
  TX  0xCCCC 0x0001         ; RX 0xBBBB -> TX party[200:400] ; capture partner part
  TX  0xCCCC 0x0001         ; RX 0xBBBB -> TX party[400:600] ; capture partner part
  TX  0xCCCC 0x0003         ; RX 0xBBBB -> TX Mail block (empty, size 220)
  TX  0xCCCC 0x0004         ; RX 0xBBBB -> TX Ribbons block (empty, size 40)
  state LinkCMD + idle -> proceed to negotiation

CONNECTION / negotiation (all carried in 0x8888 CONT_BLOCK, real cmd in word[1])
  RX  0x8888 0xBBBB     -> TX block{0xBBBB} ; followup TX block{0xCCDD START_TRADE}
  RX  0x8888 0xAABB idx -> TX block{0xDDDD SET_MONS_TO_TRADE, idx} ; tradePkmnAtIndex(idx)
  (cancel paths: 0xEEAA REQUEST_CANCEL->0xEEBB BOTH_CANCEL_TRADE->lounge ;
                 0xBBCC READY_CANCEL_TRADE->0xDDEE PLAYER_CANCEL_TRADE)
  RX  0x5FFF READY_CLOSE_LINK -> TX 0x5FFF ; +400ms ; -> DISCONNECT (or LOUNGE if cancelled)

DISCONNECT (announce LINKTYPE_TRADE_DISCONNECTED 0x1144)
  TX  0x2222 0x1144                      SEND_LINK_TYPE = disconnected
  RX  0xBBBB -> TX LinkPlayer block ; idle -> proceed
  RX  0x2FFE -> TX 0x2FFE                READY_EXIT_STANDBY
  RX  0x8888 0xABCD -> TX block{0xABCD READY_FINISH_TRADE} ; next iter TX block{0xDCBA CONFIRM_FINISH_TRADE}
  RX  0x5FFF -> TX 0x5FFF ; +400ms ; -> CONNECTION  (loop for next trade)

LOUNGE (announce LINKTYPE_TRADE 0x1111)  [reached on cancel]
  TX  0x2222 0x1111
  RX  0xBBBB -> TX LinkPlayer block
  RX  0xCAFE 0x16 READY -> TX move READY -> nextSection=connection
  RX  0xCAFE 0x17 EXIT_ROOM -> TX move EXIT_ROOM -> nextSection=exit
  RX  0x5FFF -> close ; +200ms ; -> CONNECTION / EXIT
```

---

## 9. LinkPlayer struct layout — `payloads/linkPlayer.{c,h}`

`LinkPlayerBlock` = `char magic1[16]` ("GameFreak inc.") + `LinkPlayer` + `char magic2[16]`.
`LinkPlayer` (`linkPlayer.h:5-19`): `version u16`, `lp_field_2 u16`, `trainerId u16`,
`secretId u16`, `name[8]`, `progressFlags u8` (low nibble=hasNationalDex, high=clearedGame),
`neverRead u8`, `progressFlagsCopy u8`, `gender u8`, `linkType u32` (overwritten per-call by
`linkPLayer(linkType)`, `linkPlayer.c:24-28`), `id u16`, `language u16` (=5/English).
Sample values: version `0x4004`, trainerId `0x529E`, secretId `0x1805`,
name `{C8 DD E0 E7 FF 00 00 00}`, progressFlags `0xFF`.

## 10. Party buffer — `payloads/pokemon.{hpp,cpp}`
`g_party` = `std::array<uint8_t,600>` (`pokemon.cpp:28`) = 6 slots × 100 bytes
(`slot(i)=subspan(i*100,100)`, `pokemon.cpp:42-45`). Shipped in 3 × 200-byte blocks
(`getParty().subspan<0/200/400,200>`). The partner's party lands in
`g_partnerParty[600]` via `partnerPartyConstruct` (`pokemon.cpp:83-93`), 14 bytes/frame,
clamped at 200-byte chunk thresholds. `tradePkmnAtIndex(index)` copies partner slot→own slot
(`pokemon.cpp:97-100`). A hardcoded 100-byte `g_fillerPkmnArray` fills empty own-slots
(`pokemon.cpp:13-25,49-52`).
