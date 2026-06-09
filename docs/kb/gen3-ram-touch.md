# Gen-3 Pokémon RAM addresses — game-aware touch (v1.1 Stage 2)

The SMART touch mode reads the **running core's** live RAM (via mGBA's `busRead8/16/32`, exposed as
`gbacore_read8/16/32`) to react to game state — tap-to-walk in the overworld, FIGHT/BAG/POKéMON/RUN
shortcut buttons in battle. These are the per-game addresses it uses (`source/gamestate.c`,
`GameProfile`).

## Verified addresses (US v1.0 / rev0 builds)

All four columns are **verified against pret's official byte-matched symbol maps** — the `symbols`
branch of `pret/pokeemerald` and `pret/pokefirered`, which are generated from the agbcc build that
matches the retail ROM byte-for-byte. (A `make modern` gcc build does **not** match — it pads EWRAM
differently, shifting these addresses ~12 bytes. Only the agbcc/symbols-branch sym is canonical.)
FireRed and LeafGreen are the same engine and share one EWRAM/IWRAM map.

| Game | code | gSaveBlock1Ptr | gBattleTypeFlags | gActionSelectionCursor[0] |
|---|---|---|---|---|
| Emerald   | BPEE | `0x03005D8C` | `0x02022FEC` | `0x020244AC` |
| FireRed   | BPRE | `0x03005008` | `0x02022B4C` | `0x02023FF8` |
| LeafGreen | BPGE | `0x03005008` | `0x02022B4C` | `0x02023FF8` |

Sources (HIGH confidence):
- `https://raw.githubusercontent.com/pret/pokeemerald/symbols/pokeemerald.sym`
- `https://raw.githubusercontent.com/pret/pokefirered/symbols/pokefirered.sym`
- `https://raw.githubusercontent.com/pret/pokefirered/symbols/pokeleafgreen.sym`
- sym format: column 1 = absolute GBA bus address (anchored: `gHeap`=0x02000000 EWRAM start,
  `gMain`=0x030022C0 IWRAM). Cross-checked vs Data Crystal FRLG RAM map + Ironmon-Tracker.

## How each is used

- **gSaveBlock1Ptr** — a `u32` pointer. Dereference it (must look like an EWRAM pointer, high byte
  `0x02`), then the first two `s16` at the pointed-to address are the **player tile X / Y**. Used by
  tap-to-walk for the on-screen "xy=" readout and (later) stall detection.
- **gBattleTypeFlags** — a `u32`, **nonzero while in a battle**. Switches the SMART overlay between
  the walk field and the battle action buttons.
- **gActionSelectionCursor[0]** — a `u8[4]` (one byte per battler); element [0] is the player's
  action-menu cursor: **0=FIGHT, 1=BAG, 2=POKéMON, 3=RUN** (`col = cursor&1`, `row = cursor>>1`).
  The battle touch controller reads this each frame and drives discrete D-pad presses to move the
  cursor to the tapped cell, then presses A — a closed loop, so a missed press self-corrects. If a
  game has no cursor address (field = 0), smart-battle degrades to the plain gamepad rather than
  reading garbage.

## Useful nearby symbols (cached at /tmp/*.sym during research, re-fetch from the URLs above)

- `gMoveSelectionCursor[0]` = action cursor **+4** (Emerald 0x020244B0, FRLG 0x02023FFC) — the
  move-select submenu cursor; needed if Stage 2 later adds move-pick shortcuts.
- `gBattleMons` (Emerald 0x02024084 / FRLG 0x02023BE4), `gBattleStruct` — for deeper battle state.
- Note `gSaveBlock2Ptr` is the adjacent +4 word (Emerald 0x03005D90 / FRLG 0x0300500C) — Data
  Crystal mislabels 0x0300500C "personal data"; that's SaveBlock2, not SaveBlock1.

## Caveats

- These are **rev0 (v1.0)** US addresses. rev1 carts (`_rev1.sym`) are essentially identical for
  these data symbols, but add the rev1 profile if a v1.1 cart misreads.
- Reading a core's RAM happens on the **main thread**. It's safe only when the worker is parked
  (the normal per-frame handshake): the SMART read is therefore **skipped while a link is active**
  (workers free-run) to avoid a cross-thread read race. Smart-battle + link are not used together.
