#!/usr/bin/env python3
"""ramp.py — a sharpness measure that is FAIR across sources of different alpha depth.

`crisp.py`'s distinct-colour count is a proof of "unresampled" only for A4 (4-bit) bcfnt
glyphs: an 8-bit antialiased *baked plate* can be perfectly 1:1 and still show 200 colours.
So for cross-source comparison use the shape of the alpha histogram instead:

    alpha(p) = clamp( <p - bg, ink - bg> / |ink - bg|^2 , 0, 1 )

    solid = fraction of lit pixels with alpha >= 0.75      <- ink that reads as ink
    mid   = fraction of lit pixels with 0.25 <= alpha < 0.75   <- the smear
    edge  = mean number of consecutive mid pixels per horizontal ink run

A hinted/bitmap-crisp run puts most of its mass at alpha ~ 1 and has short (<=1 px) ramps.
A resampled or heavily-antialiased run pushes mass into the middle and lengthens the ramp:
that is exactly what "blurry" looks like to the eye.

Usage: ramp.py IMG.png x,y,w,h [label]
"""
import sys
from collections import Counter
from PIL import Image


def alphas(im, box):
    x, y, w, h = box
    px = im.crop((x, y, x + w, y + h)).convert("RGB")
    cols = Counter(px.getdata())
    bg = cols.most_common(1)[0][0]
    ink = max(cols, key=lambda c: sum(abs(c[i] - bg[i]) for i in range(3)))
    d = [ink[i] - bg[i] for i in range(3)]
    n2 = sum(v * v for v in d) or 1
    p = px.load()
    a = [[max(0.0, min(1.0, sum((p[i, j][k] - bg[k]) * d[k] for k in range(3)) / n2))
          for i in range(w)] for j in range(h)]
    return bg, ink, a


def measure(im, box):
    bg, ink, a = alphas(im, box)
    lit = [v for row in a for v in row if v > 0.1]
    solid = sum(1 for v in lit if v >= 0.75)
    mid = sum(1 for v in lit if 0.25 <= v < 0.75)
    runs, cur = [], 0
    for row in a:
        for v in row:
            if 0.10 < v < 0.75:
                cur += 1
            elif cur:
                runs.append(cur); cur = 0
        if cur:
            runs.append(cur); cur = 0
    return {"bg": bg, "ink": ink, "lit": len(lit),
            "solid": 100.0 * solid / max(1, len(lit)),
            "mid": 100.0 * mid / max(1, len(lit)),
            "ramp": sum(runs) / max(1, len(runs))}


if __name__ == "__main__":
    im = Image.open(sys.argv[1]).convert("RGB")
    box = tuple(int(v) for v in sys.argv[2].split(","))
    lab = sys.argv[3] if len(sys.argv) > 3 else "run"
    m = measure(im, box)
    print("{:44s} lit={:5d}  solid>=75%={:5.1f}%  mid={:5.1f}%  mean-ramp={:.2f}px".format(
        lab, m["lit"], m["solid"], m["mid"], m["ramp"]))
