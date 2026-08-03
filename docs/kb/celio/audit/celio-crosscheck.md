# Celio firmware cross-check — what the WORKING dongle actually does (room walk focus)

**Source provenance.** The scratchpad copy at `scratchpad/celio/Celio-Firmware/` had been **hollowed out** (directory skeleton intact, every file 0-deleted — a tmp cleaner artifact; `.git` too). I re-cloned `github.com/Celio-Link/Celio-Firmware` (URL recorded in `projects/3DGBA/docs/kb/celio/`) into
`/private/tmp/claude-501/-Users-guyshtainer-VSCodeProjects-3ds-toolkit/76fd34ed-8701-4fa9-b9ee-e61b1fb732f5/scratchpad/celio/Celio-Firmware-fresh/`
at HEAD `a985282fb1c02f336b6eb4daff32c0a43b98443f` (2026-06-27, "Merge pull request #6 from GB-Link/master"). **All `file:line` citations below are relative to `Celio-Firmware-fresh/src/`.**

Build configuration that matters: `prj.conf:44: CONFIG_SECTIONS_USE_MASTER_MODE=y` — every trade section, **including tradeSetup, runs the dongle as hardware MASTER** clocking the game. The shipped board is RP2040/PIO; all `#ifdef CONFIG_STM32F0` blocks in packetLayer (14 ms packet-timeout timer, `MasterClock`) are **compiled out** on this build.

---

## Q1. What does the master send during the room phase when it has no script move? Does it ever go silent?

**Verdict: BOTH halves are true, and they are two different layers.**

1. **The PIO master clock NEVER stops** — it is a free-running `.wrap_target … .wrap` loop with no idle/park state (see Q5). From `link_changeMode(MASTER)` until the section tears down, a 16-bit transfer happens every gap interval, unconditionally.
2. **The idle CONTENT is all-zero command frames.** Whenever a command handler finishes, the packet layer swaps in `emptyCommand`, whose transmit callback returns `0x00` for every word, forever:

`layers/packetLayer.cpp:57-61` (end-of-frame, `TransiveState::command`, `m_commandIndex == 8`):
```cpp
if (m_handler.transiveDone != nullptr && m_handler.transiveDone() == CommandState::done)
{
    m_idle = true;
    m_handler = emptyCommand();
}
```

`callbacks/emptyCommand.cpp:3-15`:
```cpp
uint16_t emptyCommandTransive()
{
    return 0x00;
}
TransiveStruct emptyCommand()
{
    static TransiveStruct transive
    {
        .init = nullptr,
        .transive = emptyCommandTransive,
        .transiveDone = [](){ return CommandState::done; }
    };
    ...
```

And the transmit path always asks the current handler (or 0) — there is no "skip this transfer" branch:

`layers/packetLayer.hpp:216-223`:
```cpp
uint16_t transmitCommand()
{
    uint16_t txBytes = (m_handler.transive != nullptr) ? m_handler.transive() : 0x00;
    m_crc += txBytes;
    ...
```

So an **idle wire frame is `[CRC][0x0000 ×8]`** — note the CRC slot is generally **non-zero** even when idle, because the CRC accumulates BOTH sides' words of the previous frame (`receiveCommand`: `m_crc += rxBytes`, packetLayer.hpp:212; `transmitCommand`: `m_crc += txBytes`, packetLayer.hpp:219; reset to 0 at each frame start, packetLayer.cpp:32). Also: **the very first CRC word after the handshake is `0xB9A0`** — `packetLayer.hpp:239`: `uint16_t m_crc = LINK_SLAVE_HANDSHAKE; //first crc is always handshake`.

**Rate / re-arming:** the pacing values are per-word delays handed to the PIO **with every transmitted word** (`pioIsr_tx`, `linkLayer_pio.c:147`: `if (g_mode == MASTER) pio_sm_put(g_pio, g_sm, txValue.timingUs);`). They come from `m_timingUs`, re-armed inside `onTransiveDone` state machine:

- `layers/packetLayer.hpp:51-53`:
```cpp
static constexpr uint32_t timingHandshake = 30097;
static constexpr uint32_t timingCommandBytes = 1378;
static constexpr uint32_t timingBetweenCommands = 12953;
```
- handshake→command switch sets `m_timingUs = timingCommandBytes` (packetLayer.cpp:20);
- `packetLayer.cpp:37-40`: `if (m_commandIndex == 7) { m_timingUs = timingBetweenCommands; }` → the **8th (last) command word carries the ~7 ms inter-frame gap**;
- frame done (`m_commandIndex == 8`) resets `m_timingUs = timingCommandBytes` (packetLayer.cpp:47) for the next CRC word.

