# Celio Client — online-transport-seam model (`commandEmitter` + device bridge)

Scope: the client-side seam that lets a physical Celio dongle (WebUSB / WebSerial talking
to a real GBA over a link cable) be relayed across a network to a remote peer/server, and
the high-level emu-trade orchestration that uploads Pokémon and drives the dongle's
firmware "modes".

Files digested:
- `commandEmitter.abstract.ts`
- `commandEmitter.socketIO.ts`
- `commandEmitter.websocket.ts`
- `services/websocket.service.ts`
- `services/linkdevice.service.ts`
- `shared/linkDeviceUtils.ts`
- `pages/tradeEmu/tradeEmu.component.ts`

---

## 1. The `CommandEmitter` seam — the swappable transport

`CommandEmitterAbstract` is the **transport-agnostic seam**. It is *purely an RxJS event
hub plus three abstract send methods*; it knows nothing about USB or about which network
protocol carries the bytes. Concrete subclasses bind it to a specific wire.

### Message envelope (the three packet types)

Three packet kinds flow across the seam (`commandEmitter.abstract.ts:2`, imported from
`../common`):

- **`DataPacket`** — a 64-byte link payload as 32 × `UInt16`. Constructed in the websocket
  bridge as `new DataPacket(this.sequence, dataArray)` (`commandEmitter.websocket.ts:52`),
  i.e. it carries a **sequence number** plus a `DataArray` (32 uint16s).
- **`CommandPacket`** — `{ uuid: string, command: CommandType }`
  (`commandEmitter.websocket.ts:60`). `uuid` generated via `uuidv4()`.
- **`StatusPacket`** — wraps a `LinkStatus` (sent as `status.linkStatus`,
  `commandEmitter.websocket.ts:79` / `commandEmitter.socketIO.ts:47`).

### The abstract contract (`commandEmitter.abstract.ts`)

```ts
export abstract class CommandEmitterAbstract {
  protected readonly closeSubject   = new Subject<void>();         // L6
  protected readonly commandSubject = new Subject<CommandPacket>();// L7
  protected readonly dataSubject    = new Subject<DataPacket>();   // L8

  data$():    Observable<DataPacket>    // L10  inbound data from the wire
  command$(): Observable<CommandPacket> // L14  inbound command from the wire
  close$():   Observable<void>          // L18  remote/session closed

  abstract receiveData(data: DataPacket): void;     // L22  push data TO the wire
  abstract receiveStatus(status: StatusPacket): void;// L24 push status TO the wire
  abstract destroy(): void;                          // L26
}
```

**Direction convention (critical, easy to invert):**
- `receiveData` / `receiveStatus` = "the emitter *receives* this from the local side and
  forwards it OUT onto the transport." These are **outbound** sends.
- `data$()` / `command$()` / `close$()` = **inbound** observables; what arrived FROM the
  transport, re-published to local subscribers.

Note the asymmetry: outbound has `receiveData` (data) + `receiveStatus` (status) but **no
`receiveCommand`** — commands are inbound-only across this seam. (Outbound command sending
to the dongle is done elsewhere, via `LinkDeviceService.sendCommand` /
`StatusEmitter.receiveCommand`.) Inbound has data + command + close, but no status$.

### Concrete transport A — Socket.IO (`commandEmitter.socketIO.ts`)

Server-relayed transport (the "real online peer" path), built on `WebSocketService`.

- Subscribes to three socket.io events and republishes them onto the abstract subjects:
  - `'deviceData'` → `dataSubject.next(data)` — uses **ack** form; immediately
    `ack(true)` (`socketIO.ts:14-20`, with `//FIXME better ack handling`).
  - `'deviceCommand'` → `commandSubject.next` (`socketIO.ts:22-28`).
  - `'sessionClose'` → `closeSubject.next()` then `this.destroy()` (`socketIO.ts:30-38`).
- Outbound (`socketIO.ts:42-48`):
  ```ts
  receiveData(data)   { this.websocketService.emit('deviceData', data); }
  receiveStatus(stat) { this.websocketService.emit('deviceStatus', stat); }
  ```
  So the inbound/outbound event names are **symmetric** (`deviceData`, `deviceStatus`,
  `deviceCommand`, `sessionClose`); the server routes them to the peer.
- `destroy()` just unsubscribes the bundled `Subscription` (`socketIO.ts:50-52`).

### Concrete transport B — raw local WebSocket (`commandEmitter.websocket.ts`)

A **localhost** bridge (`ws://localhost:51784`, subprotocol `"celio_local"`,
`websocket.ts:18`) — used to bridge to a **local helper/emulator process**, not the cloud
server. Has its own `open(retryDelay=1000)` with auto-reconnect (`websocket.ts:12-67`):
sets `binaryType="arraybuffer"`, retries on close until first successful open
(`this.retry=false` on open).

