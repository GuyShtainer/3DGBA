# Celio-Firmware `src/callbacks/` — command callbacks digest

All paths relative to
`.../scratchpad/celio/Celio-Firmware/src/callbacks/`.

## 0. The dispatch contract — `TransiveStruct` (`TransiveStruct.hpp`)

Every command is a value of type `TransiveStruct` returned from a factory function.
The struct is three function pointers + a data span (`TransiveStruct.hpp:11-22`):

```cpp
using InitCallback         = void(*)(std::span<const uint16_t>);   // line 13
using TransiveCallback     = uint16_t(*)();                        // line 14
using TransiveDoneCallback = CommandState(*)();                    // line 15

std::span<const uint16_t> userData;   // line 17
InitCallback         init;            // line 19
TransiveCallback     transive;        // line 20
TransiveDoneCallback transiveDone;    // line 21
```

`CommandState` is `{ resume, done }` (`TransiveStruct.hpp:5-9`).

**Dispatch mechanism (inferred from the contract, confirmed by every callback's
internal index machine):**
- `init(userData)` is called once at command start (may be `nullptr`; most commands
  pass `nullptr` and instead reset their `static` index inside the factory or a
  dedicated `*Init`/`*Setup` setter).
- `transive()` is pumped **once per SIO word**. It returns one 16-bit word and
  advances an internal `static` index. A frame is **exactly 8 words** (index 0..7);
  on the 8th pull the index wraps to 0.
- `transiveDone()` is called at frame boundary (after the 8 words). Returning
  `CommandState::resume` keeps the same command active for another frame;
  `CommandState::done` ends the command and hands control back to the driver to pick
  the next command. **Crucial:** the per-word `transive` index resets to 0 at word 8,
  so `transiveDone` deciding `resume` re-emits a fresh 8-word frame.

So the reply frame for a command is the sequence of 8 `transive()` returns, and the
"how many frames" question is answered by `transiveDone()`.

`commands.hpp` is just the aggregate include of all ten command headers
(`commands.hpp:1-10`). `CMakeLists.txt` registers all ten `.cpp` as Zephyr library
sources.

## LINKCMD constants (from `../link_defines.h`, used below)

| macro | value |
|---|---|
| `LINKCMD_SEND_LINK_TYPE` | `0x2222` |
| `LINKCMD_READY_EXIT_STANDBY` | `0x2FFE` |
| `LINKCMD_DUMMY_1` | `0x5555` |
| `LINKCMD_READY_CLOSE_LINK` | `0x5FFF` |
| `LINKCMD_CONT_BLOCK` | `0x8888` |
| `LINKCMD_INIT_BLOCK` | `0xBBBB` |
| `LINKCMD_SEND_HELD_KEYS` | `0xCAFE` |
| `LINKCMD_SEND_BLOCK_REQ` | `0xCCCC` |
| `LINK_SLAVE_HANDSHAKE` | `0xB9A0` |
| `LINK_MASTER_HANDSHAKE` | `0x8FFF` |

These are the canonical Gen-3 link command words. Each command callback emits its
LINKCMD in **word 0** of the frame, the payload (if any) in **word 1**, and zero-pads
words 2..7. NOTE: there is **no CRC computed in these callbacks** — the 8-word frame
here is the *command payload*; the additive mutual CRC of the 9-word wire frame is
applied elsewhere (driver layer), not in callbacks/. Words here are raw command words.

---

## 1. `emptyCommand` (`emptyCommand.cpp`)

- **Matches / emits:** no LINKCMD; pure filler. `emptyCommandTransive` returns `0x00`
  unconditionally (`emptyCommand.cpp:3-6`).
- **Reply frame (8 words):** `[0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]` (it returns
  `0x00` on *every* pull; note it has no internal index, so it streams zeros forever
  word-by-word, but the driver still frames it in 8s).
- **Network:** none. **Mutates:** nothing.
- **Dispatch:** `init=nullptr`, `transiveDone` always returns `CommandState::done`
  (`emptyCommand.cpp:14`) → exactly one frame then done.

## 2. `sendDummyCommand` → `dummyCommand()` (`sendDummyCommand.cpp`)