**CRITICAL UNIT FINDING — these are NOT microseconds.** The value is consumed by the PIO as a 1-instruction-per-iteration countdown loop (`pio_master_mode.pio:16-17`: `wait_till_next_transmission: jmp y-- wait_till_next_transmission`), and the state machine runs at clkdiv 67.816 → **~542.5 ns per instruction** (`linkLayer_pio.c:231`: `sm_config_set_clkdiv(&sm_config, 67.816f); // ~540 ns per inst, 16 inst equal baud 115200`). Real-time values:

| constant | count | real time |
|---|---|---|
| `timingHandshake` | 30097 | **≈ 16.33 ms** (one handshake word per ~60 Hz frame) |
| `timingCommandBytes` | 1378 | **≈ 0.748 ms** inter-word gap |
| `timingBetweenCommands` | 12953 | **≈ 7.03 ms** gap after the frame's last word |

Full packet = 9 words (CRC + 8 cmd). Wire time ≈ 9 × (~0.3–0.5 ms tx+rx) + 8 × 0.748 ms + 7.03 ms ≈ **~16.5 ms → one full command frame per ~60 Hz game frame, continuously**. (If a port treats 1378/12953 as *microseconds*, the packet period becomes ≈ 24–25 ms ≈ 40 packets/s — which matches the 3DGBA-observed "slave ≈ 40 fps" symptom exactly.)

**Answer to "WHICH":** Celio never goes silent *in clocking terms*, but it absolutely goes silent *in content terms* — after the movement script (and between move responses) the game's slave receives pure `[CRC][0x0000×8]` frames at ~60 packets/s, and **their real slave GBA demonstrably walks through that regime** (the local player walks to the machine after the partner's script ends, while the dongle is idle — there is no key-frame heartbeat anywhere in the firmware; grep confirms `moveCommand` is only ever installed from the `LINKCMD_SEND_HELD_KEYS` / `LINK_KEY_CODE_EXIT_ROOM` branches of tradeSetup/tradeLounge). So: **all-zero command frames do NOT stall a real slave — the continuous per-word clock at the correct ~60 Hz packet cadence is the heartbeat, not non-zero key content.** There is **no proactive `[0xCAFE, LINK_KEY_CODE_EMPTY]` idle heartbeat in Celio at all** — the constant `LINK_KEY_CODE_EMPTY 0x11` (link_defines.h:66) is *defined but never referenced* by any section or callback.

---

## Q2. moveCommand — frame layout, 21-repeat semantics, install trigger, post-script idle

**Frame layout** — `callbacks/moveCommand.cpp:15-32`: word0 = `0xCAFE`, word1 = the key code, words 2–7 = `0x00` (on the wire this is preceded by the CRC word → 9 words total):
```cpp
uint16_t moveCommandTransive()
{
    switch(g_index)
    {
        case 0:
            g_index++;
            return 0xCAFE;
        case 1:
            g_index++;
            return g_move;
        case 7:
            g_index = 0;
            return 0x00;
        default:
            g_index++;
         return 0x00;
    }
}
```

**Repeat semantics** — `moveCommand.cpp:40-48`:
```cpp
.transiveDone = []()
{
    if (g_repeats != 20)
    {
        g_repeats++;
        return CommandState::resume;
    }
    return CommandState::done;
}
```
`g_repeats` starts at 0 (`moveCommandInit`, lines 8-13) and `transiveDone` runs after each completed frame → the identical `[0xCAFE, key]` frame is sent **21 times** (frames 1–20 return `resume`, frame 21 hits `g_repeats == 20` → `done`). At ~16.5 ms/frame that is ~350 ms per move — i.e. one held key sustained for ~21 game frames (a 16-frame walk step + margin). Only after `done` does the handler fall back to `emptyCommand` (packetLayer.cpp:57-61) and `idle()` become true.

