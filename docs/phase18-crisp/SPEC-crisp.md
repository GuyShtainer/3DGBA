# SPEC-crisp — make the UI text SHARP (phase 18, work item 1 of 3)

Status: **investigation complete, cause PROVEN, fix specified — not implemented.**
Author pass: 2026-08-12. Scope: `source/assets.c` text path + `tools/build_assets.sh` font bake +
every `assets_text*` call site. **Out of scope and untouched:** emulation, threading,
`celiolink.c` / `netlink.c` / gbacore SIO, the HD-2D render passes.

User complaint being answered (real New-3DS hardware, phase-17 build):

> "its way better but mostly very blurry, can you see that? Text can and should be sharp!"

**Verdict: the user is right, and the blur is total — every single text draw in the app is
resampled off 1:1.** 16 distinct `(face, px)` combinations exist; 15 of them are downscales
between **0.577× and 0.903×**, and the 16th is a 1.049× upscale. Nothing is drawn at 1.0.
The cause is not one of the three hypotheses in the brief — it is **hypothesis (a) at roughly
double the assumed magnitude**, plus **(c) exactly as suspected**, while **(b) is real but has a
different mechanism than assumed**. All three must be fixed together.

---

## 0. Executive summary

| | Claim | Status |
|---|---|---|
| **C1.a** | Bakes are ~1.4–1.6× larger than the sizes drawn; every draw is a resample | **PROVEN** (3 independent ways) |
| **C1.b** | Font glyph sheets sample with `GPU_LINEAR` | **PROVEN — but citro2d bakes it into the texture at load, it is *not* inherited from the game blit** |
| **C1.c** | Fractional draw origins blend each glyph across two texels | **PROVEN, and measured: worth as much blur as the wrong scale** |
| **C1.d** | `s_native[]` is not what the code comments say | **PROVEN: it is `{26, 27, 24, 24}`, read live over gdb — the comments claim `{14, 13, 9, 10}`** |
| **C1.e** | learn-skill design-handoff **invariant 4**'s measurement recipe is wrong | **PROVEN — the recipe is what hid the bug for two phases** |

The whole defect reduces to one exact identity, derived below and confirmed against the running
app:

> **effective glyph texel scale = `px / lineFeed(face)`** — and *only* when the draw origin is an
> integer. Text is pixel-perfect iff `px == lineFeed` **and** `x`, `y` are whole numbers.

Today `lineFeed` is `19 / 17 / 12 / 14` (SG-bold / SG-med / JBM-med / JBM-bold) while `px` at the
call sites is `8 … 20`. Hence the blur.

---

## 1. C1 — proving the cause

### C1.1 The citro2d scale contract (from the shipped binary, not from docs)

`assets_text()` (source/assets.c:105-110) does:

```c
float sc = px / s_native[f];
C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, sc, sc, col);
```

`sc` is **not** the texel scale. Disassembly of the installed
`/opt/devkitpro/libctru/lib/libcitro2d.a`:

* `font.o : C2Di_PostLoadFont` computes `font->textScale = 30.0f / tglp->cellHeight`
  (`vldr s13, =41f00000` = 30.0f; `ldrb r3,[r2,#1]` = `TGLP_s.cellHeight`; stored at `font+8`).
* `text.o : C2D_DrawText` loads `font->textScale` (`vldr s15,[r3,#8]`) and multiplies **both**
  incoming scales by it (`s17 = textScale*scaleX`, `s21 = textScale*scaleY`) before calling
  `C2D_FontCalcGlyphPos`.
* `text.o : C2D_TextGetDimensions` returns
  `height = ceilf(finf.lineFeed * scaleY * font->textScale) * text->lines`
  (`ldrb r3,[r2,#29]` = `CFNT_s + 29` = `FINF_s.lineFeed`; `bl ceilf`; `× text->lines`).

So:

```
texel_scale = sc * 30 / cellHeight
s_native[f] = C2D_TextGetDimensions(scale 1.0) = ceil(lineFeed * 30 / cellHeight)
⇒ texel_scale = px * 30 / (ceil(lineFeed*30/cellHeight) * cellHeight)
             ≈ px / lineFeed          (exactly px/lineFeed once the ceil() is removed)
```

**C1.1.1** — The record must state that `C2D_TextGetDimensions(..., 1.0f, 1.0f, ...)` does **not**
return the bitmap's native pixel height. It returns citro2d's *normalised* height, where scale 1.0
means "a 30 px line" for **every** font regardless of bake size. Measuring with it and dividing is
a no-op that looks like a correction.

### C1.2 What the four shipped faces actually are

Parsed directly out of `data/fnt_*.bin` (BCFNT `FINF` + `TGLP` blocks):

| face | file bytes | `-s` used | **lineFeed** | cellW×cellH | baseline | sheet | `s_native` (=`ceil(lf·30/cellH)`) | **px that is 1:1** |
|---|---|---|---|---|---|---|---|---|
| `fnt_sg_bold`  | 527,740 | 11 pt | **19** | 20×22 | 17 | 1024×1024 A4 | 26 | **19.07** |
| `fnt_sg_med`   | 527,740 | 10 pt | **17** | 18×19 | 15 | 1024×1024 A4 | 27 | **17.10** |
| `fnt_jbm_med`  | 530,376 |  7 pt | **12** |  9×15 | 11 | 1024×1024 A4 | 24 | **12.00** |
| `fnt_jbm_bold` | 530,376 |  8 pt | **14** | 11×18 | 13 | 1024×1024 A4 | 24 | **14.40** |

**C1.2.1** — `tools/build_assets.sh:27-30`'s comments (`~14px native`, `~13px`, `~9px`, `~10px`)
are wrong by ~1.4× on every line. `mkbcfnt -s <pt>` produces `lineFeed ≈ 1.72 × pt`, not `≈1.3 ×
pt` as the learn skill's design-handoff note says. The comment must be replaced by the measured
`lineFeed`, and the bake script must print it.

