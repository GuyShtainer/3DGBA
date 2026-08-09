# sweep-verify — adversarial re-reproduction of the g1..g4 claims

Evidence root: `tools/emutest/runs/sweep-verify/`
Method: each claim re-driven from its own stated repro (or an equivalent, tighter movie),
captured with `see rec` / `see shot`, judged from the pixels only.

## Run V1 — picker (fixtures + `mkroms.py` dummies, settings reset to defaults, recent.bin removed)
movie `m ovie_picker_sweep.json` (copy of g1's) -> `picker.ctm`; `rec-picker/` 300 frames @4fps both screens.

### g1-F1 segmented control prints baked "Aspect-fit" over the live label — **CONFIRMED (critical)**
`rec-picker/bottom_00050.png` (2-Games selected) and `bottom_00090.png` (1-Game selected).
Zoom `zooms/z-seg.png`: the highlighted half is an unreadable black smear of two superimposed
strings; the unselected half ("1 Game" / "2 Games") is clean. Reproduced in BOTH modes.

### g1-F3 "settings · ZR" chip overprints the plate's baked footer hint — **CONFIRMED (major)**
`rec-picker/bottom_00050.png`: opaque chip bottom-left; hint reads "...ove ↑ · ⚡ linked = trade /
battle-ready" (2-game) and "...ull 3D + touch · link locally or online" (1-game,
`bottom_00090.png`). Chip right edge lands mid-word; same baseline.

### g1-F6 every ROM row draws the same cart chip — **CONFIRMED (polish)**
`rec-picker/top_00050.png`: 8 rows, 8 identical dark-purple chips with a green cap, for 8
different titles. Design ref `03-game-select.png` has per-title chip colours.

### g1-F4 splash button shows the sprite's baked "▶ START" — **CONFIRMED-as-described, MISJUDGED severity**
`rec-picker/bottom_00018.png`, zoom `zooms/z-splash-btn.png`: the pill reads a crisp "▶ START";
no second label visible anywhere. Design ref says "▶ TAP TO START". Legible, correct meaning,
purely a copy difference from the mockup — see verdict discussion.

## Run V2 — picker touch probe (`m_tapprobe.json`: taps ONLY, no Y)
`rec-tap/` 280 frames @4fps bottom.

### g1-F2 every picker tap resolves to "1 Game"; no picker touch target responds — **CONFIRMED (major)**
State before any touch: `rec-tap/bottom_00140.png` = **2 Games** (right half highlighted).
First touch of the movie is at (240,20) — the RIGHT half, i.e. "2 Games", the mode already
active — and the screen nevertheless flips to **1 Game** (`rec-tap/bottom_00180.png`).
The three later taps (80,20 = left half; 240,20 again; 40,225 = the "settings · ZR" chip;
160,100 = slot A card) produce NO pixel change at all through the end of the recording
(`rec-tap/bottom_00276.png`): still 1 Game, no settings screen, no slot filled.
Cross-check from V1: there the mode DID toggle 2->1->2 before any touch — that was the two
`tap Y` presses (Y toggles the mode), and the first touch there also landed on 1 Game.
Net: taps never select a segment by position, never open settings, never pick a slot.

## Run V3 — ROM-less boot + g3's `m1_tabtour.json` (`rec-tabs/`, 285 frames @3fps both)

### g1-F5 ROM-less start = dead black session, no empty state — **CONFIRMED (minor)**
`rec-tabs/top_00040.png` / `bottom_00040.png`: no picker, no "no games" copy; HUD reads
"+ gameA" / FOCUS / 3D / 59fps / 02:00 over pure black, bottom "+ gameB" + "tap screen · pause menu".

### g2-F1 chips/dots render as a PLUS (broken 3-slice) — **CONFIRMED (major)**
Pause top chip row `rec-tabs/top_00080.png`, zoom `zooms/z-chiprow.png`: all 8 chips are crosses
(tall centre block + a thin bar protruding left and right at mid height). "Touch Off" and "Co-op"
overflow onto the protruding bar. In-game identity mark: zoom `zooms/z-hud-dot.png` — a green
PLUS before "gameA", not a dot. NOTE the FOCUS chip on the same bar is a clean rectangle, so it
is the pill/dot helper specifically, not "every chip".

### g2-F2 in-game "3D" badge is dark navy on black — **CONFIRMED (major)**
`rec-tabs/top_00040.png`, zoom `zooms/z-hud-3d.png` at 8x: the two glyphs are barely resolvable
inside a blue outline; the adjacent "59fps" is crisp yellow.

### g3-D1 selected segment prints "Aspect-fit" over the real label — **CONFIRMED (critical)**
DISPLAY `rec-tabs/bottom_00110.png` (FILTER's selected chip reads "Aspect-fit" next to "Smooth";
HUD's selected chip is a smear), AUDIO `bottom_00135.png` (MIX MODE selected = "Aspect-fit" next
to "Mixed"/"Split"), ENHANCE `bottom_00160.png` (TILT selected = smear), TOUCH `bottom_00210.png`
(TOUCH MODE selected = "Aspect-fit" next to "Gamepad"/"Smart").