**Install trigger** — moves are installed **only reactively**, when the game sends `LINKCMD_SEND_HELD_KEYS` (0xCAFE) *and* the packet layer is idle *and* script entries remain. `sections/tradeSetup.cpp:78-96`:
```cpp
case LINKCMD_SEND_HELD_KEYS:
{
    if (command[1] == LINK_KEY_CODE_EXIT_ROOM)
    {
        moveCommandInit(LINK_KEY_CODE_EXIT_ROOM);
        m_packetLayer.setTransiveHandler(moveCommand());
        nextSection = NextSection::exit;
        break;
    }

    if (!m_packetLayer.idle()) break;

    if (m_movementDataIndex >= m_movementData.size()) break;

    moveCommandInit(m_movementData[m_movementDataIndex]);
    m_packetLayer.setTransiveHandler(moveCommand());
    m_movementDataIndex++;
    break;
}
```
Notes:
- The **first** move only ever goes out after the game is already in the room and polling with 0xCAFE frames. While a move is mid-repeat (`!idle()`), further 0xCAFE polls are ignored; the game re-asks every frame.
- `LINK_KEY_CODE_EXIT_ROOM` (0x17) received from the game is echoed back as a moveCommand **without an idle gate** (it preempts whatever handler is installed) and arms `NextSection::exit`.

**The script** — `sections/tradeSetup.hpp:38-39`:
```cpp
size_t m_movementDataIndex = 0;
std::array<uint16_t, 6> m_movementData = {LINK_KEY_CODE_DPAD_UP, LINK_KEY_CODE_DPAD_UP, LINK_KEY_CODE_DPAD_LEFT, LINK_KEY_CODE_DPAD_UP, LINK_KEY_CODE_DPAD_RIGHT, LINK_KEY_CODE_READY};
```
= UP, UP, LEFT, UP, RIGHT, READY (0x13, 0x13, 0x14, 0x13, 0x15, 0x16) — 6 moves × 21 frames ≈ 126 CAFE frames, interleaved with the game's polls.

**After the script (the exact idle behavior):** `if (m_movementDataIndex >= m_movementData.size()) break;` (tradeSetup.cpp:90) — **nothing is installed, ever again**. The handler stays `emptyCommand`; the master keeps clocking `[CRC][0×8]` frames at ~60 Hz until the game sends `LINKCMD_READY_CLOSE_LINK` (0x5FFF), which is answered with one `readyCloseLinkCommand` frame + `k_sleep(K_MSEC(300))` + return (tradeSetup.cpp:98-101). The window between the partner's READY keypress and READY_CLOSE_LINK — during which the *local human player* walks to the machine — is spent entirely on all-zero frames. `TradeLounge` handles 0xCAFE identically but with **no walk script**: it only echoes `LINK_KEY_CODE_EXIT_ROOM` or `LINK_KEY_CODE_READY` back (tradeLounge.cpp:32-47); at all other times it clocks zeros.

---

## Q3. Full tradeSetup ordered flow with triggers

Driver context: `module/emu.cpp:6-16` — the section chain starts at `TradeSetup(LINKTYPE_TRADE_SETUP)` (0x1133) and `process()` returns route to connection / exit / cancel. Each section owns its own `PacketLayer` (`section.hpp:53`); the section destructor spins until idle (`tradeSetup.hpp:24-27`) and the PacketLayer destructor calls `awaitDisable()` → `link_changeMode(DISABLED)` (packetLayer.hpp:71-74, packetLayer.cpp:68-80) — so **every section boundary stops the clock and performs a FULL re-handshake**.

Numbered steps (master mode, the shipped config):

1. **Connect** — `connectAsMaster()` (section.hpp:24-37):
   ```cpp
   m_packetLayer.setMode(PacketLayer::Mode::master);
   while(m_packetLayer.getReceivedHandshake() != LINK_SLAVE_HANDSHAKE) { k_sleep(K_MSEC(50)); ... }
   m_packetLayer.enableHandshake();
   k_sleep(K_MSEC(500));
   m_packetLayer.connectHandshake();
   ```
   From `setMode(master)` the PIO clocks immediately; handshake-state transmit is `0xD15E` (disabled) → `0xB9A0` (enabled) → `0x8FFF` (connect) per `transmitHandshake()` (packetLayer.hpp:181-193), one word per ~16.3 ms. **Trigger to advance:** receiving the game's `0xB9A0`. Note the deliberate **500 ms of mutual B9A0** (~30 words) before the master drives `0x8FFF`. The switch to command mode happens when `rxBytes == LINK_MASTER_HANDSHAKE || txBytes == LINK_MASTER_HANDSHAKE` (packetLayer.cpp:16-24) — i.e. **the master's own transmitted 0x8FFF ends the handshake** (matches 3DGBA fix 6a).
