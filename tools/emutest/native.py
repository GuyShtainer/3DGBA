#!/usr/bin/env python3
"""native.py — reconstruct the EXACT 3DS framebuffer out of a `see shot` capture.

Why this works (and why it is not a resample):
  * `azctl boot` is pinned to Layout/use_integer_scaling=false, so Azahar scales the
    emulated screen to the window's MaxRectangle: top 400x240 -> 900x540 (2.25x),
    bottom 320x240 -> 720x540 (2.25x).
  * This session additionally pins Layout/filter_mode=false = the NEAREST screen filter.
    A nearest upscale never invents a colour: every device pixel is a verbatim copy of one
    source pixel. So sampling the device pixel at the centre of each source pixel's cell
    recovers the source framebuffer BYTE-EXACTLY.
  * Verified by `--verify`: re-expanding the reconstruction with nearest reproduces the
    capture with 0 differing pixels.

Usage:  native.py cap.top.png out400.png [--screen top|bottom] [--verify]
"""
import sys
from PIL import Image

def reconstruct(cap, w, h):
    W, H = cap.size
    sx, sy = W / w, H / h
    src = cap.load()
    out = Image.new("RGB", (w, h))
    o = out.load()
    for y in range(h):
        dy = min(H - 1, int((y + 0.5) * sy))
        for x in range(w):
            dx = min(W - 1, int((x + 0.5) * sx))
            o[x, y] = src[dx, dy]
    return out

def verify(cap, nat):
    W, H = cap.size
    w, h = nat.size
    sx, sy = W / w, H / h
    c, n = cap.load(), nat.load()
    bad = 0
    for y in range(H):
        for x in range(W):
            if c[x, y] != n[min(w - 1, int(x / sx)), min(h - 1, int(y / sy))]:
                bad += 1
    return bad, W * H

if __name__ == "__main__":
    a = sys.argv[1:]
    src, dst = a[0], a[1]
    screen = "top"
    if "--screen" in a:
        screen = a[a.index("--screen") + 1]
    w, h = (400, 240) if screen == "top" else (320, 240)
    cap = Image.open(src).convert("RGB")
    nat = reconstruct(cap, w, h)
    nat.save(dst)
    msg = "native: {} {}x{} -> {} {}x{}".format(src, *cap.size, dst, w, h)
    if "--verify" in a:
        bad, tot = verify(cap, nat)
        msg += "  | verify: {}/{} device pixels differ from a nearest re-expansion".format(bad, tot)
    print(msg)
