# External projects teardown — gen1recomp & "Project PM" (2026-08-03)

Source teardown of two projects the user flagged as relevant to 3DGBA's two open
fronts: the **3D/HD-2D look** (`hd2d-octopath-3d.md`, `emerald-3d-depth.md`) and
**seeing other players live on the map** (`coop-shared-overworld.md`).

Both were cloned and read at source level, not summarised from READMEs.

> **Round 2 (same day, deep pass):** six subsystem readers + an inline
> disassembly of the romhack itself. Synthesis in **§Round 2** below; full
> per-subsystem detail (with file:line cites) preserved in
> [`external/`](external/) — `gen1-link.md`, `melonds-async.md`,
> `pm-bridge-forensics.md`, `gen1-render.md`, `gen1-parity.md`,
> `pm-rom-abi.md`, `rom-side.md`.

---

## 1. gen1recomp — the "Pokémon Red/Blue/Yellow in 3D" project

`https://github.com/bryanthaboi/gen1recomp` — MIT (BOIS CLUB GAMES, LLC), LÖVE2D/Lua.

### What it actually is

**Not a recompilation, not an emulator, and not AI.** It is a hand-written Lua
reimplementation of the Gen-1 engine (`src/world`, `src/battle`, `src/script`,
`src/render`) that decodes tables/graphics/audio programs out of a
SHA-1-verified user ROM at first boot (`src/import/RomExtractor.lua`,
`RomImporter.lua` — 215 KB of address metadata) into a private cache, then
releases the ROM. Behaviour is ported from the **pret/pokered** disassembly and
cited per script. `docs/architecture.md` is the map.

**Because they own the renderer, the map is data — a tile grid with semantics —
not a framebuffer.** Every 3D claim below follows from that one fact.

### How the "3D" is done (two separate things, both press-labelled "voxel")

1. **Shipped in-repo: `src/render/Tilt.lua` — 155 lines, no art, no AI.** The flat
   world canvas is treated as a ground plane, rotated about the horizontal axis
   through the viewport centre, viewed through a perspective camera:
   `groundPoint()` is a 6-line projective map (`scale = d/(d − w·sin a)`),
   `meshCorners()` emits 4 warped corners with per-corner scale. Levels
   OFF/15°/35°/50°, tweened 0.25 s, purely presentational — explicitly zero
   effect on collision/movement/triggers/scripts.
2. **Not in-repo: a separate "voxel" mod** (`DramaticShapeVoxelMod`) plugging into
   the `render_pipelines` registry (`src/mods/Schemas.lua:890-927`,
   `docs/modding.md:38-63`). A mod supplies `drawWorld/worldPresent/present`
   and may replace the world pass with real geometry — "a 3D diorama overworld
   plus a tilt-shift miniature pass, in about 120 lines of glue over its
   renderer." Distributed off-GitHub (Discord).

### The finding that matters most to us

`docs/new-features.md:54-72`, verbatim: an earlier revision **tried billboarding
buildings/trees/signs out of the ground plane** using hand-curated per-tileset
tables, and **abandoned it** —

> "that chased an endless tail of special cases — dense tree canopy, fences fused
> into grass, building facades with their own baked-in fake perspective — because
> Gen 1's art was never drawn with a clean seam between ground and standing
> scenery. It wasn't merged."

Only **things that stand** (player, NPCs, item balls, attached FX) draw as
upright billboards; everything else tilts as one rigid plane.

**They had per-tile data and full renderer ownership and still could not separate
ground from scenery.** We have one composited RGB565 framebuffer. This is
independent empirical confirmation of `hd2d-octopath-3d.md` §1 row 3
(layer separation = NO) and `emerald-3d-depth.md` A2 (IMPRACTICAL) — from the
strongest possible position, which is why it counts.

### AI verdict — unchanged, now with evidence