2. **Announce link type** — immediately: `m_packetLayer.setTransiveHandler(sendLinkTypeCommand(m_linkType));` (tradeSetup.cpp:21-23) → one frame `[0x2222, 0x1133, 0…]` (sendLinkTypeCommand.cpp:7-24), then idle zeros. **No trigger — sent unprompted at connect.**
3. **LinkPlayer block** — **trigger: game sends `LINKCMD_INIT_BLOCK` (0xBBBB)** (tradeSetup.cpp:43-52): reply with the 60-byte `LinkPlayerBlock` via blockCommand — init frame `[0xBBBB, 60, 0x80, 0…]` (blockCommand.cpp:17-19 with `[1]=blockMaxSize`, line 54) then ⌈60/14⌉ = 5 × `[0x8888, 14-byte chunk]` frames (blockCommand.cpp:24-44). Then `m_blockState = RequestTrainerCard` (master) (tradeSetup.cpp:54-55).
4. **Pull the game's trainer card** — **trigger: next completed frame with `m_packetLayer.idle()`** (tradeSetup.cpp:31-38, master-only):
   ```cpp
   if (m_blockState == BlockCommandState::RequestTrainerCard && m_packetLayer.idle())
   {
       m_packetLayer.setTransiveHandler(sendBlockCommandRequestCommand(2));
       m_blockState = BlockCommandState::TrainerCard;
       continue;
   }
   ```
   → one frame `[0xCCCC, 2, 0…]` (blockCommandRequestCommand.cpp:7-24) = "send me block type 2" (the 100-byte trainer-card block).
5. **TrainerCard block** — **trigger: game sends `LINKCMD_INIT_BLOCK` again** (tradeSetup.cpp:62-67): reply `blockCommandSetup(trainerCard, sizeof(*trainerCard), 0x64)` — the placeholder card (trainerCard.c:3-41, a maxed FRLG/RSE card, trainerId 0x529E, name "Nils") padded/declared as **0x64 = 100 bytes** → init + 8 CONT frames.
6. **Standby release** — **trigger: game sends `LINKCMD_READY_EXIT_STANDBY` (0x2FFE)** (tradeSetup.cpp:74-76): echo one `[0x2FFE, 0…]` frame (readyExitStandbyCommand.cpp:6-19). This is the "both cards viewed, enter the trade room" gate.
7. **Room walk** — **trigger: each game `[0xCAFE, heldKeys]` poll** with idle + script remaining → one 21-repeat moveCommand per script entry (UP, UP, LEFT, UP, RIGHT, READY). See Q2.
8. **Post-script idle** — no trigger; pure `[CRC][0×8]` clocking while the human walks and sits.
9. **Close** — **trigger: game sends `LINKCMD_READY_CLOSE_LINK` (0x5FFF)** (tradeSetup.cpp:98-101): echo one `[0x5FFF, 0…]` frame, `k_sleep(K_MSEC(300))`, `return nextSection` (= `NextSection::connection` unless an EXIT_ROOM key set `exit`). Loop cadence: every iteration ends `k_sleep(K_MSEC(5))` (tradeSetup.cpp:105).
10. **Teardown/rehandshake** — destructor waits idle; PacketLayer dtor disables the PIO. `EmuModule` then constructs `TradeConnection` and `handleInitialDataExchange()` calls `connectAsMaster()` again (tradeConnection.cpp:15) → the D15E→B9A0→(500 ms)→8FFF sequence repeats **at every section boundary** (setup→connection→lounge/disconnect→connection…), matching 3DGBA's re-handshake-detector finding.

For contrast, in the room-equivalent phase of `TradeConnection` the *game* leads: Celio unpromptedly sends `sendLinkTypeCommand(LINKTYPE_TRADE_CONNECTING)` (0x1122) at connect (tradeConnection.cpp:16), serves LinkPlayer + 3×200-byte party parts + mail(220, zero-filled) + ribbons(40, zero-filled) blocks on each `INIT_BLOCK`, and **pulls** the game's blocks with `sendBlockCommandRequestCommand(m_requestBlockSize)` armed by a **2000 ms `k_timer`** after each of its own blocks (tradeConnection.cpp:23-30, 46-48, 57-58, …; requestBlockCommand callback tradeConnection.hpp:72-77) — i.e. if the game hasn't spontaneously requested within 2 s, Celio requests the next block itself.

---

## Q4. LinkPlayerBlock — exact bytes shipped

