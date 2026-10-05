#!/usr/bin/env python3
"""Pack the four vendored tree PNGs into data/voxel_trees.bin (3DGBA, GPLv3).

Output: a 64x64 RGBA5551 texture, 8192 bytes, little-endian, texels in PICA200 order
(8x8 tiles row-major, Morton inside a tile; same index as CtrVideo_Texel in
source/voxel/ctr_shims_pure.c). Image row 0 is texel y = 0.

Atlas layout (read from source/voxel/voxel_tree.c and BrightenCrowns in ctr_voxel.c):
  (0,0)   32x36 tree_crown        (32,0)  32x32 tree_trunk
  (32,32) 16x32 tree_small_crown  (48,32) 16x16 tree_small_trunk
The art is MIT-licensed original art (assets/voxel/trees/, see source/voxel/NOTICE.md).
Usage: tools/voxel/pack_trees.py [out]   (default data/voxel_trees.bin)
"""
import struct
import sys
from pathlib import Path

from PIL import Image

DIM = 64
ROOT = Path(__file__).resolve().parents[2]
PLACE = [("tree_crown", 0, 0, 32, 36), ("tree_trunk", 32, 0, 32, 32),
         ("tree_small_crown", 32, 32, 16, 32), ("tree_small_trunk", 48, 32, 16, 16)]


def spread3(v):
    return (v & 1) | ((v & 2) << 1) | ((v & 4) << 2)


def texel(x, y):
    return ((y >> 3) * (DIM >> 3) + (x >> 3)) * 64 + (spread3(x & 7) | (spread3(y & 7) << 1))


def pack():
    out = [0] * (DIM * DIM)
    for name, px, py, w, h in PLACE:
        im = Image.open(ROOT / "assets/voxel/trees" / (name + ".png")).convert("RGBA")
        if im.size != (w, h):
            raise SystemExit("%s: expected %dx%d, got %s" % (name, w, h, im.size))
        for y in range(h):
            for x in range(w):
                r, g, b, a = im.getpixel((x, y))
                if a < 128:
                    continue
                out[texel(px + x, py + y)] = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1
    return struct.pack("<%dH" % len(out), *out)


if __name__ == "__main__":
    dest = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data/voxel_trees.bin"
    dest.write_bytes(pack())
    print("wrote %s (%d bytes)" % (dest, dest.stat().st_size))