No AI anywhere in the project. The 3D is 155 lines of projective math plus, for
the voxel mod, hand-authored geometry over tile data. `hd2d-octopath-3d.md` §2's
split verdict stands unamended: **AI monocular depth = no** (runtime impossible
on 804 MHz ARM11; and 16 px tiles lack the photographic cues these models read —
Depth-Anything's 14 px patches make a whole sprite ≈ 1 token); **AI normal maps =
maybe, offline art-pipeline only**. An image→3D *mesh* generator is further from
useful than depth: it emits one mesh per image, and a tile-based scene has
thousands of live tile/palette combinations per map.

### What is liftable

- **The tilt math itself** (`Tilt.lua:120-153`, MIT, ~20 lines) drops straight into
  the **vertex-grid warp** `hd2d-octopath-3d.md` §3 already recommends for stereo.
  Same grid, one extra per-vertex term — the PICA200 vertex unit is our cheap
  resource, and this is a genuine "diorama" tell for ~0 net GPU cost.
- **Their kept subset maps onto data we already have.** "Only standing things
  billboard" = our OAM rect scan (`DepthSnap.spr[]`, `main.c:217`). We can do
  the subset they kept and skip the subset they abandoned.
- **UX pattern**: tweened discrete levels, gated to free-roam only, persisted,
  plus an AUTO **performance tier** that scales the extras by device
  (`src/core/Performance.lua`) — maps cleanly onto Old-3DS vs New-3DS.

### Caution

Their own README carries a `[!CAUTION]`: **`gen1recomp[.]com` is not theirs**, is
unauthorised, and is impersonating the project. GitHub + their Discord are the
only official sources. Do not fetch the voxel mod from that domain.

---

## 2. "Project PM" — live multiplayer in Pokémon Platinum

Two emulator forks by ComicartOlie + a binary romhack:
- `https://github.com/ComicartOlie/Desmume-Project-PM` (branch `platinum-mp`, GPLv2)
- `https://github.com/ComicartOlie/melonDS-Project-PM` (branch `platinum-mp`, GPLv3)
- `BasePlatinumMultiplayerV1.0BETA.xdelta` (1.07 MB VCDIFF, secondary-compressed)

### Architecture — a shared-RAM mailbox bridge, NOT emulated wireless

The entire emulator-side change is **one file** (DeSmuME:
`desmume/src/frontend/windows/mp_bridge.cpp`, 1671 lines; melonDS: the same pump
inside `src/frontend/qt_sdl/EmuThread.cpp`). Its own header states the thesis:
**"The emulated radio is never touched."**

The romhack publishes a **discovery block** in DS main RAM behind the signature
`0xCAFE1234 / 0x5678CAFE`. The emulator brute-scans all 4 MB every 60 frames for
it, then reads a pointer table (`mp_bridge.cpp:841-867`):

| slot | meaning |
|---|---|
| `[2]/[3]` | `exportBlk` / `importBlk` — legacy pairwise block |
| `[4]/[5]` | `partyExp` / `partyImp` |
| `[12]/[13]` | `pktExp` / `pktImp` |
| `[14]/[15]` | **`owExp` / `owImp` — the overworld presence channel** |
| `[17]` | debug inbox (seq/ackSeq/cmd/args — scripted control) |
| `[20]`,`[25]`,`[26]` | `blkSize`, per-role block array, per-role party array |
| `[31]` | `ctl` block, magic `'BRG1'` = `0x42524731` |

`ctl` is the two-way handshake: `+4` ROM-wants-session, `+6` **emulator heartbeat**
(so the ROM can tell a bridge-capable emulator from a plain one), `+7` status,
`+8` assigned role, `+9` peer mask, `+10/+12` party/pkt sizes.

Then, every emulated frame after `NDS_exec()` returns (RAM coherent, emulation
halted), the pump ships tagged bundles over a plain non-blocking TCP link
(host listens :7820, star topology, host relays client↔client; UDP :7821
`"PLATMP"` LAN discovery beacon):

```
[u16 len LE][tag u8][role u8][size u16 LE][payload]
  tag 1 = block + 48-byte overworld record   — EVERY FRAME
  tag 2 = party                              — on content change
  tag 3 = pkt                                — on content change
```

Receive is a `memcpy` into `importBlk` / `partyImp` / `owImp + (role−1)*48`.
The **48-byte overworld record** is the live-presence payload: `+4` x (s16),
`+6` z (s16), `+0x0C` frame counter, `+0x18` chosen-pair role
(`mp_bridge.cpp:1029, 1552-1557`; melonDS `EmuThread.cpp:291-292, 546-551`).

**The emulator is a dumb pipe. All game logic — spawning and animating the remote
avatars — is ROM code in the closed romhack.** Only the binary xdelta ships; no
public source (searched). What we get is the *pattern* and the *ABI*, not the
avatar implementation.

### The arc they walked is the arc we walked

Commit dates are decisive:
- **2026-07-20** `4508164` "Add async wireless mode for latency-tolerant link
  protocols" — make the *real emulated radio* survive network latency.
- **2026-07-21** `2ecca5d` "Platinum MP: embedded mailbox bridge over TCP" — give
  up on the radio, terminate the protocol locally and ship game state instead.

That is exactly 3DGBA's states A–E (RTT-bound emulated link) → state F (Celio
local termination). Independent convergence on the same answer by a team with a
far easier transport (TCP on a PC, not UDS on a 3DS).

