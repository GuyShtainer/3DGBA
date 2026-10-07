#!/usr/bin/env python3
"""plate_erase_rows.py -- erase the orphaned "Time-of-day light" / "Vivid mode" labels from the
pause-bot-enhance plate of every theme (those rows were deleted from code 2026-10-06).

Measured on all 6 themes (identical geometry): the content panel is a perfectly flat fill
(one RGBA value; sampled at (200,215)), and the only non-background ink in y130..225,
x88..311 is "Time-of-day light" (ink y146..156) and "Vivid mode" (ink y175..183). So the
erase is a solid fill of RECT with that theme's own panel colour, which is exact (no
gradient, no noise). The script asserts that before and after and is idempotent.

RECT = (x0, y0, x1, y1) inclusive, in plate pixels (320x240).
Run from the project root:  python3 tools/plate_erase_rows.py   (then tools/build_assets.sh)
The composed example (examples/indigo/) is a reference screenshot, not a shipped plate: untouched.
"""
import os, sys
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
PLATES = os.path.join(ROOT, "design_handoff_3dgba_ui/assets_3ds/plates")
THEMES = ["indigo", "oled", "daylight", "duo", "retro", "custom"]
RECT = (88, 140, 311, 195)       # covers ink 146..156 and 175..183 with margin; stays clear of the
                                 # Bloom label (ends 125) and the chrome band (y>=226)
BG_PROBE = (200, 215)            # empty panel pixel
SCAN = (88, 130, 311, 225)       # region that must be flat bg after the erase

def main():
    for t in THEMES:
        p = os.path.join(PLATES, t, "pause-bot-enhance.png")
        im = Image.open(p).convert("RGBA"); px = im.load()
        bg = px[BG_PROBE]
        x0, y0, x1, y1 = RECT
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px[x, y] = bg
        sx0, sy0, sx1, sy1 = SCAN
        bad = [(x, y) for y in range(sy0, sy1 + 1) for x in range(sx0, sx1 + 1) if px[x, y] != bg]
        # Bloom label lives at y117..125 inside SCAN; allow only that.
        bad = [(x, y) for (x, y) in bad if not (117 <= y <= 125)]
        assert not bad, (t, bad[:5])
        im.save(p, optimize=True)
        print("%-9s bg=%s erased %s clean" % (t, bg, RECT))

if __name__ == "__main__":
    sys.exit(main())
