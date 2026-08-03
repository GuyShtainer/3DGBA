# Celio-Firmware: Data Block Formats + USB Wire Protocol

Scope: the dongle is an **RP2040 running Zephyr** that speaks **WebUSB (vendor bulk
endpoints)** to a host (a web page at `launcher.gblink.io`). The host relays to a server.
This doc covers the on-wire data-block formats (LinkPlayer, party Pokemon, mail, trainer
card) and the USB framing the dongle uses, so the same message set can be carried over UDS.

All paths are under
`/private/tmp/.../scratchpad/celio/Celio-Firmware/src/`.

---

## 1. LinkPlayerBlock byte layout

Source: `payloads/linkPlayer.h:5-26`, instance values `payloads/linkPlayer.c:3-22`.

`struct LinkPlayer` (24 bytes, **little-endian**, naturally packed — no padding because
fields are aligned; `linkType` at 0x14 is 4-byte aligned):

| Off | Type      | Field             | Notes |
|-----|-----------|-------------------|-------|
| 0x00| u16       | version           | demo value `0x4004` (`linkPlayer.c:8`) |
| 0x02| u16       | lp_field_2        | `0x8000` |
| 0x04| u16       | trainerId         | `0x529E` |
| 0x06| u16       | secretId          | `0x1805` |
| 0x08| u8[8]     | name              | GBA-charset, `0xFF`-terminated: `{0xC8,0xDD,0xE0,0xE7,0xFF,0,0,0}` |
| 0x10| u8        | progressFlags     | `&0x0F`=hasNationalDex, `&0xF0`=hasClearedGame (`linkPlayer.h:12`) |
| 0x11| u8        | neverRead         | |
| 0x12| u8        | progressFlagsCopy | |
| 0x13| u8        | gender            | |
| 0x14| u32       | linkType          | **overwritten at runtime**, see below |
| 0x18| u16       | id                | "battle bank in battles" (`linkPlayer.h:17`) |
| 0x1A| u16       | language          | `0x0005` |

`struct LinkPlayerBlock` (56 bytes) wraps it with magic strings (`linkPlayer.h:21-26`):

```
char          magic1[16];   // "GameFreak inc." (14 chars + NUL + 1 pad)
struct LinkPlayer linkPlayer; // 24 bytes (0x10..0x27)
char          magic2[16];   // "GameFreak inc."
```

Total = 16 + 24 + 16 = **56 bytes (0x38)**. magic1 spans 0x00..0x0F, linkPlayer
0x10..0x27, magic2 0x28..0x37.

Runtime mutation — the only field set per-session is `linkType` (`linkPlayer.c:24-28`):

```c
const struct LinkPlayerBlock* linkPLayer(uint32_t linkType)
{
    g_linkPlayer.linkPlayer.linkType = linkType;
    return &g_linkPlayer;
}
```

`linkType` default `0x1133` in the static initializer (`linkPlayer.c:17`) but always
clobbered by the caller's argument before the block is handed out.

---

## 2. Party Pokemon block — size + chunking

Source: `payloads/pokemon.cpp:11-124`, structs in `payloads/pokemon.hpp:11-150`.

### Slot / party sizing — it is **6 × 100, NOT 14-byte chunks, NOT 3×200 for storage**

Key constants (`pokemon.cpp:13-34`):
- `g_party` is `std::array<uint8_t, 600>` (`pokemon.cpp:28`) → **600 bytes total**.
- `numberOfSlots = 6` (`pokemon.cpp:34`).
- Each slot is **100 bytes**: `slot(index)` returns `subspan(index * 100, 100)`
  (`pokemon.cpp:42-45`).
- `g_fillerPkmnArray` is `std::array<uint8_t,100>` (`pokemon.cpp:13`) — the "empty slot"
  sentinel; a slot is empty iff it byte-equals this 100-byte filler
  (`pokemon.cpp:56-59`).