### g3-D2 rounded rects render as plus/cross — **CONFIRMED (major)**
`zooms/z-swatches.png`: the five GAMEPAD·COLOR swatches are unmistakable crosses; same family as
the chip row above.

### g3-D3 DISPLAY Swap/Skip row overprinted by the hint line — **CONFIRMED (major)**
`rec-tabs/bottom_00110.png`, zoom `zooms/z-display-bottom.png`: "Skip" is smeared into
"L/R tab  A select  B resume  (or tap)"; the toggle knob sits on the hint glyphs; both toggles
are clipped by the bottom screen edge.

### g3-D4 TOUCH EDGES row collides with the hint + baked caption cut off — **CONFIRMED (major)**
`rec-tabs/bottom_00210.png`, zoom `zooms/z-touch-bottom.png`: "Soft"/"Sharp" have the hint text
running through them letter-for-letter; a baked caption below is sliced by the screen edge.

### g3-D5 AUDIO volume rows: stray "A"/"B" glyph, no numeric level — **CONFIRMED (minor)**
`rec-tabs/bottom_00135.png`: a tiny "A"/"B" sits half on the bar's top edge next to the "-"
button; no percentage anywhere on the row (design `pause-tab-3-audio.png` shows 80 / 70).

### g3-D6 TOUCH tab missing explainer paragraph — **CONFIRMED (minor)**
`rec-tabs/bottom_00210.png`: ~55 px of empty plate between TOUCH MODE and the Preview buttons.

### g3-D7 right-edge scrollbar — **CONFIRMED with a correction (polish)**
`rec-tabs/bottom_00110.png`, zoom `zooms/z-rightedge.png` + a pixel profile: there IS a thumb, but
track=RGB~183 and thumb=~211 (Δ28/255) so the whole strip reads as one near-white bar, the
brightest thing on the screen. Extra defect the original missed: on the DISPLAY tab scrolled to
the TOP the thumb sits at the BOTTOM of the track (y 440-505 of 540).

### g3-D8 overlay row labels smaller/dimmer than baked labels — **CONFIRMED (polish)**
LINK `rec-tabs/bottom_00185.png`: "Link cable" / "Net link (loopback)" are large and bright while
the third row's label is a tiny dim all-caps "CO-OP". Same for TILT (`bottom_00160.png`) and
EDGES (`bottom_00210.png`).

## Run V4 — real ROMs (fixtures), dual session + pause + touch modes + lobby (`rec-rom/`, 390 frames @3fps)

### g2-F4 uninitialised white / grey texture at session start — **CONFIRMED (minor)**
`rec-rom/top_00133.png` = solid WHITE video rect, `bottom_00133.png` = flat MID-GREY rect, HUD
already drawn, 0fps, no spinner/copy. Correction to the original: the grey is the aspect-fit
video RECT, not the whole 320x240 screen.

### g4-F9 Smart mode: no mode chip + raw cyan debug readout on the footer line — **CONFIRMED (minor)**
`rec-rom/bottom_00215.png` / `bottom_00230.png`: bottom-left cyan "field p=9,4 key=-" in a
different font/colour, on the same baseline as the centred "START+SELECT · pause menu" hint;
header carries only "+ Pokemon FireRed", no touch-mode chip, no design guidance labels.

### g4-F8 wireless lobby bottom = "scanning..." dead end — **NOT REPRODUCED**
`rec-rom/bottom_00260.png`: the lobby bottom screen renders the FULL design entry screen —
"LOCAL · SAME ROOM (UDS)", gold "Host a session", "Scan for lobbies", "OR · OVER THE INTERNET",
"Connect online (soon)", "‹ back to menu". It stayed unchanged for the remaining ~45 s
(no further diff after frame 252). Same entry tap coordinate (201,91) as the original claim.
Lobby top screen (`top_00300.png`) also matches the design (seat cards + RTT/LOSS).

### g4-F5 pause TOP: live game composited over the pause panel — **NOT REPRODUCED as described**
`rec-rom/top_00190.png` (pause over the GAME FREAK screen): the game image is clearly DIMMED
(compare the live frame `top_00165.png`) and sits BEHIND the "Pokemon Emerald ⇄ Pokemon FireRed"
row, which is white and legible. There is no grey panel at all — the app dims the video rect
instead of drawing the design's hatched backdrop. Busy, but no z-order error visible.

### g2-F5 HUD bar as a translucent scrim over the game — pending (needs the aspect-fit scale; run V5)
With the default 1:1 scale the video is a 240x160 inset well BELOW the HUD bar (`top_00165.png`)
— nothing overlaps.

## Run V5 — Gamepad touch mode + aspect-fit scale (`setbin sTop=1 sBot=1 touch=1 padColor=0 padEdge=0`,
resume-prompt boot via the restored `recent.bin`, `rec-pad2/`)