### C1.3 Live confirmation on the running app (gdb, Azahar 2125.1.2)

Booted `3DGBA.3dsx`, `gdbio resume`, `g_renderSeq` advancing (424 → 455 → 3899, app alive):

```
s_native+0  @0x004a131c = 0x41d00000 = 26.0f     FNT_SG_BOLD
s_native+4  @0x004a1320 = 0x41d80000 = 27.0f     FNT_SG_MED
s_native+8  @0x004a1324 = 0x41c00000 = 24.0f     FNT_JBM_MED
s_native+12 @0x004a1328 = 0x41c00000 = 24.0f     FNT_JBM_BOLD
s_ready     @0x004a132c = 1
```

Exactly the values predicted from the `.bin` files. **The measurement path is not broken — it is
measuring the wrong quantity, correctly.**

### C1.4 Every call site, with its resulting scale

Extracted by parsing all `assets_text / _c / _r / _w` invocations in `source/*.c` (multi-line
calls included) and applying C1.1's formula with the shipped `s_native`. `assets_button`'s label
`px` is taken from its callers; `assets_seg` hardcodes 10.

| face | px | sites | **effective texel scale** | verdict |
|---|---|---|---|---|
| `FNT_SG_BOLD` | 11 | 1 | **0.577** | downscale |
| `FNT_SG_BOLD` | 12 | 7 | **0.629** | downscale |
| `FNT_SG_BOLD` | 13 | 8 (all `assets_button` labels) | **0.682** | downscale |
| `FNT_SG_BOLD` | 14 | 1 | **0.734** | downscale |
| `FNT_SG_BOLD` | 16 | 1 | **0.839** | downscale |
| `FNT_SG_BOLD` | 20 | 1 (`"Settings"`) | **1.049** | upscale |
| `FNT_SG_MED` | 10 | 1 (`assets_seg`) | **0.585** | downscale |
| `FNT_SG_MED` | 12 | 12 | **0.702** | downscale |
| `FNT_SG_MED` | 13 | 3 | **0.760** | downscale |
| `FNT_SG_MED` | 14 | 2 | **0.819** | downscale |
| `FNT_JBM_MED` | 8 | 14 | **0.667** | downscale |
| `FNT_JBM_MED` | 8.5 | 5 | **0.708** | downscale |
| `FNT_JBM_MED` | 9 | 21 | **0.750** | downscale |
| `FNT_JBM_BOLD` | 10 | 4 | **0.694** | downscale |
| `FNT_JBM_BOLD` | 13 | 2 | **0.903** | downscale |

**C1.4.1** — Zero call sites draw at 1.0. The two most common sizes in the app —
`FNT_JBM_MED@9` (21 sites: every section label, HUD clock/fps, hint line) and
`FNT_JBM_MED@8` (14 sites: every chip and badge) — sit at **0.75 and 0.667**, i.e. the *smallest*
text in the UI takes the *heaviest* resample. That is precisely the user's "mostly very blurry".

**C1.4.2** — `source/ui.c:70-72` carries a comment asserting "the baked FNT_JBM_MED at its ~9 px
native size draws near scale 1.0 (learn-skill invariant 4), so coverage is ~1.0 and the nominal
contrast is the real one." The measured scale is **0.667**. The contrast reasoning built on that
comment is therefore also unsound and must be re-derived after the fix (a resampled glyph's
coverage is < 1.0, so measured contrast is worse than nominal — the fix *improves* it).

### C1.5 The texture filter — hypothesis (b), corrected

There is no `C3D_TexSetFilter` on any font sheet anywhere in `source/` (the 18 filter calls in
`main.c` are all game/peer/DoF/bloom textures). **But the filter is not inherited from the
preceding draw.** `font.o : C2Di_PostLoadFont` writes a literal `0x00001106` into every glyph
sheet's `C3D_Tex.param` (offset +12 in `C3D_Tex`; the loop stores `data/-12`, `fmt|size/-8`,
`height/-4`, `width/-2`, `param/+0`, then `add r3,r3,#24`). Decoding with libctru's
`GPU_TEXTURE_MAG_FILTER(v)=(v&1)<<1`, `GPU_TEXTURE_MIN_FILTER(v)=(v&1)<<2`:

```
0x1106 = 0b0001000100000110 → bit1 = 1 → MAG = GPU_LINEAR
                             → bit2 = 1 → MIN = GPU_LINEAR
```

**C1.5.1** — citro2d hard-sets `GPU_LINEAR/GPU_LINEAR` on every `.bcfnt` glyph sheet at load.
citro2d binds the texture with its own `param`, so the game blit's `GPU_LINEAR` is irrelevant —
the font would still be LINEAR even if every other draw were NEAREST.

**C1.5.2** — The installed citro2d exposes the correct hook:
`void C2D_FontSetFilter(C2D_Font, GPU_TEXTURE_FILTER_PARAM mag, GPU_TEXTURE_FILTER_PARAM min)`
(`c2d/font.h`; `font.o` disassembly shows it clears bits 1–2 of each sheet's `param` and ORs the
new pair). This is the only supported way to change it — `C2D_Font_s` is opaque.

### C1.6 The fractional-origin penalty — hypothesis (c), measured

`assets_text_c` (assets.c:118) passes `cx - assets_text_w(...)/2.0f` and `assets_text_r`
(assets.c:121) passes `rx - width`; neither is rounded, and `assets_text` does not round either.
Widths are odd more often than not, so most centred/right-aligned runs land on a half-pixel.
Vertical origins are worse: `assets.c:202` uses `y + (h - px)/2.0f - 0.5f`, `assets.c:225` uses
`y + h/2.0f - 5.0f`, `main.c:2347` uses `y + (h - 12.0f)/2.0f`.