Struct — `payloads/linkPlayer.h:5-26` (offsets are in the source comments; total **16 + 28 + 16 = 60 bytes**, no padding):
```c
struct LinkPlayer
{
    /* 0x00 */ uint16_t version;
    /* 0x02 */ uint16_t lp_field_2;
    /* 0x04 */ uint16_t trainerId;
    /* 0x06 */ uint16_t secretId;
    /* 0x08 */ uint8_t name[8];
    /* 0x10 */ uint8_t progressFlags; // (& 0x0F) is hasNationalDex, (& 0xF0) is hasClearedGame
    /* 0x11 */ uint8_t neverRead;
    /* 0x12 */ uint8_t progressFlagsCopy;
    /* 0x13 */ uint8_t gender;
    /* 0x14 */ uint32_t linkType;
    /* 0x18 */ uint16_t id; // battle bank in battles
    /* 0x1A */ uint16_t language;
};
struct LinkPlayerBlock
{
    char magic1[16];
    struct LinkPlayer linkPlayer;
    char magic2[16];
};
```

Values — `payloads/linkPlayer.c:3-28`:
```c
static struct LinkPlayerBlock g_linkPlayer =
{
    .magic1 = "GameFreak inc.",
    .linkPlayer =
    {
        .version = 0x4004,
        .lp_field_2 = 0x8000,
        .trainerId = 0x529E,
        .secretId = 0x1805,
        .name = {0xC8, 0xDD, 0xE0, 0xE7, 0xFF, 0x00, 0x00, 0x00},
        .progressFlags = 0xFF,
        .neverRead = 0x00,
        .progressFlagsCopy = 0x00,
        .gender = 0x00,
        .linkType = 0x1133,
        .id = 0x0000,
        .language = 0x0005
    },
    .magic2 = "GameFreak inc."
};
const struct LinkPlayerBlock* linkPLayer(uint32_t linkType)
{
    g_linkPlayer.linkPlayer.linkType = linkType;
    return &g_linkPlayer;
}
```

