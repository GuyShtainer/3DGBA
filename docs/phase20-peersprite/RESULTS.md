# Phase 20 — `RESULTS.md`: the emulator slice (§S6), what the pixels actually show

Every capture below was taken this session in Azahar 2125.1.2 on this machine, and **every image in
this document was opened and read by the author** — the descriptions are of what is in the frame,
not of what the code was supposed to draw. Where a claim is only *partly* carried by an image, that
is said in the row rather than glossed.

The ask this answers, verbatim:

> "Oh wow. Show more locations and fix the player image to the geniuene one"

The before picture is `docs/phase18-crisp/evidence/coop-both-screens.png`: the same two screens with
a **magenta placeholder** standing where the peer should be.

Start with **`evidence/peer-sprite.html`** — one captioned contact sheet, before/after first.

*A note on the `-native` files.* They are `native.py` reconstructions of the 400×240 / 320×240
framebuffers from the window capture. `--verify` reported ~4.7 % of device pixels differing from a
perfect nearest re-expansion, so on this run they are a **faithful downsample, not the byte-exact
reconstruction** `native.py`'s doc claims for an ideal capture. Nothing in this document rests on
them: every claim is carried by the as-captured PNG or by a gdb readout. Worth a look by whoever owns
the harness.

---

## The headline

**The peer's avatar is now the peer's own trainer sprite, read live out of their game's emulated OAM
+ OBJ VRAM + palette RAM.** We ship no art. It animates, it turns, and it mirrors correctly, because
those are properties of the peer's bytes and not of our code.

| goal | verdict |
|---|---|
| G1 — the genuine sprite, resolved (not silently fallen back) | **PROVEN** |
| G2 — the walk animation and the east/west flip | **PROVEN** |
| G3 — co-op across several locations, both screens | **3 locations delivered** (indoor link room, indoor lobby, outdoor daylight); the §S6 shortlist could not be reached — see "the sweep" |
| G4 — a resolve failure degrades to the placeholder, no garbage, no crash | **PROVEN (observed, not simulated)** |
| Q1 — DISPCNT is really readable over `gbacore_read16` | **ANSWERED — yes** |
| Q2 — the §S3.7 tiling proof passes on the device | **ANSWERED — yes** |

---

## G1 — the genuine sprite

**Files:** `evidence/g1-mauvillepc.top.png`, `evidence/g1-mauvillepc.bottom.png` (as captured, 900×540
/ 720×540), `evidence/g1-mauvillepc-top-native.png`, `evidence/g1-mauvillepc-bottom-native.png`
(reconstructed 400×240 / 320×240), `evidence/g1-zoom-peer-vs-local.png` (8× zoom),
`evidence/loc-mauvillepc.txt` (the gdb readout at that instant).

**Setup:** Emerald + Emerald (seat B's ROM and save are copies of seat A's, as two separate files —
the picker refuses the same file twice). Both seats reached Mauville Pokémon Center 2F (map `10-6`)
by a normal CONTINUE from the user's own save; seat B was then separated by a **real D4 walk**
(`R2`), not by a save patch. Map `10-6` and the `+2,+0` tile delta are the same fixture the phase-18
picture used, so this is a like-for-like A/B against it.

**The readout, at the captured frame** — the resolve predicate read over GDB, so this is *not* a
silent fallback that happens to look plausible:

```
enabled=1 artOk=1 pairReason=0 sprTileOk=1 sprFlags=0x00
  game0: reason=0(ok) drawn=1 ... dTile=+2,+0
         SPR reason=0(ok) 16x32 pal=0 hF=0 vF=0 map1d=1 gfxId=0x59 anim=4/1 uploads=65
  game1: reason=0(ok) drawn=1 ... dTile=-2,+0
         SPR reason=0(ok) 16x32 pal=0 hF=1 vF=0 map1d=1 gfxId=0x59 anim=7/3 uploads=72
```

