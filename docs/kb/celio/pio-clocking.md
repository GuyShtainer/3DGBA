# Celio link-layer clocking — portable semantics (for mGBA completeEvent port)

Scope: extract the *timing* and *role-selection* semantics from Celio's RP2040-PIO
link layer so they can be re-implemented on top of mGBA's `completeEvent` scheduler.
RP2040/PIO/STM32 register minutiae are deliberately ignored except where they pin
down a real-world duration.

## 1. The transfer model (what one "transfer" is)

Celio drives a **GBA Multi-Boot / Normal-mode SIO word exchange = one 16-bit
transfer per round**. Each round: the bus master clocks 16 bits out on SO while
sampling 16 bits in on SI; both sides end up with each other's 16-bit word.

- Bit framing is UART-style: a start bit, 16 data bits, a stop bit. Master PIO:
  `set pins, 0b0100 [15] ; UART start bit` then `transmit_data: out pins ... [14] / jmp x-- transmit_data` with `set x, 15` (16 bits), then `set pins, 0b1100 [15] ; UART stop bit` (`pio_master_mode.pio:28-38`).
- Data is **bit-reversed** going in/out. The done-ISR reverses the received word:
  `rxData = reverse_bit16(rxData);` (`linkLayer_pio.c:131`), and `reverse_bit16`
  is a full 16-bit reverse + byte swap (`linkLayer_pio.c:116-122`). Implication for
  the port: the PIO shifts LSB-first on the wire; mGBA gives you the logical word,
  so do **not** re-reverse unless you are reproducing wire bytes.
- One PIO instruction ≈ **542.5 ns** (clkdiv 67.816 at 8 MHz-ish PIO clock):
  `sm_config_set_clkdiv(&sm_config, 67.816f); // ~540 ns per inst, 16 inst equal baud 115200`
  (`linkLayer_pio.c:231` master, `:280` slave). So the **bit rate is ~115200 baud**;
  16 data bits ≈ 139 µs of actual on-wire shifting. The *round period* is much longer
  than the shift time — pacing (below) dominates.

## 2. Per-transfer / per-round timing (the numbers that matter)

There are **two** independent timing mechanisms in the firmware; the protocol-faithful
one is the **PIO master-mode "timing value"**, NOT the STM32 `MasterClock` counter.

### 2a. PIO master timing value (authoritative round pacing)

In MASTER mode the firmware pushes a per-transfer **timing word** (in PIO
delay-loop iterations) *ahead of* the data word:

```c
if (g_mode == MASTER) pio_sm_put(g_pio, g_sm, txValue.timingUs);
pio_sm_put(g_pio, g_sm, txValue.value);
```
(`linkLayer_pio.c:147-148`). The master PIO loads it into Y and spins
`wait_till_next_transmission: jmp y-- wait_till_next_transmission` before the next
transfer (`pio_master_mode.pio:16-26`). One iteration ≈ **542.5 ns**.

The two protocol pacing constants (`awProtocol.hpp:79-85`):

```c
constexpr uint32_t timingPacketXg = 15370;   // ≈ 8.3 ms  (2 rounds/frame)
constexpr uint32_t timingSync     = 30765;   // ≈ 16.7 ms (1 round/frame)
```

The comment ties them directly to gpSP's fake-master rates:
> Matches gpsp's fake-master rates: SLAVE_IRQ_CYCLES_2P (8.3 ms, 2/frame) and
> SLAVE_IRQ_CYCLES_H (16.7 ms, 1/frame, used in SYNC/INTERSYNC).

So: **normal data flow paces at ~8.3 ms/round (≈2 transfers per 16.7 ms GBA frame);
SYNC/INTERSYNC states slow to ~16.7 ms/round (1 transfer per frame).** Verify:
15370 × 542.5 ns ≈ 8.34 ms; 30765 × 542.5 ns ≈ 16.69 ms. Good.

> In slave mode the value is ignored — the parent GBA paces the bus. (`awProtocol.hpp:82-83`)

### 2b. STM32 `MasterClock` counter (a coarser, separate "burst" scheme)

`masterClock.hpp` is a different/older pacing path (STM32 TIM16/TIM17 counters), kept
for the "section/handshake" flow rather than the AW data flow. Timer tick = STM32 APB
48 MHz ÷ (prescaler+1). TIM16 has `st,prescaler = <0x0F>` → ÷16 → **3 MHz tick**
(`nucleo_f070rb.overlay:17-23`).

```c
struct counter_top_cfg m_periodicConfig = { .ticks = 50700, ... }; // ≈ 16.9 ms @3MHz
struct counter_top_cfg m_transmissionConfig = { .ticks = 58000, ... }; // ≈ 19.3 ms @3MHz
```
(`masterClock.hpp:72-86`). Pattern: a **periodic** tick (~16.9 ms, one GBA frame)
kicks off a **burst of exactly 8 transmissions**, each paced ~19.3 ms apart, then stops:

```c
void onTransmissionCounter() {
    if (m_transmissionCount >= 8){ counter_stop(m_transmissionCounter); return; }
    link_startTransive();
    m_transmissionCount++;
}
```
(`masterClock.hpp:60-68`). The "8" is the 8-word command frame. For the mGBA port the
**8.3/16.7 ms PIO numbers (2a) are the load-bearing ones**; this counter scheme is a
firmware-board artifact (note `//FIXME only one timer`, `masterClock.hpp:88`).

