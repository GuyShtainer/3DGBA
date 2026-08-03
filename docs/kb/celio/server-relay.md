# Celio-Server relay — architecture digest

Source tree: `Celio-Server/src/` (`index.ts`, `client.ts`, `session.ts`, `sessionManager.ts`, `messages/gameboy.ts`, `messages/session.ts`).

The server is a **Socket.IO (WebSocket-only) relay** between two GBA/emulator clients. It does
**not** emulate the link; it brokers the link handshake and then forwards opaque 32×u16 data
frames host↔peer with per-packet ordered delivery + ack-based retry + sequence-gap repair.

---

## 1. Transport / server bootstrap (`index.ts`)

```ts
const port = 443;
const io = new Server(httpServer, {
    cors: { origin: "*" },
    transports: ["websocket"], // 🚀 only WebSocket
    pingInterval: 500,
    pingTimeout: 2000
});
```
- `index.ts:8-15` — **WebSocket transport only** (no HTTP long-poll fallback), aggressive
  liveness: ping every **500 ms**, dead after **2000 ms** with no pong.
- `index.ts:26` — **client identity is supplied by the client** in the Socket.IO handshake auth:
  `let clientId = socket.handshake.auth.clientId;`. The server trusts it verbatim (no validation).
- `index.ts:28-29` — identity-based reconnect: if a `Client` with that id already exists,
  `clients.get(clientId)!.reconnect(socket)` rebinds the new socket to the existing logical
  client; otherwise a new `Client` is created. So **clientId is the stable logical identity;
  the underlying socket is disposable.**
- `clients` is a `Map<string, Client>` keyed by clientId (`index.ts:18`).

---

## 2. The Socket.IO event vocabulary

### 2a. Control events — client→server, registered in `Client.eventHandlers` (`client.ts:9-32`)
All take a Socket.IO **ack callback** (`responseHandler`) the server calls back with a result.

| Event | Args | Server action | Reply |
|---|---|---|---|
| `sessionCreate` | `(sessionId, responseHandler)` | `sessionManager.createSession(this)` (the passed sessionId is ignored — a fresh `nanoid()` is generated) | `responseHandler(Result<SessionState,ErrorType>)` |
| `sessionJoin` | `(sessionId, responseHandler)` | `sessionManager.enterSession(this, sessionId)` | `responseHandler(Result<...>)` |
| `sessionLeft` | `()` | `sessionManager.leaveSession(this)` | none |
| `disconnect` | `()` (Socket.IO builtin) | schedules `checkDisconnect()` after **5000 ms** | none |

`SessionState = { id: string; full: boolean }` (`sessionManager.ts:14-17`).
`ErrorType` ∈ `NotFound | AlreadyExists | SessionFull` (`sessionManager.ts:8-12`).
The `Result` type is `true-myth/result` (`isOk` / `.value`), serialized to the client over the ack.

> NOTE: `messages/session.ts` defines `CreateSessionMessage/JoinSessionMessage/LeaveSessionMessage`
> envelopes (each carries `clientId`, join also `sessionId`) but these types are **not imported
> anywhere** in the handlers shown — the wire handlers use positional args, not these shapes. Treat
> them as aspirational/unused for the actual protocol.

### 2b. Server→client events (emitted by `Session`)
| Event | Payload | Emitted when |
|---|---|---|
| `partnerJoined` | none | a 2nd client enters; sent to the already-present client (`session.ts:225`) |
| `partnerLeft` | none | a client leaves before the link started (`session.ts:244`) |
| `sessionClose` | none | session evicted / torn down (`session.ts:267`) |
| `deviceCommand` | `CommandPacket {uuid, command}` | handshake orchestration (multiple sites) |
| `deviceData` | `DataPacket {sequence, data[32]}` | **the relayed link frame** (`session.ts:85→queue`, plus gap-repair `session.ts:96`) |

### 2c. Per-session client→server events — registered in `Session.socketEventHandlers` (`session.ts:67-100`)
Subscribed only while a client is *in* a session (`session.ts:222-224`, via `client.fromEvent`):
| Event | Payload | Meaning |
|---|---|---|
| `deviceStatus` | `StatusPacket {uuid, linkStatus}` | client reports its link state machine position |
| `deviceData` | `DataPacket {sequence, data[32]}` | a link frame to forward to the peer |
| `requestData` | `[number]` (array of missing sequence numbers) | NACK / gap-repair request |

---

## 3. The message envelope (the load-bearing structs) — `session.ts:6-27`, `gameboy.ts`

```ts
type UInt16 = number & { __uint16: true };
type DataArray = [ /* exactly 32 × UInt16 */ ];   // session.ts:7-12

interface DataPacket   { sequence: number; data: DataArray; }   // session.ts:14-17
interface CommandPacket{ uuid: string; command: CommandType; }  // session.ts:19-22
interface StatusPacket { uuid: string; linkStatus: LinkStatus; }// session.ts:24-27
```