`SPR reason=0` is `PSPR_R_OK` — the live cell is what is being drawn. `uploads=65/72` means real
pixel gathers have happened (a fallback would sit at 0). `16x32` is the overworld trainer size read
from OAM, not assumed. The app's own HUD says the same thing on screen: `spr: OK 16x32 t0800 p0
F-1 u72`.

**What I see in the top capture:** the Pokémon Center 2F, three attendants behind the counters, and
**two trainers standing side by side** — the local player on the left facing the camera, and the
peer two tiles to its right under a yellow `GUYA` nameplate. The peer is drawn in the same 16×32
Gen-3 overworld art style as the local player: white-and-green bandana, brown hair, red/white
outfit. There is no magenta anywhere in the frame.

**What I see in the bottom capture:** the same room from seat B's camera, and the roles are
*swapped* — seat B's own trainer is on the right, and the `GUYA` nameplate sits over the trainer on
its **left**, which is the correct side for `dTile=-2,+0`. Each screen is drawing the *other* game's
player.

**The zoom** (`g1-zoom-peer-vs-local.png`, 8×) puts the local and the peer trainer side by side at
readable size. Honest note on it: the peer looks slightly darker than the local player in that
frame. That is **pose, not tint** — the peer is standing in the east-facing side profile
(`anim=7/3`, `hF=1`), where the hair covers the face, while the local player is in the front-facing
pose. The bottom screen shows the reverse pairing and the two look alike there. No per-peer colour
modulation is applied; the peer avatar receives the same `C2D_ImageTint` as the game frame it is
drawn over.

---

## G2 — the animation and the flip

**Files:** `evidence/g2-walk/` (120 PNG frames + `manifest.json` + `rec_top.mp4`),
`evidence/g2-frame00012-zoom.png`, `evidence/g2-frame00022-zoom.png`.

Seat B was walked `D1 L3 R3 U1` with the D4 channel while `see rec` captured the top screen at
3 fps and read **two `g_presDiag` fields at every frame** over gdb: `sprAnim[1]` (`+0xa8`) and
`sprPal[1]` (`+0x98`, which carries the flip bits). The state came back on all 120 frames
(`state_error: null`).

The transitions, straight out of `manifest.json`:

| frames | `animNum/animCmdIndex` | `hFlip` | what the script was doing |
|---|---|---|---|
| 0–1 | 7/3 | **1** | standing, facing east |
| 4–5 | 4/2 → 4/3 | 0 | walking **south** (`D1`) |
| 8–16 | 6/0 → 6/1 → 6/2 → 6/3 | 0 | walking **west** (`L3`) |
| 18–26 | 7/0 → 7/1 → 7/2 → 7/3 | **1** | walking **east** (`R3`) |
| 28–31 | 5/2 → 5/3 | 0 | walking **north** (`U1`) |

Two things are proven by that table alone, and neither is producible by our fallback poser:

1. **The stride is real.** `animCmdIndex` cycles `0 → 1 → 2 → 3` *within* a single direction. Those
   are the peer's own animation frames; we never compute them.
2. **The facing follows travel, and the mirror is the peer's, not ours.** `animNum` changes 4/6/7/5
   in exactly the order the script walked, and `hFlip` is **1 only for east** — which is the Gen-3
   convention (the west-facing art is the base, east is its horizontal mirror). We read the flip bit
   out of OAM attr1 and apply it; we do not decide it.

**What I see in the two zooms:** in frame 12 the peer is in a left-facing side profile, mid-stride,
one leg forward. In frame 22 the same sprite is in a **right-facing** side profile — the identical
artwork, mirrored, with the head and body pointing the other way. It is mirrored the right way
round: the character faces the direction it is travelling, not backwards.

---

## G3 — the sweep: three locations, both screens

**The §S6 shortlist as written could not be reached, for a reason worth knowing** — see "`place.py`
cannot move the player" below. Locations were therefore reached the honest way: by **walking the two
games there**, through real warps, with the D4 channel. Each row below is both screens at once, each
game drawing the *other* game's trainer.

| # | location | map | files | what is in the frame |
|---|---|---|---|---|
| 1 | **Pokémon Center 2F** (the link/trade floor) | `10-6` | `g1-mauvillepc.{top,bottom}.png` + `-native` + `g1-zoom-peer-vs-local.png` | Three attendants behind the counters. Local trainer and peer standing two tiles apart, `GUYA` nameplate over the peer. The direct A/B against the phase-18 placeholder picture — same map, same `+2,+0` delta. |
| 2 | **Pokémon Center 1F** (the lobby) | `10-5` | `loc-pokecenter-1f.{top,bottom}.png` + `-native` + `-zoom` | The nurse behind the counter, the healing machine with its Poké Balls, the PC, a potted plant, two NPCs. Local trainer by the stairs facing east; peer two right and one down, mid-lobby, facing south. |
| 3 | **Mauville City, outdoors, daylight** | `0-2` | `loc-mauville-outdoors.{top,bottom}.png` + `-native` + `-zoom` | In front of the Pokémon Center — red roof, Poké Ball crest, the `P.C` sign, the bicycle rack, grass and paving. Both trainers side by side on the path. A completely different palette and light level from #1/#2, and the sprite reads correctly in it. |

Each was read at capture time over gdb; the readouts are in `loc-*.txt`. All three report
`reason=0(ok) drawn=1` on **both** games and `SPR reason=0(ok) 16x32` on both, so in every one of
them the peer avatar on screen is the live cell and not the fallback.

**The `-zoom` images are the ones to look at.** In `loc-mauville-outdoors-zoom.png` (8×) the local
player and the peer stand side by side in daylight: the same 16×32 Gen-3 trainer art, the same
palette, the peer in the east-facing side profile and the local player facing front. That pairing is
also what settles the "is the peer darker?" question — it is the pose, not a tint.

One extra thing fell out of the walk: when seat A got outside and seat B was still in the lobby, the
readout went `reason=9(far) drawn=0` with `selfMap=0-2 peerMap=10-5`. Different maps, no avatar
drawn — the cull working, unprompted.

---

## G4 — the fallback

Not simulated — **observed**, three times, without being asked for. The best instance is
`evidence/g4-fallback-live.png` with `g4-fallback-live.txt`, because there the presence gate is
*provably open* and the refusal is a real resolve-ladder clause:

```
game0: reason=0(ok) drawn=1 ...          <- presence IS drawing this peer
       SPR reason=9(notinuse) ...        <- but gSprites[id].inUse == 0