Measured on a real glyph (`'S'` from a correctly-sized `sg-med` bake, `lineFeed 12`, drawn at
`px = 12`, i.e. **exact scale 1.0**), reproducing the PICA200's bilinear sample
`u = (i + 0.5 − origin)/s − 0.5`:

| origin fractional part | distinct grey levels in the glyph | edge acutance |
|---|---|---|
| **0.00** | **14** | **0.0971** |
| 0.25 | 32 | 0.0878 |
| 0.50 | 22 | 0.0764 |
| 0.75 | 30 | 0.0858 |
| *(reference: shipped bake at scale 0.706, integer origin)* | *29* | *0.1057* |

**C1.6.1** — A half-pixel origin at perfect scale is **as blurry as the current 0.706× downscale**
(22–32 grey levels vs 29). Fixing C2 (scale) without fixing C4 (origin) leaves every centred and
right-aligned label — buttons, tab labels, the HUD's right-to-left flow, dialog titles — exactly
as soft as it is today. **C4 is not a polish item; it is half the fix.**

### C1.7 Before/after, same output size, apples to apples

Same glyph, same final on-screen size, only the bake size differs (host reproduction of the GPU's
sampling; see C5.2 for the recipe):

| | cell | eff. scale | output | grey levels (LINEAR) | widest edge ramp |
|---|---|---|---|---|---|
| **shipped** `sg_med` (`-s 10`, lf 17) drawn at 12 px | 18×19 | 0.706 | 13×13 | **29** | 4 px |
| **fixed** `sg_med` (`-s 7`, lf 12) drawn at 12 px | 13×13 | **1.000** | 13×13 | **14** | 3 px |
| **shipped** `jbm_med` (`-s 7`, lf 12) drawn at 9 px | 9×15 | 0.750 | 7×11 | **19** | 4 px |
| **fixed** `jbm_med` (`-s 5`, lf 9) drawn at 9 px | 7×9 | **1.000** | 7×9 | **10** | 2 px |

**C1.7.1** — At `eff == 1.0` the LINEAR and NEAREST results are **bit-identical** (both runs
produced the same numbers), because the bilinear sample coordinate collapses onto texel centres.
This is the formal statement of "NEAREST is only correct at scale 1.0" — and the reason the filter
change must land *with* the re-bake, never before it (C3.3).

**C1.7.2** — Grey-level count is the metric that discriminates cleanly (≈2× reduction in both
pairs). Acutance is confounded by output size and must only be compared at equal output size.

### C1.8 Harness findings from this investigation (record these; they affect the fix slice)

**C1.8.1 — the `see` channel returned pure black today.** `tools/emutest/run see shot both`
produced 1-colour images while `g_renderSeq` was advancing and the window chrome captured fine
(159 colours in the raw 2560×1136 window grab, render area uniform `#000000`). Screen Recording is
granted; the GPU surface simply did not appear in the capture. The verifier must not treat a black
crop as "the app drew nothing" — cross-check with `gdbio poll g_renderSeq --changed` (smoke.sh
already does this) and fall back to C1.8.2.

**C1.8.2 — a new zero-permission, pixel-exact capture channel exists and should be built out.**
libctru's framebuffer pointers are in the ELF and readable over the existing gdb stub:

```
gfxTopFramebuffers      @0x0054c9b8 = 0x30000000 / 0x3008ca00   (max size 576000 = 2×400×240×3)
gfxBottomFramebuffers   @0x0054c9b0 = 0x30119400 / 0x30151800   (max size 230400 = 320×240×3)
gfxFramebufferFormats   @0x0054c9a4 = 0x0101  → GSP_BGR8_OES (3 bpp) on both screens
gfxIsVram = 0 (linear heap), gfxIsDoubleBuf = 1, gfxCurBuf readable
```

A 64 KB read completes in **0.15 s**; a whole top framebuffer is ~0.6 s. Byte order is `B,G,R`;
the theme background reads `30 18 20` = RGB(32,24,48), matching the Indigo plate. **Two blockers
must be solved before this is evidence-grade:**

* **C1.8.2.a — `gdbio read` corrupts the tail of every internal chunk.** A single 288,000-byte
  read shows 3–5 wrong bytes at **every 4096-byte boundary** (breaks at 4092-4095, 8188-8191,
  12284-12287, …); chunking the request at 2048 moves the corruption to 2045-2047. Short reads
  (≤ 4096 in one request) are clean and self-consistent. This is a real `tools/emutest/gdbio.py`
  defect — file it and fix it before relying on bulk reads.
* **C1.8.2.b — tearing.** The app keeps rendering during the dump (the stub's halt does not
  freeze the frame across a multi-request dump), so consecutive chunks come from different frames
  and the reassembled image is scrambled. The dump needs a single-request read of a whole
  framebuffer, or a frame-freeze (e.g. a debug flag the app polls that parks the render thread).

Until C1.8.2 is finished, the **host-side reproduction (C5.2) is the primary pixel evidence** and
the emulator is used only for "does it still look right / did anything overflow" checks.

### C1.9 What is *not* the cause (checked, ruled out)

* **C1.9.1** — Not the theme. All six themes share one text path; the resample is colour-blind.
* **C1.9.2** — Not `assets_gen.h` / the art pack. Plates and widgets are `tex3ds` RGBA8 and are
  drawn at 1:1 (`assets_draw_plate` uses scale 1,1); only text resamples.
* **C1.9.3** — Not a stale `data/` build. The `.bin` files parsed here are the ones linked into
  the `3DGBA.elf` whose live `s_native` was read (C1.3 matches C1.2 exactly).

---

## 2. C2 — the decision: what to change, and what it costs

### C2.1 The invariant the fix establishes

> **R1 (the crispness law).** For every text draw: `px == lineFeed(face)` and the draw origin
> `(x, y)` is an integer. Then every glyph texel maps to exactly one device pixel.