(File misnamed in includes — it includes `readyExitStandbyCommand.hpp` but defines
`dummyCommand`/`dummyTransive`.)
- **Matches / emits:** `LINKCMD_DUMMY_1` = `0x5555` in word 0.
- **Reply frame (index-by-index, `sendDummyCommand.cpp:6-20`):**
  - word 0 → `LINKCMD_DUMMY_1` (`0x5555`)
  - words 1..6 → `0x00` (default case increments index)
  - word 7 → `0x00`, resets `index=0`
  - Frame: `[0x5555,0,0,0,0,0,0,0]`
- **Network:** none. **Mutates:** `static uint8_t index` only.
- **Dispatch:** `init=nullptr`, `transiveDone` → `done` → one frame.

## 3. `readyExitStandbyCommand` (`readyExitStandbyCommand.cpp`)

- **Emits:** `LINKCMD_READY_EXIT_STANDBY` = `0x2FFE` in word 0.
- **Reply frame:** `[0x2FFE,0,0,0,0,0,0,0]` (`readyExitStandbyCommand.cpp:8-19`;
  identical index machine: case 0 emits cmd, case 7 resets, default pads `0x00`).
- **Network:** none. **Mutates:** `static index`. **Dispatch:** `done` → one frame.

## 4. `readyCloseLinkCommand` (`readyCloseLinkCommand.cpp`)

- **Emits:** `LINKCMD_READY_CLOSE_LINK` = `0x5FFF` in word 0.
- **Reply frame:** `[0x5FFF,0,0,0,0,0,0,0]` (`readyCloseLinkCommand.cpp:8-19`).
- **Network:** none. **Mutates:** `static index`. **Dispatch:** `done` → one frame.

## 5. `sendLinkTypeCommand(uint16_t type)` (`sendLinkTypeCommand.cpp`)

- **Emits:** `LINKCMD_SEND_LINK_TYPE` = `0x2222` in word 0, the link **type** in word 1.
- **Reply frame (index-by-index, `sendLinkTypeCommand.cpp:9-23`):**
  - word 0 → `0x2222`
  - word 1 → `g_type` (the `type` arg, e.g. `LINKTYPE_TRADE` `0x1111`)
  - words 2..6 → `0x00`; word 7 → `0x00` and resets index
  - Frame: `[0x2222, type, 0,0,0,0,0,0]`
- **Network:** none (type comes from caller arg). **Mutates:** `static index`,
  `static g_type` (set in factory `sendLinkTypeCommand.cpp:28`).
- **Dispatch:** `init=nullptr`, `transiveDone` → `done` → one frame.

## 6. `blockCommandRequestCommand` → `sendBlockCommandRequestCommand(uint16_t type)` (`blockCommandRequestCommand.cpp`)

- **Emits:** `LINKCMD_SEND_BLOCK_REQ` = `0xCCCC` in word 0, `type` in word 1.
- **Reply frame (`blockCommandRequestCommand.cpp:9-23`):**
  - word 0 → `0xCCCC`; word 1 → `g_type`; words 2..6 → `0x00`; word 7 → `0x00`,
    resets index. Frame: `[0xCCCC, type, 0,0,0,0,0,0]`.
