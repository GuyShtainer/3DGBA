#!/usr/bin/env python3
"""sheet.py — captioned contact sheets as self-contained HTML (phase-16 slice E4).

Port of the PokeDNA harness sheet.py (gba-toolkit scratchpad, read 2026-08-08), carrying
its standing rule: every comparison ships as ONE artifact, captioned, with a marker
pointing at the thing being claimed — a wall of loose PNGs makes the reader hunt
(report §Rules; H6.5 adversarial-verification carry-over). Two deliberate changes:
  1. Output is HTML with base64-inlined images, not a PNG — the report rule "galleries as
     HTML with inlined images because raw attachments don't render" (SPEC-harness H2.5
     spelling: `sheet out.html img:caption...`). One file, openable anywhere, no loose
     assets.
  2. Size-agnostic cells — the original hardcoded 240x160 GBA shots; 3DGBA evidence mixes
     see.py top crops (400x240-scaled), bottom crops (320x240-scaled), zoom.py output and
     full-window captures, so every cell takes its image's natural size (PHASE.md
     deliverable: "sheet adapted to dual-screen geometry").

Markers are drawn INTO the image (PIL) before inlining, so the claim survives any viewer:
    ring=x,y      double ring (black outline + colour) centered on the claim
    rect=x,y,w,h  rectangle around the claim region
    good / bad    green / red cell border + caption (default: neutral grey)
Coordinates are in the IMAGE's own pixels (a see.py top crop is already scaled — measure
in the file you pass, e.g. via zoom.py).

CLI (venv shim):
    sheet out.html [--title T] [--zoom N] ITEM...
    ITEM = path[:caption[:MARK...]]   MARK in ring=x,y | rect=x,y,w,h | good | bad
    (caption may contain ':' — marker segments are recognized from the right)
Exit 0 ok, 2 operator error. Prints the output path (evidence goes in the run dir, H2.8).
"""

import argparse
import base64
import html
import io
import os
import sys

from PIL import Image, ImageDraw

GOOD = (90, 210, 130)
BAD = (240, 100, 100)
DIM = (150, 156, 170)

_CSS = """
body { background:#181a20; color:#eceef4; font:14px/1.45 ui-monospace,Menlo,monospace;
       margin:0; padding:16px; }
h1 { font-size:19px; margin:0 0 14px 2px; font-weight:600; }
.grid { display:flex; flex-wrap:wrap; gap:18px; }
figure { margin:0; }
figure img { display:block; image-rendering:pixelated; }
figcaption { margin-top:6px; max-width:480px; color:#969caa; white-space:pre-wrap; }
figure.good img { outline:2px solid rgb(90,210,130); }
figure.good figcaption { color:rgb(90,210,130); }
figure.bad img { outline:2px solid rgb(240,100,100); }
figure.bad figcaption { color:rgb(240,100,100); }
figure.neutral img { outline:2px solid #3a3f4c; }
"""


def parse_item(spec):
    """path[:caption[:MARK...]] -> dict. Marker segments are recognized from the RIGHT so
    captions may contain ':' (module doc)."""
    parts = spec.split(":")
    item = {"path": parts[0], "caption": "", "ring": None, "rect": None, "good": None}
    rest = parts[1:]
    while rest:
        seg = rest[-1].strip()
        try:
            if seg == "good":
                item["good"] = True
            elif seg == "bad":
                item["good"] = False
            elif seg.startswith("ring="):
                x, y = (int(v) for v in seg[5:].split(","))
                item["ring"] = (x, y)
            elif seg.startswith("rect="):
                x, y, w, h = (int(v) for v in seg[5:].split(","))
                item["rect"] = (x, y, w, h)
            else:
                break
        except ValueError:
            raise SystemExit("sheet: bad marker segment {!r} in {!r}".format(seg, spec))
        rest.pop()
    item["caption"] = ":".join(rest) or os.path.basename(item["path"])
    return item


def render_item(item, zoom=1):
    """-> (png_bytes, w, h) with markers drawn in (colour follows good/bad/neutral)."""
    im = Image.open(item["path"]).convert("RGB")
    if zoom > 1:
        im = im.resize((im.size[0] * zoom, im.size[1] * zoom), Image.NEAREST)
    colour = GOOD if item["good"] else (BAD if item["good"] is False else DIM)
    d = ImageDraw.Draw(im)
    if item["ring"]:
        x, y = item["ring"][0] * zoom, item["ring"][1] * zoom
        rr = max(9, im.size[1] // 24)
        for wdt, col in ((4, (0, 0, 0)), (2, colour)):
            d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=col, width=wdt)
    if item["rect"]:
        x, y, w, h = (v * zoom for v in item["rect"])
        d.rectangle([x, y, x + w, y + h], outline=colour, width=3)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return buf.getvalue(), im.size[0], im.size[1]


def build_sheet(out_path, items, title="", zoom=1):
    cells = []
    for it in items:
        png, w, h = render_item(it, zoom)
        cls = "good" if it["good"] else ("bad" if it["good"] is False else "neutral")
        cells.append(
            '<figure class="{cls}"><img width="{w}" height="{h}" alt="{alt}" '
            'src="data:image/png;base64,{b64}"><figcaption>{cap}</figcaption></figure>'
            .format(cls=cls, w=w, h=h, alt=html.escape(it["caption"], quote=True),
                    b64=base64.b64encode(png).decode("ascii"),
                    cap=html.escape(it["caption"])))
    doc = ("<!doctype html><meta charset=\"utf-8\"><title>{t}</title><style>{css}</style>"
           "<h1>{t}</h1><div class=\"grid\">{cells}</div>\n").format(
               t=html.escape(title or "emutest sheet"), css=_CSS, cells="".join(cells))
    d = os.path.dirname(os.path.abspath(out_path))
    if d:
        os.makedirs(d, exist_ok=True)
    with open(out_path, "w") as f:
        f.write(doc)
    return out_path


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="captioned HTML contact sheet with claim markers (H2.5/H6.5)")
    ap.add_argument("out", help="output .html")
    ap.add_argument("items", nargs="+",
                    metavar="path[:caption[:ring=x,y|rect=x,y,w,h|good|bad]]")
    ap.add_argument("--title", default="")
    ap.add_argument("--zoom", type=int, default=1,
                    help="nearest-neighbour pre-zoom of every cell (default 1)")
    a = ap.parse_args(argv)
    items = [parse_item(s) for s in a.items]
    for it in items:
        if not os.path.isfile(it["path"]):
            print("ERROR: image not found: {}".format(it["path"]), file=sys.stderr)
            return 2
    out = build_sheet(a.out, items, title=a.title, zoom=max(1, a.zoom))
    print("sheet: {} ({} cells)".format(out, len(items)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
