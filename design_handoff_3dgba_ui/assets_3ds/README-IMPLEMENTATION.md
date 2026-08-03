# 3DGBA — Device-native art pack & implementation guide

Everything here is **real, sliced art** exported from the approved prototype at **exact device
pixels** — not spec composites. It is built to be drawn *over* live content, because a running
emulator UI can't be a frozen picture. Follow the draw model below and the screens will look
like the mockups on hardware.

> **The #1 rule:** the game video, the ROM list, focused/selected states, and live values
> (fps, clock, battery, RTT, toggle positions, names) are **drawn in code over the art**. The
> art gives you the exact *skin* (backgrounds, panels, frames, dividers, tab rail, HUD bars,
> splash logo) and the exact *widget shapes* (buttons, chips, toggles, dots…). You composite.

---

## Folder map

```
assets_3ds/
├─ README-IMPLEMENTATION.md   ← you are here
├─ fonts/FONTS.md             ← Space Grotesk + JetBrains Mono: sourcing, OFL, mkbcfnt, sizes
├─ plates/
│  ├─ manifests.json          ← every dynamic rectangle per screen (x/y/w/h + what/which widget)
│  └─ indigo/                  ← 24 chrome plates, exact device px, transparent where video goes
│     ├─ splash-top.png (400×240)  splash-bot.png (320×240)
│     ├─ resume-top/-bot  select-dual-top/-bot  select-single-top/-bot
│     ├─ play-top.png     ← shared: in-game / gamepad / smart TOP (game A + HUD bar)
│     ├─ ingame-dual-bot  controller-bot  pad-bot  smart-bot
│     ├─ pause-top  pause-bot-{session,display,audio,enhance,link,touch}
│     └─ wless-idle-top/-bot  wless-conn-top/-bot
├─ widgets/
│  ├─ indigo/                  ← 42 individual transparent sprites (see catalog below)
│  ├─ atlas/widgets-indigo.png + .json   ← same sprites packed + coordinates
│  └─ _sizes.json             ← device px size of every widget
└─ examples/indigo/           ← the 24 screens fully composed = the target look ("together")
```

Themes: **Indigo (shipped default) is complete.** The other five (OLED, Daylight, Duo, Retro,
Custom) reuse the *identical geometry* — only fill colors change — and drop into `plates/<theme>/`
and `widgets/<theme>/` as they're generated. `manifests.json`, `_sizes.json`, and the atlas
layout are theme-independent, so you write your loader once.

---

## The per-frame draw model (do this every frame)

Back-to-front, per screen:

```
1. GAME VIDEO   — blit the emulator frame into the video rect (aspect-fit). See manifest
                  "type":"video" rects. On plates that have video, those pixels are alpha 0.
2. CHROME PLATE — C2D_DrawImageAt(plate, 0, 0, depth) over the whole screen. Its transparent
                  holes let the video show; its opaque parts (HUD bars, panels, rail) draw on top.
3. DYNAMIC      — draw everything in that screen's manifest entry, on top of the plate:
                  • text (names, fps, clock, RTT, volume %) in the right font/size (FONTS.md)
                  • widgets (buttons, segmented pills, toggles, chips, dots, battery) from the
                    widget sheet, at the manifest x/y
                  • lists / seat cards / the virtual gamepad, laid out per manifest
```

Screens with **no** video (splash, resume, select, pause, wireless, single-controller) have a
fully opaque plate — draw plate first, then dynamic items on top; no video step.

---

## Using the plates (citro2d)

Each plate is one PNG at the physical screen size (top 400×240, bottom 320×240). Build a
texture per plate and blit it at the origin.

```sh
# NPOT-safe: tex3ds packs into a POT atlas and stores the UVs, so 400×240 / 320×240 are fine.
tex3ds -i plates/indigo/play-top.png -o romfs/gfx/play-top.t3x -f rgba8 -z auto
```
```c
C2D_SpriteSheet ss = C2D_SpriteSheetLoad("romfs:/gfx/play-top.t3x");
C2D_Image plate = C2D_SpriteSheetGetImage(ss, 0);
// ... each frame, top screen, after drawing the game video:
C2D_DrawImageAt(plate, 0.0f, 0.0f, 0.5f, NULL, 1.0f, 1.0f);
```

- **`-f rgba8`** keeps straight (unpremultiplied) alpha — required for the transparent holes.
- `play-top.png` is shared by in-game (dual & single), gamepad, and smart tops.
- Pause plates: the content panel scrolls; `pause-bot-*` shows the visible frame + section
  labels. Draw the active-tab highlight (panel2 fill + 2px accent left border) yourself over the
  rail per the current tab — the rail *labels* are baked, the highlight is not.

## Using the widgets (citro2d)

Two ways, both provided:

**A. Let tex3ds pack the individual PNGs (recommended for citro2d).** Point a `.t3s` at
`widgets/indigo/*.png`; tex3ds builds a spritesheet and gives you one `C2D_Image` per name.
```c
C2D_SpriteSheet w = C2D_SpriteSheetLoad("romfs:/gfx/widgets-indigo.t3x");
C2D_Image btnPrimary = C2D_SpriteSheetGetImage(w, IDX_btn_primary);
C2D_DrawImageAt(btnPrimary, x, y, 0.6f, NULL, 1.0f, 1.0f);
```
**B. Use the prebuilt atlas.** `widgets/atlas/widgets-indigo.png` + `.json` give `{id:{x,y,w,h}}`
for a manual subrect blit or a non-citro2d pipeline.