**`DataPacket`** is the actual relayed payload: a monotonically-increasing `sequence` plus a
**fixed 32-entry array of 16-bit words** (`data[32]`, total 64 bytes of link payload). The server
never inspects `data` — it is opaque GBA link content. **Our UDS transport must carry exactly:
{u32 sequence, u16[32] data}.**

**`CommandPacket` / `StatusPacket`** both carry a `uuid` (string) used purely for de-dup of
status (see §5); commands carry it too but it is not de-duped on the command path.

### Enums — `gameboy.ts`
```ts
enum LinkStatus {              // client→server, in StatusPacket
    Empty             = 0xFFFF,
    AwaitModeEmulator = 0xFF01,
    AwaitMode         = 0xFF02,
    HandshakeReceived = 0xFF03,
    HandshakeFinished = 0xFF04,
    LinkConnected     = 0xFF05,
    LinkReconnecting  = 0xFF06,
    LinkClosed        = 0xFF07,
}
enum CommandType {             // server→client, in CommandPacket
    SetMode        = 0x00,
    SetModeMaster  = 0x10,
    SetModeSlave   = 0x11,
    StartHandshake = 0x12,
    ConnectLink    = 0x13,
}
```
These integer codes are the on-wire protocol contract — keep them identical across UDS / online.

---

## 4. Pairing two players (`sessionManager.ts` + `session.ts`)

- A `Session` holds `clients: Client[]` (max 2) and `clientState: Map<Client, ClientState>`
  (`session.ts:43,49`). `isFull() = clients.length >= 2` (`session.ts:199-201`).
- **Create:** `createSession` makes `sessionId = nanoid()`, constructs a `Session`, subscribes
  once to its `close$` for cleanup, stores it, then immediately enters the creator
  (`sessionManager.ts:24-38`). One client per `Client` object — re-create returns
  `AlreadyExists` (`sessionManager.ts:25`).
- **Join:** `enterSession` looks up by id, rejects `NotFound` / `SessionFull`, records
  `clientToSession`, calls `session.enter(client)` (`sessionManager.ts:40-53`).
- **`Session.enter`** (`session.ts:215-228`): pushes client, creates a fresh `ClientState`,
  wires up the three per-session event handlers (`deviceStatus`/`deviceData`/`requestData`) as
  rxjs subscriptions, then **`emitToOppositeSocket(client, "partnerJoined")`** to notify the
  peer, and marks `client.inSession(true)`.
- So **pairing = "first client creates a room id, shares it out-of-band, second client joins the
  same id"**. There is no matchmaking; the room id (nanoid) is the rendezvous token. Exactly 2
  slots; a 3rd join gets `SessionFull`.

---

## 5. Handshake orchestration — server as link-mode arbiter (`session.ts:126-185`)

The server drives the GBA link bring-up by reacting to each client's reported `LinkStatus` and
issuing `deviceCommand`s. `started` flips true on the first status received (`session.ts:129`).

```ts
case LinkStatus.AwaitMode:                       // session.ts:133-141
    if (!this.masterSelected) {
        client.emit("deviceCommand", makeCommand(CommandType.SetModeMaster));
        this.masterSelected = true;              // first-to-ask becomes MASTER
    } else
        client.emit("deviceCommand", makeCommand(CommandType.SetModeSlave));
    break;
case LinkStatus.AwaitModeEmulator:               // session.ts:143-145
    client.emit("deviceCommand", makeCommand(CommandType.SetModeSlave)); // emulator side always SLAVE
    break;
case LinkStatus.HandshakeReceived:               // session.ts:147-157
    clientState.status = HandshakeReceived;
    if (every state === HandshakeReceived)        // barrier: BOTH must be ready
        StartHandshake -> this client AND the opposite;
    break;
case LinkStatus.LinkConnected:                   // session.ts:163-166
    emitToOppositeSocket(client, ConnectLink);
    break;
case LinkStatus.LinkClosed:                       // session.ts:172-183
    if (all closed) setTimeout(() => this.evict(), 2000);
```

Key facts:
- **Master election is "first AwaitMode wins"** (`masterSelected` latch, `session.ts:45,134-137`).
- `StartHandshake` is a **barrier**: only fired once *both* clients reported `HandshakeReceived`
  (`session.ts:150-156`), and it's sent to both.
- **De-dup of status:** `receivedStati: Set<string>` keyed by `statusPacket.uuid`; a repeated
  uuid is dropped with `"Received duplicate status packet"` (`session.ts:47,70-74`). So the
  client may safely re-send a status under the same uuid for reliability; the server processes it
  once. (Commands and data are NOT de-duped this way.)
- `HandshakeFinished`/`LinkReconnecting` only record state, no command emitted.

---

## 6. Forwarding a data frame host↔peer (the relay core)

