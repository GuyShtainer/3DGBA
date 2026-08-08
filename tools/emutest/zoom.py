#!/usr/bin/env python3
"""zoom.py — crop + nearest-neighbour upscale a region of a screenshot (phase-16 E4).

Port of the PokeDNA harness zoom.py (gba-toolkit scratchpad, read 2026-08-08).
Nearest-neighbour so a pixel stays a pixel: this is for READING — 5x7 GBA glyphs, the
3DGBA HUD text, OAM rows — not for pretty pictures (the PokeDNA "4-8x zoom crops for 5x7
glyphs" rule, report §SEE). Size-agnostic: works on see.py's top (400x240-scaled) and
bottom (320x240-scaled) crops alike.

CLI (SPEC-harness H2.5 spelling):
    zoom in.png out.png --rect x,y,w,h [--scale 4]
Rect is in the INPUT image's pixels; --scale defaults to 4 (H2.5 range 4..8 is advisory,
any >= 1 accepted). Exit 0 ok, 2 operator error (bad rect / missing file).
"""

import argparse
import os
import sys

from PIL import Image


def zoom(in_path, out_path, rect, scale):
    x, y, w, h = rect
    im = Image.open(in_path).convert("RGB")
    if w <= 0 or h <= 0:
        raise ValueError("empty rect {}x{}".format(w, h))
    if x < 0 or y < 0 or x + w > im.size[0] or y + h > im.size[1]:
        raise ValueError("rect {},{},{},{} outside image {}x{}".format(
            x, y, w, h, im.size[0], im.size[1]))
    out = im.crop((x, y, x + w, y + h)).resize((w * scale, h * scale), Image.NEAREST)
    d = os.path.dirname(os.path.abspath(out_path))
    if d:
        os.makedirs(d, exist_ok=True)
    out.save(out_path)
    return out


def _rect(spec):
    try:
        x, y, w, h = (int(v) for v in spec.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError("--rect wants x,y,w,h (got {!r})".format(spec))
    return (x, y, w, h)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="crop + nearest-neighbour zoom for reading glyphs (H2.5)")
    ap.add_argument("infile")
    ap.add_argument("outfile")
    ap.add_argument("--rect", type=_rect, required=True, metavar="x,y,w,h",
                    help="crop rect in input-image pixels")
    ap.add_argument("--scale", type=int, default=4,
                    help="integer upscale factor (default 4; 4-8 is the useful range)")
    a = ap.parse_args(argv)
    if a.scale < 1:
        print("ERROR: --scale must be >= 1", file=sys.stderr)
        return 2
    try:
        out = zoom(a.infile, a.outfile, a.rect, a.scale)
    except (FileNotFoundError, ValueError) as exc:
        print("ERROR: {}".format(exc), file=sys.stderr)
        return 2
    print("{} {}x{}".format(a.outfile, out.size[0], out.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
