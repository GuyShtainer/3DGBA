#!/usr/bin/env python3
"""fbdump.py — pixel-EXACT screen capture over the gdb stub (phase 18; SPEC-crisp C1.8.2 / Q2).

WHY: `see` grabs the Azahar window with the OS screen-recorder. On this machine that returns a
uniform black rectangle for the GPU surface (three sessions now — 2026-08-08, -08-12 x2) while
`g_renderSeq` advances, so it proves nothing. And even when it works it is a 2.25x FILTERED
resize of a 400x240 screen, which can never answer "is this glyph 1:1?".

This reads libctru's own framebuffer out of emulated memory instead: zero OS permissions,
pixel-exact, and it is the SAME bytes the LCD would latch.

  gfxTopFramebuffers    u8*[2]   400x240   (max 2*400*240*3 = 576000)
  gfxBottomFramebuffers u8*[2]   320x240
  gfxFramebufferFormats u8[2]    0x01 = GSP_BGR8_OES, 3 bytes/px
  gfxCurBuf             u8[2]    which of the two buffers each screen last presented

Framebuffer layout (libctru gfx.c / GSP): COLUMN-major and rotated — the byte at
  (x * height + (height - 1 - y)) * 3
is the pixel at screen (x, y) counting y downward, stored B, G, R.

TEARING (C1.8.2.b) is real and is NOT solved here: a full top screen is 288 KB = 71 stub reads
and the app keeps rendering throughout. It does not matter on a STATIC screen (identical frames
=> identical bytes), and it wrecks an animated one. `--verify` re-reads a stripe at the end and
reports whether the screen moved under the dump, so a torn capture is never mistaken for a real
one. Use it on menus/pickers, not on the splash pulse or a running game.

(C1.8.2.a — "gdbio read corrupts 3-5 bytes at every 4096-byte boundary" — did NOT reproduce:
16384 bytes read out of `fnt_jbm_med_7_bin` matched data/fnt_jbm_med_7.bin byte for byte, twice.
The earlier observation was almost certainly this same tearing seen on live memory.)

Usage:
  tools/emutest/fbdump.py top out.png [--verify] [--rect x,y,w,h] [--zoom N]
"""
import argparse
import struct
import subprocess
import sys
import zlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(HERE, "run")

SCREENS = {"top": ("gfxTopFramebuffers", 400, 240, 0),
           "bottom": ("gfxBottomFramebuffers", 320, 240, 1)}


def gdb_read(sym_or_addr, n, out=None):
    cmd = [RUN, "gdbio", "read", sym_or_addr, str(n)]
    if out:
        cmd += ["--out", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("gdbio read failed: %s%s" % (r.stdout[-400:], r.stderr[-400:]))
    return r.stdout


def read_bytes(addr, n, tmp):
    gdb_read("0x%08x" % addr, n, out=tmp)
    with open(tmp, "rb") as f:
        return f.read()


def u32_at(dump_text, idx=0):
    # parse the hex dump gdbio prints: "0xADDR  b0 b1 ... |ascii|"
    vals = []
    for line in dump_text.strip().split("\n"):
        if "|" not in line:
            continue
        body = line.split("  ", 1)[1].split("|")[0]
        vals += [int(t, 16) for t in body.split()]
    return struct.unpack_from("<I", bytes(vals), idx * 4)[0]


def png_gray_or_rgb(path, w, h, rgb, zoom=1):
    raw = bytearray()
    for y in range(h):
        for _ in range(zoom):
            raw.append(0)
            row = rgb[y * w * 3:(y + 1) * w * 3]
            for x in range(w):
                px = row[x * 3:x * 3 + 3]
                for _ in range(zoom):
                    raw += px
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    hdr = struct.pack(">IIBBBBB", w * zoom, h * zoom, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", hdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("screen", choices=["top", "bottom"])
    ap.add_argument("out")
    ap.add_argument("--rect", default=None, help="x,y,w,h in DEVICE pixels (default whole screen)")
    ap.add_argument("--zoom", type=int, default=1)
    ap.add_argument("--verify", action="store_true", help="re-read a stripe and report movement")
    a = ap.parse_args()

    sym, W, H, si = SCREENS[a.screen]
    ptrs = gdb_read(sym, 8)
    base = u32_at(ptrs, 0)
    cur = gdb_read("gfxCurBuf", 4)
    curbuf = 0
    for line in cur.strip().split("\n"):
        if "|" in line:
            body = line.split("  ", 1)[1].split("|")[0].split()
            curbuf = int(body[si], 16)
            break
    if curbuf:
        base = u32_at(ptrs, 1)

    if a.rect:
        rx, ry, rw, rh = [int(v) for v in a.rect.split(",")]
    else:
        rx, ry, rw, rh = 0, 0, W, H
    rx = max(0, min(rx, W - 1)); ry = max(0, min(ry, H - 1))
    rw = max(1, min(rw, W - rx)); rh = max(1, min(rh, H - ry))

    tmp = "/tmp/.fbdump.bin"
    # Only the COLUMNS the rect needs; each column is H*3 contiguous bytes.
    colbytes = H * 3
    start = base + rx * colbytes
    n = rw * colbytes
    # C1.8.2.a, pinned down: a SINGLE `gdbio read` of more than ~64 KB slips by one byte
    # somewhere past 64 KB — proven by re-probing offset 200000 of a 288000-byte dump and
    # getting a 1-byte-rotated copy of the same repeating background pattern (the rotation is
    # what turns the reassembled image into coloured vertical stripes). Reads up to 32 KB are
    # byte-exact against ground truth (16 KB of fnt_jbm_med_7_bin matched data/ exactly), so
    # the dump is stitched from independent <=32 KB invocations and the seams are re-probed.
    SLICE = 32 * 1024
    data = bytearray()
    off = 0
    while off < n:
        k = min(SLICE, n - off)
        data += read_bytes(start + off, k, tmp)
        off += k
    data = bytes(data)
    # seam check: re-read 64 bytes at every slice boundary and require they still agree
    seams_bad = 0
    for off in range(SLICE, n, SLICE):
        again = read_bytes(start + off, 64, tmp)
        if again != data[off:off + 64]:
            seams_bad += 1
    if seams_bad:
        print("fbdump: WARNING — %d slice seam(s) disagree on re-read; the screen moved or the "
              "stub slipped. Treat this capture as UNPROVEN." % seams_bad)

    rgb = bytearray(rw * rh * 3)
    for cx in range(rw):
        col = data[cx * colbytes:(cx + 1) * colbytes]
        for yy in range(rh):
            y = ry + yy
            o = (H - 1 - y) * 3
            b, g, r = col[o], col[o + 1], col[o + 2]
            d = (yy * rw + cx) * 3
            rgb[d] = r; rgb[d + 1] = g; rgb[d + 2] = b

    png_gray_or_rgb(a.out, rw, rh, rgb, zoom=a.zoom)
    ncol = len(set(bytes(rgb[i:i + 3]) for i in range(0, len(rgb), 3)))
    print("fbdump: %s buf%d @0x%08x rect %d,%d %dx%d -> %s (zoom %d, %d distinct colours)"
          % (a.screen, curbuf, base, rx, ry, rw, rh, a.out, a.zoom, ncol))

    if a.verify:
        again = read_bytes(start, min(n, 4096), tmp)
        moved = sum(1 for i in range(len(again)) if again[i] != data[i])
        print("fbdump: verify — %d/%d bytes of the first stripe changed during the dump%s"
              % (moved, len(again), "" if moved == 0 else "  ** TORN: the screen is animating **"))
        if moved:
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