```ts
deviceData: (client, dataPacket) => {                 // session.ts:78-86
    let receivedPacketMap = this.clientState.get(client)?.packets;
    ...
    receivedPacketMap.set(dataPacket.sequence, dataPacket);   // CACHE by sequence
    this.emitAckedToOppositeSocket(client, "deviceData", dataPacket);
}
```
- Each inbound `deviceData` is **cached** in that sender's `ClientState.packets:
  Map<sequence, DataPacket>` (`session.ts:38,84`) so it can be re-served on `requestData`.
- It is then forwarded to the **other** client via `emitAckedToOppositeSocket`
  (`session.ts:296-301`) → `queueAckablePacket` → `this.send$.next(...)`.
- **Ordered, ack-gated delivery pipeline** (`session.ts:51,102-113`):
```ts
this.ackablePacketSubscription = this.send$.pipe(
    concatMap((packet) =>
        packet.client.emitWithRetry<boolean>(packet.event, packet.args)
            .catch(err => { ...; this.evict(); ... })   // give up -> tear down session
    )
).subscribe();
```
  `concatMap` serializes: **packet N+1 is not emitted until packet N's ack resolves** — strict FIFO,
  one-in-flight, per session. A permanently-failing ack `evict()`s the whole session.

### Reliability / retry (`client.ts:94-122`)
`emitWithRetry`: `retries=5, timeout=1000ms, backoff=100ms*attempt`. Uses Socket.IO
`socket.timeout(timeout).emit(event, data, ackCb)`; on timeout it retries with increasing delay;
after >5 attempts it `reject("Max retries reached")` → session eviction. **So data delivery to the
peer is at-least-once with an application ack, in order, with bounded retry.**

### Gap repair / NACK (`session.ts:88-99`)
```ts
requestData: (client, missingSequenceNumbers: [number]) => {
    let receivedPacketMap = this.clientState.get(client)?.packets;
    missingSequenceNumbers.forEach(seqNum => {
        let packet = receivedPacketMap.get(seqNum);
        if (packet) client.emit("deviceData", packet);   // direct re-send, NOT ack-queued
        else console.log("Requested data packet " + seqNum + " not found");
    });
}
```
- A client detecting a missing `sequence` sends `requestData([..gaps..])`; the server replays the
  cached packets **of that same client's map** back to *that same client* via plain `emit`
  (no retry, no ordering). (Note: it reads `clientState.get(client).packets`, i.e. the requester's
  own cache — see "gotchas".)

---

## 7. Ordering / reliability / reconnect guarantees (summary)

- **Ordering:** per-session FIFO on the forward path via `concatMap` over a single `send$` subject
  (`session.ts:103-113`). One outstanding ack at a time → forwarded frames arrive in send order.
  The relay does **not** reorder by `sequence`; ordering is delivery-order, and `sequence` exists
  for the *receiver* to detect/repair gaps.
- **Reliability:** application-level ack + retry (5×, 1 s timeout, linear backoff) on every
  forwarded `deviceData`/queued event (`client.ts:94-122`). Status de-duped by uuid. Data
  recoverable via `requestData` from the server-side per-client cache.
- **Reconnect:** logical client survives socket loss. On `disconnect`, server waits **5 s**
  (`client.ts:30`) then `checkDisconnect()` (`client.ts:59-65`): if still `socket.disconnected`,
  leave session + remove client; else "recovered". A returning socket with the same
  `auth.clientId` calls `Client.reconnect()` (`client.ts:42-49`), which rebinds handlers and
  pushes the new socket into `socket$` (a `BehaviorSubject<Socket>`); `fromEvent` uses
  `switchMap` over `socket$` so all per-session subscriptions **automatically re-bind to the new
  socket** (`client.ts:71-75`). `LinkReconnecting` (0xFF06) is the client-side counterpart state.
- **Teardown:** `leave` before link start → `partnerLeft` to peer + remove. `leave` after a full
  started session → full `evict()` (`session.ts:240-247`). `evict` emits `sessionClose` to all and
  removes them (`session.ts:265-270`). Empty session fires `close$` → `SessionManager.deleteSession`
  (`session.ts:259`, `sessionManager.ts:73-87`). All links `LinkClosed` → `evict` after 2 s.

---

## 8. Gotchas an implementer must know

- `sessionCreate`'s incoming `sessionId` arg is **ignored**; the server always mints a new
  `nanoid()` and returns it in `SessionState.id` (`client.ts:11-15`, `sessionManager.ts:26`). The
  creator must read the id from the ack.
- `clientId` is **client-asserted and unauthenticated** (`index.ts:26`) — fine for LAN/UDS, a spoof
  risk for any online relay; an online relay must add auth or namespacing.
- `requestData` replays from `clientState.get(client).packets` — the **requester's own** cache, not
  the peer's. For our re-implementation the intent is "re-deliver the frames the peer sent me";
  verify which cache you key gap-repair against rather than copying this verbatim.
- The forwarded `deviceData` carries the **original sender's** `sequence` namespace; each direction
  has its own independent sequence stream (per-`ClientState` map).
- `port = 443` but plain `createServer` (no TLS) — it's HTTP-on-443, not HTTPS (`index.ts:1-8`).
- Backpressure: because `concatMap` allows only one un-acked frame in flight per session, a slow/lossy
  peer throttles the whole link to round-trip-bound throughput (matches the observed ~latency-bound
  fps in our UDS experiments).