### Directly transferable, ranked

1. **Emulated-time stretch as a latency backstop** (`Wifi.cpp` case 14, added by
   `4508164`). When a reply hasn't arrived: first a **bounded real-time spin**
   (12 × 1 ms) that eats the frame limiter's idle slack — free on a healthy host;
   then, still missing, park the transfer in a wait phase that burns **emulated**
   time in ~1 ms steps, resuming the instant the reply lands, capped below the
   game's own protocol timeout. Their measured envelope: **33 ms works, 120 ms
   tears the link down.** This is a principled version of our whole-idle-frame
   hold and is the most promising lever for **Tier D** (the universal ~4-5 fps
   fallback in `wireless-strategy.md`).
   Their hard-won rule, matching ours: *"Playing back anything but
   same-exchange-fresh replies breaks the WM link layer (measured twice), so
   pipelining is not an option."*
2. **Resend-on-peer-activation.** `freshPeer` clears the on-change send caches so
   a peer that connects *after* our last send still receives party/pkt
   (`mp_bridge.cpp:830-835, 999-1011`). Their bug report — "the accepter never
   gets the message… waits on partner party bytes forever" — is our run-#6
   `txSeq=0` mutual-hold deadlock in a different costume. Same fix shape.
3. **Host-relay star with stable role slots.** Slot *i* is permanently role
   `2+i`, so a reconnecting player gets its old role back; the host forwards
   every client bundle to the other clients (`:294-304, 994-996`). This is a
   concrete answer to the deferred **3–4 player** item: the *presence* layer can
   be N-seat with a host relay even though Celio trade-synthesis is 2-seat.
   Their 3+ routing lesson: with ≥2 game-active peers the pairwise channel must
   accept **only** the chosen partner, or a mid-battle pair's broadcasts land in
   a third player's import (`:1020-1055`).
4. **A write-watch tripwire.** `MpWatch_OnWrite` is called from the core's write
   paths for every CPU/DMA write while armed, logging address/value/**writing
   PC**/LR/CPSR for writes into watched ranges, with SDK-region filtering and
   (PC, 16-byte bucket) dedup (`:748-816`). They used it to catch a scribbler
   corrupting an armed alarm handler. Cheap to add to our mGBA core, and it would
   have de-blinded several 3DGBA runs (the wedge, the send-queue overflow) in
   one session instead of several.
5. **A scripted-input harness.** Env-driven autopilot (`MELONDS_AP=host|join`,
   patterns), plus a **file-driven tile-exact walker**: `melonds_move.txt`
   containing `"L1 U20 R3 D2"` holds each direction until the live grid
   coordinate read from the export block reaches target, with a wall timeout
   (`EmuThread.cpp:143-268`), and a CSV telemetry dump per frame. Our 3DGBA
   equivalent — read a walk script off `sdmc:` at boot — would make hardware
   runs repeatable instead of hand-driven.