So each Pokemon transmitted = **100 bytes (0x64)** — this matches a GBA party-mon's
`struct Pokemon` (80-byte BoxPokemon + 20-byte battle stats). Confirmed by
`usbReceivePkmFile` which accumulates into `std::array<uint8_t,0x64> encryptedPkm`
(`pokemon.cpp:106`) and fires once `g_receivedBytes >= 0x64` (`pokemon.cpp:109`).

### The "200" you asked about = the *partner-party* receive chunk size, not slot size

`chunkSize = 200` (`pokemon.cpp:33`) is used ONLY by `partnerPartyConstruct`
(`pokemon.cpp:83-93`) to gate writes into a *separate* 600-byte `g_partnerParty`
(`pokemon.cpp:31`). It clamps each incoming write so it never crosses a 200-byte
boundary (`pokemon.cpp:85-90`):

```cpp
size_t nextThreshold = ((g_partnerReceivedBytes / chunkSize) + 1) * chunkSize;
if (g_partnerReceivedBytes + data.size() > nextThreshold)
    data = data.first(nextThreshold - g_partnerReceivedBytes);
std::ranges::copy(data, std::span(g_partnerParty).subspan(g_partnerReceivedBytes).begin());
g_partnerReceivedBytes += data.size();
```

i.e. the partner's 600-byte party arrives as **three 200-byte chunks** (600 = 3 × 200),
each chunk holding two 100-byte mons. Contrast with the *local* upload path
`usbReceivePkmFile` which is **one 100-byte (0x64) mon at a time**, placed into the first
empty slot (`pokemon.cpp:111-118`).

### Two distinct receive paths — do not conflate

- **Local party upload (host → dongle):** `usbReceivePkmFile(data, void*)`
  (`pokemon.cpp:104-121`). Streams 0x64 bytes into a static buffer, then drops the
  completed mon into the first empty (filler-matching) slot. Resets `g_receivedBytes`
  to 0 after each mon. This is the USB **DATA** handler for the local party.
- **Partner party (server → dongle):** `partnerPartyConstruct` (200-byte chunked, above)
  + `tradePkmnAtIndex(index)` which copies one partner slot over the local slot:
  `copy(subspan(index*100,100), slot(index).begin())` (`pokemon.cpp:97-100`).

### `struct Pokemon` layout (pokemon.hpp) — the 100-byte mon

`BoxPokemon` (`pokemon.hpp:100-117`) = personality(4) + otId(4) + nickname[10] +
language(1) + flags(1) + otName[7] + markings(1) + checksum(2) + unknown(2) +
`PokemonSubstruct secure`(48) = **80 bytes**. `secure` is the 4×12-byte substructs
(type0..type3, `pokemon.hpp:13-98`) — note these are stored **decrypted/unencrypted in
the struct definition**, but the wire buffer is named `encryptedPkm` (`pokemon.cpp:106`),
so on the wire the 48-byte secure block is the standard GBA XOR-encrypted+shuffled form.
`Pokemon` (`pokemon.hpp:119-132`) appends status(4)+level(1)+mail(1)+hp/maxHP/atk/def/
speed/spAtk/spDef (7×u16=14) = 20 bytes → **80 + 20 = 100 bytes total**. Matches the
100-byte slot exactly.

---

## 3. Mail and Trainer Card

### Mail — `payloads/mail.hpp:6-13`, `payloads/mail.cpp`

`struct Mail` (36 bytes, little-endian):