Inbound `onmessage` discriminates by JS type (`websocket.ts:42-63`):
- `ArrayBuffer` → parse **32 big-endian uint16** and emit a `DataPacket` with a
  locally-incremented `sequence`:
  ```ts
  const dataArray = Array.from({ length: 32 }, (_, i) =>
    view.getUint16(i * 2, false) as UInt16);     // false = big-endian  (websocket.ts:48-50)
  this.dataSubject.next(new DataPacket(this.sequence, dataArray));
  this.sequence++;                               // (websocket.ts:52-53)
  ```
- string `'sessionClose'` → `destroy()`.
- any other string → parsed as a number → `CommandType`, wrapped in a fresh-uuid
  `CommandPacket`.

Outbound (`websocket.ts:70-81`): data sent as a `Uint16Array` blob; status sent as
`status.linkStatus.toString()` (a **decimal string**, not binary). Both guarded by
`readyState == OPEN`.

**Wire-format divergence between the two transports (DO NOT assume one format):**
- Socket.IO path passes structured JS objects (the server serializes).
- Local-WS path is **binary 32×uint16 big-endian** for data and **stringified numbers**
  for command/status. Endianness here is explicitly big-endian, whereas the USB/serial
  device path below treats the same 64 bytes as **native/little-endian** uint16
  (`Uint16Array` over the raw buffer). This BE-vs-host mismatch is a real footgun.

### `WebSocketService` (`services/websocket.service.ts`)

Thin socket.io-client wrapper, `providedIn: 'root'` singleton.
- `io(environment.apiUrl, { transports:["websocket"], autoConnect:false,
  reconnectionAttempts:4, reconnectionDelay:100, reconnectionDelayMax:1000, timeout:5000 })`
  (`websocket.ts:10-17`).
- Auth handshake carries a per-instance `clientId = uuidv4()` (`websocket.ts:19-23`).
- `fromEvent<T>` (plain), `fromEventWithAck<T>` (delivers `{data, ack}` so the handler can
  ack — used by the data channel), `emit`, `connect()` (races connect vs connect_error →
  boolean), `disconnect()`.

---

## 2. The local device bridge — `LinkDeviceService` (WebUSB / WebSerial → GBA)

This is the **physical-dongle half** the seam bridges to the network. It exposes the same
three logical channels (command / data / status) over **two physical transports**, chosen
at connect time: `Mode = 'usb' | 'serial'` (`linkdevice.service.ts:5`).

### Device identity / endpoints
- USB filters: VID `0x2fe3` PID `0x0100` / `0x00a`; plus `0x8086/0xf8a1`
  (`linkdevice.service.ts:30-36`). Serial filter: `usbVendorId 0x2fe3`, **115200 baud**
  (`:37-39`, `:108`).
- USB endpoints: **statusEndpoint = 1**, **dataEndpoint = 2**, buffer 64
  (`:27-29`). Config 1, interface 0 (`:90-92`).
- **Commands and status share endpoint 1** on USB: `sendCommand` writes to
  `statusEndpoint` (`:276`), while status is *read* from `statusEndpoint` (`:235`). So on
  USB, OUT to ep1 = command, IN from ep1 = status. Data is ep2 both ways.

### Three RxJS output subjects (inbound from device)
- `statusEvents$` (`LinkStatus`), `dataEvents$` (`DataArray` = 32 uint16),
  `dataRawEvents$` (`Uint8Array` — non-64-byte data-channel payloads, i.e. **command
  replies** like `GetFirmwareInfo`), `disconnectEvents$` (`:41-53`, `:47-50`).
- USB disconnect is detected via `navigator.usb.ondisconnect` (`:65-72`).

### Serial framing (`SerialLayer`, GBLink firmware) — exact layout
Frame: `| 0x47 0x42 | channel:1 | len:2 LE | payload[len] |` (`:8-15`):
```
SYNC_0=0x47('G') SYNC_1=0x42('B')
CH_CMD=0x00  CH_DATA=0x01  CH_STATUS=0x02   MAX_PAYLOAD=64
```
- RX is a byte-fed state machine `sync1→sync2→channel→lenLo→lenHi→payload`
  (`feedByte`, `:146-184`). `len` is **little-endian** (`rxLen |= b<<8`, `:165`);
  `len>MAX_PAYLOAD` resets to sync1 (`:166`).
- `dispatchFrame` (`:186-198`) routing by `(channel, byteLength)`:
  - `CH_DATA` & len==64 → `dataEvents$` (32 uint16 view over the buffer).
  - `CH_DATA` & len>0 (not 64) → `dataRawEvents$` (raw bytes = command reply).
  - `CH_STATUS` & len==2 → `statusEvents$` (1 uint16).
- TX `writeFrame(channel,payload)` (`:200-217`): builds `5+len` bytes, len written LE
  (`frame[3]=len&0xFF; frame[4]=(len>>8)&0xFF`).