### Where it agrees and disagrees with `coop-shared-overworld.md`

- **Agrees on the hard part.** Project PM never injects avatars from outside the
  game either. It patches the ROM so the *game's own code* spawns them. Our §2
  rejection of `gObjectEvents[]` injection from the emulator side is untouched by
  this project — it uses a different lever entirely.
- **The lever we don't have.** Their whole design assumes you may modify the game.
  On vanilla FR/EM we may not. That leaves our two documented options unchanged:
  **(a) the citro2d overlay avatar** (recommended, read-only, cosmetic ceiling),
  or **(b) our own distributable xdelta-style patch** for FR/EM that does what
  Project PM's ROM side does — a real project in its own right, and an IP
  question (`ip-legal`) before any line of it ships.
- **Sizing input for M4.** Their presence record is 48 B at 60 Hz over PC TCP.
  Ours is a ~16 B `DgbaOverworld` beacon at 2–8 Hz over UDS — correct, given our
  measured ~23 ms UDS round latency; their rate is not evidence ours can be
  raised. Fields worth copying: an explicit **frame/round counter** (they use
  `owExp+0x0C`, we already planned `round`) and a **pair-role** byte so presence
  and pairing share one record.

### Does `ds-toolkit` help?

**No.** Project PM is a Windows PC emulator fork plus a DS ROM hack; `ds-toolkit`
is native libnds homebrew running on DS hardware. Nothing runs on a 3DS and no
code crosses over. The transferable content is architectural, listed above, and
it belongs to 3DGBA, not to `ds-toolkit`.

### Licensing note before lifting any code

DeSmuME is GPLv2, melonDS GPLv3, gen1recomp MIT; 3DGBA is GPLv3. MIT and GPLv3
are safe to incorporate; a GPLv2-**only** file is not. Verify the per-file header
before copying anything from the DeSmuME fork — and prefer reimplementing the
*idea*, which carries no obligation at all.

---

# Round 2 — deep pass (2026-08-03, six readers + romhack disassembly)

Round 1 established the patterns; round 2 extracted the mechanisms. Everything
below is new or corrects round 1. Detail + cites: [`external/`](external/).

## R2.1 The romhack itself is open now (inline disassembly → `external/rom-side.md`)

- **Project PM's ROM side is a pret/pokeplatinum decomp build.** The xdelta's
  application header names its target `pokeplatinum.us.nds` (the decomp's build
  output), patched against clean **USA Rev 1**. Not a binary hack — modified
  decomp source, rebuilt, shipped as a VCDIFF delta carrying no game content.
  **This is the distribution model for any future FR/EM presence patch of ours**
  (pret/pokeemerald → modify → build → xdelta; ip-legal consult before shipping).
- User's dump is Rev 0 → checksum refusal; `xdelta3 -d -n` recovers every ADD
  byte exactly (static analysis only, not bootable).
- Read from the ROM's own Thumb code: `'BRG1'` stamped at fixed EWRAM
  **0x021DA5E0**; partySize **0x590 = 1424** (= 6×236B Gen-4 party mon + 8);
  pktSize **0x51C**; a 24-word scan at 0x020D1E34 exporting `OSi_AlarmQueue` to
  ctl+16 — the ROM side cross-validates the emulator-side ABI, plus two magics
  round 1 missed (`0x465A4231`, `0xB17FACE5`+counter telemetry block).
- No BizHawk fork public (comments reference one; ComicartOlie publishes only
  the two emulator forks).

## R2.2 Tier D engine — the full async-stretch design (`external/melonds-async.md`)

The complete mechanism, not round 1's sketch:

1. **Two-stage wait**: bounded *real-time* spin first (12×1ms — free while the
   console makes frame rate; spends the frame-limiter's idle slack), then park
   ONLY the wireless TX state machine in an emulated-time stretch phase polling
   every ~1ms emulated while the rest of the console keeps running.
2. **The 33ms envelope belongs to the GUEST**: its WM-library command timer
   keeps ticking during the host's stretch (120ms tears the link, measured).
   **GBA has no hardware SIO timeout** — our envelope is the game's software
   supervision (Gen-3 counts vblanks and tolerates idle rounds), so start with
   a ~2-vblank cap, then deliver 0xFFFF absent-slave and let the game's own
   error path run (bounded failure — the F1 wedge-escape lesson, principled).
3. **Only the master stretches; the slave never sleeps** and must ignore any
   master-timeline hint (their runahead=0 fix: honoring the stretched host's
   schedule made the client batch commands and reply from stale slots — the
   exact bug class behind our state-D "host-rate-follow" slowness).
4. **Same-exchange-fresh replies, verbatim** ("measured twice; pipelining is
   not an option") — our edge-strict gate already embodies this. **BUT** the
   DS-specific reason doesn't hold on GBA multi: **the slave's word is
   pre-loaded into SIOMLT_SEND before the master clocks**, so the slave can
   push each newly-written word over UDS proactively and the master completes
   against a word that already crossed — **one-way ~11.5ms instead of 23ms
   RTT**, roughly doubling Tier-D rate while staying hardware-accurate
   (sequence-numbered, consumed once).
5. **Receive-path law**: per-traffic-class queues (a misc lookup must never
   destroy a queued link word; bridge-class reliable, link words unsequenced),
   long retention for link words, accept late replies, and **drain-to-newest**
   (their stale-head bug pinned updates at ~5/s — a dead ringer for our slow
   rounds).
6. **Slot reaper**: reap half-connected joins on a timer or retries hit a
   phantom-full room — pair with our ≥2s no-transfer reset, at session layer.
7. **Honest bound**: this buys robustness, not throughput — ~43 transfers/s at
   one crossing per transfer (~86/s with pre-push). Tier D becomes smooth and
   unkillable, not fast. Fast-for-everything remains Tier 3.

## R2.3 Tier 3 blueprint — gen1recomp's link play (`external/gen1-link.md`)

The only other project running Pokémon link play between two independent
instances, and it validates our mirrored-pair architecture:

- **Symmetric lockstep replicas, no host authority**: both sides run the full
  engine on one host-dealt seed; only per-turn action INDICES cross the wire;
  anything both sides must agree on serializes host-side-first; every speed
  knob is pinned for the session. (Their luxury we lack: injected RNG — we
  must instead audit mGBA for per-instance entropy: RTC, audio-timing feedback.)
- **Severity-classed desync checksums — the single most transferable design**:
  per-turn 3-component hash; only *decisive* components (actives/bench) may end
  a match; *volatile* bookkeeping mismatches are logged drift (ending on those
  "cost players games they were winning, over nothing"). For dual-mGBA: hash
  RAM in components (party/battle-state EWRAM = fatal; timing/scratch = drift),
  NAME the diverged component + frame in the log, and record hashes for offline
  first-divergence sweeps over netlogs.
- **Trade atomicity**: commit = both-confirms-received; the local cancel goes
  DEAD the moment your own confirm is sent; save at the commit instant before
  the animation (pokered's own discipline). Matches + sharpens our trade FSM.
- **Link-surface fingerprint** (their answer to our run-#12 version guard):
  hash ONLY fields that decide link compatibility (over-hashing rejected
  genuinely compatible peers — their #511), pin as a committed golden WITH
  mutation tests (each field must move the hash), bless-to-change discipline.
  Ours should carry: game code+revision (FR rev0/rev1 bit us), protocol rev,
  celiolink FSM rev, mode flags — exchanged at UDS connect.
- **>2 players**: never N-way lockstep — one 2-player match + spectator
  replicas fed the same seed+action fan-out through a relay star. Online tier:
  dial-out TCP relay + 6-char Crockford room codes (no 0/O/1/I/L — right UX
  for D-pad entry), no NAT tricks.

## R2.4 Hardware-run forensics to port (`external/pm-bridge-forensics.md`, ranked)

| # | Feature | Catches | Size |
|---|---|---|---|
| 1 | **Game-frameCounter hang catcher**: FR/EM frame counter frozen >180f in-session → auto-dump mGBA PC/LR/SP/CPSR + GBA IE/IF/IME + 32-word stack to sdmc | the run-#4 silent-wedge class in ONE run (took us a 5-reader audit) | ~80 lines |
| 2 | **File-driven tile-exact movement**: `sdmc:/cias/control/move_p1.txt` `"L1 U20 R3 D2"`, closed-loop on gamestate.c coords, wall timeout, `!` abort | manual two-console choreography (our biggest per-run cost); makes run-#13's repro hands-free | ~150 lines |
| 3 | **Per-frame CSV**: own + peer game state + celio counters (section, gateN, forceN, txSeq/txAcked, rxDelivered, sioMode) as columns; host/join diff mechanically | run-#7's `rxDelivered=0` visible in the first 10 rows | ~40 lines on the bb227ee logger |
| 4 | **Write-watch tripwire**: armed ranges via a no-rebuild sdmc aim file; log frame/addr/val/**writing PC**/LR/CPSR; (PC,bucket) dedup + hit cap | scribblers; first target: the suspected FR/LG mapObjects address (bb227ee) | ~150 lines + 1 core hook |
| 5 | **Breadcrumb globals + watchdog**: numbered site IDs in every blocking loop; the RENDER thread (survives worker wedges) samples every 200ms, STUCK lines at 1/4/12s | the close-from-wireless-hang class | ~40 lines |
| 6 | **Input record/replay** anchored at field entry, on-change masks | repro assets + IS the Tier-3 determinism experiment (record seat A, replay into a second core from the same save, compare checksums — netplay not required) | ~100 lines |

Plus the recipe discipline: anchor scripts on celio FSM sections (never wall
clock), park saves at the scene under test, one long hold + settle over taps,
same-save boots to kill variance.

## R2.5 Presence plan — validated and sharpened (`external/pm-rom-abi.md`)

The crux, now proven from both sides: **PM's romhack exists because (a) stock
emulators lack game knowledge and (b) their game renders remote avatars
natively. We already have (a) for free** (gamestate.c reads gObjectEvents/
SaveBlock directly), so the entire *export* half of PM's ABI is redundant for
our overlay plan. A patch would buy exactly one thing — native avatars with
occlusion/collision/talk — and would then need only discovery + ctl + ow-import
+ inbox (drop their pairwise battle/party channels; local termination and
input-sync supersede that tier).

Adopt regardless of patch:
- **UDS presence packet mirroring the 48B ow record**: x, z, facing/appearance,
  mapId, frameCounter, pairRole. The frameCounter doubles as liveness/wedge
  detection.
- **Two-tier presence**: UDS-connected ≠ game-active; flip in-game state only
  on real game-data frames; latch milestones on the game's own view.
- **On-change-channel law** (they hit our run-#6 deadlock twice): every
  on-change channel invalidates its send cache on peer join/rejoin/activation;
  receive paths drain even when the local side isn't ready.
- **3-4P playbook, paid for by their regressions**: stable seat→role map,
  host-relay star with echo drop, per-peer arrays always update + pairwise
  buffers gated on published pairing intent, session-scope STICKY peer
  counting (an aging count re-opened a routing door mid-battle), stale-state
  purge at session boundaries, reap half-connected joins.
- **Debug inbox** `{seq, ackSeq, cmd, args}` (write args→cmd→seq=ackSeq+1):
  today's degenerate form is our EWRAM pokes (auto-mode gLinkType); if we ever
  author a patch, include it verbatim as the harness control channel.

## R2.6 Tilt render — round 1 correction: simpler than planned (`external/gen1-render.md`)

- **The tilted world is ONE 4-vertex quad**, not a grid. Their GLSL trick
  (premultiply UV by per-vertex scale, divide back per pixel) exists only
  because LÖVE draws 2D — it reconstructs perspective-correct interpolation.
  **The PICA200 does that in fixed function when clip-space w is real**: draw
  the frame quad genuinely rotated about X with a perspective matrix (focal =
  frame height) → pixel-exact, 4 vertices, zero fragment/TEV work, no
  projective texcoords. This SIMPLIFIES the hd2d plan's tilt (the stereo
  depth-warp grid remains a separate, still-valid tool).
- **Billboard = pure translation of the foot anchor** (never depth-scale —
  keeps pixel art crisp), y-sort by baseline, attached FX ride the owner's
  foot anchor. Transfers verbatim to our co-op overlay avatars.
- **Their billboard pass requires layer separation** (entities removed from the
  ground pass; holes never exist). Final verdict for DepthSnap.spr[] cutouts:
  unsupported — tilt the whole frame, billboard ONLY our own overlay
  (avatars/HUD). Their heal-machine "glued OAM" rule + abandoned building
  billboards close the case from both directions.
- **View growth is the ceiling**: they render up to 2.56× more world; we have
  a fixed 240×160 frame. Overscan-crop cost: ~17% at 15° (fine), ~57% at 35°
  (no). **Ship tilt as a mild ~15° diorama toggle**, VOID-FILL the ring.
- Hygiene: tilt strictly additive behind one `tiltActive()` (tween counts) so
  the flat GX_DisplayTransfer path stays byte-identical; GPU_LINEAR on the
  tilted texture, GPU_NEAREST flat; even canvas dims (no phase shimmer).

## R2.7 Suite/method upgrades (`external/gen1-parity.md`, sized)

- **TRIVIAL, do soon**: (1) a **laggyPair shim** in the celio PC suite modeling
  the measured ~23ms UDS round — our suite delivers instantly, which is exactly
  the blind spot they call out ("every desync players actually hit lived
  outside" the happy path); (2) per-round hash agreement asserted across both
  synthesized sides (not just final outcomes); (3) skipped test rungs print
  `skip <reason>`, never silent; (4) source citations inside check messages
  (`cable_club.c:NNN — ...`); (5) a `KNOWN-DIFFERENCES.md` tri-ledger for the
  celio port (intentional divergences — proactive heartbeat, whole-idle holds,
  wedge ceiling, over-drain pacing — each with the Celio/pret cite and a
  hw-validated / PC-only / unmeasured status).
- **DAY**: back-to-back-sessions test (trade → re-enter → trade, N× in one
  process — the re-entry path IS our open bug class); a netlog **golden-trace
  replay harness** (feed archived hw-run logs through the FSM on PC, assert the
  same `# celio-trace` transitions); the fingerprint golden + mutation tests;
  greppable per-subsystem netlog TAGs + a post-run wrapper that greps a run
  folder into a per-checklist verdict; verify-on-disk before reporting any
  artifact captured (run-#11's lost log is exactly their screenshot rule).
- **WEEK, when Tier 3 starts**: their `link_desync_fuzz` design verbatim — two
  mGBA cores in one process, seeded input streams (seed = the only input),
  deliberately ASYMMETRIC harness clients (frameskip/audio/0-8-frame lag each
  way), component-split per-frame checksums, failures replayable from the seed
  line alone.
- **Legal posture** (aligns with ip-publishing-policy): revision-exact SHA-1
  allowlist before patch/extraction; ship only metadata manifests + patches
  keyed to verified hashes; derived assets generated on-device into the app's
  cache from the user's ROM; CI lint that no ROM-derived bytes are committed.

## R2.8 What round 2 did NOT change

Round 1's negative results all held under deeper reading: no AI anywhere in
gen1recomp (the "voxel" look is hand geometry over tile data); AI depth still
wrong for us; layer separation still unbridgeable; object-event injection still
correctly rejected; ds-toolkit still irrelevant to Project PM; GPLv2-only
DeSmuME files still un-liftable into GPLv3 (reimplement ideas instead).