## 3. Master vs slave role selection

Role is **statically chosen at section/handshake time, then locked into the PIO
program** — it is not auto-negotiated per round.

- Two whole PIO programs exist: master (`pio_master_*`) and slave (`pio_slave_*`),
  selected by `link_changeMode(MASTER|SLAVE|DISABLED)` →
  `link_configureMaster()` / `link_configureSlave()` (`linkLayer_pio.c:299-312`).
- Build-time default: `CONFIG_SECTIONS_USE_MASTER_MODE=y` (`prj.conf:44`); Kconfig
  says the device starts and "in next section device will switch to master."
- Handshake words decide who is who at the protocol layer
  (`link_defines.h:61-62`):
  ```c
  #define LINK_MASTER_HANDSHAKE   0x8FFF
  #define LINK_SLAVE_HANDSHAKE    0xB9A0
  ```
  `connectAsMaster()` spins until it *receives* `LINK_SLAVE_HANDSHAKE`;
  `connectAsSlave()` likewise (`section.hpp:24-43`, `usbSection.cpp:8,23-31`).
- **Crucial inversion** — the adapter's PIO link-mode is the *opposite* of the GBA's
  bus role (`awProtocol.hpp:223-227`):
  > `gbaIsMaster=true` → adapter in SLAVE link mode, GBA is bus master → master_send() semantics.
  > `gbaIsMaster=false` → adapter in MASTER link mode, GBA is bus slave → master_send()... update() semantics.

  i.e. when the **adapter** clocks the bus (PIO MASTER) the **GBA is the slave**, and
  vice-versa. For the dual-GBA 3DS port: whichever side owns the clock runs the
  master pacing (2a); the other side just answers when clocked.

- Slave PIO does NOT pace; it forces the GBA into master by driving SO low and then
  waits for the GBA's clock edge: `set pins, 0b1000 ; drive SO low so SI of GBA is
  low and GBA switches to master` then `wait 0 pin 0` (`pio_slave_mode.pio:12-16`).

## 4. Inter-transfer gap

The gap is exactly the pacing delay of §2a — the master burns `timingX` delay-loop
iterations between the *end* of one transfer and the *start* of the next
(`pio_master_mode.pio:16-17` Y-loop), so the **round period = shift time (~139 µs) +
gap**, and the gap is whatever makes the total hit 8.3 ms / 16.7 ms. After receive the
master also inserts fixed settle delays (`push [16]`, `set pins,0b1100 [30]`,
`pio_master_mode.pio:56-58`), but those are sub-µs and dwarfed by the Y pacing loop.

Slave-side fixed micro-gaps to "match original behaviour": `nop [31]` before and after
its transmit (`pio_slave_mode.pio:29,42`) — ≈ 17 µs each; negligible vs the round.

## 5. Timeouts

Two distinct timeouts:

- **Per-round RX timeout (master)** — if the slave never pulls the start bit, master
  bails to a dummy receive: `set x, 31 ; timeout value (~500us with delay, was ~50us)`
  then `wait_for_rx: jmp x-- check_state [7]` / `jmp !x receive_begin`
  (`pio_master_mode.pio:43-48`). So a master round **never hangs >~500 µs** waiting for
  a slave word; a missed slave just yields a garbage/dummy word that round.
- **Software fallback TX value** if the protocol callback is absent:
  `struct NextTransmit txValue = { 0xDEAD, 50000 };` (`linkLayer_pio.c:140-144`) —
  50000 iterations ≈ 27 ms default round.
- **Link-level reconnect timeout** (status only, not protocol-fatal):
  `keepaliveMs = 250` re-sends suppressed announcements; quiet-link "reconnecting"
  report matches gpsp's `MAX_FRAME_TIMEOUT` 240 frames ≈ **4 s**
  (`awProtocol.hpp:98-104`).

## 6. Frame-size limits (carried alongside timing)

`maxFrameWords = 255` (cnt is 8 bits of the MAW1 flags word), `replayQueueWords = 512`
(= gpsp MAX_FPACK), `accumWords = 300` (`awProtocol.hpp:88-96`). Not timing, but a
round that ships a frame can be up to 255 data words long — pacing is per-round, so a
255-word frame at 8.3 ms/round is ~2.1 s; real frames are tiny (8-word commands).

## 7. Port recipe (PIO → mGBA completeEvent)

- Drop PIO/STM32 entirely. Schedule the **next** SIO transfer's `completeEvent` at
  `now + period` where `period = 8.3 ms` (data) or `16.7 ms` (SYNC/INTERSYNC).
- Only the **clock-owning** side schedules; the answering side fires its complete
  when clocked. With two soft GBAs co-hosted, the clock owner = the GBA the adapter
  would be SLAVE to (`gbaIsMaster=true`).
- 16-bit word per transfer; logical word (no bit-reverse needed in mGBA land).
- Keep the ~500 µs "missed slave → dummy word" semantic: don't stall the round if the
  peer hasn't produced a word; substitute the idle/dummy and continue.
- Default/idle word `0xDEAD` only when no protocol callback; real idle word is the
  protocol's `RECEIVED_NOTHING`, not this.