### USB read loop
`readData` / `readStatus` are **self-re-arming** `transferIn` loops (`:219-242`): each
success schedules the next `transferIn` while `mode==='usb'`. Data: 64 bytes → 32 uint16
→ `dataEvents$`; >0 & ≠64 → `dataRawEvents$`. Status: exactly 2 bytes → uint16 →
`statusEvents$`.

### TX API (outbound to device)
- `sendData(DataArray)` — 32 uint16; serial→`writeFrame(CH_DATA,...)`, usb→`transferOut(ep2)`
  (`:244-252`).
- `sendDataRaw(Uint8Array)` — **≤64 bytes** (`:254-255`); used to upload Pokémon chunks.
- `sendCommand(command, args=∅)` — message = `[command, ...args]`; serial→`writeFrame(CH_CMD)`,
  usb→`transferOut(statusEndpoint=ep1)` (`:268-285`).

### Firmware query pattern (`linkDeviceUtils.ts:70-90`)
`getFirmwareVersion` shows the **reply-on-data-channel** idiom: subscribe to
`dataRawEvents$` filtered by `bytes[0]===CommandType.GetFirmwareInfo` and length≥4 **before**
issuing `sendCommand(GetFirmwareInfo)`; reply layout `[0x0F, major, minor, patch, flags]`
(`:47-49` comment, parsed `:77-81`). Times out → `undefined`.

### Mode-enable / ready handshake (`linkDeviceUtils.ts`)
`tryEnableLinkMode` (`:57-65`) sequence, the canonical "arm a firmware mode" dance:
1. `sendCancel` → `receiveCommand(CommandType.Cancel)` (`:8-17`, `:61`).
2. `delay(500)` (`:62`).
3. `enableLinkMode` → `receiveCommand(CommandType.SetMode, [mode, variant?])` (`:19-33,:63`).
   Args: `args[0]=mode`, optional `args[1]=variant`.
4. `await createReadyPromise` — resolves when `status$` emits `LinkStatus.DeviceReady`,
   2500 ms timeout (`:35-55`). (Note these helpers go through a `StatusEmitterAbstract`,
   not `LinkDeviceService` directly — `StatusEmitter` is the outbound-command sibling of
   the inbound-only `CommandEmitter` seam.)

---

## 3. High-level emu-trade orchestration (`tradeEmu.component.ts`)

Drives the **local USB/serial dongle** directly (this page does NOT go through the network
`CommandEmitter` seam — it's the local-device trade-emulation flow). 4-step wizard
(`StepsState`, `:9-14`): ConnectingCelioDevice → SelectingPokemon → UploadingPokemon →
Ready.

- **Connect** (`:67-78`): `linkDeviceService.connectDevice(kind)` ('usb'|'serial'),
  gated by `usbSupported`/`serialSupported`. On success → SelectingPokemon.
- **Disconnect detection** (`:40-44`): `disconnectEvents$` → reset files, back to
  ConnectingCelioDevice.
- **File select** (`slotSelected`, `:86-100`): accepts files of **exactly 100 or 80
  bytes** (the two GBA Pokémon record sizes) → `PkmnFile.fromFile`.
- **`enableTradeMode`** (`:102-135`): inline version of the cancel→SetMode→wait-Ready
  handshake, here with `LinkMode.tradeEmu` and a 2000 ms timeout; resolves on
  `LinkStatus.DeviceReady`.
- **`upload`** (`:141-151`): after trade mode is ready, for each pkm file send the
  `encryptedBuffer` in **two `sendDataRaw` chunks**: `bytes.slice(0,50)` then
  `bytes.slice(50)` (because `sendDataRaw` caps at 64 bytes; a 100-byte record won't fit in
  one). Then → Ready.
- **Session-finished loop** (`:46-53`): `statusEvents$ === LinkStatus.EmuTradeSessionFinished`
  → clear files, return to SelectingPokemon (ready for the next trade).
- The dongle/firmware performs the actual cable-level Gen-3 trade against the real GBA; the
  web app only **uploads the mon, sets the mode, and watches status**.

---

## 4. Bridging picture (USB dongle ⇄ network)

```
 real GBA  <==link cable==>  Celio dongle (GBLink fw)
                                   |  WebUSB ep1(cmd/status) ep2(data)  OR  WebSerial 'GB' frames
                              LinkDeviceService   (statusEvents$ / dataEvents$ / dataRawEvents$ ; sendData/sendCommand)
                                   |
                              [ relay glue, not in these files ]
                                   |
                              CommandEmitterAbstract  (data$/command$/close$  ;  receiveData/receiveStatus)
                                 /                         \
                  CommandEmitterSocketIO            CommandEmitterWebsocket
                  (server relay, socket.io           (ws://localhost:51784, binary 32×uint16 BE)
                   events deviceData/Command/
                   Status/sessionClose)
                                   |
                              remote peer / emulator
```
The `CommandEmitter` seam is what makes the link **location-transparent**: the same
DataPacket/CommandPacket/StatusPacket envelope is carried either by a cloud socket.io relay
or by a local websocket to an emulator, swappable by instantiating a different subclass.
