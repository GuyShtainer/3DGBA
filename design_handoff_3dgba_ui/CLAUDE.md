# CLAUDE.md — 3DGBA UI redesign (read me first)

You are implementing a **UI redesign** of **3DGBA**, a working **C / Nintendo 3DS** app
(devkitPro / libctru + **citro2d / citro3d**) that runs two Game Boy Advance games at once.
This folder is the complete design handoff: **device-native art at exact hardware pixels**, the
screen-by-screen spec, reference screenshots, and a clickable prototype.

**Do NOT touch** emulation, threading, the link/net drivers, or the HD-2D render passes. You are
only changing **UI drawing + the theme system + settings/persistence + the pause-menu structure
+ the splash + the app icon.**

---

## Read in this order

1. **`README.md`** — the screen-by-screen spec. Maps every screen to the exact C functions that
   draw it (`run_session`, `run_splash`, `rompicker_run`, `wireless_lobby_run` in `source/main.c`),
   lists the 6 theme palettes + fixed role colors, and gives the suggested build order. **This is
   the "what" and "where."**
2. **`assets_3ds/README-IMPLEMENTATION.md`** — how to turn the art into on-screen pixels: the
   per-frame draw model, `tex3ds` / `C2D_DrawImageAt` / `C2D_SpriteSheet` snippets, 9-slice
   notes, and the gotchas that make a hand-drawn version "read as nothing alike." **This is the
   "how."**
3. **`assets_3ds/fonts/FONTS.md`** — the single highest-impact item. Space Grotesk + JetBrains
   Mono (OFL), where to get them, how to bake `.bcfnt`, and exact device-px sizes per text role.
4. **`assets_3ds/GALLERY.html`** — open in a browser to see all 24 screens composed + the widget
   sheet + compositing proofs. **`screenshots/`** holds the original spec composites — the bar is
   1:1 with these.

---

## The one rule that makes it match: composite, don't screenshot

A live emulator UI can't be a frozen picture. Every frame, per screen, draw **back-to-front**:

```
1. GAME VIDEO   → blit the emulator frame into the "video" rect (aspect-fit).
2. CHROME PLATE → C2D_DrawImageAt(plate, 0, 0, ...). The plate is the exact static skin; its
                  video area is a transparent hole, so the video shows through.
3. DYNAMIC      → on top of the plate, draw the live stuff from the manifest:
                  • text  (names, fps, clock, RTT, volume %) in the FONTS.md font/size
                  • widgets (buttons, segmented, toggles, HUD chips, dots, battery) from the sheet
                  • lists / seat cards / the virtual gamepad
```

`assets_3ds/plates/manifests.json` tells you, for every screen, the exact x/y/w/h of each dynamic
rectangle and which widget/text goes there. Geometry is identical across all 6 themes — only the
plate PNG colors change.

## Where the art is (all 1× device px, straight alpha, PNG)

- `assets_3ds/plates/<theme>/` — 24 chrome plates per theme (top 400×240 / bottom 320×240).
- `assets_3ds/widgets/<theme>/` — 42 individual sprites; `assets_3ds/widgets/atlas/` — one atlas
  PNG + JSON coords per theme.
- `assets_3ds/plates/manifests.json` — dynamic-region map. `assets_3ds/index.json` — machine
  index of themes / widget ids / plate units.
- Themes: `indigo` (shipped default), `oled`, `daylight`, `duo`, `retro`, `custom`.
- App icon already provided: `app_icon/icon-48.png` (drop in as `icon.png`) + `icon-512.png`.

Pipeline for citro2d: run the PNGs through `tex3ds` (NPOT-safe, keeps alpha with `-f rgba8`) to
build `.t3x` textures / spritesheets, load with `C2D_SpriteSheetLoad`, blit with
`C2D_DrawImageAt`. Full commands in the implementation guide.

---

## Suggested order (from README.md)

1. `theme.h` → `Theme` struct + `g_theme[6]`; route `THEME_*` through the active theme; add the
   custom generator. 2. `Settings` → add `theme`/custom hues/`gameMode`/`padColor`/`padEdge`;
   load+save with the tolerate-older-files length check. 3. Pause menu → tab rail + 6 tabs over
   the existing `MENU_ITEMS` actions + top-screen summary. 4. Splash + app icon (the two the last
   pass skipped). 5. Resume prompt + game-select 1-/2-game. 6. Touch gamepad color/edge tokens.
   7. Wireless lobby seat-map + local/online.

## Hard rules

- **Match the reference** (`screenshots/` + `GALLERY.html`) 1:1 — colors, hierarchy, spacing,
  copy. Re-fit only *geometry* to the real panels; the manifests already give device-px rects.
- **Embed the fonts.** The system font will never match. Bake `.bcfnt` per FONTS.md.
- **Keep the draw order** (video → plate → dynamic) and export textures with **straight alpha**
  (`-f rgba8`), or the transparent holes and translucent HUD/gamepad break.
- **Fixed role colors** stay constant across themes: Game A `#63B23C`, Game B `#3E86D6`, 3D badge
  `#a9d4ff`/`#3E86D6`, quit/error `#E4462E`/`#E4796B`. Everything else comes from the active theme.
- Don't modify emulation / threading / drivers / HD-2D passes. Preserve existing hardware controls
  (Y focus, X swap, ZR/ZL scale/filter, START+SELECT pause, 3D slider depth).

## Done when

Every screen (splash + icon included) matches its reference at 1:1, all 6 themes selectable, the
6-tab pause menu drives the same `MENU_ITEMS` actions, settings persist, and no emulator/perf
regressions.