```

**What I see in that frame:** the peer's game is sitting on the Emerald main menu (`CONTINUE /
PLAYER GUYA / TIME 75:10 / POKéDEX / BADGES`) — the field is gone, so the player's sprite slot is
not in use. The app draws the **phase-15 magenta placeholder** with the `GUYA` nameplate over it,
cleanly composited on top of the menu. The HUD says `spr: NOTINUSE g59`. No torn cell, no garbage
pixels, no stale half-sprite, no crash; the app keeps rendering (49 fps in that frame).

`g4-placeholder-drawn.png` / `-zoom.png` is the same behaviour caught earlier at `spr: NOOBJ g00`,
while the GBA was still black-booting — the zoom shows the placeholder on its own, so you can see
exactly what "degraded" looks like.

This is the interesting half of the design working: the live path is *additive*. When it cannot
answer, the thing phase 15 shipped draws instead, and the diag names which of the 15 clauses refused.

---

## Q1 and Q2 — the two questions P1–P4 could not close on a PC

Both were answered by the first live session, from one `g_presDiag` read:

* **Q2 — `sprTileOk = 1`.** The §S3.7 init proof passed **on the device**: our CPU Morton
  tiled-texture encoder is byte-identical to what `C3D_SyncDisplayTransfer` produces, including
  block-row order, byte order and the `GX_TRANSFER_FLIP_VERT(0)` row convention. This was the one
  genuine risk in the upload path, and it is now measured rather than argued.
* **Q1 — `sprFlags = 0x00`,** i.e. no `PSPR_D_NOIO` bit. `gbacore_read16(c, 0x04000000)` really does
  return DISPCNT under this mGBA build, so the video mode and the 1D/2D OBJ mapping bit are **read**.
  The pret-cited default was never needed. (`map1d=1` in the readout is that bit, read live.)

---

## What this run does NOT prove

| # | claim | why it is still open |
|---|---|---|
| Q3 | Ruby / Sapphire | no RS ROM exists on this machine; the profile columns remain VERIFIED-SYM only |
| Q5 | 32×32 forms (bike / surf) | `gfxId` stayed `0x59` all session — the fixture save never mounted a bike, so the larger cell was never exercised |
| Q6 | the 1-texel seam at sheet row 96 | needs a tilt-on capture at the sprite's top edge; tilt was 0 for these captures |
| — | **the frame budget with the live path on** | Azahar cannot model core-2 contention or the 804 MHz budget (CLAUDE.md #6). The session ran at 17–20 fps here, which is Azahar's own ceiling for two GBA cores and says nothing about hardware. **This remains a real-New-3DS sign-off item.** |

---

## Harness findings this slice paid for (details in `BUILDLOG.md`)

1. **A CTM movie that runs out stops Azahar's emulation and wedges the gdb stub.** It looks exactly
   like an app hang: identical captures, frozen fps counter, every gdb read timing out. Any run that
   must outlive its own scripted input needs the movie's tail padded past the whole session
   (`movie_pick_long.ctm`: 150 000 idle frames).
2. **Never `pkill` the gdb broker** — it spends the boot's one gdb client and the stub does not
   re-listen.
3. **`place.py` cannot move the player at all** — see below. Positions must be reached by **real
   walking** (the D4 channel), which moves player and camera together.

---

## `place.py` cannot move the player — why the §S6 shortlist was not used

This is the finding that reshaped the sweep, and it invalidates an assumption `SPEC.md` §S6.1 states
as settled ("**this is the whole sweep**: two `place.py` calls, one boot, two screenshots").

`SaveBlock1.pos` is the **camera** tile — `presence.h:150` says exactly that. Patching it in the save
does not move the player, and the evidence is unambiguous:

* **Same-map patch.** Both seats warped to `10-6`, seat B's x moved `9 → 11`. The app's own `s_gsLog`
  ring, read live over gdb, gave `pos=(11,5) obj=(16,12)`. `objY - py = +7` = `MAP_OFFSET`, correct;
  `objX - px = +5`, short by exactly the +2 that was patched. The live player never left x = 9. It is
  visible in the frame too — seat B's trainer sits left of centre while seat A's is centred, because
  seat B's camera is two tiles off its player. Walking does not heal it: an `R2` moved `pos.x`
  11 → 13, carrying the same +2 error, because the field engine *increments* `pos` rather than
  re-reading it from the object event.
* **Different-map patch.** Both seats patched to Mauville City `0-2 (8,6)`. The HUD duly reported
  `me 0-2@8,6` — **and the screen still showed the Pokémon Center interior**
  (`loc-mauville-city.top.png`). The game had not warped anywhere. `place.py` had rewritten precisely
  the fields presence reads, and nothing else, so the readout was lying and presence — correctly —
  refused to draw (`reason=12(selfobj)`, `drawn=0`).

So `place.py` does not teleport; it *desynchronises the telemetry from the world*. Presence's
object-agreement gate is what catches it, which is the gate doing its job.

**This retro-explains phase 18's run 4**, where "steady state settles on `reason 8 (obj)` /
`12 (selfobj)`" was written down as an unexplained oddity, and it means phase 18's door slice
("run 6 removed the walk: `place.py` moved the fixture save to …") deserves a re-check on the same
suspicion.

**Consequence for anyone continuing the sweep:** the remaining §S6 locations (Mauville Mart, Route
117, Rusturf Tunnel, Granite Cave) need either a real walked route, or a fixture tool that moves the
player rather than the camera. The three locations above were all reached by walking, which is why
they are trustworthy.