Corollaries that make R1 checkable rather than aspirational:

**C2.1.1** — `s_native[f]` must become **exact**, not `ceil()`-quantised:
```c
FINF_s* fi = C2D_FontGetInfo(s_fonts[f]);          // public API, c2d/font.h
s_native[f] = (float)fi->lineFeed * 30.0f / (float)fi->tglp->cellHeight;
```
With that, `sc = px / s_native[f]` gives `texel_scale = px / lineFeed` **exactly**, with no
residual. (Keeping the `ceil()` leaves up to a 2.9 % shrink — `fnt_jbm_bold` is off by 2.9 %
today, `fnt_sg_bold` by 0.35 %, `fnt_sg_med` by 0.6 %, `fnt_jbm_med` by 0 %.)

**C2.1.2** — Swapping `ceil` for exact changes on-screen text size by **+0.35 % / +0.6 % / 0 % /
+2.9 %** for the four current faces. That is inside layout noise and needs no re-fit on its own.
It must land *before* the re-bake so the re-bake can be verified against a clean formula.

**C2.1.3** — `px` must stop being a free float. Introduce a **text-role enum** in `assets.h`
(one entry per rung of the ladder, e.g. `TXT_TITLE`, `TXT_BUTTON`, `TXT_BODY`, `TXT_HUD_NAME`,
`TXT_SECTION`, `TXT_CHIP`, `TXT_VALUE`) mapping role → `(AFont, px)`. Call sites pass a role.
This is what makes C5.1's host gate exhaustive instead of a hand-maintained list, and it is what
stops the ladder rotting the next time someone types a literal.

### C2.2 The bake ladder mkbcfnt can actually hit

`mkbcfnt -s <pt>` → `lineFeed`, measured across both families (identical for Medium and Bold
within a family; **charset-independent** — verified by baking with and without an ASCII whitelist
and getting the same `lineFeed`, only `cellHeight` moves, and `cellHeight` cancels out of R1):

| `-s` | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **Space Grotesk** lineFeed | 5 | 7 | 9 | 10 | **12** | **14** | 15 | **17** | 19 | 20 | 22 |
| **JetBrains Mono** lineFeed | 5 | 7 | **9** | 11 | **12** | **14** | 16 | 18 | 19 | 21 | 23 |

**C2.2.1** — Reachable line heights are quantised. There is **no** 8 px and **no** 13 px rung for
either family. Any call site at 8, 8.5 or 13 px must move.

### C2.3 The size cost, honestly

**C2.3.1** — `mkbcfnt` emits a **fixed 1024×1024 A4 sheet** regardless of glyph count. Proven:
a 10-glyph whitelist bake is 524,488 B with `sheet=1024x1024, sheetSize=524288`; a 108-glyph
whitelist bake is 524,856 B; the shipped full-charset bake is 527,740 B. **Whitelisting saves
nothing.** Every additional face costs **~525 KB in `data/`**.

**C2.3.2** — Runtime cost is *double* the file size. `font.o : C2D_FontLoadFromMem` does
`linearAlloc(size)` + `memcpy` and `C2Di_PostLoadFont` points each `C3D_Tex.data` into that copy —
so each face is resident twice (`.rodata` in the loaded image **plus** a linear-heap copy),
≈ **1.05 MB per face**. Today's four faces ≈ **4.2 MB** resident.

**C2.3.3** — Current totals for reference: `data/fnt_*.bin` = **2,116,232 B**;
`3DGBA.3dsx` = 3,822,776 B; `3DGBA.cia` = 1,939,392 B. Budget headroom: `APP_SYSTEM_MODE_EXT =
124MB` on New 3DS (Makefile:45), so +1–2 MB is affordable **there**; the `.3dsx` path and Old 3DS
(`64MB`) are the tight cases, and the emutest harness runs the `.3dsx`.

### C2.4 Options weighed

| | Option | Faces | `data/` delta | Layout churn | Verdict |
|---|---|---|---|---|---|
| **O1** | Re-bake the existing 4 faces to their *dominant* rung; snap every call site to it | 4 | **0 B** | Severe — `"Settings"` 20→12 (−40 %), `"No games found"` 16→12 | **Reject.** Collapses the whole type hierarchy into one size per family. |
| **O2** | Snap the call sites to a small ladder, one face per rung (**recommended**) | 7 | **+1,575 KB** | Moderate, enumerated in C2.5 | **Accept**, with O4 as the follow-up. |
| **O3** | Bake at 2× and draw at 0.5 | 4 | 0 B | none | **Reject.** 0.5 is still a resample: a 2:1 box downscale of an already-antialiased A4 glyph destroys 9 px stems. It also cannot reach 9 px from any reachable rung (needs `lineFeed 18` → `-s 10` for JBM, fine, but 12 px body needs `lineFeed 24` which does not exist). Only helps if the fix were "one size", which O1 already rejects. |
| **O4** | Write a **re-sheet tool** (detile → repack to a 256×256 or 512×512 sheet → retile), then take the full ladder | 7–10 | **−1,700 KB** (≈ 35 KB/face at 256², ≈ 135 KB at 512²) | same as O2 | **The right end state.** 108 glyphs at cell ≤ 21×23 fit 12×11 = 132 slots in 256×256. Needs a new tool + host test; do it as slice 4, not slice 1. |
| **O5** | Move fonts to romfs/SD instead of `data/*.bin` | any | halves *resident* cost (drops the `.rodata` copy) | none | Orthogonal; note it, do not do it now (adds romfs plumbing to a `.cia`+`.3dsx` build that currently needs none). |

**C2.4.1 — Requirement: adopt O2 now, O4 as a follow-up slice.** O2 is the smallest change that
satisfies R1 without flattening the design's type hierarchy. O4 turns O2's +1.5 MB into a
−1.7 MB saving and unlocks extra rungs, but it is a new binary-format tool and must not gate the
user-visible fix.