### g4-F2 L / R drawn on top of the bottom-screen HUD text — **CONFIRMED (major)**
`rec-pad2/bottom_00120.png`, zoom `zooms/z-pad-lr.png`: the L box cuts "+ Pokemon FireRed" in half
at mid-glyph height; the R box swallows "17fps 02:00" (the digits are inside the button).

### g4-F3 START overlaps the footer hint and is clipped by the screen edge — **CONFIRMED (major)**
zoom `zooms/z-pad-start.png`: "START+SELECT · pause menu" runs through the button's lower half
("SELECT" is behind the fill); the button has a top border but no bottom border. The "menu" chip
bottom-right is also half off-screen.

### g4-F4 gamepad keys = overlapping squares with opaque corner blocks — **CONFIRMED (major)**
zoom `zooms/z-pad-dpad.png` at 4x: every key is a square with four black opaque corner blocks; the
four D-pad squares overlap so their borders cross each other.

### g2-F5 HUD bar is a translucent scrim over the game — **CONFIRMED (polish)** (needs a non-1:1 scale)
zoom `zooms/z-hud-scrim.png`: with SCALE = aspect-fit the video fills 400x240 and the bar is
translucent over it — a hard vertical seam at the video's left edge, black to the left, dimmed
green game pixels to the right. NOT visible at the default 1:1 scale (V4).

## Run V6 — padEdge=2 "Sharp", same padColor (`rec-edge/`) + tilt=3
### g4-F10 EDGES option makes no visible difference — **NOT REPRODUCED**
`rec-pad2/bottom_00120.png` (padEdge=0 "Round") vs `rec-edge/bottom_00120.png` (padEdge=2
"Sharp"), same padColor=0, same frame index, zooms `zooms/z-pad-dpad.png` vs
`zooms/z-pad-dpad-sharp.png`, diff `zooms/diff-edges.png` (4.67% of pixels): with Sharp the four
black opaque CORNER BLOCKS are gone; with Round they are present. The setting has a clear,
visible effect — and it is "Round" that renders the broken corner blobs of g4-F4.

## Run V7 — tilt=3, everything else default (the original's exact `setbin.py tilt=3`) (`rec-tilt/`)
### g4-F6 diorama tilt engages on intro/title screens (perspective warp) — **NOT REPRODUCED**
`rec-tilt/top_00120.png` (GAME FREAK logo) and `top_00220.png` (the grass intro pan) with the HUD
chip reading TILT3: the video is a plain axis-aligned rectangle — no trapezoid, no black corner
wedges. Same at aspect-fit scale (`rec-edge/top_00120.png`, `top_00200.png`, `top_00250.png`).
Honest caveat: my sessions never reached the overworld, so this says only that the intro screens
are NOT warped — it does not prove the tilt works where it should.

## Run V8 — theme = Daylight (`setbin theme=2`) (`rec-theme/`)
### g4-F1 non-Indigo theme goes dark-on-dark — **CONFIRMED (major)**
LINK tab `rec-theme/bottom_00135.png`: "Save state" / "Load state" / "Load .sav" are dark navy ink
on the dark navy Indigo plate, barely readable (crisp white in the Indigo run,
`rec-tabs/bottom_00185.png`). SESSION tab `bottom_00110.png`: "Change games" the same. Resume
prompt `top_00030.png`: the game names inside the A/B cards are dark-on-dark; bottom
`bottom_00030.png`: "Pick new games" and "Use defaults" likewise. Lobby `bottom_00160.png`:
"Scan for lobbies" and "Connect online (soon)" likewise.

### g4-F7 the app is half dark, half light (lobby screen pair) — **PARTLY CONFIRMED (major/minor)**
CONFIRMED for the in-game screen: `rec-theme/top_00090.png` — the letterbox around the GBA frame
is near-white/beige while the HUD bar stays dark grey with dim orange text, and the pause menu is
still dark navy.
NOT REPRODUCED for the lobby: at the same theme both lobby screens are the dark Indigo plate
(`rec-theme/top_00160.png` + `bottom_00160.png`) — no white bottom screen, no split pair.

## g2-F3 single-game bottom "CONTROLLER" screen is empty — **NOT REPRODUCED (could not reach)**
Four attempts (`rec-single`, `rec-single2`, `rec-single3`, `rec-single4`) with gameMode=1 and with
settings.bin deleted: the picker fills the slot and enables START but no d-pad sequence
(A / A,A / A,DOWN,A / A,A,DOWN,A / repeated A) ever starts the session, and the console START
button starts a 2-game DEFAULTS session instead (`rec-single2/top_00150.png`). No 1-Game session
was ever on screen, so the claim cannot be judged from my own pixels.

### NEW (not in any claim, medium confidence): the ROM picker is close to unstartable
Across V2/V5/V8 and the four single-game attempts, taps never actuate ANY picker control (g1-F2)
and the d-pad/A path started a session in only ONE of six runs (V4). `rec-single4/bottom_00200.png`
shows both slots filled and START enabled after A,A,DOWN,A,A — and no session.
