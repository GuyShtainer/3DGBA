# The Gen-3 Pokémon GBA link wire format (and relaying it over a network)

Portable, verified knowledge for emulating or bridging the **GBA serial (SIO MULTI) link** that Gen-3
Pokémon games (Ruby/Sapphire/Emerald/FireRed/LeafGreen) speak during a cable-club **trade / battle /
Berry-blend / Record-mix**. Useful for any tool that links two emulated GBAs, relays a real link over a
network, or reverse-engineers the protocol. Verified against the open-source **Celio-Link** firmware
(`github.com/Celio-Link/Celio-Firmware`: `src/layers/packetLayer.*`, `src/link_defines.h`,
`src/sections/tradeConnection.cpp`) **and** the **pret** decomps (`github.com/pret/pokeemerald` +
`pokefirered`: `include/link.h`, `src/link.c`, `src/trade.c`). Both agree.

## The link is a state machine: HANDSHAKE phase, then repeating COMMAND frames

### Handshake phase — SINGLE repeated words (~30 ms each), not commands
- `SLAVE_HANDSHAKE  = 0xB9A0`
- `MASTER_HANDSHAKE = 0x8FFF`
- `HANDSHAKE_DISABLE = 0xD15E`  (e-Reader = `0xCCD0`)
- Both sides exchange single handshake words until **`0x8FFF` (master handshake) is seen** (sent or
  received); then they switch to the command phase. **This is a stream of single repeated words — NOT an
  8-word command frame.** Framing the handshake as an 8-word command drops the `B9A0` forever and the link
  hangs before it ever establishes. (This was a real, costly bug.)

### Command phase — repeating 9-word frames = `[CRC word][8 command words]`
- `CMD_LENGTH = 8`. Each frame = **1 CRC word + 8 command words = 9 transfers**, ~1.4 ms/word, ~13 ms/frame.
- **CRC = additive sum.** Each side accumulates `m_crc += word` over **both** sides' command words (its own
  tx **and** the peer's rx), seeded with the handshake value. Because the sum is mutual, **relaying the
  peer's genuine CRC word matches the game's own locally-computed CRC** — you don't recompute it, you pass
  it through.
- `QUEUE_CAPACITY = 50` — the game's send/recv FIFO is 50 deep. This slack is what lets one side run
  *bounded*-ahead without desync.
- **An all-zero (idle) 8-word command = `RECEIVED_NOTHING`** → the game **skips it** (in `link.c`) **before**
  any CRC check. So a **whole all-zero frame is a SAFE idle substitute** when you have nothing to send; a
  **mid-frame idle is FATAL** (it corrupts the running checksum → a link error). **Substitute idle only at
  frame boundaries, never within a frame.**
- The idle/nothing word is **`0x0000`**. (Do **not** use `0x7FFF` as "idle" — that value is
  `LINKCMD_COUNTDOWN`, a real command.)

### `LINKCMD_*` — the command words (Celio `link_defines.h`, matches pret)
`INIT_BLOCK=0xBBBB`, `CONT_BLOCK=0x8888`, `SEND_BLOCK_REQ=0xCCCC`, `SEND_LINK_TYPE=0x2222`,
`READY_TO_TRADE=0xAABB`, `SET_MONS_TO_TRADE=0xDDDD`, `START_TRADE=0xCCDD`, `READY_FINISH_TRADE=0xABCD`,
`CONFIRM_FINISH_TRADE=0xDCBA`, `READY_CLOSE_LINK=0x5FFF`, `SEND_HELD_KEYS=0xCAFE`, `NONE=0xEFFF`,
`READY_EXIT_STANDBY=0x2FFE`; cancel variants `0xEEAA/0xEEBB/0xEECC/0xBBCC/0xDDEE`; `LINK_PLAYER_ID=0x81`;
`COUNTDOWN=0x7FFF`.

### `LINKTYPE_*` — what the link session is for
`TRADE=0x1111`, `TRADE_CONNECTING=0x1122`, `TRADE_SETUP=0x1133`, `BATTLE=0x2211`, `SINGLE_BATTLE=0x2233`,
`DOUBLE_BATTLE=0x2244`, `RECORD_MIX_BEFORE/AFTER=0x3311/0x3322`, `BERRY_BLENDER=0x4422`, …