### C2.5 The recommended ladder, and every call site's move

Seven faces. Each row's `-s` comes from C2.2's table; each face is drawn at exactly one `px`.

| new face | `-s` | lineFeed = draw px | replaces | design role (FONTS.md) |
|---|---|---|---|---|
| `fnt_sg_bold_12` | 7 | **12** | SG-bold @ 11, 12, 13, 14 | Button label (12–14) |
| `fnt_sg_bold_17` | 10 | **17** | SG-bold @ 16, 20 | Screen title (14–16) |
| `fnt_sg_med_10` | 6 | **10** | SG-med @ 10 | HUD game name (10–11) |
| `fnt_sg_med_12` | 7 | **12** | SG-med @ 12, 13, 14 | List row / body (11–13) |
| `fnt_jbm_med_7` | 4 | **7** | JBM-med @ 8, 8.5 | HUD chips (7–7.5), badge, tiny hint (7.5–8) |
| `fnt_jbm_med_9` | 5 | **9** | JBM-med @ 9 | Section label (9), stat line, caps (9–10) |
| `fnt_jbm_bold_11` | 6 | **11** | JBM-bold @ 10, 13 | Caps / values |

Per-site moves (magnitude of the size change, for layout triage):

| from | to | Δ | sites | risk |
|---|---|---|---|---|
| SG-bold 13 → 12 | −7.7 % | 8 | all `assets_button` labels (rompicker 208/209/371/373/601/606, wireless 235/236/259, main 4492/4762/4807) | low — buttons are 9-sliced and width-elastic |
| SG-bold 12 → 12 | 0 | 7 | main 2559, 4464, 4466, 4755, 4756; rompicker 602, 607 | none |
| SG-bold 11 → 12 | +9.1 % | 1 | main 4772 `"Done"` | low |
| SG-bold 14 → 12 | −14.3 % | 1 | main 1709 presence-card name | check the card grid |
| SG-bold 16 → 17 | +6.3 % | 1 | rompicker 363 `"No games found"` | low |
| SG-bold 20 → 17 | −15.0 % | 1 | main 4721 `"Settings"` | **intentional** — 20 was above FONTS.md's own 14–16 title range |
| SG-med 14 → 12 | −14.3 % | 2 | main 2578/2579 loading-screen game names | check the two-name split at x=122/280 |
| SG-med 13 → 12 | −7.7 % | 3 | rompicker 203, 204, 600 | low |
| SG-med 12 → 12 | 0 | 12 | — | none |
| SG-med 10 → 10 | 0 | 1 | `assets_seg` (assets.c:225) | none |
| JBM-med 9 → 9 | 0 | 21 | — | none |
| JBM-med 8.5 → 7 | −17.6 % | 5 | rompicker 561, wireless 208/211/227/229 | check the wireless card columns |
| JBM-med 8 → 7 | −12.5 % | 14 | ui.c 82/88 (chips), touch.c 47/59/63, main 1712/1714/1715/1719/2603/2607/4809, wireless 206/250 | **lowest risk direction** — everything shrinks, nothing can overflow |
| JBM-bold 10 → 11 | +10.0 % | 4 | main 1710, 1713, 4478, 4759 | check the tilt-level right-align at `x+w` |
| JBM-bold 13 → 11 | −15.4 % | 2 | wireless 218, 220 | flattens the wireless "big value" — see Open Question Q3 |

**C2.5.1** — All `px` literals disappear from call sites in favour of C2.1.3's role enum. The two
existing constants (`UI_CHIP_PX` in ui.c:76, `TEXPL_PX` in main.c:2406) become role aliases.

**C2.5.2** — `assets_button`'s `px` parameter and `assets_seg`'s hardcoded `10.0f` must take roles
too, or the ladder leaks straight back in through the widget helpers.