Field-by-field notes:
- **magic1/magic2**: the 14-char ASCII `"GameFreak inc."` + **2 NUL pad bytes** each (char[16]). Both bounds must be present; the game validates this string on the received LinkPlayerBlock.
- **version = 0x4004** = `0x4000 | 4` (4 = FireRed's game version; the game ORs 0x4000 into the version word it sends).
- **lp_field_2 = 0x8000** (high bit set, matching what a real game advertises).
- **trainerId/secretId** as two u16 (byte-identical to the game's u32 TID/SID pair, little-endian).
- **name** = `C8 DD E0 E7 FF 00 00 00` — Gen-3 charset "Nils" + 0xFF terminator + NUL pad. Note **name is only 8 bytes here**, with bytes 0x10–0x12 reinterpreted as progressFlags/neverRead/progressFlagsCopy — byte-compatible with the game's layout.
- **progressFlags = 0xFF** (national dex + game clear) but **progressFlagsCopy = 0x00** — they ship them UNEQUAL and the game accepts it.
- **gender = 0** (male).
- **linkType is a u32 at 0x14** (not u16!) — upper half zero. It is **overwritten per section** via `linkPLayer(linkType)`: `0x1133` LINKTYPE_TRADE_SETUP in tradeSetup (tradeSetup.cpp:51 with `m_linkType` from emu.cpp:13), `0x1122` LINKTYPE_TRADE_CONNECTING in tradeConnection (tradeConnection.cpp:43), `0x1111` LINKTYPE_TRADE in tradeLounge (tradeLounge.cpp:26), `0x1144` LINKTYPE_TRADE_DISCONNECTED in tradeDisconnected (tradeDisconnected.cpp:24). **The linkType in the block must match the section**, and must equal what `sendLinkTypeCommand` announced (`[0x2222, sameValue]`).
- **id = 0x0000** — even though the dongle is the hardware master (player 0), it ships id 0.
- **language = 0x0005** (Gen-3 language code 5 = German — a real-cart value; any legal language code works).
- Wire form: `blockCommandSetup(linkPlayerBlock, 60, 60)` (tradeSetup.cpp:52) → init frame `[0xBBBB, 0x003C, 0x0080, 0, 0, 0, 0, 0]` + **5 CONT frames** `[0x8888, …14 payload bytes…]` (last chunk = 4 real bytes + 10 zero-pad, blockCommand.cpp:30-43).

Checks for the 3DGBA 60-byte block: both magics with NUL padding; version 0x4004 (FRLG); lp_field_2 0x8000; linkType u32, per-section value, consistent with the 0x2222 announcement; name 0xFF-terminated; progressFlags nonzero is fine even with copy=0.

---

## Q5. PIO master clocking — continuous? at what timing?

**Continuous, from the instant `link_changeMode(MASTER)` runs, with no pause state.** The master PIO program is an infinite wrap loop — `pio_firmware/pio_master_mode.pio:11-62`:
```
.wrap_target
set pindirs, 0b1101 ; pins SD | SO | SI | SC
set pins, 0b1101
wait_till_next_transmission:
    jmp y-- wait_till_next_transmission
irq TX_VALUE_FLAG
set x, 30
wait_for_tx_irq:
    jmp x-- wait_for_tx_irq [2] ; wait for irq to fill fifo with next timing value and tx value
pull ; timing value for next transmission
mov y osr ; store in y for wait_till_next_transmission
set x, 15
pull ; tx value -> out
...transmit 16 bits, stop bit, flip SD to input, wait/receive 16 bits, push...
irq RX_TX_DONE_FLAG
.wrap
```
Every iteration = one 16-bit transfer + one 16-bit reply, then a programmable gap, then it wraps and does it again — there is **no conditional that skips a transfer**. The gap is `y` iterations of a 1-cycle `jmp` at **~542.5 ns/cycle** (`linkLayer_pio.c:231`: clkdiv 67.816, "16 inst equal baud 115200" → bit time 8.68 µs ≈ 115200 baud, the GBA multi-mode rate).

The CPU side feeds it from the TX IRQ — `linkLayer_pio.c:137-152`:
```c
static void pioIsr_tx(const void* arg)
{
    struct NextTransmit txValue = { 0xDEAD, 50000 };
    if (g_transmitCallback) txValue = g_transmitCallback(g_transmitUserData);
    if (g_mode == MASTER) pio_sm_put(g_pio, g_sm, txValue.timingUs);
    pio_sm_put(g_pio, g_sm, txValue.value);
    ...
```
i.e. **each transmitted word carries the delay to insert before the NEXT word** (pulled into `y`, consumed at the next wrap). The only way the clock stops is `link_changeMode(DISABLED)` at section teardown (`PacketLayer::awaitDisable`, packetLayer.cpp:68-80 — as master it first waits up to 100 ms for the in-flight frame to complete cleanly).

**Exact cadence** (from Q1's constants × 542.5 ns):
- Handshake phase: one word per **~16.33 ms** (30097) → ≈ 60 Hz single-word transfers. (First word after enable goes out with `m_timingUs = 0` — packetLayer.hpp:241 — then 30097 applies.)
- Command phase, per 9-word packet: CRC word and command words 0–6 each followed by **~0.748 ms** (1378); the 8th command word followed by **~7.03 ms** (12953, armed at `m_commandIndex == 7`, packetLayer.cpp:37-40). With ~0.3–0.5 ms of wire time per word (start bit 16 cycles + 16×16 data + stop, then SD turnaround + up-to-~250-cycle rx timeout + 16×16 rx — .pio lines 31-58), the packet period is **≈ 16.5 ms → one full `[CRC][8-word]` frame per ~60 Hz game frame**, matching the game's one-link-frame-per-video-frame expectation.
- Whether idle (`emptyCommand`) or active (block/move/etc.), **the cadence is identical** — content changes, timing never does.
- Sidenote: the STM32F0 backend paces differently (hardware counters, `masterClock.hpp:72-86`, ticks 50700/58000, one sync tick + 8 word ticks) — that code is compiled out on the shipped RP2040 build, but it confirms the same design intent: 9 words per fixed period, clock never conditional on having data.

---

## Implications for the 3DGBA JOIN-side freeze (cross-check conclusions only)

1. Celio has **no proactive key heartbeat**; the proven-working room behavior is *continuous clocking of all-zero frames at ~60 packets/s*. If the 3DS slave game freezes on zero frames, the divergence from Celio is **cadence/pacing**, not content.
2. If the 3DGBA port consumed `30097/1378/12953` as **microseconds**, its packet period is ~24–25 ms ≈ 40 packets/s — exactly the observed "slave runs ~40 fps" signature. Celio's real units are **542.5 ns PIO cycles** → 16.33 ms / 748 µs / 7.03 ms.
3. Move responses are **reactive** (game's 0xCAFE poll → 21-repeat reply), gated on `idle()`; EXIT_ROOM echo is not idle-gated; after the 6-entry script the correct behavior is zeros forever, not key frames.
4. Every section boundary = clock stop + full D15E→B9A0→(500 ms dwell)→8FFF re-handshake; the master's own transmitted 8FFF is what exits handshake state; first CRC word after handshake is 0xB9A0.