### Widget catalog (42) — `widgets/indigo/`
- **Buttons** (label is drawn in code on top; sprite = background): `btn-primary` (gold),
  `btn-secondary` (panel2), `btn-ghost` (transparent), `btn-destructive` (red), `btn-accent-outline`
  — each has a `-focus` variant (D-pad focus = 2px ring). For arbitrary widths use the **9-slice
  fills** `fill-primary-r8`, `fill-secondary-r8`, `fill-panel-r9`, `fill-card-r8` (slice inset =
  corner radius, 8–9px; stretch the middle, keep the corners).
- **Toggle:** `toggle-on` (gold track, knob right), `toggle-off` (line track, knob left).
- **Segmented:** `seg-track` (empty container), `seg-active` (the gold highlight — move it over
  the selected cell, draw labels in code), `seg-example` (a full 3-up for reference).
- **HUD chips:** `chip-3d`, `chip-focus`, `chip-link`, `chip-3dtop`.
- **Badges / pills:** `badge-a` (green A), `badge-b` (blue B); feature pills `pill-3d`,
  `pill-dof`, `pill-green`, `pill-dim`.
- **Status dots:** `dot-green` (running/host), `dot-blue` (slot B/filled), `dot-gold` (you),
  `dot-red` (error), `dot-dim` (open/idle).
- **Battery:** `battery-full` / `battery-mid` / `battery-low` (draw the one matching charge, or
  use the outline + a filled rect you size in code).
- **Cartridge chips:** `cart-blank` (list slot, panel2), `cart-tinted` (with per-game top stripe
  — recolor the stripe in code per ROM), `cart-solid-a/-b` (filled slot icons).
- **Brand:** `logo` (the stacked-cartridge glyph), `wordmark` ("3DGBA").

---

## Reading `plates/manifests.json`

```jsonc
"play-top": { "face":"top", "size":[400,240], "dynamic":[
  { "label":"game A video (aspect-fit)", "type":"video",  "x":20,"y":27,"w":360,"h":213 },
  { "label":"FOCUS chip",  "widget":"chip-focus", "x":120,"y":7,"w":35,"h":12 },
  { "label":"fps A",       "type":"text",  "x":307,"y":7,"w":27,"h":12 },
  ...
]}
```
- `type:"video"` → transparent hole; blit the emulator frame here (step 1).
- `type:"text"` → draw the live string here in the font from FONTS.md (right-align fps/clock/
  battery cluster; left-align names).
- `widget:"…"` → blit that widget sprite here (or its active/focused variant per state).
- `type:"list" / "cards" / "widgets"` → you lay out rows/cards/controls in this box.
- Rects are **device px, theme-independent**. Only the plate PNG changes per theme.

---

## Themes

Indigo is baked into `plates/indigo/` + `widgets/indigo/`. For the other five, either drop in
the matching `plates/<theme>/` + `widgets/<theme>/` folders (same filenames), **or** recolor at
runtime from your `Theme` tokens — the geometry never changes. Fixed **role colors** stay
constant across all themes: Game A green `#63B23C`, Game B blue `#3E86D6`, 3D badge `#a9d4ff`
on `#3E86D6`, quit/error `#E4462E`/`#E4796B`. The gold accent, panels, text, dim, etc. come from
the active theme (`theme.h`). Token table lives in the handoff `README.md`.

---

## Gotchas that break the "same look" (from the notes on the last pass)

1. **Font roles, not just the font.** Titles/buttons = Space Grotesk; codes/labels/HUD =
   JetBrains Mono, **tracked out +0.5…+3px**. Swapping them is the biggest tell. See FONTS.md.
2. **Widget corner radii are specific:** buttons 8–9px, chips 3px, pills 999 (full), toggle
   track 999, cards 8px, cartridge `2 2 3 3`. Gamepad radius follows the edge setting
   (Round 11 / Soft 5 / Sharp 1; buttons 50% / 32% / 22%).
3. **HUD bar is semi-transparent black** (`rgba(0,0,0,.5)`), baked into the plate — it darkens
   the video under it. Don't redraw it opaque.
4. **Draw order.** Video → plate → dynamic. If the plate goes under the video, the holes vanish.
5. **Straight alpha.** Export textures `-f rgba8`; premultiplying muddies the AA edges and the
   translucent gamepad/HUD.
6. **Accent-outline buttons** (START — LINKED, Start trade) are a ~12%-accent fill + 1px accent
   border + accent text — not a solid gold button.
7. The wordmark "3D" carries a ±1.5px blue chromatic split; the logo dot has a faint outer ring.

---

## Regenerating / extending

The exporter harnesses that produced this pack live at project root: `_harness.dc.html`
(plates + composed) and `_widgets.dc.html` (sprites). They're driven by `window.__set(...)` /
`window.__W(id,theme)` and render at exact device px; assets are captured on black+white and
alpha-recovered, then downscaled from 2×. Re-run them to add a theme, tweak a widget, or add a
screen — geometry and this guide stay valid.