**C2.5.3** — Byte cost of C2.5 as written: 7 × ~525 KB = **3,675 KB**, vs 2,116 KB today =
**+1,559 KB** in `data/` and ≈ +1.6 MB resident. Verify the `.3dsx` still loads under Azahar and
on hardware **before** claiming the slice done (CLAUDE.md #6).

### C2.6 Bake-script requirements

**C2.6.1** — `tools/build_assets.sh` grows one `bake()` call per ladder rung with the `-s` from
C2.5, and its comments state the **measured `lineFeed`**, not a guessed px.

**C2.6.2** — `bake()` must **print and assert** the produced `lineFeed` (parse `FINF+9` of the
output `.bin`) and fail the build if it differs from the rung the ladder declares. A silent
`mkbcfnt` version bump that shifts pt→px by one is exactly how this defect was born.

**C2.6.3** — `data/` grows 3 new `fnt_*.bin`; `source/assets.c`'s four `extern const u8` pairs and
the `AFont` enum grow to seven. `assets_gen.h` is generated only for plates/widgets and is **not**
involved — fonts are declared by hand in `assets.c` (lines 16-19). Either extend that by hand or
teach `build_assets.sh` to emit an `ASSET_FONTS` X-macro; the X-macro is preferred so the bake
script stays the single source of truth for which faces exist.

**C2.6.4** — Any glyph whitelist is **forbidden unless** it includes the 12 non-ASCII codepoints
the UI actually prints — `U+00B7 ·` (×31), `U+2014 —` (×23), `U+2261 ≡` (×10), `U+2026 …` (×3),
`U+2191 ↑` (×3), `U+25CF ●` (×2), `U+00AB «` (×2), `U+25CB ○` (×2), `U+00D7 ×` (×2),
`U+26A1 ⚡` (×2), `U+00A7 §` (×1), `U+2212 −` (×1) — **and** the Latin-1 supplement, because ROM
list rows print user filenames. Since whitelisting saves nothing today (C2.3.1) the safe default
is **no whitelist**; it only becomes relevant under O4. `mkbcfnt`'s whitelist file takes decimal
or `0x` codepoints — **not** `U+XXXX`. (`tools/.fontcache/whitelist.txt` is in `U+XXXX` form and
silently produces `Empty font`; delete it or convert it.)

---

## 3. C3 — the texture filter

**C3.1** — In `assets_init()`, immediately after each successful `C2D_FontLoadFromMem`, call:
```c
C2D_FontSetFilter(s_fonts[f], GPU_NEAREST, GPU_NEAREST);
```
This is the only supported hook (C1.5.2) and it must be applied **per face**, after load, before
the first draw.

**C3.2** — It is a **one-shot, load-time** call. Do **not** flip the filter per draw. citro2d
batches quads and citro3d defers texture state to draw time, so mutating a bound texture's `param`
mid-frame retroactively changes already-queued quads. If a per-draw filter ever becomes necessary,
it must be bracketed by `C2D_Flush()` (declared in `c2d/base.h`) — but the whole point of C2 is
that no draw is off 1.0, so no per-draw switching should exist.

**C3.3** — **Ordering constraint (binding).** C3.1 must land in the *same* commit as C2.5's
re-bake + snap, never before it. NEAREST at 0.667× drops one texel row in three and shatters a
9 px glyph; NEAREST at exactly 1.0 is bit-identical to LINEAR (C1.7.1) and costs nothing. The
filter change is therefore *insurance against future drift*, not the source of the improvement —
say so in the commit message so nobody "reverts the useless line" later.

**C3.4** — The fallback path (`ui_text*`, the system font, used only when the asset pack fails to
load) stays LINEAR. Its scales (0.32, 0.5) are far from 1.0 against the 30 px shared font and
cannot be made 1:1 at UI sizes. Documented degradation, not a regression.

---

## 4. C4 — integer origins

**C4.1** — `assets_text()` rounds both coordinates before drawing:
```c
x = floorf(x + 0.5f);
y = floorf(y + 0.5f);
```
Rounding inside `assets_text` (not at the call sites) means `_c` and `_r` inherit it for free and
no call site can opt out.

**C4.2** — `assets_text_c` and `assets_text_r` must round **after** the width subtraction
(`cx - w/2`, `rx - w`), which C4.1 gives automatically since they delegate. Do **not** round the
measured width itself — `assets_text_w` is also used for layout flow (main.c:4181/4184/4382,
ui.c:82) and rounding it there would accumulate drift across a right-to-left HUD run.

**C4.3** — Layouts that currently rely on sub-pixel centring: none do so *deliberately*. Every
fractional origin found is an accident of `w/2`, `(h - px)/2`, `h/2 - 5`, or `- 0.5f` nudges. The
maximum shift from rounding is **0.5 px**, always toward the pixel grid. Expected visible effects,
all to be re-checked in C5.3: button labels may shift 0.5 px within their 9-slice body
(assets.c:202 — the `- 0.5f` nudge there should be deleted, it exists to fight the very blur this
spec removes); segmented-control labels (assets.c:225) likewise; the presence card's stacked rows
(main.c:1709-1719) may close up by ≤ 1 px.

**C4.4** — Vertical rhythm: with R1 satisfied, citro2d's per-line advance is
`ceilf(lineFeed × 1.0)` = `lineFeed`, an integer, and the glyph's `left`/`charWidth` advances are
integers too (`C2D_FontCalcGlyphPos` multiplies integer `charWidthInfo_s` fields by an effective
scale of exactly 1.0). So **one** rounded origin keeps the whole run on the grid — there is no
per-glyph accumulation to fight.

**C4.5** — `main.c:2412`'s `(int)(assets_text_w(...) + 0.5f)` already rounds a width for the
`uihit` measure callback and must keep doing so; C4.2 does not change it.

---

## 5. C5 — the proof plan

Three tiers: an **exact** host gate that cannot be argued with, a **pixel** host reproduction that
produces before/after images and numbers, and an **on-device** look-and-overflow check.

### 5.1 Tier 1 — the exact host gate (this is the real regression barrier)

**C5.1.1** — New suite `test/host/test_typography.c` (11th app host suite; header carries its
compile line like the others):
```
clang -std=c11 -Wall -Wextra -O2 -I test/host -I source test/host/test_typography.c -o /tmp/ty && /tmp/ty
```

**C5.1.2** — It parses each `data/fnt_*.bin` directly: locate `"FINF"`, read `lineFeed` at
`FINF+9`; locate `"TGLP"`, read `cellWidth/cellHeight/baselinePos` at `TGLP+8/9/10` and
`nSheets/sheetFmt/nRows/nLines/sheetWidth/sheetHeight` at `TGLP+16..26`. (Confirmed layout — this
is how every number in C1.2 was obtained.)

**C5.1.3** — For **every** entry of the C2.1.3 role table it asserts
`fabsf(px / (float)lineFeed − 1.0f) <= 0.0f` — exact equality, not a tolerance. The ladder is
built so that equality is achievable; a tolerance is an invitation to drift.

**C5.1.4** — It asserts `s_native` is derived exactly, by recomputing
`lineFeed * 30.0f / cellHeight` and checking the app's formula against it (the formula moves into
a tiny pure-C helper in `assets.h`/a new `typography.h` so the host can call the *same* code —
CLAUDE.md rule #4, no duplicated arithmetic).

**C5.1.5** — A grep lint (in the suite or `tools/verdict.sh`) fails if any `assets_text*` /
`assets_button` / `assets_seg` call site passes a numeric `px` literal instead of a role.

**C5.1.6** — It asserts each face's sheet is present and that the 12 codepoints of C2.6.4 have a
non-empty glyph cell (guards invariant 5 / tofu, and guards a whitelist mistake under O4).

### 5.2 Tier 2 — the pixel reproduction (before/after images and the objective number)

A host tool (`tools/fontlab/` — a working prototype was written during this investigation and
lives at `<scratchpad>/crisp/bcfnt.py`; port it into the repo) that renders a glyph run exactly as
the PICA200 does:

**C5.2.1 — decode.** A4 glyph sheets are **8×8 Morton-tiled**, 2 px/byte, low nibble first,
tiles row-major:
```
morton(x,y) = (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3)
byte  = sheet[(tileY*(W/8) + tileX)*32 + (morton(x%8, y%8) >> 1)]
value = ((morton & 1) ? (byte >> 4) : (byte & 0x0F)) * 17
```
Glyph `gi` occupies cell `(row, line)` where `line = (gi % (nRows*nLines)) / nRows`,
`row = ... % nRows`, at pixel `x = row*(cellW+1)+1`, `y = line*(cellH+1)+1`.
*(Verified: this decode produces a legible glyph sheet from `data/fnt_sg_med.bin`.)*

**C5.2.2 — resample.** Destination pixel `i` samples source coordinate
`u = (i + 0.5 − originFrac)/s − 0.5`, bilinear, clamped. `s = px / lineFeed`.

**C5.2.3 — the objective measures.** Report all three, per glyph run:
1. **Distinct grey levels** in the rendered run (rounded to integer). *Primary.* A run at exact
   scale and integer origin has ≈ half the levels of the same run resampled — measured 29→14 and
   19→10 in C1.7. **Gate: the post-fix count must be ≤ 60 % of the pre-fix count for the same
   string at the same output size.**
2. **Widest edge ramp**: the longest run, on any scanline, of pixels whose value lies strictly
   between 12 % and 88 % of the run's peak. Crisp ≈ 2–3 px (the font's own antialiasing);
   resampled ≈ 4+ px. **Gate: ≤ 3 px.**
