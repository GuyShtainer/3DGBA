# Gen-3 Pokemon Saves: Format, Genuine-Trade Mechanics, and Licensing

A reference for reading, validating, bit-exactly rewriting, and legally shipping
Gen-3 Pokemon save (`.sav`) code. All offsets/constants below are Gen-3-specific
(Ruby/Sapphire/Emerald/FireRed/LeafGreen) unless tagged otherwise. DS Gen-4/5 and
3DS Gen-6/7 reuse the *concepts* (per-mon encryption, shuffled substructs, section
checksums) but use **different layouts, sizes, and key derivations** — never reuse
a Gen-3 number on a later gen.

- [Container layout](#container-layout-gba-specific)
- [Section checksum](#section-checksum-fold-add-u32--u16)
- [Slot selection](#slot-selection)
- [4-game detection](#4-game-detection-rs--emerald--frlg)
- [Per-mon encryption kernel](#per-mon-encryption-kernel)
- [IVs / EVs / nature / stats](#ivs--evs--nature--stats)
- [What makes a trade "genuine"](#what-makes-a-trade-genuine)
- [The trades counter and security key](#the-trades-counter-and-security-key)
- [Zero-padding checksum finding](#zero-padding-checksum-finding-load-bearing)
- [EWRAM-frugal box reader](#ewram-frugal-box-reader)
- [Implementation gotchas](#implementation-gotchas)
- [Licensing posture](#licensing-posture)
- [Gen-3 vs DS/3DS](#what-carries-to-ds-gen-45--3ds-vs-is-gen-3-specific)

## Container layout (GBA-specific)

A full `.sav` is `0x20000` (128 KiB) = **2 save slots × 14 sectors × 4096 bytes**;
slot size `0xE000`. Framing is byte-identical across Ruby/Sapphire/Emerald/FRLG —
only the *contents* of SaveBlock1/2 differ.

Each 4096-byte sector = **3968 data bytes + 116 unused + 12-byte footer**:

| Footer offset | Size | Field |
|---|---|---|
| `0xFF4` | u16 | logical section id |
| `0xFF6` | u16 | section checksum |
| `0xFF8` | u32 | signature, must equal `0x08012025` |
| `0xFFC` | u32 | per-slot save counter |

Logical section ids (the 14 sectors of a slot are **rotated on every save**, so
physical index does NOT equal logical id):

- `0` = **SaveBlock2** (trainer info)
- `1..4` = **SaveBlock1** (party, items, secret bases, game stats, Pokedex copies)
- `5..13` = **PokemonStorage** (the PC boxes)

The rest of the 128 KiB image (Hall of Fame `0x1C000`, Mystery Gift `0x1E000`,
Recorded Battle `0x1F000`) lies outside the two save blocks.

**Scan by section id, never by sector position.** Iterate all 14 sectors, skip any
whose signature at `0xFF8 != 0x08012025`, and match the id at `0xFF4`.

SaveBlock2 fields (identical across RSE):

| SB2 offset | Field |
|---|---|
| `0x00` | player name, 7 chars + `0xFF` terminator |
| `0x08` | gender (0 male / 1 female) |
| `0x0A` | trainer id (TID lo/hi, SID lo/hi) |
| `0x0E` / `0x10` / `0x11` | playtime hours u16 / minutes u8 / seconds u8 |
| `0x18` | Pokedex structure |
| `0xAC` | u32 **security/encryption key**, Emerald only (RS: nothing here) |

Reassemble SaveBlock1 by concatenating the 3968 data bytes of ids `1,2,3,4`
**in id order** into one contiguous `4 × 3968 = 15872`-byte buffer. An absolute
offset `O` inside reassembled SaveBlock1 maps to section id `1 + O/3968`, byte
`O%3968`.

## Section checksum (fold-add u32 → u16)

Zero a u32 accumulator, add `size/4` little-endian u32 words, then fold to u16:

```c
uint16_t gen3_checksum(const void* data, uint16_t size) {
  uint32_t checksum = 0;
  uint16_t words = size / 4;
  for (uint16_t i = 0; i < words; i++) { checksum += rd32(p); p += 4; }
  return (uint16_t)((checksum >> 16) + checksum);
}
```

Full (3968-byte) sections checksum over all 3968 data bytes. Partial sections
(ids `0`, `4`, `13`) check over a smaller, version-specific size — but see the
[zero-padding finding](#zero-padding-checksum-finding-load-bearing): for editing
real saves you can checksum the full 3968 and get a bit-correct result, dodging the
per-game size table entirely (guard it first).

## Slot selection

Current slot = the one with the **higher save counter**. Read the u32 counter at
`0xFFC` from the **first signature-valid sector** of each slot (not a fixed
sector). Pick slot 1 iff `counter[1] > counter[0]`, else slot 0; fall back to
whichever slot is valid.

## 4-game detection (RS / Emerald / FRLG)

The per-game SaveBlock2 size (RS `0x890`, E `0xF24`, FRLG `0xF2C`) **cannot**
reliably separate Emerald from FRLG via the section-0 checksum: their sizes are
near-equal and the boundary bytes are zero, so FRLG validates at Emerald's size
too. Use structural probes instead:

- **RSE vs FRLG, and RS vs Emerald:** only RSE has **secret bases**. Detect by
  which candidate offset holds a valid (all-sane, ≥1 used) secret-base array:
  `bases@Emerald(0x1A9C) > 0` → Emerald; `bases@RS(0x1A08) > 0` → Ruby/Sapphire;
  else FRLG (or no bases). On a tie (often both 0), fall back to Emerald iff the
  `0xAC` security key is nonzero.
- **FRLG vs RSE** also distinguishes by **party offset** (FRLG party count/data is
  at SB1 `0x034`/`0x038`; RSE is at `0x234`/`0x238`).
- **Ruby vs Sapphire is NOT distinguishable from save data** (identical format).
  Refine from the filename / game code (RUBY/AXVE → Ruby, SAPP/AXPE → Sapphire).

## Per-mon encryption kernel

A `struct Pokemon` is **80 bytes boxed (PC)** and **100 bytes in the party** (the
party form appends 20 runtime bytes: status, level, current/max HP, the five battle
stats). The decrypt/checksum/substruct logic is identical for both forms.

Live party lives in reassembled SaveBlock1: count u8 `@0x234`, then 6 × 100-byte
mons `@0x238` (RSE; FRLG `@0x034`/`@0x038`).

Per-mon layout:

| Offset | Field |
|---|---|
| `0x00` | personality (u32) |
| `0x04` | otId (u32) |
| `0x13` | flags byte: bit0 = bad-egg, bit2 = is-egg (these live HERE, not in the substructs) |
| `0x1C` | checksum (u16) |
| `0x20` | secure block, 48 bytes = 12 u32 words |
| `0x54` | level (u8, party form only) |
| `0x58..0x62` | plaintext battle stats (maxHP/atk/def/spe/spA/spD), party form only |

Decrypt + validate, per mon:

1. Read flags at `mon+0x13` (bad-egg / egg).
2. **Key = `rd32(mon+0x00) ^ rd32(mon+0x04)`** (personality ^ otId). This is the
   box-mon XOR and is **separate** from the Emerald SaveBlock2 security key at
   `0xAC` (which obfuscates money/items/the trades counter, not the mon).
3. **Decrypt the 48-byte secure block at `mon+0x20`**: XOR each of the 12 u32 words
   with the key.
4. **Validate:** sum the 24 decrypted u16 halfwords (mod 2^16), compare to the
   stored u16 at `mon+0x1C`. Mismatch → treat as corrupt and skip.
5. **Substruct order = `personality % 24`.** A 24-row permutation table gives the
   slot (0..3) of each 12-byte substruct (Growth, Attacks, EVs, Misc) within the
   48-byte block. A wrong key or wrong row decrypts to plausible bytes that fail
   the `0x1C` checksum.
6. **Skip rule:** bad-egg, egg, `species==0`, or checksum mismatch → skip.

Fields by substruct:

- **Growth:** species `@+0`, heldItem `@+2`.
- **Attacks:** moves[0..3] `@+0,+2,+4,+6`.
- **EVs:** 6 EV bytes `@+0..+5`.
- **Misc:** holds IVs / ability / origin (see next section).

To **write**, reverse it: edit the decrypted substruct, recompute the `0x1C`
checksum (sum of the 24 decrypted halfwords), re-encrypt by XORing each word with
the same key, store back. Then recompute the enclosing **section** checksum.

## IVs / EVs / nature / stats

- **IVs** — Misc substruct's IV/Egg/Ability word (u32 at Misc `+0x04`):
  bits 0-4 HP, 5-9 Atk, 10-14 Def, 15-19 Spe, 20-24 SpA, 25-29 SpD,
  bit30 isEgg, bit31 abilityNum.
- **EVs** — all six bytes in the EVs substruct at `+0..+5` (keep the full spread;
  don't collapse to an average if you need stats).
- **Nature = `personality % 25`** (distinct from the `% 24` used for substruct
  order — compute both separately).
- **Computed stats** — apply the Gen-3 stat formula using species base stats (need
  a base-stat table), level, IVs, EVs, and nature. Party-form mons also store the
  pre-computed plaintext stats at `0x58..0x62` and level at `0x54`.

### Everything is derived from the PID — to change one trait, *search* for a PID

A single 32-bit personality value (PID) simultaneously fixes **nature** (`pid % 25`),
**ability slot** (`pid & 1`), **gender** (`(pid & 0xFF)` vs the species' gender ratio),
**shininess** (`(tid ^ sid ^ (pid>>16) ^ (pid & 0xFFFF)) < 8`), **substruct order**
(`pid % 24`), and **Unown letter**. So you **cannot bit-twiddle the PID to set one trait** —
changing any bits shifts the others (and the substruct order) unpredictably. To set a desired
trait while keeping the rest, **brute-force a PID** that satisfies all the constraints at once
(a short capped loop is plenty: nature is 1/25, a forced Unown letter 1/28, etc.; for *shiny*,
construct the PID by forcing the high half so `tid^sid^lo^hi < 8` instead of searching). This is
safe because the lossless edit core re-encrypts on commit using the new PID (it re-derives
substruct order + the `pid^otId` XOR key from canonical decrypted substructs).

**Unown letter** = `formId % 28` (0..27 = A..Z, !, ?), where
`formId = ((pid&0x03000000)>>18) | ((pid&0x00030000)>>12) | ((pid&0x00000300)>>6) | (pid&3)`
(2 bits from each PID byte). Displaying the real letter needs **28 form-specific sprites**, not
the one species sprite — sprite packs ship them as `UNOWN.png` (=A) + `UNOWN_1..27.png`; index a
form table and fall back to form 0 for any missing one.

## What makes a trade "genuine"

To replicate a real link trade on a receiver save, apply exactly the in-game
side-effects:

- **Preserve the original OT** (personality + otId untouched). Because the receiver
  was not the OT, the game treats the mon as an **outsider**, which grants the
  **1.5× EXP boost and the obedience/level cap automatically — no save flag
  needed.** Changing OT to "fix" anything destroys both.
- **Reset friendship** to the species' base friendship on trade (commonly 70; some
  species differ — use a base-friendship table). Stored in the Growth substruct.
- **Trade evolutions** — apply species change for the plain trade-evo species and
  the held-item trade-evos (and consume the held item). Write evo species in
  **national dex**, then convert to the internal index (handle Clamperl's Hoenn
  reorder). Re-encrypt and re-checksum the mon afterward.
- **Pokedex: owned + seen.** Set the receiver's Pokedex `owned/caught` bit AND all
  **three SEEN copies** (Gen-3 anti-cheat keeps the seen flag in three places that
  must agree). Pokedex is in SaveBlock2 (section id 0) `@0x18`; in all four games:
  `nationalMagic @ SB2 0x1A` (== `0xDA` enables non-Hoenn national flags),
  **owned @ SB2 0x28** (single copy), **seen copy 0 @ SB2 0x5C**. Bit index =
  `national - 1` → byte `(n-1)/8`, bit `(n-1)%8`; convert internal→national first.
  The other two seen copies live in SaveBlock1 at per-game offsets:

  | Game | seen-copy1 (SB1) | seen-copy2 (SB1) |
  |---|---|---|
  | Ruby/Sapphire | `0x938` | `0x3A8C` |
  | Emerald       | `0x988` | `0x3B24` |
  | FireRed/Leaf  | `0x5F8` | `0x3A18` |

- **Increment the trades game-stat counter** (see next section).

A trade touches receiver **sections 0 (SB2 Pokedex), 1 (party + seen1),
2 (game stats), and 4 (seen2)** → recompute each affected section checksum with the
bit-exact write path, then verified-write the whole file.

**FRLG are valid trade partners** (they have no secret bases, so they're excluded
from *mixing*, but a trade flow must accept them) — needs a 4-game-aware picker.

## The trades counter and security key

`GAME_STAT_POKEMON_TRADES = index 21` in the game-stats array — **same index** in
R/S, Emerald, AND FRLG. The game-stats array lives in SaveBlock1 at per-game
offsets:

| Game | gameStats (SB1 offset) |
|---|---|
| Ruby/Sapphire | `0x1540` |
| Emerald       | `0x159C` |
| FireRed/Leaf  | `0x1200` |

On **Emerald and FRLG**, every game-stat is stored **XOR'd with the SaveBlock2
security key** (`SB2 @0xAC`): read = `stored ^ key`, write = `value ^ key`. On
**Ruby/Sapphire the stats are plaintext** — no XOR. So to increment trades: read
through the key (E/FRLG) or directly (R/S), add 1, write back through the same key.

The same Emerald security key obfuscates **money** (Section 1 `@0x0490`, XOR full
32-bit key), **coins** (`@0x0494`, XOR low 16 bits), and item quantities (XOR low
16 bits). R/S store all of these in **plaintext** — skip the XOR there. (Confirm
exact in-section money/item offsets against a primary spec before writing.)

## Zero-padding checksum finding (load-bearing)

**Section padding past the real data is ZERO on real saves.** Because adding zero
words doesn't change a fold-add checksum, you can recompute a partial section's
checksum over the **full 3968 data bytes** and get a **bit-correct** result —
**no per-game section-size table required.**

**Guard it first:** verify the assumption holds for the loaded save (e.g. a
`trade_sections_safe()` / equivalent check that the regions you're about to edit
are well-formed) and abort if not. This finding was confirmed via a full
cross-game Emerald↔Ruby swap that left both saves fully validating.

## EWRAM-frugal box reader

A full PokemonStorage reassembly (ids 5..13) is ~36 KiB — too much to hold on a
memory-tight handheld alongside the 128 KiB save image. Instead, read **one box at
a time** with a **straddle-aware per-box reader**: a box's 80-byte mon records can
cross a 3968-byte section boundary, so the reader stitches across the boundary and
materializes only the requested range (≈2.4 KiB), never the whole storage. Decode
each 80-byte boxed mon with the same decrypt/checksum/`personality%24` kernel as
the party. (General homebrew principle: stream sections; never reassemble the whole
storage region just to read a slice.)

## Editable non-mon save areas (offsets that take hours to re-find)

These live **outside** the per-mon kernel and are worth recording because the decomp scatters
them. All edits still go through the **verified-write** path (re-checksum the touched sections,
backup, `.tmp`→compare→rename) — and most are **plaintext**, so editing is just a byte poke.

- **Trainer identity (SaveBlock2 / section 0, plaintext, same across R/S/E):** player name 7 chars
  + `0xFF` terminator @ `0x00`; gender @ `0x08`; **TID @ `0x0A`, SID @ `0x0C`** (two u16); playtime
  hours u16 @ `0x0E`, minutes @ `0x10`, seconds @ `0x11`. Encode the name with the same Gen-3 char
  table as nicknames.
- **PC storage layout (sections 5..13 reassembled):** `0x0000` currentBox; `0x0004`
  BoxPokemon[14][30] (80 B each); **box names `0x8344` (9 B each: ≤8 chars + `0xFF`)**;
  **box wallpapers `0x83C2` (1 byte each)**.
- **Event flags = a plaintext bit array in SaveBlock1**, per-game base: E `0x1270`, R/S `0x1220`,
  FR/LG `0x0EE0`; `get/set = sb1[base + (n>>3)] >> (n&7) & 1`. Flag *numbers* differ per game and
  are exprs like `(SYSTEM_FLAGS + 0x7)`; the base chains through trainer-count constants in other
  files, so **pre-seed the canonical bases** (`SYSTEM_FLAGS` = E `0x860`, FR/LG `0x800`, R/S `0x800`)
  and self-check a known result. **Badge 1 = base+0x7** ⇒ E `0x867`, FR/LG `0x820`, R/S `0x807`
  (badges 1-8 are consecutive). **Emerald Battle-Frontier silver/gold symbols** are flags too,
  `SYSTEM_FLAGS + 0x64..0x71` (Tower S/G, Dome S/G, … Pyramid S/G).
- **Money / game-record counters are XOR-encrypted** in E/FR/LG with the SaveBlock2 **security key**
  (E key @ SB2 `0xAC`, FR/LG `0xF20`; **R/S store them plaintext** — key 0). Money in SB1 (E/RS
  `0x0490`, FRLG `0x0290`); stats array XOR'd the same way.
- **Emerald "Walda"/secret box wallpapers.** A box whose wallpaper byte = **16** (`WALLPAPER_FRIENDS`)
  doesn't store its own graphic — it shows `sWaldaWallpapers[patternId]` from the **`WaldaPhrase`
  struct in SaveBlock1 @ `0x3D70`** (EMERALD ONLY; the offset differs on R/S/FR/LG): `colors[2]` u16
  @ +0, `text[16]` @ +4, `iconId` @ +20, **`patternId` @ +21, `patternUnlocked` @ +22**. To set a
  secret wallpaper from a tool: write the box byte = 16 **and** set `patternId` + `patternUnlocked`
  (it's the same single slot game-wide, so all "Friends" boxes share it — matching the cartridge).
- **Reconstructing the Gen-3 PC box wallpaper graphics** (a self-contained tile job): each is
  `tiles.4bpp` (= `frame.4bpp` ++ `bg.4bpp`, concatenated) + a 20×18 `tilemap.bin` + 2 palettes
  (the frame.png + bg.png indexed palettes). The game's `DrawWallpaper` adds **paletteNum 3** to
  each tilemap entry and loads the two palettes at BG banks 4/5 ⇒ **tilemap bank 1 → frame palette,
  bank 2 → bg palette** (bank 0 → a shared UI palette). Composite = the bg-pattern tiled as a base
  (its transparent index-0 → the interior tone `bg_pal[1]`, else sparse wallpapers show white gaps),
  then the tilemap overlaid with color-index-0 transparent. The 16 Walda wallpapers are **frame-less**
  and share one of two `friends_frame{1,2}` tilesets. **Render and eyeball a contact sheet** before
  shipping — the palette-bank mapping is the part that's easy to get subtly wrong.

## Implementation gotchas

- **All multi-byte access is explicit little-endian** via byte readers
  (`rd16`/`rd32`/`wr16`). Never cast to `u16*`/`u32*` at odd offsets — ARM faults
  on unaligned access. (Packed-struct member access is fine.) Bonus: byte readers
  let the pure-C core dual-compile and run on a PC host for tests.
- **Don't C-bitfield format flag bytes.** Bitfield bit-order is
  implementation-defined; store a raw `uint8_t` and decode with masks (LSB-first on
  little-endian ARM). The secret-base flags byte `@0x01`, LSB-first:
  `toRegister:4, gender:1, battledOwnerToday:1, registryStatus:2`.
- **Secret-base array** = 20 × 160 bytes, **Emerald `@0x1A9C`, RS `@0x1A08`**, both
  spanning SaveBlock1 sections **2 & 3** (so only those two sectors get rewritten).
  `SecretBase` is byte-identical RS vs Emerald except byte `0x0D` (`language` in E,
  padding in RS).
- **The stale base-party bug:** a base's shown party is written ONLY at link-mix
  start, never on a normal save — a normally-saved file carries a **stale** team.
  If you mix, regenerate the base party from the LIVE party first.
- **Verified-write discipline** (general homebrew, critical on flashcarts with no
  write-retry): always back up the original immutably, write to a temp, re-read and
  byte-compare, then atomically replace. Snapshot BOTH saves before writing either
  in a bidirectional operation; validate both spliced images before committing.

## Licensing posture

Save-format **facts** (byte offsets, the `personality%24` permutation table, the
`personality^otId` XOR formula, the fold-add section checksum, the `0x08012025`
signature) are **not copyrightable** — you may learn them from any source and
re-express them in your own clean code. What you may **not** copy is *expression*:
variable names, control flow, comment structure.

| Project | License | Verdict |
|---|---|---|
| **savaughn/pksav** | **MIT**, pure C, no malloc/no file I/O in its core | **SHIP** — the safe shippable basis; vendor `lib/gba/` + headers. Caveat: **US-region saves only** (EU/JP need your own work) |
| ncorgan/pksav | MIT, archived | reference only — prefer the maintained savaughn fork |
| **PKHeX (kwsch)** | **GPLv3**, C#/.NET | **SPEC ONLY** — authoritative, but porting C#→C makes a GPLv3 derivative that relicenses your whole tool; also can't run on a handheld |
| **pret decomps** (pokeemerald/pokeruby/pokefirered) | **UNLICENSED**, C | **REFERENCE ONLY** — byte-exact ground truth; read → confirm → re-express. Never ship verbatim. Pin citations to a **commit hash**, never `/master/` |
| libspec (Chase-san) | MIT, C11 | reference only — self-declared **unfinished**, not a stable dependency |
| ScoreUnder/pksv | GPL-3.0 | **AVOID** — a ROM *script* editor (wrong tool) + copyleft; name clashes with `pksav` |
| ads04r/Gen3Save | Unlicense, Python | reference cross-check only — not shippable C |
| Bulbapedia Gen-III pages | (facts) | **PRIMARY SPEC** — pair with pret for byte-exactness |

**Clean-room rule:** every shipped byte of save-format logic is independently
authored from non-GPL, non-unlicensed specs (Bulbapedia's three Gen-III pages +
MIT `pksav`). Keep a clear wall between "I read their code to learn the format" and
"I wrote this implementation." Where sources disagree, Bulbapedia is the
human-readable spec and pret is byte-exact ground truth — but the **shipped
expression** should match `pksav` or your own.

**Pick each new tool's license early.** If a single GPLv3-derived line lands, the
whole tool becomes GPLv3. MIT + Apache-2.0 is the natural target for a tool linking
permissive flashcart/FS libraries; do not adopt GPL unless you deliberately pull in
GPL code.

(Note: a common research finding mislabels PKHeX as "MIT" — it is GPL-3.0-or-later
per its repo `LICENSE` and the `PKHeX.Core` NuGet package. Verify before relying on
any second-hand license claim.)

## What carries to DS Gen-4/5 vs is Gen-3-specific

- **Carries (concepts) to DS Gen-4/5 and 3DS Gen-6/7, details differ:** per-mon
  encryption with a shuffled set of substructs keyed off the personality value; a
  per-mon checksum; section/block checksums; "outsider OT ⇒ trade bonus + obedience"
  trade semantics; reset-friendship-on-trade; the "Pokedex seen/owned must be set"
  trade effect. The *idea* of streaming sections instead of reassembling the whole
  storage is general homebrew, not gen-bound.
- **Gen-3-specific (do NOT reuse these numbers elsewhere):** 128 KiB / 2-slot /
  14-sector framing; the `0x08012025` signature; section ids 0/1-4/5-13; the
  3968+footer sector; the fold-add u32→u16 section checksum; `key = personality ^
  otId`; substruct order `= personality % 24`; nature `= personality % 25`; the
  `0x1C` 24-halfword mon checksum; the Emerald `@0xAC` security-key XOR over
  money/items/game-stats; `GAME_STAT_POKEMON_TRADES = 21`; all the per-game offsets
  above. DS Gen-4/5 uses a different save size, block layout, encryption seeding,
  and substruct count — re-derive everything from a Gen-4/5-specific spec. (Exact
  DS/3DS layouts are out of scope here; do not fabricate them.)