- **Network:** none. **Mutates:** `static index`, `static g_type`.
- **Dispatch:** `done` → one frame.
- **Distinction from `blockRequest` (#7):** this one is parameterised by `type` and
  uses the `0xCCCC` LINKCMD with the type word; `blockRequest` is a fixed canned frame.

## 7. `blockRequestCommand` → `blockRequest()` (`blockRequestCommand.cpp`)

- **Emits:** fixed array `g_blockCommandContent[8] = {0xCCCC, 0x02, 0,0,0,0,0,0}`
  (`blockRequestCommand.cpp:6`).
- **Reply frame:** `[0xCCCC, 0x02, 0,0,0,0,0,0]` — streamed word-by-word via index
  0..7 wrap (`blockRequestCommand.cpp:8-18`).
- **Network:** none. **Mutates:** global `g_index` (note: non-static, file-scope).
- **Dispatch:** `done` → one frame. This is the canonical "request a 0x02-type block".

## 8. `moveCommand` (`moveCommand.cpp`) — repeated held-keys frame

- **Emits:** `0xCAFE` (`LINKCMD_SEND_HELD_KEYS`) in word 0, the move/keys in word 1.
- **Init:** `moveCommandInit(uint16_t data)` sets `g_index=0, g_repeats=0, g_move=data`
  (`moveCommand.cpp:8-13`).
- **Reply frame (`moveCommand.cpp:17-31`):**
  - word 0 → `0xCAFE`; word 1 → `g_move`; words 2..6 → `0x00`; word 7 → `0x00`,
    resets index. Frame: `[0xCAFE, move, 0,0,0,0,0,0]`.
- **REPEAT COUNT — load-bearing (`moveCommand.cpp:40-48`):**
  ```cpp
  .transiveDone = []() {
      if (g_repeats != 20) { g_repeats++; return CommandState::resume; }
      return CommandState::done;
  }
  ```
  → the frame is re-emitted while `g_repeats != 20`, incrementing each frame.
  Sequence of `g_repeats` at decision time: 0→resume,1→resume,…,19→resume,20→done.
  That is **21 frames total** (g_repeats values 0..20: 20 `resume`s then 1 `done`).
  i.e. the move/held-keys command is sent **21 times** before completing.
- **Network:** none (move comes from `moveCommandInit`). **Mutates:** `g_index`,
  `g_repeats`, `g_move`.

## 9. `blockCommand` (`blockCommand.cpp`) — chunked block transfer

The data-pump command. Two distinct frame shapes: an **INIT_BLOCK** header frame, then
repeated **CONT_BLOCK** content frames carrying 14-byte chunks.

Constants: `MAX_CHUNK = 14` (`blockCommand.cpp:6`), `BLOCK_SIZE_INDEX = 1`
(`blockCommand.cpp:7`).

State globals (`blockCommand.cpp:9-15`): `g_src`, `g_srcSize`, `g_pos`, `g_index`,
`g_initSend`, `g_blockMaxSize`.

Frame templates (`blockCommand.cpp:17-21`):
```cpp
g_blockCommandInit[8]    = {LINKCMD_INIT_BLOCK /*0xBBBB*/, 0x00, 0x80, 0,0,0,0,0};
g_blockCommandContent[8] = {LINKCMD_CONT_BLOCK /*0x8888*/, 0,0,0,0,0,0,0};
```

### Setup — `blockCommandSetup(src, size, blockMaxSize)` (`blockCommand.cpp:46-55`)
Sets `g_src=src, g_srcSize=size, g_index=0, g_initSend=false, g_pos=0,
g_blockMaxSize=blockMaxSize`, and writes the **advertised block size** into the INIT
header word 1: `g_blockCommandInit[BLOCK_SIZE_INDEX] = blockMaxSize;`
(`blockCommand.cpp:54`). So the INIT frame becomes:
`[0xBBBB, blockMaxSize, 0x80, 0,0,0,0,0]`.

### Per-word emit — `blockCommandTransive` (`blockCommand.cpp:57-69`)
```cpp
uint16_t ret = g_initSend ? g_blockCommandContent[g_index] : g_blockCommandInit[g_index];
g_index++;
if (g_index == 8) { g_index = 0; g_initSend = true; }   // first 8-word frame = INIT, then content
```
- The **first frame** (while `g_initSend==false`) streams `g_blockCommandInit`:
  `[0xBBBB, blockMaxSize, 0x80, 0,0,0,0,0]`. After word 7, `g_initSend` flips true.
- **Every subsequent frame** streams `g_blockCommandContent` (the chunk-filled buffer).

### 14-byte chunk + zero-pad — `blockCommandChunk` = `transiveDone` (`blockCommand.cpp:24-44`)
Runs at each frame boundary, prepares the NEXT content frame:
```cpp
if (g_blockMaxSize == 0) return CommandState::done;   // line 26  -> end when advertised quota exhausted
if (!g_initSend) return CommandState::resume;         // line 28  -> skip prep right after INIT frame

memset((uint8_t*)g_blockCommandContent + 2, 0x00, MAX_CHUNK);   // line 30: zero the 14 payload bytes (words 1..7)

uint16_t chunkSize    = g_srcSize < MAX_CHUNK ? g_srcSize : MAX_CHUNK;        // line 32
uint16_t maxChunkSize = g_blockMaxSize < MAX_CHUNK ? g_blockMaxSize : MAX_CHUNK; // line 33

if (g_blockMaxSize > 0)
    memcpy((uint8_t*)g_blockCommandContent + 2, (uint8_t*)g_src + g_pos, chunkSize); // line 37

g_srcSize      -= chunkSize;     // line 40
g_blockMaxSize -= maxChunkSize;  // line 41
g_pos          += chunkSize;     // line 42
return CommandState::resume;     // line 43
```
**Load-bearing details:**
- Payload lives in **bytes 2..15 of the 8-word buffer** = words 1..7 (14 bytes =
  `MAX_CHUNK`). Word 0 stays `LINKCMD_CONT_BLOCK` `0x8888`.
- Each content frame carries **min(remaining src, 14)** real bytes; the rest of the 14
  is **zero-padded** by the `memset` at line 30 (so a final short chunk is zero-filled).
- **Advertised-size drives termination, not src size:** `g_blockMaxSize` is decremented
  by `maxChunkSize` (capped at 14) each frame and the command ends (`done`) only when
  `g_blockMaxSize == 0` (line 26). So you send exactly `ceil(blockMaxSize/14)` content
  frames. If `blockMaxSize > size`, the tail frames copy 0 real bytes (`chunkSize`
  becomes 0 once `g_srcSize` hits 0) and are **all-zero padded** — the receiver still
  gets the full advertised block length.
- **Frame ordering:** 1 INIT frame, then N content frames. INIT is special-cased: the
  `if (!g_initSend) return resume` at line 28 means after the INIT frame is streamed
  (which set `g_initSend=true` mid-stream) the FIRST `transiveDone` call still sees…
  actually `g_initSend` is true by the time `transiveDone` runs, so line 28 is the
  guard for the degenerate pre-INIT case; the normal path prepares chunk N for the next
  content frame. The first content frame's payload is prepared by the `transiveDone`
  that fires right after the INIT frame completes.
- **Edge case (`blockCommand.cpp:26`):** if `blockMaxSize == 0` at setup, the command is
  `done` immediately after the INIT frame (no content).

### `blockCommand()` factory (`blockCommand.cpp:71-81`)
`init=nullptr` (state is primed by `blockCommandSetup` instead),
`transive=blockCommandTransive`, `transiveDone=blockCommandChunk`.

---

## 10. `usbLinkCommand` (`usbLinkCommand.cpp`) — the USB→SIO relay (the important one)

This is the bridge that streams real partner traffic (received over USB from the host /
network) onto the emulated SIO link, one 8-word frame at a time.

### Message queue — `K_MSGQ_DEFINE(g_packetQueue, 16, 200, 1)` (`usbLinkCommand.cpp:13`)
Zephyr msgq params: **msg size = 16 bytes** (= 8× `uint16_t` = one SIO frame),
**max msgs = 200** (queue depth 200 frames), **alignment = 1**. So the relay buffers up
to 200 pending 8-word frames. Comment notes the put/get run in ISR context and rely on
Zephyr's internal queue sync (no extra mutex) (`usbLinkCommand.cpp:10-12`).

### Receive handler — `usbLink_receiveHandler(std::span<const uint8_t> data, void*)` (`usbLinkCommand.cpp:15-21`)
Receives a **64-byte** USB chunk and **splits it into four 16-byte frames**, enqueuing
each:
```cpp
k_msgq_put(&g_packetQueue, data.subspan<0,16>().data(),  K_NO_WAIT);
k_msgq_put(&g_packetQueue, data.subspan<16,16>().data(), K_NO_WAIT);
k_msgq_put(&g_packetQueue, data.subspan<32,16>().data(), K_NO_WAIT);
k_msgq_put(&g_packetQueue, data.subspan<48,16>().data(), K_NO_WAIT);
```
→ 64 bytes in = 4 frames × (8 words × 2 bytes). `K_NO_WAIT` = drop/fail silently if full
(non-blocking, ISR-safe). **This is the network→firmware ingress.** The host sends
fixed 64-byte USB packets, each carrying 4 SIO frames.

### Frame load — `loadTransivePacket()` (`usbLinkCommand.cpp:23-26`)
Pops one 16-byte frame into `g_packet[8]`; sets `g_packetAvailable = (get==0)`
(true iff a frame was available).

### Per-word emit — `usbLinkTransive()` (`usbLinkCommand.cpp:28-47`)
```cpp
if (!g_packetAvailable) return 0x00;          // line 30: idle -> emit 0x00 when queue empty
uint16_t ret = g_packet[g_index];             // line 31

// 0xFF02 / 0xFF06 / 0xFF07 -> 0x5FFF fixup, ONLY on word 0 (g_index==0)  (lines 34-37)
if (g_index == 0 && (g_packet[0]==0xFF02 || g_packet[0]==0xFF06 || g_packet[0]==0xFF07))
    ret = 0x5FFF;

g_index++;
if (g_index == 8) { g_packetAvailable = false; g_index = 0; }  // lines 41-45: frame consumed
return ret;
```
**Load-bearing details:**
- **Idle behaviour:** when no frame is queued, every word is `0x00` (line 30). This is
  the relay's idle/de-dup output — it streams zeros rather than re-sending the last
  frame, so a stalled network does not spam stale link commands.
- **0xFF0x → 0x5FFF fixup (`usbLinkCommand.cpp:33-37`):** if the **first word** of a
  received frame is `0xFF02`, `0xFF06`, or `0xFF07`, it is rewritten to `0x5FFF`
  (`LINKCMD_READY_CLOSE_LINK`). The TODO comment says this is only seen "on Reconnect
  and only from slaves → master and is consistent, so no random flip" — i.e. a known
  reconnect-path quirk where the partner emits a spurious `0xFF0x` that must be coerced
  into the close-link command. Applies **only to word 0**, not the payload words.
- **Frame consumption:** after word 7, `g_packetAvailable=false` so the next
  `transiveDone` must reload via `loadTransivePacket()`.

### Factory — `usbLinkCommand()` (`usbLinkCommand.cpp:49-67`)
```cpp
.userData = std::span<const uint16_t>(),     // empty
.init = [](std::span<const uint16_t>) {       // lines 54-58
    k_msgq_purge(&g_packetQueue);             // flush all buffered frames on (re)start
    g_index = 0;
    g_packetAvailable = false;
},
.transive = usbLinkTransive,
.transiveDone = []() {                         // lines 60-64
    loadTransivePacket();
    return CommandState::resume;               // <-- RESUME FOREVER
},
```
**Load-bearing details:**
- **`init` purges the queue** and resets index/availability — a fresh relay session
  starts empty (`usbLinkCommand.cpp:55-57`).
- **Resume-forever:** `transiveDone` **always** returns `CommandState::resume`
  (`usbLinkCommand.cpp:63`). The usbLink relay **never self-terminates** — it runs as
  the steady-state command, pulling one queued frame per SIO frame indefinitely. When
  the queue is empty it emits all-zero idle frames (via `g_packetAvailable=false` path)
  and keeps resuming, waiting for the next USB packet.
- **Pump cadence:** one `loadTransivePacket()` per completed 8-word frame, so the relay
  consumes exactly one queued frame per SIO frame of bandwidth; the 64-byte USB packet
  (4 frames) thus drains over 4 SIO frames.

---

## Cross-cutting frame-emit pattern (all simple commands)

`sendDummy`, `readyExitStandby`, `readyCloseLink`, `sendLinkType`,
`blockCommandRequest` all share the identical 8-state index machine:
- `case 0` → emit the LINKCMD (and `index++`),
- `case 1` → emit the payload word if any (`index++`),
- `case 7` → return `0x00` and reset `index=0`,
- `default` → return `0x00` and `index++`.
They all return `CommandState::done` from `transiveDone` → exactly one 8-word frame.
`blockRequest` is the same but reads from a fixed array instead of a switch.
The two that **repeat** are `moveCommand` (21 frames) and `usbLinkCommand` (forever);
`blockCommand` repeats `ceil(blockMaxSize/14)` content frames after its INIT frame.