3. **Edge acutance**: mean `|Δ|` between horizontally adjacent pixels ÷ peak. Only ever compared
   at **equal output size** (C1.7.2).

**C5.2.4 — the fixed corpus.** Run the measures over these exact strings, one per ladder rung, so
before/after is comparable run to run:

| rung | string | why |
|---|---|---|
| `sg_bold_12` | `Resume this pairing` | longest button label |
| `sg_bold_17` | `Settings` | the one 20 px site, biggest move |
| `sg_med_10` | `Aspect-fit` | segmented-control label |
| `sg_med_12` | `pick a game (d-pad + A)` | list row, mixed case + punctuation |
| `jbm_med_7` | `HOST` / `BPEE` | chip + game code, all-caps stems |
| `jbm_med_9` | `SCALE · TOP` | **the run the user photographed**; includes `U+00B7` |
| `jbm_bold_11` | `12:34` | digits, the HUD clock face |

**C5.2.5** — Emit a `tools/emutest/run sheet` HTML artifact with before/after pairs at 8× NEAREST
zoom plus the three numbers per pair. That is the deliverable that answers "can you see that?".

### 5.3 Tier 3 — on device / in Azahar

**C5.3.1** — Screens to capture, in this order (each has a distinct rung mix):
1. **ROM picker, populated** — `sg_med_12` rows, `jbm_med_7` codes, `sg_bold_12` START/LINK
   buttons, `jbm_med_7` `settings · ZR` chip.