| Off | Type   | Field      | Default |
|-----|--------|------------|---------|
| 0x00| u16[9] | words      | nine `0xFFFF` (the mail message body) |
| 0x12| u8[8]  | playerName | `0xFF` × 8 (only 8 shown though 7 init'd) |
| 0x1A| u8[4]  | trainerId  | `{0,0,0,0}` |
| 0x1E| u16    | species    | `0x1` |
| 0x20| u16    | itemId     | `0` |

Total 0x22 = **34 bytes** by field math (0x20 + 2). NOTE the firmware's "empty mail
payload" is a **separate 220-byte all-`0xFF` blob**, NOT the 34-byte struct:
`g_array` is `std::array<uint8_t,220>` filled with `0xFF` (`mail.cpp:3-7`), exposed via
`getEmptyMailPayload()` as bytes (`mail.cpp:9-11`). 220 bytes = the size of the full
Gen-3 mailbox payload (6 party mail entries area). The `struct Mail` is the per-entry
template; the transmitted empty-mail block is 220 bytes of 0xFF.

### Trainer Card — `payloads/trainerCard.h:3-44`, values `payloads/trainerCard.c:3-41`

`struct TrainerCardRSE` (`trainerCard.h:3-27`), offsets as annotated in header
(little-endian; note compiler padding makes the annotated 0x04→0x06 gap = bool array
padding):

| Off | Type   | Field |
|-----|--------|-------|
|0x00|u8|gender|
|0x01|u8|stars|
|0x02|bool|hasPokedex|
|0x03|bool|caughtAllHoenn|
|0x04|bool|hasAllPaintings|
|0x06|u16|hofDebutHours|
|0x08|u16|hofDebutMinutes|
|0x0A|u16|hofDebutSeconds|
|0x0C|u16|caughtMonsCount|
|0x0E|u16|trainerId|
|0x10|u16|playTimeHours|
|0x12|u16|playTimeMinutes|
|0x14|u16|linkBattleWins|
|0x16|u16|linkBattleLosses|
|0x18|u16|battleTowerWins|
|0x1A|u16|battleTowerStraightWins|
|0x1C|u16|contestsWithFriends|
|0x1E|u16|pokeblocksWithFriends|
|0x20|u16|pokemonTrades|
|0x24|u32|money|
|0x28|u16[4]|easyChatProfile|
|0x30|u8[8]|playerName|

`struct TrainerCard` (`trainerCard.h:29-44`) appends the FRLG/Emerald extension at 0x38:
version(u8 @0x38), hasAllFrontierSymbols(u16 @0x3A), berryCrushPoints(u32 @0x3C),
unionRoomNum(u32 @0x40), berriesPicked(u32 @0x44), jumpsInRow(u32 @0x48),
shouldDrawStickers(bool @0x4C), hasAllMons(bool @0x4D), monIconTint(u8 @0x4E),
facilityClass(u8 @0x4F), stickers[3](u8 @0x50), monSpecies[6](u16 @0x54). Ends at 0x60 →
**0x60 (96 bytes)**. `easyChatProfile` words are packed as `(group<<9)|index`
(`trainerCard.c:26`). Returned as a fixed placeholder via `trainerCardPlaceholder()`
(`trainerCard.c:43-45`) — the dongle ships a canned trainer card, not a real one.

---

## 4. USB wire protocol — the message set to carry over UDS

Source: `layers/usbLayer.hpp` (the whole framing), `layers/usbLayer.cpp` (descriptors).

### Transport = WebUSB vendor interface, FOUR bulk endpoints, 64-byte packets

`usbLayer.cpp:312-367` defines interface 0 (`USB_BCC_VENDOR`, `bNumEndpoints = 4`) with
**two logical channels, each a bulk IN/OUT pair**. Endpoint addresses
(`usbLayer.hpp:19-22`):

| Const                | Addr  | Direction        | Purpose |
|----------------------|-------|------------------|---------|
| `commandOutEndpoint` | 1 (EP1 OUT) | host → dongle | **COMMAND** channel in |
| `statusInEndpoint`   | 129 (EP1 IN, 0x81) | dongle → host | **STATUS** channel out |
| `dataOutEndpoint`    | 2 (EP2 OUT) | host → dongle | **DATA** channel in |
| `dataInEndpoint`     | 130 (EP2 IN, 0x82) | dongle → host | **DATA** channel out |

So: **EP1 = command(OUT)/status(IN)**, **EP2 = data(OUT)/data(IN)**. All four are
`USB_DC_EP_BULK` with `wMaxPacketSize = 64` (`usbLayer.cpp:339-365`,
`m_endpointSize = 64` `usbLayer.hpp:24`). `bInterval = 0` in the descriptor
(`usbLayer.cpp:341` etc.) though a comment in `sendData` mentions bInterval=1 polling.

### There is NO custom length/CRC/command-ID header at the USB layer

Critical: the USB layer is a **raw byte-stream relay**, not a TLV framer. Framing =
**endpoint + transfer size only**. Each bulk transfer carries up to 64 raw payload bytes;
the *receive size* is the length field.

- **Receive (host → dongle):** `perpareNextReceive` posts a 64-byte read on the endpoint
  (`usbLayer.hpp:156-160`); on completion `receive(size, delegate)` (`usbLayer.hpp:162-170`)
  passes `span(buffer, size)` straight to the registered handler. `size` (the actual USB
  transfer length, ≤64) is the only length indicator. There is no magic/opcode parsing
  here — command vs data is distinguished **purely by which endpoint** (EP1 = command
  handler, EP2 = data handler), wired via `setReceiveCommandHandler` /
  `setReceiveDataHandler` (`usbLayer.hpp:88-98`, delegates at 172-184). The DATA handler
  is e.g. `party::usbReceivePkmFile` which itself does the 0x64-byte reassembly.

- **Send DATA (dongle → host):** `sendData(span<const uint8_t>)` (`usbLayer.hpp:65-86`)
  copies into a 64-byte `m_sendData` and does one `usb_transfer(dataInEndpoint=130, …,
  USB_TRANS_WRITE, …)`. Payload must be ≤64 bytes per call. Larger logical frames are
  **split across multiple 64-byte transfers by the caller** (comment `usbLayer.hpp:70-72`:
  "a >64-byte protocol frame split across transport chunks"). A semaphore
  `m_dataTransferDone` serializes back-to-back sends so the buffer isn't overwritten
  mid-transfer (`usbLayer.hpp:72`, released in `m_usbWriteDataCallback` 207-211).

- **Send STATUS (dongle → host):** `sendStatus(span<const uint8_t, 2>)`
  (`usbLayer.hpp:41-63`) — **STATUS is a fixed 2-byte message** on EP 129. Buffer is
  `std::array<uint8_t,2> m_sendStatusData` (`usbLayer.hpp:187`); `data.size_bytes()` is
  always 2. Same serialize-via-semaphore (`m_statusTransferDone`, 100 ms timeout,
  `usbLayer.hpp:50-60`). Comment names two example statuses: **DeviceReady** and
  **AwaitMode** (`usbLayer.hpp:47-48`) — these are 2-byte status codes the host polls to
  drive GBA online mode. (The enum values live elsewhere, not in these files.)

### Byte order

Little-endian throughout (RP2040 is LE; structs above are LE). USB descriptor fields use
`sys_cpu_to_le16/32` (`usbLayer.cpp:340,440` etc.) but **payload data is passed as raw
bytes** — endianness of the data blocks is whatever the GBA expects (LE), unchanged by
the USB layer.

### Summary of the message set to reproduce over UDS

To carry this over UDS you must reproduce **two independent ordered byte channels**
plus a **2-byte status channel**, not a single multiplexed stream:

1. **COMMAND channel** (host→device, was EP1 OUT): variable-length ≤64-byte messages;
   handler is app-defined (e.g. mode-select commands).
2. **STATUS channel** (device→host, was EP1 IN/0x81): **exactly 2 bytes**, e.g.
   DeviceReady / AwaitMode. Must be deliverable independently and serialized (one in
   flight at a time).
3. **DATA channel** (bidirectional, EP2 OUT/IN): raw payload bytes in ≤64-byte transfers;
   logical blocks (56-byte LinkPlayerBlock, 100-byte/0x64 mon, 200-byte party chunks,
   220-byte empty-mail, 96-byte trainer card) are **reassembled by the application
   handler** counting received bytes — the transport gives no length/opcode header, so
   the UDS carrier must preserve **message boundaries and the OUT vs IN direction** and
   the app layer keeps the running byte count to know when a logical block is complete.