### The trade flow (Celio `tradeConnection.cpp`, cross-checked with pret `trade.c`)
handshake → link-type negotiation (`SEND_LINK_TYPE`) → **block exchange** (a `LinkPlayer` block first, then
the **party = 600 B sent as 3×200**, plus mail + ribbons, via `INIT_BLOCK`/`CONT_BLOCK` in 14-byte chunks)
→ select (`SET_MONS_TO_TRADE`) → confirm (`START_TRADE` / `READY_FINISH_TRADE` / `CONFIRM_FINISH_TRADE`) →
close (`READY_CLOSE_LINK`). **`INIT_BLOCK` (`0xBBBB`) is NOT proof of a real Pokémon trade** — it also fires
during the connection's `LinkPlayer` exchange. Don't read "saw `BBBB`" as "trading party data."

## The failure mode you'll actually hit: RECEIVED_NOTHING (not checksum)
- Decode `gLinkStatus` (pret `LINK_STAT` bits): `LOCAL_ID=0x03`, `PLAYER_COUNT=0x1C`, `MASTER=0x20`,
  `CONN_ESTABLISHED=0x40`, `RECEIVED_NOTHING=0x100`, `UNK_FLAG_9=0x200`, **`ERRORS=0x7F000`** (HARDWARE
  `0x1000` / CHECKSUM `0x2000` / QUEUE_FULL `0x4000` / LAG_MASTER `0x10000` / INVALID_ID `0x20000` /
  LAG_SLAVE `0x40000`).
- The classic relay failure is **`gRemoteLinkPlayersNotReceived` → `SetMainCallback2(CB2_LinkError)`**: the
  master keeps receiving idle/"nothing" from the slave, the connection forms then times out, and the game
  drops to its link-error screen. In the logs this is **`gLinkStatus & 0x7F000 == 0`** (no error bit set) —
  so it is **NOT a checksum, lag, or queue error.** Don't misdiagnose it as one. The cause is upstream: the
  relay fed the game whole idle commands because the peer's real words weren't available in time (the relay
  raced ahead, the clocks diverged, or the radio dropped). Fix the supply of real frames, not the checksum.
- **Read the GAME's own link-error state, not just your transport counters:** `gLinkErrorOccurred` (bool),
  `gLinkStatus` (u32), `sLinkErrorBuffer` (latches status + queue counts at the error),
  `gRemoteLinkPlayersNotReceived` (per-player "got nothing" flags, e.g. `0x01010101`). These are ground
  truth for *why* the game aborted; raw relayed-word logs are seductive and easy to over-read into a wrong
  story.

## The architecture that actually works: LOCAL TERMINATION
- **Don't relay one raw SIO word per network round-trip.** That parks the emulated core per word waiting on
  the radio → latency-bound (a Gen-3 MULTI link does ~9 transfers per emulated VBlank; one network
  round-trip per word ⇒ a few fps).
- **Terminate the link protocol LOCALLY** (the Celio-Link model). Each endpoint's link driver **is** the
  partner: it answers **every** SIO transfer itself at full GBA speed — handshake answered locally;
  command-phase frames fed from a local buffer; a **whole idle frame `[0×9]`** substituted when the peer's
  next frame hasn't arrived (never a mid-frame idle). Only the **semantic 8-word commands** (and the party
  block) cross the network, asynchronously. The guest never waits on the radio → it runs full-speed, and it
  is never fed mid-stream "nothing" → no `RECEIVED_NOTHING` error under normal pacing. This is a real
  subsystem (a `PacketLayer`-style state machine), not a pacing tweak.
- This also gives a clean **transport seam**: the same command stream can ride a local-wireless transport
  today and a socket/server transport later (online), behind one interface.

## Rules that cost real hardware cycles to learn
1. **Read the game's `gs`/link-error state FIRST**, before the raw-word relay log. Decode `gLinkStatus`
   against the bits above — if `0x7F000 == 0`, it is NOT checksum/lag/queue; say so.
2. **The handshake is single-word; the command phase is `[CRC][8 cmd]` 9-word frames.** Don't invent
   framing. Substitute idle only as a **whole frame**, never mid-frame.
3. **Relay the peer's genuine CRC word** (the mutual additive sum) — don't recompute it.
4. **`0xBBBB` ≠ a trade** (also fires during the `LinkPlayer` connection exchange).
5. **Verify every constant against both Celio and pret**, not memory — getting a word wrong silently breaks
   establishment with no error bit set.