2. **ROM picker, empty** (`rompicker.c:363-373`) — `sg_bold_17` `"No games found"` + `jbm_med_9`.
3. **Settings** (ZR) — `sg_bold_17` title, `jbm_med_9` hint lines, `sg_med_10` segmented control,
   `jbm_bold_11` tilt level right-aligned at `x+w` (C2.5's highest-risk right-align).
4. **In-game HUD, both screens** — `jbm_med_9` clock/fps flowed right-to-left, `jbm_med_7` chips.
   This is the only screen where a size change can *overflow* rather than just look different.
5. **Pause menu** — `jbm_med_9` section labels above `sg_med_12` rows (`main.c:2345/2347`).
6. **Wireless lobby** — `jbm_bold_11` values (the −15.4 % move), `jbm_med_7` columns.
7. **Presence card** (`main.c:1709-1719`) — the stacked `sg_bold_12` / `jbm_bold_11` / `jbm_med_7`
   rows, the tightest vertical stack in the app.

**C5.3.2 — all six themes.** Repeat screens 1, 3 and 4 in Indigo, OLED, Daylight, Duo, Retro and
Custom. A fix validated only in Indigo is not validated. (Text geometry is theme-independent by
construction, so this is a *contrast/legibility* check, not a geometry one — but the ink colours
change and a thinner, sharper glyph reads differently on Daylight.)

**C5.3.3 — glyph runs to zoom.** `SCALE · TOP` (pause tab label), `BPEE` (list code),
`Resume this pairing` (button), the HUD clock. `tools/emutest/run zoom --rect x,y,w,h --scale 8`.

**C5.3.4 — capture fidelity.** The `see` crop is **900×540 for a 400×240 screen (2.25×,
non-integer, filtered)** — it is *not* a faithful record of the framebuffer and must never be the
source of a grey-level count. Two acceptable paths:
* **preferred** — finish C1.8.2 (framebuffer over gdb) and measure on true 400×240 / 320×240
  pixels; or
* **acceptable** — measure on the `see` crop but **self-calibrate**: include a known 1-device-pixel
  feature in the same crop (`ui_border_round`'s 1 px frame, or the segmented-control pill edge),
  measure *its* transition width as the capture's own blur floor, and report glyph edge width as a
  multiple of that floor. Report before/after with the identical capture pipeline and window
  geometry.

**C5.3.5 — overflow assertions** (things that break silently, not blurrily): the HUD's
right-to-left flow must not run off the left edge; `ui_chip_measure`-derived chip widths must stay
inside their bars; the presence card rows must not collide; button labels must stay inside their
9-slice bodies; the `uihit` measure callback (`main.c:2412`) must still produce hit rects that
match what is drawn (touch targets follow the text).

**C5.3.6 — hardware gate.** Per CLAUDE.md #6 this is a *visual* change, not a timing one, so
Azahar is sufficient to iterate — but the user reported the defect on hardware and must confirm it
on hardware. Ship the C5.2.5 before/after sheet with the build so the confirmation is a
comparison, not a memory test.

### 5.4 Regression guards

**C5.4.1** — `bash tools/emutest/tests/run_host_tests.sh` stays green.
**C5.4.2** — All ten existing app host suites stay green; the new `test_typography.c` makes eleven.
**C5.4.3** — `test/host/test_uihit.c` and `test_uigeom.c` must be re-run *after* the snap, since
hit rects and the pill-radius ladder are derived from text metrics.
**C5.4.4** — `git diff --stat source/celiolink.c` must be **empty** (phases 13–17 invariant).

---

## 6. Slice plan

| slice | content | proves |
|---|---|---|
| **S1** | C2.1.1 exact `s_native` (+ the shared pure-C helper), C4 integer origins, `test_typography.c` skeleton asserting the formula | Text moves ≤ 3 % and centred labels stop half-pixel blending. Small, reversible, no asset change. |
| **S2** | C2.1.3 role enum; every call site converted; C5.1.5 lint | No `px` literals left; ladder is enforceable. Still uses the 4 old faces (so text is *still* blurry — say so in the BUILDLOG, do not claim a win here). |
| **S3** | C2.5 ladder: 7 bakes, C2.6 script changes, C3.1 NEAREST, layout re-fit, full C5.2 + C5.3 evidence | **The user-visible fix.** |
| **S4** *(optional)* | O4 re-sheet tool; shrink `data/` back below today's size; add rungs if wanted | Recovers the 1.5 MB and then some. |

Bank each slice to `docs/phase18-crisp/BUILDLOG.md`.

---

## 7. Risks

**7.1** — `+1.56 MB` in `data/` (C2.5.3). Mitigations: verify the `.3dsx` boots in Azahar *and* on
hardware; if it bites, drop `sg_med_10` (fold the segmented control into `sg_med_12`) and
`jbm_bold_11` (fold into `jbm_med_9` where weight allows), or bring S4 forward.

**7.2** — 14 sites shrink 8→7 px. Smaller is the safe direction for overflow but the *hostile*
direction for legibility on a 3.5" screen. If 7 px reads badly on hardware, the fallback is
`jbm_med_9` for everything JBM-medium (all 40 sites at 9 px) — 1 face instead of 2, and 8→9 grows
chips 12.5 %, which then needs the C5.3.5 overflow pass in earnest.

**7.3** — The design pack's own numbers (FONTS.md §3) are "device px" for *line* size and were
never reconciled against `mkbcfnt`'s `lineFeed`. C2.5 maps every rung onto a FONTS.md role, but
the mapping is an interpretation; if a rung looks wrong on hardware, change the rung, not R1.

**7.4** — `mkbcfnt` version drift silently changes pt→`lineFeed`. C2.6.2's assert is the guard.

**7.5** — This spec's numbers were derived from the toolchain at `/opt/devkitpro` on 2026-08-12 and
from the *currently linked* `data/*.bin`. Re-running `tools/build_assets.sh` with a different
devkitPro would invalidate C2.2's table — re-measure, do not assume.

---

## Open questions

**Q1 — Is the `see` black-capture failure environmental or a real regression?** It failed for two
independent sessions today while the SKILL records both TCC grants "granted and verified working"
on 2026-08-08. Needs a determination (display asleep / Space / GPU-surface capture) before the
verifier depends on `see` at all. Blocking for C5.3 tier-3 evidence, not for the fix.

**Q2 — Who owns C1.8.2 (framebuffer-over-gdb capture)?** It is the better channel — pixel-exact,
zero permissions, 0.6 s per screen — but it needs the `gdbio read` chunk-tail bug fixed (C1.8.2.a)
and a frame-freeze (C1.8.2.b). Is that this phase's work item or a harness phase of its own?

**Q3 — Should the wireless "big value" (`wireless.c:218/220`, JBM-bold @13) really drop to 11?**
That is a −15.4 % move that flattens a deliberate hierarchy on the lobby screen. Alternatives:
an 8th face at `lineFeed 12` (`-s 7`, +525 KB) or `lineFeed 14` (`-s 8`), or redraw those two
values with `sg_bold_12`. Needs a look at the screen, which needs Q1 or Q2 resolved.

**Q4 — `U+26A1 ⚡` is in a string literal (×2).** Neither Space Grotesk nor JetBrains Mono has it;
it is rendering as tofu today (learn-skill invariant 5). Is it on screen, or in a log/comment
string? If on screen, it needs a drawn substitute — in scope for "text is right", out of scope for
"text is sharp".

**Q5 — What is `"Settings"` at 20 px meant to be?** FONTS.md caps screen titles at 16. C2.5 moves
it to 17. If the intent was a genuinely larger screen-title tier, the ladder needs a `lineFeed 20`
(`-s 12`) rung for it and the design doc should say so.

**Q6 — Does the `.3dsx` (Old-3DS, 64 MB) path have room for +1.6 MB resident?** The `.cia` gets
124 MB (Makefile:45) and is the shipping target, but the emutest harness boots the `.3dsx`. If it
does not, S3 must be gated behind S4.

**Q7 — Should the fallback `ui_text*` system-font path be removed instead of kept blurry?** It only
runs when the asset pack fails to load, which (post-fix) is a "the build is broken" state. Keeping
a permanently-soft fallback may be worse than a loud failure.
