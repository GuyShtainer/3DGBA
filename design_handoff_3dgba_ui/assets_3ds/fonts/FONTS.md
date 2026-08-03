# Fonts — 3DGBA (Space Grotesk + JetBrains Mono)

Typography is the single biggest reason a hand-drawn recreation "reads as nothing alike."
The 3DS system font is a generic sans; you **must** embed these two fonts as `.bcfnt` bitmap
fonts or the text can never match the mockups.

Both fonts are **SIL Open Font License 1.1 (OFL)** — free to use, bundle, and **embed** in an
application, including converting to another format (e.g. `.bcfnt`). Keep the `OFL.txt` from
each source in your repo, and don't sell the fonts on their own. That's the whole obligation.

---

## 1. Get the source .ttf/.otf (official, OFL)

**Space Grotesk** — display / UI. By Florian Karsten.
- GitHub (source, has `OFL.txt` + static TTFs): `https://github.com/floriankarsten/space-grotesk`
  → `fonts/ttf/SpaceGrotesk-{Regular,Medium,SemiBold,Bold}.ttf`
- Google Fonts: `https://fonts.google.com/specimen/Space+Grotesk`
- Weights this UI uses: **400 Regular, 500 Medium, 600 SemiBold, 700 Bold**.

**JetBrains Mono** — labels / codes / HUD. By JetBrains.
- GitHub: `https://github.com/JetBrains/JetBrainsMono`
  → `fonts/ttf/JetBrainsMono-{Regular,Medium,Bold}.ttf`
- Google Fonts: `https://fonts.google.com/specimen/JetBrains+Mono`
- Weights this UI uses: **400 Regular, 500 Medium, 700 Bold**. (600 in the web mock maps to
  Medium/Bold on device — see the table; ship 500 + 700 and pick the nearer one.)

> I can't manufacture font binaries for you, but these are the canonical OFL sources — clone the
> repo or download from Google Fonts and you have the exact files the mockups were rendered with.

---

## 2. Convert to 3DS bitmap fonts (.bcfnt)

devkitPro ships **`mkbcfnt`** (in `citro3d`/`3dstools`; get it via `sudo dkp-pacman -S 3dstools`
or it's already on your PATH with the standard toolchain). Bitmap fonts are rasterized at a
fixed pixel size, so **bake one `.bcfnt` per size you draw at** for the crispest result, or bake
one mid-size and scale with `C2D_DrawText`'s scale args (softer).

```sh
# one face, one size (repeat per size in the table below)
mkbcfnt SpaceGrotesk-Bold.ttf   -o romfs/fonts/sg_bold_14.bcfnt   -s 14
mkbcfnt SpaceGrotesk-SemiBold.ttf -o romfs/fonts/sg_semi_11.bcfnt -s 11
mkbcfnt JetBrainsMono-Medium.ttf -o romfs/fonts/mono_9.bcfnt      -s 9
# ...run `mkbcfnt --help` to confirm the flags on your toolchain version;
#    -s/--size is points→px at 96dpi, so -s 14 ≈ 14px. Tune ±1 to taste.
```

Load + draw:
```c
C2D_Font sgBold14 = C2D_FontLoad("romfs:/fonts/sg_bold_14.bcfnt");
C2D_TextBuf buf = C2D_TextBufNew(256);
C2D_Text t; C2D_TextFontParse(&t, sgBold14, buf, "3DGBA");
C2D_TextOptimize(&t);
C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, 1.0f, 1.0f, THEME_TEXT);  // scale 1.0 = native bcfnt size
```

If you prefer **one** font texture and runtime scaling, bake at the largest size you need
(≈34px for the wordmark) and pass scale = targetPx / bakedPx to `C2D_DrawText`.

**Fallback (only if you can't use bcfnt):** I can render a glyph-atlas PNG + metrics JSON you
blit yourself — ask and I'll add it. `.bcfnt` is the native, correct path though.

---

## 3. Exact type roles (device px — match these)

Sizes are in **real device pixels** (the mockups are authored at 0.9× and already converted
here). Letter-spacing matters for the mono labels — they are tracked out.

| Role | Font / weight | px | Tracking | Where |
|---|---|---|---|---|
| Wordmark "3DGBA" | Space Grotesk 700 | 33 | -0.5 | splash, header |
| Screen title (game name, pause) | Space Grotesk 600 | 14–16 | 0 | pause summary, in-game overlay |
| Button label | Space Grotesk 700 | 12–14 | 0 | all primary/secondary buttons |
| List row / body | Space Grotesk 500–600 | 11–13 | 0 | ROM list, cards, toggle labels |
| HUD game name | Space Grotesk 600 | 10–11 | 0 | in-game HUD bar |
| Section label (SCALE·TOP…) | JetBrains Mono 600 | 9 | +1.5 | pause tabs, select header |
| Game code (BPEE) / stat line | JetBrains Mono 500 | 8–9 | +0.5 | list, HUD fps/clock |
| HUD chips (3D / FOCUS / LINK) | JetBrains Mono 600 | 7–7.5 | +0.5 | in-game HUD |
| A / B badge | JetBrains Mono 600 | 8 | 0 | list, seat map |
| Tiny hint / license | JetBrains Mono 500 | 7.5–8 | +0.5–1.5 | footers, hints |
| PAUSED / divider caps | JetBrains Mono 700 | 9–10 | +3 | pause title, "TWO CORES" |

**Rule of thumb:** titles & anything the user taps = Space Grotesk; anything that looks like a
readout, code, tag, or label = JetBrains Mono, tracked out ~+0.5 to +3px. Never mix them the
other way — that inversion is what breaks the look.

The wordmark "3D" is JetBrains-blue `#3E86D6` with a ±1.5px chromatic split
(`+1.5px` light blue, `-1.5px` dark blue) behind it; "GBA" is `THEME_TEXT`.
