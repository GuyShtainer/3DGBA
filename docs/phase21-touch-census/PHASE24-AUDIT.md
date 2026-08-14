# PHASE 24 — ADVERSARIAL AUDIT

_Auditor pass, 2026-08-14, on `main` at **`2f04db9`** (working tree clean apart from
`tools/emutest/__pycache__`). Both lanes had finished and **no Azahar was running** (`ps aux` →
none), so the emulator was left alone; everything below was decided by re-running host code,
re-reading the shipped binaries in `roms/`, re-parsing the fixture saves, and diffing the commits._

The brief was to disbelieve. What follows is what survived and what did not.

---

## 0. What I actually ran (nothing here is quoted from the lanes)

| # | Check | Result |
|---|---|---|
| 1 | All **16** host suites rebuilt and re-run from the project root at `2f04db9` | 0 failures; counts below |
| 2 | Same two suites re-run from an **archive of `7ad4504`** (lane B1's own merge commit) | touchgeom **158971**, profiles **1660** — lane B1's close-out numbers reproduce |
| 3 | Three suites re-run from an **archive of `df06524`** (lane A1's close-out commit) | touchgeom **362691**, profiles **1697**, fieldtrav **1099** — lane A1's numbers reproduce |
| 4 | `tools/emutest/tests/run_host_tests.sh` | `Ran 169 tests … OK` |
| 5 | **Mutation test**: re-introduced the `ft_rom_ptr(m->mapHeader)` guard that `e11b620` removed | fieldtrav → **exactly 4 failures** in TEST 16, precisely as the commit claims |
| 6 | Literal-pool scan of the **actual ROMs in `roms/`** for every new address | see §2 |
| 7 | Gen-3 save parse of all three banked fixtures (slot/counter/pos/map/badge flags) | see §3 |
| 8 | `git ls-files` + `git log --all --stat` for `roms/` / `*.gba` / `*.sav` | **nothing tracked, nothing ever committed** |
| 9 | `git diff` of `source/fieldpath.{c,h}` over the whole phase, on `main` **and** both lane branches | **empty** — last touched at `010a138` (phase 18) |
| 10 | Byte-hash of every file in `evidence/impl/` | two duplicate pairs — see §4 |
| 11 | `FLAG_SYS_B_DASH` / `IsRunningDisallowed` / `sLockFieldControls` re-derived from the local pret clones | see §2, §5 |

Suite counts at `2f04db9` (all 0 failures): celiolink 1259 · control 6940 · diag 376 ·
**fieldpath 1808** · fieldtrav 1102 · netlink 66 · peersprite 62078 · presence 61376 ·
profiles 1697 · theme 83444 · tilt 1756 · touchgeom 371892 · trace_replay 58 (+4 loud SKIPs) ·
typography 1419 · uigeom 18332 · uihit 1834.

> One methodology note the lanes should carry forward: **`test_fieldtrav` silently drops
> TEST 15/16 (34 checks) when it cannot open `roms/emerald.gba` from the CWD** — 1102 from the
> project root, 1068 from anywhere else. It prints a loud SKIP line, so it is honest, but a
> "1099" quoted without the CWD is not reproducible. That is what made me briefly think lane A1's
> number was wrong; it is not.

---

## 1. Verdicts

### CONFIRMED

| Claim | What decided it |
|---|---|
| **P1 SURF PROVEN** | The proof is a read of the game's own `gPlayerAvatar` surf bit (0→1) plus a final position on an elevation-1 ocean tile. Neither is fakeable from a screenshot, and `EM-P24-P1c` shows the avatar on a surf mount over water. |
| **P2 CUT PROVEN** | The verdict rests on ending at (11,2). Its "the dry walk could not have got there" premise is corroborated **without** `gate.py` (which was scratchpad-only and is gone): `touch.c` makes the tier order structural — `prog_plan` is only consulted *after* `walk_plan` has already failed, so `hm=CUT` appearing in the trace at all is itself proof the dry route did not exist. |
| **P3 ROCK SMASH PROVEN** | Same tier-order argument, plus `progEdges` 2→1 = the rock's own object slot going inactive. |
| **P4 EXCURSION PROVEN** | Same argument at the third tier (`exc_plan` only runs after both lower tiers fail), plus the per-leg map/pos trace and `EM-P24-P4d` showing the avatar in the water beside the bathers. |
| **The six lane-A defects are real defects** | `e11b620` verified by mutation (check 5). The other five are single-cause `touch.c` fixes whose diagnoses match pret source I re-read. |
| **`fieldpath.c/.h` frozen** | Byte-identical across the phase on `main` and both lane branches; 1808 checks unchanged. |
| **No ROMs/saves committed** | Nothing in `git ls-files`, nothing anywhere in history. |
| **The fixture saves were NOT manipulated into the success condition** | Parsed directly: `emerald-shore.sav` (47,58) map (26,14); `emerald-r117.sav` (15,3) map (0,32) = Route 117; `emerald-lavaridge.sav` (9,7) map (0,12) = Lavaridge Town. All are *start positions*, none is a goal state. (8 badges + `FLAG_SYS_B_DASH` set on all three — legitimate preconditions, and the same on the user's untouched `emerald.sav`.) |
| **Every new address is right for the ROM actually in `roms/`** | §2. |
| **No LeafGreen-carries-FireRed's-pointers bug this phase** | BPGE's phase-24 ROM columns are **explicit zeros** (FAM-MAP, `cb2List`, pager); the only BPGE value added is the IWRAM `fieldLock`, and that one is corroborated *in `leafgreen.gba` itself*. Lane B2's "no new addresses" also holds — the `0x081248D4/0x08170274` line in `d262062` is a context reflow, and the two `cb2List` values are already in that row's `cb2FullUi`. |
| **`test_touchgeom` TEST 17's oracle is genuinely independent** | It is a separately-shaped predicate, not a copy of `rungeom_tile_ok`, and I re-derived both engines from pret: RSE `MetatileBehavior_IsRunningDisallowed` = {0x0A, 0x03, 0x28} ∪ `IsPacifidlogLog` plus the Fortree-bridge/even-elevation clause; FRLG's `IsFortreeBridge` and `IsPacifidlogLog` are literal `return FALSE;` stubs. **RSE 7 / FRLG 1 is correct.** |
| **Lane B1's B3 fix and its signal** | `sLockFieldControls` really is the level signal (`LockPlayerFieldControls` is called by egg hatch, cable car, and every field script); `dlggeom_route` is correctly fenced to `ctx == GCTX_OVERWORLD` so it cannot steal a frame from a classified screen. |
| **All quoted host gates were really run at the tree they were quoted at** | Checks 2 and 3 reproduce lane B1's and lane A1's close-out numbers exactly. |

### OVERSTATED

**O1 — "smartTraverse = 2 … is NOT the shipped default" (lane A1 caveat 2).**
True but it reads as "1 is". The shipped default is **0 = Off** (`theme.c:35`, `g_prefs = { …, /* smartTraverse */ 0 }`; `settings_load` leaves older files at 0 by design). **None of P1–P4 does anything on a default install** — not the excursion, not Surf, not Cut, not Rock Smash. The emulator's `traverse = 1` that lane A1 "restored to the value found" was a local leftover, not the ship state. The four PROVEN verdicts are proofs of an **opt-in** feature at a **non-default** setting.

**O2 — the close-out host gate is quoted for work it does not cover.**
"Six defects were found and fixed … Host gate at close: 16 suites, 0 failures" invites the reading that the gate protects the fixes. It does not. **No host suite compiles `source/touch.c`** (I checked every suite's link line; the only `source/touch.c` strings in `test/` are filename *literals* inside `test_typography`). Of the seven lane-A fixes, exactly one — `e11b620`, in `fieldtrav.c` — has a regression barrier. `2dde03e` (the level-triggered YES), `b0ae8d9` (the DLG cadence), `70f963d` (closed-loop FACE), `e1c6041`, `a1cbdca`, `2fa4976` (all three excursion leg-boundary fixes) are **emulator-only, with nothing that would catch a regression**. The gate is green and honest; it is being cited in the wrong place.

**O3 — "FtEngCfg.runShoes is pinned per engine … EM 0x8C0, FR 0x82F, and the two DIFFER".**
This is the "golden pins the implementation's shape, not the truth" pattern. The check pins a **two**-engine table; Gen 3 has **three** flag numberings. `fp_engine()` sends AXVE/AXPE to `FP_ENG_RSE`, so **Ruby and Sapphire inherit Emerald's numbers**: `runShoes 0x8C0` and `flagsOff 0x1270`, where pokeruby has `SYSTEM_FLAGS 0x800` (⇒ `FLAG_SYS_B_DASH = 0x860`) and `SaveBlock1.flags` at **0x1220**. The RS read therefore lands at `sb1 + 0x1388`, which is inside `vars[]` (0x1340) — not the flags array at all.
*Severity today: inert.* `RUNG_MAP` is deliberately never set for RS, and `rungeom_decide` requires `RUNG_LATCHED` (which includes `RUNG_MAP`), so D2 can never fire on RS and the bad byte is never acted on. But it is a wrong constant documented as a right one, and **the same table already mis-feeds RS's HM badge gates** (`badgeCut 0x867` vs Ruby's `0x807`, same 0x1270/0x1220 offset error) — that half is *pre-existing* from phase 22.2, is **not** inert, and is reachable the moment a Ruby/Sapphire player sets `smartTraverse ≥ 1`. It defeats `test_fieldtrav` TEST 10's whole promise ("never prompt what the game will refuse") on two of the five shipped titles. Nothing in the suite can see it, because the suite only ever asks for two engines.

**O4 — lane B1 Entry 3's headline evidence is a renamed copy of an earlier screenshot.**
`evidence/impl/EM-msgmode-zero-while-box-waits.png` is **byte-identical** (blob `6a027c8…`) to `evidence/impl/EM-B3-fixed-4taps-no-route.png`, which was already committed in `2241a32`. It is therefore not a capture taken at the moment of the `msgMode = 0` read, and the sentence "…and `msgMode = 0` read out of the running game **at the same moment**" cannot be corroborated by it. The *finding* — `sFieldMessageBoxMode` is a PRINTING flag, not a "box is up" flag — stands on the gdb reads and on pret, and I am not disputing it; the evidence sentence is doing work the picture cannot do.

**O5 — rows A1 / A2 are marked VERIFIED against the lane's own definition.**
Entry 5 defines VERIFIED as "the screen itself was reached this session **and** the verb was proven by a state delta the game produced". The evidence given for A1/A2 is `ctx = 9 TITLE` plus "the TITLE tap=A verb was proven in phase 23". No verb delta was produced this session. By the lane's own strictness these are `VERIFIED-mech`, which would make the counts 4 VERIFIED / 21 VERIFIED-mech rather than 6 / 19.

**O6 — row F4 is half-proven.**
Reaching the CRY screen by drag is genuinely proven (`dlgSteps 4→10` plus the screen the game drew). The row's *verb* — "tap = A = the play button" — rests on S9, whose only delta is `dlgTaps 38→39`, i.e. **our own counter**. Nothing game-side shows the cry actually played. The screen is VERIFIED; the verb on it is asserted.

**O7 — "settings.bin restored **byte-identically** from the lane backup" (lane B1 Entry 7).**
The live `tools/emutest/az-b/user/sdmc/3DGBA/settings.bin` is **104 bytes**; the only backup on disk (`settings.bin.overnight2.bak`) is **100 bytes** (a pre-`traverse` file). The *value* claim holds — I decoded the file and `swapped` (word 5) is **0**, `traverse` (word 25) is 0 — but byte-identity against that backup is impossible. (Instance b's sdmc *is* ROM-less, so the `clean-fixtures` half is confirmed.)

**O8 — two counted claims in lane B1 Entry 7 are simply wrong numbers.** "six code+doc commits" — the branch carries **ten** (`525dd56 c710060 d6fb043 ab676f8 f31e574 a97942d 2bfa19f 04c5ebd 8260ca4 8326a45`). "all **15** host suites re-run" — sixteen are then listed. Cosmetic, but they are counted claims.

### FALSE

**F1 — "a full-width textbox … with the ▼ prompt drawn" (lane B1 Entry 3).**
There is no ▼ in `EM-msgmode-zero-while-box-waits.png`. I opened it and zoomed the box's bottom-right corner at 3×: the box contains the line "I am your guide to the Set KO Tourney!" and nothing else — no arrow, no cursor glyph. The detail was written to make the picture prove "the box is *waiting*", and the picture does not show that. (The claim it supports is still true for other reasons; the stated observation is not.)

---

## 2. New addresses vs. the ROMs actually in `roms/`

The carts on this machine are **BPEE rev 0**, **BPRE rev 1**, **BPGE rev 1**, **AXVE rev 2**, **AXPE rev 2** (read from the header at 0xBC). That matches every sym map the lanes cited.

`sLockFieldControls` is IWRAM, so it is not *in* the ROM — but it is referenced from literal pools, and that is checkable. Word-aligned little-endian hits:

| ROM (rev) | Claimed address | Word-aligned hits | First offsets |
|---|---|---|---|
| `emerald.gba` (BPEE r0) | `0x03000F2C` | **3** | 0x98E5C, 0x98E68, 0x98E74 |
| `firered.gba` (BPRE **r1**) | `0x03000F9C` | **3** | 0x6995C, 0x69968, 0x69974 |
| `leafgreen.gba` (BPGE **r1**) | `0x03000F9C` | **3** | **0x6995C, 0x69968, 0x69974** |
| `ruby.gba` (AXVE **r2**) | `0x030006A4` | **3** | 0x65558, 0x65564, 0x65570 |
| `sapphire.gba` (AXPE **r2**) | `0x030006A4` | **3** | 0x6555C, 0x65568, 0x65574 |

Three literal-pool references 12 bytes apart is exactly the shape of `LockPlayerFieldControls` /
`UnlockPlayerFieldControls` / `ArePlayerFieldControlsLocked` sitting side by side. And LeafGreen's
hits are at **the same file offsets as FireRed's** — that is the shared-IWRAM claim proven in the
binary rather than copied from a sym map. Emerald's `0x03000F2C` additionally matches the one
`pokeemerald.sym` on this machine (`03000f2c l 00000001 sLockFieldControls`).

`FLAG_SYS_B_DASH`, re-derived from the local pret clones: Emerald `SYSTEM_FLAGS 0x860 + 0x60 = 0x8C0` ✅ · FireRed `SYS_FLAGS 0x800 + 0x2F = 0x82F` ✅ · **Ruby `0x800 + 0x60 = 0x860` — the value the shipped RSE row does not carry (O3).**

**Residual, low risk:** the FR/LG rev **0** half of "`pokefirered.sym == pokefirered_rev1.sym`" is not
verifiable here (no rev0 cart on disk). The read is compare-only and fail-safe — a wrong value can
only disable the re-route — so this is a note, not a finding.

---

## 3. Fixtures, boots, and staging

The three banked saves parse cleanly and are ordinary continue-points, not doctored states
(§1 table). The four lane-A boots and the three lane-B boots all have real run directories under
`tools/emutest/runs*/` whose `events.log` records the staged fixture, the launched `.3dsx`, and a
clean profile restore. Nothing suggests a manufactured run.

Two housekeeping residuals, neither claimed otherwise by the lanes but both worth naming:

- the **main** Azahar sdmc still holds `gameA.gba` / `gameB.gba` (16 MB each) plus the r117
  fixture save staged as `gameA.sav`/`gameB.sav`. Lane B1 ran `clean-fixtures` for instance *b*
  (verified ROM-less); instance *a* was not cleaned.
- **`3DGBA.cia` is stale** — 14 Aug 06:56, a day behind `3DGBA.3dsx`/`3DGBA.elf` (20:13, current
  with `main.c` at 20:12). CLAUDE.md rule 1 makes the `.cia` the real target; whatever gets
  installed next is pre-phase-24 unless `make cia` is re-run.

---

## 4. Evidence-image integrity

66 files in `evidence/impl/`. Two byte-identical pairs:

| Hash | Files | Verdict |
|---|---|---|
| `1bcfe410…` | `EM-B3-fixed-4taps-no-route.png` · `EM-msgmode-zero-while-box-waits.png` | **This phase — O4/F1 above.** |
| `1ec6a255…` | `FR-dlg-P2-tapped-through-to-overworld-startmenu.bottom.png` · `FR-dlg-P4-hold-B-exited-dex.bottom.png` | Pre-existing (both added by `62b3976`, phase 23). Out of scope here, but the same defect: two different claims illustrated by one frame. Worth a phase-23 correction.|

Everything else is distinct. The four lane-A captures I opened (`P1c`, `P2a`, `P2c`, `P4d`) show
what their captions say.

---

## 5. Two things the lanes got right that were the most likely places to find a lie

1. **The excursion test really does bite.** Re-introducing `ft_rom_ptr(m->mapHeader)` produced
   exactly the four TEST 16 failures `e11b620` claims. That is a golden testing the truth, not the
   code.
2. **The FRLG run-tile table is not Emerald's with the names changed.** I chased "FRLG blocks
   exactly 1 behaviour" expecting the Fortree-bridge clause to have been dropped — FRLG's
   `IsRunningDisallowed` *does* route through `MetatileBehaviorForbidsBiking`, which *does* have
   the bridge clause. It is inert only because `MetatileBehavior_IsFortreeBridge` is a
   `return FALSE;` stub in pokefirered. The shipped table and its oracle are both right, for the
   right reason.

---

## 6. What is owed

1. **State the real default.** Every P1–P4 verdict should read "with `smartTraverse` set to 1 (or 2
   for P4); the shipped default is 0 = Off." (O1)
2. **Give the six `touch.c` fixes a barrier**, or say plainly in the lane log that the host gate
   does not cover them. `dlggeom_route`/`owngest_step`/`rungeom_*` were extracted into pure
   `touchgeom.c` precisely so they *could* be graded — the ANSWER/FACE/DLG cadence and the
   excursion leg-boundary state machine are the next candidates. (O2)
3. **Split the RSE engine row, or refuse RS.** Either add an `s_cfgRs` with `flagsOff 0x1220` /
   `SYSTEM_FLAGS 0x800` badge ids / `runShoes 0x860`, or make `fieldtrav_cfg` return NULL for
   AXVE/AXPE so the traversal tier declines instead of reading `vars[]` as flags. Then add the
   third column to `test_fieldtrav` TEST 2 so the check pins the truth rather than the table's
   shape. (O3)
4. **Re-capture or re-caption** `EM-msgmode-zero-while-box-waits.png`, and drop the "▼ prompt
   drawn" sentence. (O4/F1)
5. **Demote A1/A2 to `VERIFIED-mech`** and re-state the counts, or re-prove the TITLE tap verb with
   a delta this session. (O5)
6. **Rebuild `3DGBA.cia`** before anything is installed. (§3)

---

## Disposition (main session, 2026-08-14)

Every finding below was re-verified independently before it was accepted; the audit was not taken
on trust any more than the lanes were.

| # | Verified how | Action |
|---|---|---|
| **O1** default | Read `source/theme.c:35` — the default is `smartTraverse 0 = **Off**`, not 1. The audit is right and the lane's phrasing understated it. | **Accepted, surfaced to the user.** P1–P4 are real but describe an opt-in feature. Whether Off stays the default is the user's call — the existing rationale (a feature that moves the player and answers its yes/no prompts should not switch itself on) is sound, so it is not being flipped unilaterally. |
| **O3** RS flags | Read pokeruby `global.h:701-702` directly: `/*0x1220*/ u8 flags[]`, `/*0x1340*/ u16 vars[]`. Our RSE row uses Emerald's `0x1270`, so a Ruby badge read lands at `sb1+0x137C` — past the end of `flags[]`, inside `vars[]`. **Confirmed, and worse than "inert": the badge half is live.** | **Accepted — being fixed** with a third constant table and a TEST 10 regression that asks for the third numbering. `FpEngine` is frozen (fieldpath.h:34) so the discriminator is routed through fieldtrav.h. |
| **O4 / F1** evidence | `shasum` — `EM-msgmode-zero-while-box-waits.png` is byte-identical (`1cc0973d…`) to `EM-B3-fixed-4taps-no-route.png`. Opened the frame: no ▼. | **Accepted.** Duplicate withdrawn from the tree, the claim corrected in place in LANE-B-TAPVERIFY.md, the underlying finding explicitly preserved (it never rested on the picture). |
| **§3** stale `.cia` | `ls -la` — `.cia` 14 Aug 06:56 vs `.elf` 20:13. | **Fixed:** rebuilt at 20:42. CLAUDE.md rule 1 makes the `.cia` the real target, so this blocked any hardware test of phase 24. |
| **O2** no touch.c coverage | Confirmed by inspection — no host suite compiles `source/touch.c`, so six of lane A's seven fixes have zero regression cover. | **Accepted as a real gap**, logged for the next phase. It is an architectural hole (the suites test pure cores; touch.c is not one), not something to paper over with a token test. **CLOSED, phase 25 lane C2** (`085644e`): the six decisions were EXTRACTED into `source/progseq.{c,h}` (the interact sequencer) and `source/excseq.{c,h}` (the excursion leg machine) the way `touchgeom`/`uihit`/`fieldpath` were, and graded by `test_progseq` (877 checks) + `test_excseq` (677) — the gate is 18 suites now. Not a token test: TEST 4 drives the real sequencer at a transcription of pokeemerald's own `Task_HandleYesNoInput` and shows the pre-`2dde03e` rule failing the same oracle. Both extractions were re-proven live afterwards (P1 Surf `cea8d42`, P4 excursion `43486e4`), and the suite found a residue nobody had seen (the dims history survives a leg boundary). |
| **O5 / O6 / O8** grading | Read the lane's own VERIFIED definition against the cited evidence. | **Accepted.** Counts restated as 4 VERIFIED / 21 VERIFIED-mech; F4's screen is verified, its verb is asserted. |
| **O7** settings.bin | 104 B live vs 100 B backup. | **Accepted, cosmetic.** The value claim (`swapped=0`) holds; "byte-identical" did not. |
| phase-23 dup | `shasum` — `FR-dlg-P2-tapped-through-to-overworld-startmenu.bottom.png` ≡ `FR-dlg-P4-hold-B-exited-dex.bottom.png` (`2d7e33ae…`). | **Confirmed pre-existing.** One frame is illustrating two different claims; flagged for re-capture, not silently deleted, because unlike O4 it is not the sole support for either. |

**The pattern worth keeping:** three of these (O4, F1, the phase-23 pair) are the same failure —
a screenshot asked to prove more than a screenshot can. The house rule already says "a screenshot
that merely looks right is not proof; read the game's own state". Every finding that survived here
survived *because* it had a state read behind it. That is the rule earning its keep.
