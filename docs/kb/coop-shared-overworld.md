# Co-op shared overworld (Gen-3, PokeMMO-style) — feasibility & plan

_Verified design study (2026-06-10). Same-console first, wireless later._

All findings are verified against the live code. I have everything: the tile-to-screen math (`gx = px + c, gy = py + r + 2` at main.c:290, player fixed at screen tile (7,5)), the parked window, the dual-`game_read` precedent, the OAM-read precedent, the GameProfile gaps (no sb2ptr, no link symbols, no RS rows), `gbacore` exposing only read/write8/16 (no write32, no ROM-call), and the wireless KB doc with the netlink-on-core-1 / second-channel design. Now I'll produce the architecture document.

---

# Shared Overworld for Dual-GBA (Gen-3) — Architecture & Verdict

**Scope:** PokeMMO-style shared overworld for Pokémon Gen-3 (Ruby/Sapphire first) running as two cores in one 3DS, extensible to two consoles over UDS. Everything below is grounded in the verified findings against the pokeemerald decomp and the live code at `projects/3DGBA/source/`.

---

## 1. Verdict

### (a) SHOW the other player on the same overworld map — **FEASIBLE**

Achievable today via the **citro2d overlay** path (Approach B). The data is free same-console (both games' RAM live in our process), the parked-read window and dual-`game_read` precedent already exist (`main.c:532-536`, `:577`, `:595-616`), and the screen-placement math is already in the codebase and proven correct:

- Player is fixed at screen tile **(7,5)** — `POP3D_PLAYER_GX 112` = 7×16 (`main.c:220`).
- `build_depth_grid` maps visible tile (c,r) → map grid `(px+c, py+r+2)` (`main.c:290`).
- `gSaveBlock1Ptr->pos` is the **top-left visible tile** (verified: `DrawWholeMapViewInternal` spans `pos.x..pos.x+14`), so the player tile is `(pos.x+7, pos.y+5)`. The MAP_OFFSET=7 bias cancels because both games read `pos` identically. Therefore peer A appears on host B's screen at screen-tile **`(7 + (Apx − Bpx), 5 + (Apy − Bpy))`** — no offset bookkeeping needed.

Gated on `(mapGroup, mapNum)` equality (SaveBlock1.location +0x04/+0x05). **Fragility:** purely cosmetic — B's engine doesn't know the avatar exists (no collision, no z-ordering by elevation), and sub-tile smoothness needs reading `gSpriteCoordOffsetX/Y` (not yet in the profile). Honest ceiling: "you can see them, you can't bump them."

The *robust native* alternative (Approach A', `gLinkPlayerObjectEvents`) is **IMPRACTICAL with the current core** — see §4.

### (b) INTERACT (trade / battle / mix / see ID) — **PARTIAL**

Split cleanly into two halves:

- **See ID / name / gender — FEASIBLE.** Pure read of the peer's `gSaveBlock2`: name@+0x00 (8B, 0xFF-terminated GBA charmap), gender@+0x08, visible TID = `read16(sb2+0x0A)` (proven: `trainer_card.c:722` computes exactly this LE u16). Renders via the existing C2D text path. **One required addition: there is no `sb2ptr` in `GameProfile` today** — it must be added (BPEE `gSaveBlock2Ptr = 0x03005D90`, immediately after `sb1ptr`).

- **Trade / battle / record-mix — PARTIAL, and only via the in-game link UI.** Verified hard correction across all three studies: **a trade/battle is NOT a callable function and cannot be force-started by RAM pokes.** It is a link-state machine requiring a live link with `GetLinkPlayerCount() >= 2`, populated `gLinkPlayers[]`, matching `gLinkType`, both games idle in the overworld, and a real trainer-card block exchange over `gBlockSend/RecvBuffer`. Poking `callback2` skips the linkup task that establishes `gLinkPlayers[]` → near-certain crash. **What IS feasible:** drive both games through their own Union-Room / cable-club handshake over the existing emulated SIO lockstep link (which the project already has). The cleanest trigger is to set the field special's variable (`gSpecialVar_0x8004`) and let `TryTradeLinkup`/`TryBattleLinkup` (`cable_club.c:610/571`) run — never raw `callback2`. Preferred and safest: **warp both into the real Union Room** via the existing specials (`RunUnionRoom`/`TryBecomeLinkLeader`/`TryJoinLinkGroup`, `data/specials.inc:432-436`), where every map-coordinate assumption `TryInteractWithUnionRoomMember` makes is already satisfied. This is PARTIAL because (i) none of these symbols are in our profile yet, (ii) it depends on the emulated link actually passing a real Union-Room trade end-to-end, and (iii) firing the linkup from arbitrary route coords (not a cable-club seat) is the fragile sub-path.

---

## 2. Recommended Architecture

### Presence: **overlay-avatar, NOT object-injection**

Verified across all three studies and corrected hard in two of them:

- **Reject naive `gObjectEvents[]` injection.** A bare struct write yields an invisible, inert slot — a live object event needs a paired `gSprites[]` entry, palette load, and a movement-type callback (built by `SpawnSpecialObjectEventParameterized`/`TrySetupObjectEventSprite`, `event_object_movement.c:1510/1418`), and `RemoveObjectEventsOutsideView` (`:1677`) culls any untracked non-player object every camera tick. Our core can't even poke the struct cleanly: **`gbacore.h` exposes only `read8/16/32` and `write8/16` — no `write32`, no ROM-call/trampoline.**
- **Reject Union-Room-avatar-on-real-maps as a drop-in.** Its group-member avatars are `CreateVirtualObject` *sprites* at hardcoded lobby coords with no object-event backing; `TrySpawnObjectEvent` needs a pre-placed map template. Good evidence the engine renders peers locally, but not portable.
- **The "right" native path (A', `gLinkPlayerObjectEvents[4]`, cull-exempt) is real but out of reach:** every driver function (`SpawnLinkPlayerObjectEvent`, `CreateLinkPlayerSprite`, `SetPlayerFacingDirection`, `FacingHandler_DpadMovement`) is `static`, requires a ROM-call mechanism we don't have, only runs in the link-overworld CB1 mode, and needs `SetPlayerFacingDirection` pumped **every frame** (not once per tile) for smooth walking. Park as a future option, not a milestone.

**→ Use the citro2d overlay.** Read peer state in the parked window, draw our own avatar sprite at `(7+Δpx, 5+Δpy)*16` through the same bottom-screen transform `touch_to_gba`/`render_game` already uses (`main.c:189`). Can't corrupt either game; reversible; reads-only.

### Interaction: **lean on the link / Union Room — never inject**

Presence is overlay/cosmetic; *real* trade/battle/mix rides the **existing emulated SIO link** through the game's own Union-Room or cable-club path. "See ID" is a pure SaveBlock2 read, no link. This is the robust split.

### Position sync: **same-console shared memory first, UDS beacon later**

- **Same-console:** read peer `(mapGroup, mapNum, currentCoords.x/y, facing, graphicsId, TID, name)` in the parked block (`main.c:532-536`) where both workers are blocked — race-free, identical to the existing snapshots. Add two ~10-read passes (one per core).
- **Wireless (later):** a ~16-byte packed `DgbaOverworld` beacon (round, mapGroup, mapNum, x, y, facing, moveState, graphicsId, trainerId) at 2–8 Hz on tile-change + keepalive, on a **second non-zero UDS data channel**, serviced by netlink on **core 1** (per `wireless-link-architecture.md:106`). Fire-and-forget, newest-wins, stale dropped by `round`. **Separate from the SIO link channel** — the beacon must never ride the `GBASIONetDriver` (which parks a core per transfer). `nwm::UDS` is already granted (`app.rsf:169`) — no build change.

---

## 3. Milestone Plan (each independently testable, starting same-console: Ruby + Sapphire, no networking)

### M0 — Profile groundwork (prerequisite, do first)
Author the missing data. **Verified gap: there are no Ruby/Sapphire (`AXVE`/`AXPE`) rows at all** — the table is BPEE/BPRE/BPGE only. RS layout is identical to Emerald; only base addresses differ and must come from RS-specific byte-matched symbol maps (not derivable from Emerald).
- **Files:** `source/gamestate.c` (add `AXVE`/`AXPE` rows), `source/gamestate.h` (add `sb2ptr` field to `GameProfile`; for M1+ smoothness also `spriteCoordOffsetX/Y`).
- **Verify:** boot Ruby in one core, Sapphire in the other; confirm existing smart-touch (which uses `sb1ptr`/`mapObjects`) works on both — that proves the new rows are byte-correct before building anything on them.

### M1 — Same-map detection + name/coords readout
Read both games' `(mapGroup, mapNum)` and player tile in the parked window; when both pairs match, draw a HUD line "PEER: <name> @ (x,y) — same map" + a marker (e.g. an arrow/dot at the computed screen tile).
- **Reads:** `mapGroup=read8(sb1+0x04)`, `mapNum=read8(sb1+0x05)`; player tile from `gObjectEvents[0]` (mapObjects+0, slot 0) `currentCoords` +0x10/+0x12 (subtract 7 for map tile) — or reuse the existing `out->px/py` SaveBlock1 read; name/TID from `sb2`.
- **Files:** new `source/overworld_peer.c`/`.h` (cross-read + same-map gate), `source/main.c` (call it in the parked block alongside the existing depth/touch reads; draw the HUD line).
- **Verify:** walk both characters onto the same map → HUD appears with correct coords; walk to different maps → it disappears. No avatar art needed yet.

### M2 — Overlay the peer avatar at the right screen spot, moving
Replace the M1 marker with a real walking sprite at `(7+Δpx, 5+Δpy)`, picking male/female frame by peer gender (`sb2+0x08`) and direction by facing (`oe+0x18 & 0x0F`), animated by a walk-phase counter. Add `gSpriteCoordOffsetX/Y` to the profile and add them for sub-tile scroll so the avatar tracks the host's BG scroll. Interpolate `previousCoords → currentCoords` over the ~8-frame step (no fractional logical coord exists in RAM).
- **Asset:** ship one generic 16×32 4-direction overworld sprite as our own C2D sheet (ripping the peer's VRAM frames is not worth it — swapped VRAM window + CPU frame DMA).
- **Files:** `source/overworld_peer.c` (interpolation + frame selection), `source/main.c` (`C2D_DrawSprite` through the bottom transform; mirror on top screen for the reverse direction), new asset under `gfx/`.
- **Verify:** on the same map, peer avatar appears at the correct relative position, faces the right way, walks smoothly tile-to-tile, scrolls in lockstep with the host BG, disappears off-screen / on map change.

### M3 — Interaction trigger (same-console, over the emulated link)
When the two avatars are adjacent + facing + both press A, show a "Trade / Battle / Card" prompt. **Card** = the pure SaveBlock2 ID overlay (no link). **Trade/Battle** = warp both games into the real Union Room via the existing specials, or set `gSpecialVar_0x8004` and let `TryTradeLinkup`/`TryBattleLinkup` run over the existing emulated SIO link — never poke `callback2`.
- **New profile symbols required:** `gSaveBlock2Ptr` (done in M0), `gLinkType`, `gSpecialVar_0x8004`, `gBlockSend/RecvBuffer`, and the linkup/Union-Room special entries — added as RS-verified `GameProfile` fields.
- **Files:** `source/gamestate.h`/`.c` (new fields), `source/overworld_peer.c` (adjacency+A detection, trigger), `source/main.c` (prompt UI).
- **Verify:** "Card" shows correct peer TID/name immediately (read-only, safe). "Trade" via Union-Room warp completes a real trade over the link (start with the safest path-a warp). Per CLAUDE.md #6, link-timing correctness is hardware-final, but same-console it completes instantly.

### M4 — Wireless (two consoles over UDS)
Swap the data source: instead of reading the sibling core's RAM, receive the `DgbaOverworld` beacon over a **second UDS data channel** serviced by netlink on core 1; gate the avatar draw on the packet's `(mapGroup,mapNum)` matching ours. Trade/battle routes through the wireless SIO `GBASIONetDriver` (the separate, reliable channel). The render/inject layer is identical to M2/M3 — it doesn't care if the position came from sibling EWRAM or the radio.
- **Files:** `source/netlink.c`/`.h` (add the overworld channel + beacon TX on tile-change/keepalive, RX into a peer-state struct), `source/overworld_peer.c` (consume packet instead of RAM read), `source/main.c` (wiring). No `app.rsf` change.
- **Verify:** two 3DSs in a lobby, same map → each shows the other's avatar at 2–8 Hz, smooth via interpolation; trade works over wireless. **Hardware-only sign-off** (Azahar models neither UDS latency nor core-2 contention).

---

## 4. Risks & Honest Take

- **Same-map-only constraint** — Inherent. The avatar only renders when `(mapGroup,mapNum)` match; cross-map presence (PokeMMO's "see them on the route map next door") is not in scope and not free. Accept it as the design boundary.
- **Sub-tile scroll** — There is no fractional logical coordinate in RAM; the GBA moves the *sprite*, not the tile, between centers. Use `gSpriteCoordOffsetX/Y` (the global per-sprite pixel scroll, only applied when `coordOffsetEnabled` — fine for our own overlay) plus `previousCoords→currentCoords` interpolation. Without this the avatar snaps tile-to-tile (still usable for M1, ugly for M2).
- **Sprite art** — Ship a generic overworld sprite; pick gender from `sb2`. 95% convincing, zero VRAM-ripping complexity. The peer's real frames are not worth chasing.
- **Link-trigger reliability** — The biggest interaction risk. Trade/battle is a real lockstep handshake, not a poke. It only works if the emulated SIO link genuinely passes a Union-Room trade end-to-end, and firing from arbitrary route coords may hit map-state assumptions the cable-club path normally guarantees. Mitigation: **prefer warping both to the real Union Room** (path a) where all assumptions hold; treat direct-route linkup (path b) as experimental.
- **Profile gaps are the real upfront cost** — No RS rows, no `sb2ptr`, no link symbols. M0 must produce RS-verified addresses from RS symbol maps before anything runs. This is the single biggest gap between "the studies say it's feasible" and "it runs on Ruby+Sapphire."
- **Robust vs. brittle — the bottom line:** the robust version is **overlay presence + official link/Union-Room interaction** (what this plan recommends). The brittle version is **full object-event injection + programmatic trade trigger** — and the latter is not merely brittle, it's *blocked* by the current core (no `write32`, no ROM-call) and was correctly rejected in every study. Build the robust one; the native `gLinkPlayerObjectEvents` path stays a documented future option behind a ROM-call capability that `gbacore.h` would first need to grow.

**Files of record:** `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/gamestate.h` (add `sb2ptr`, `spriteCoordOffsetX/Y`, link symbols; no RS rows today), `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/gamestate.c` (PROFILES — BPEE/BPRE/BPGE only, add AXVE/AXPE), `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/gbacore.h` (read/write8/16 only — no write32/ROM-call), `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/main.c` (parked window :532-536, dual game_read :577/:595-616, OAM read :601, tile→screen :290 / player-tile :220, transform :189), `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/source/touch.c` (slot-read precedent), `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/docs/kb/wireless-link-architecture.md` (netlink core-1, second channel), `/Users/guyshtainer/VSCodeProjects/3ds-toolkit/projects/3DGBA/app.rsf` (nwm::UDS:169, 804MHz/L2/Core2:76-78); new files to create: `source/overworld_peer.c`/`.h`, `source/netlink.c`/`.h` (M4), a `gfx/` avatar sheet. Engine refs: `/tmp/pret/pokeemerald/include/global.h`, `include/global.fieldmap.h`, `src/event_object_movement.c`, `src/overworld.c`, `src/cable_club.c`, `src/trade.c`, `src/union_room.c`, `src/union_room_player_avatar.c`, `src/trainer_card.c`, `data/specials.inc`.