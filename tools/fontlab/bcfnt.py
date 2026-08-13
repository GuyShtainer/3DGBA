#!/usr/bin/env python3
"""fontlab/bcfnt.py — read a .bcfnt, decode its A4 glyph sheets, and reproduce EXACTLY what
the PICA200 puts on screen for a citro2d text draw.

Why this exists (phase 18, SPEC-crisp C5.2): the emulator's window capture is a filtered
2.25x resize of the framebuffer, so it can never answer "is this glyph 1:1?". This tool
works on the same bytes the ELF embeds, applies citro2d's own scale arithmetic and the
GPU's own sampler, and reports objective numbers. It is the primary pixel evidence.

Format references (verified against data/fnt_*.bin this session, see selftest()):
  CFNT header : 'CFNT' magic, u16 bom, u16 hdrSize, u32 version, u32 fileSize, u32 nBlocks
  FINF        : +8 fontType u8, +9 lineFeed u8, +10 alterCharIndex u16,
                +12 default (left,glyphWidth,charWidth) u8[3], +15 encoding u8,
                +16 tglpOff u32, +20 cwdhOff u32, +24 cmapOff u32,
                +28 height u8, +29 width u8, +30 ascent u8
  TGLP        : +8 cellW u8, +9 cellH u8, +10 baselinePos u8, +11 maxCharWidth u8,
                +12 sheetSize u32, +16 nSheets u16, +18 fmt u16, +20 nColumns u16,
                +22 nRows u16, +24 sheetW u16, +26 sheetH u16, +28 dataOff u32
  CWDH        : +8 startIndex u16, +10 endIndex u16, +12 nextOff u32, then
                (left i8, glyphWidth u8, charWidth u8) per glyph
  CMAP        : +8 codeBegin u16, +10 codeEnd u16, +12 mappingMethod u16, +14 pad u16,
                +16 nextOff u32, then method-specific payload
                (0 = DIRECT: u16 offset; 1 = TABLE: u16[n]; 2 = SCAN: u16 n + (u16,u16)*n)

The citro2d contract (disassembled from the installed libcitro2d.a, SPEC-crisp C1.1):
  font->textScale = 30.0 / cellHeight
  C2D_DrawText multiplies the caller's scale by textScale before laying glyphs out
  => on-screen texel scale = callerScale * 30 / cellH
  and C2D_TextGetDimensions(1,1) returns ceil(lineFeed * 30 / cellH)
  => with assets.c's sc = px / s_native, texel scale == px / lineFeed  (exactly, once
     s_native is the un-ceil'd lineFeed*30/cellH).
"""
import struct
import sys

# ---------------------------------------------------------------- container


def _u8(b, o):
    return b[o]


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _i8(b, o):
    return struct.unpack_from("<b", b, o)[0]


class Glyph:
    __slots__ = ("index", "left", "glyph_w", "char_w")

    def __init__(self, index, left, glyph_w, char_w):
        self.index = index
        self.left = left
        self.glyph_w = glyph_w
        self.char_w = char_w


class Bcfnt:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.raw = f.read()
        b = self.raw
        if b[0:4] != b"CFNT":
            raise ValueError("%s: not a CFNT (magic %r)" % (path, b[0:4]))
        self.path = path
        self.file_size = _u32(b, 12)

        finf = b.find(b"FINF")
        tglp = b.find(b"TGLP")
        if finf < 0 or tglp < 0:
            raise ValueError("%s: missing FINF/TGLP" % path)
        self.finf_off, self.tglp_off = finf, tglp

        self.line_feed = _u8(b, finf + 9)
        self.default_char_w = _u8(b, finf + 14)
        self.height = _u8(b, finf + 28)
        self.width = _u8(b, finf + 29)
        self.ascent = _u8(b, finf + 30)

        self.cell_w = _u8(b, tglp + 8)
        self.cell_h = _u8(b, tglp + 9)
        self.baseline = _u8(b, tglp + 10)
        self.max_char_w = _u8(b, tglp + 11)
        self.sheet_size = _u32(b, tglp + 12)
        self.n_sheets = _u16(b, tglp + 16)
        self.fmt = _u16(b, tglp + 18)
        self.n_cols = _u16(b, tglp + 20)
        self.n_rows = _u16(b, tglp + 22)
        self.sheet_w = _u16(b, tglp + 24)
        self.sheet_h = _u16(b, tglp + 26)
        self.sheet_data_off = _u32(b, tglp + 28)

        self._widths = {}
        self._cmap = {}
        self._read_cwdh()
        self._read_cmap()

    # ---- widths (CWDH chain) ----
    def _read_cwdh(self):
        b = self.raw
        off = self.raw.find(b"CWDH")
        seen = set()
        while off > 0 and off not in seen:
            seen.add(off)
            start = _u16(b, off + 8)
            end = _u16(b, off + 10)
            nxt = _u32(b, off + 12)
            p = off + 16
            for gi in range(start, end + 1):
                self._widths[gi] = Glyph(gi, _i8(b, p), _u8(b, p + 1), _u8(b, p + 2))
                p += 3
            # nextOffset points 8 bytes past the next block header start
            off = (nxt - 8) if nxt else 0

    # ---- codepoint -> glyph index (CMAP chain) ----
    def _read_cmap(self):
        b = self.raw
        off = self.raw.find(b"CMAP")
        seen = set()
        while off > 0 and off not in seen:
            seen.add(off)
            lo = _u16(b, off + 8)
            hi = _u16(b, off + 10)
            method = _u16(b, off + 12)
            nxt = _u32(b, off + 16)
            p = off + 20
            if method == 0:  # DIRECT
                base = _u16(b, p)
                for cp in range(lo, hi + 1):
                    gi = base + (cp - lo)
                    if gi != 0xFFFF:
                        self._cmap[cp] = gi
            elif method == 1:  # TABLE
                for cp in range(lo, hi + 1):
                    gi = _u16(b, p)
                    p += 2
                    if gi != 0xFFFF:
                        self._cmap[cp] = gi
            elif method == 2:  # SCAN
                n = _u16(b, p)
                p += 2
                for _ in range(n):
                    cp = _u16(b, p)
                    gi = _u16(b, p + 2)
                    p += 4
                    if gi != 0xFFFF:
                        self._cmap[cp] = gi
            off = (nxt - 8) if nxt else 0

    # ---- derived: the numbers assets.c / citro2d care about ----
    @property
    def text_scale(self):
        """citro2d's font->textScale."""
        return 30.0 / float(self.cell_h)

    @property
    def s_native_exact(self):
        """What s_native SHOULD be: lineFeed * 30 / cellH (no ceil)."""
        return self.line_feed * 30.0 / float(self.cell_h)

    @property
    def s_native_shipped(self):
        """What C2D_TextGetDimensions(1,1) returns today: ceil(lineFeed*30/cellH)."""
        import math

        return float(math.ceil(self.s_native_exact - 1e-6))

    def texel_scale(self, px, s_native=None):
        """On-screen texel scale for assets_text(px)."""
        sn = self.s_native_shipped if s_native is None else s_native
        return (px / sn) * self.text_scale

    def glyph_index(self, ch):
        return self._cmap.get(ord(ch), self._cmap.get(0xFFFD, 0))

    def has_glyph(self, cp):
        gi = self._cmap.get(cp)
        return gi is not None and gi != 0

    def width_info(self, gi):
        return self._widths.get(gi, Glyph(gi, 0, self.cell_w, self.cell_w))

    # ---- A4 sheet decode ----
    @staticmethod
    def morton(x, y):
        return ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2)
                | ((x & 4) << 2) | ((y & 4) << 3))

    def sheet_bytes(self, si):
        # sheetDataOffset is an ABSOLUTE file offset (verified: sg_med TGLP@52 sectionSize=32
        # -> the 32-byte header only; dataOff=128 and CWDH lands at exactly 128+524288).
        base = self.sheet_data_off + si * self.sheet_size
        return self.raw[base:base + self.sheet_size]

    def decode_sheet(self, si):
        """-> (w, h, bytearray of alpha 0..255), row-major."""
        s = self.sheet_bytes(si)
        w, h = self.sheet_w, self.sheet_h
        out = bytearray(w * h)
        tw = w // 8
        for ty in range(h // 8):
            for tx in range(tw):
                tbase = (ty * tw + tx) * 32
                for yy in range(8):
                    for xx in range(8):
                        m = self.morton(xx, yy)
                        byte = s[tbase + (m >> 1)]
                        v = (byte >> 4) if (m & 1) else (byte & 0x0F)
                        out[(ty * 8 + yy) * w + tx * 8 + xx] = v * 17
        return w, h, out

    def _sheet_cache(self):
        if not hasattr(self, "_sc"):
            self._sc = {}
        return self._sc

    def glyph_bitmap(self, gi):
        """Raw cellW x cellH alpha block for glyph index gi (list of rows)."""
        per = self.n_cols * self.n_rows
        si, rem = divmod(gi, per)
        line, col = divmod(rem, self.n_cols)
        cache = self._sheet_cache()
        if si not in cache:
            cache[si] = self.decode_sheet(si)
        w, _h, px = cache[si]
        x0 = col * (self.cell_w + 1) + 1
        y0 = line * (self.cell_h + 1) + 1
        return [[px[(y0 + y) * w + x0 + x] for x in range(self.cell_w)]
                for y in range(self.cell_h)]


# ---------------------------------------------------------------- rendering


def render_run(font, text, scale, origin_x=0.0, origin_y=0.0, filt="linear"):
    """Reproduce a citro2d text run as the PICA200 rasterises it.

    `scale` is the TEXEL scale — what the caller's scale becomes after citro2d multiplies by
    font->textScale (30/cellHeight). Use Bcfnt.texel_scale() to derive it from a (px, s_native)
    pair. It is passed explicitly and never defaulted, because the whole phase-18 defect was a
    scale that looked right and was not: a renderer that computes it for you can hide the bug it
    exists to expose.

    Returns (w, h, [[value]]) in device pixels. The math mirrors C2D_FontCalcGlyphPos + the
    texture sampler:
      pen advance per glyph = charWidth * scale
      glyph quad at pen + left*scale, width glyphWidth*scale, height cellH*scale
      sample u = (i + 0.5 - quadX)/scale - 0.5   (bilinear, clamp) for GPU_LINEAR
                 round((i + 0.5 - quadX)/scale - 0.5) for GPU_NEAREST
    """
    # lay out
    quads = []
    pen = origin_x
    for ch in text:
        gi = font.glyph_index(ch)
        wi = font.width_info(gi)
        quads.append((pen + wi.left * scale, origin_y, gi, wi))
        pen += wi.char_w * scale
    total_w = int(pen - origin_x + max(0.0, origin_x - int(origin_x)) + 2.5)
    out_h = int(font.cell_h * scale + abs(origin_y - int(origin_y)) + 1.5)
    out_w = max(1, total_w + int(origin_x) + 2)
    img = [[0] * out_w for _ in range(out_h)]

    for (qx, qy, gi, wi) in quads:
        bm = font.glyph_bitmap(gi)
        gw, gh = font.cell_w, font.cell_h
        dw = wi.glyph_w * scale
        dh = gh * scale
        # source sub-rect for this glyph inside the cell starts at x=0 (bcfnt cells are
        # already per-glyph); glyphWidth columns are the inked ones.
        x_lo = int(qx) - 1
        x_hi = int(qx + dw) + 2
        y_lo = int(qy) - 1
        y_hi = int(qy + dh) + 2
        for iy in range(max(0, y_lo), min(out_h, y_hi)):
            for ix in range(max(0, x_lo), min(out_w, x_hi)):
                u = (ix + 0.5 - qx) / scale - 0.5
                v = (iy + 0.5 - qy) / scale - 0.5
                if u < -0.5 or u > wi.glyph_w - 0.5 or v < -0.5 or v > gh - 0.5:
                    continue
                if filt == "nearest":
                    su = int(u + 0.5)
                    sv = int(v + 0.5)
                    su = min(max(su, 0), gw - 1)
                    sv = min(max(sv, 0), gh - 1)
                    val = bm[sv][su]
                else:
                    u0 = int(u // 1)
                    v0 = int(v // 1)
                    fu = u - u0
                    fv = v - v0
                    def tap(xx, yy):
                        xx = min(max(xx, 0), gw - 1)
                        yy = min(max(yy, 0), gh - 1)
                        return bm[yy][xx]
                    val = (tap(u0, v0) * (1 - fu) * (1 - fv)
                           + tap(u0 + 1, v0) * fu * (1 - fv)
                           + tap(u0, v0 + 1) * (1 - fu) * fv
                           + tap(u0 + 1, v0 + 1) * fu * fv)
                iv = int(val + 0.5)
                if iv > img[iy][ix]:
                    img[iy][ix] = iv
    return out_w, out_h, img


# ---------------------------------------------------------------- metrics


def metrics(img):
    """SPEC-crisp C5.2.3: the three objective measures."""
    flat = [v for row in img for v in row]
    peak = max(flat) if flat else 0
    levels = len(set(v for v in flat if v > 0))
    ramp = 0
    if peak:
        lo, hi = 0.12 * peak, 0.88 * peak
        for row in img:
            run = 0
            for v in row:
                if lo < v < hi:
                    run += 1
                    ramp = max(ramp, run)
                else:
                    run = 0
    acut = 0.0
    if peak:
        tot = 0
        n = 0
        for row in img:
            for i in range(len(row) - 1):
                tot += abs(row[i + 1] - row[i])
                n += 1
        acut = (tot / n / peak) if n else 0.0
    # THE binary proof of 1:1 sampling: a bcfnt sheet is A4, so every source texel is a
    # multiple of 17 (nibble*17). If the sampler passes texels through untouched, every
    # output value is still a multiple of 17. One off-grid pixel == the glyph was resampled.
    offgrid = sum(1 for v in flat if v % 17 != 0)
    ink = sum(1 for v in flat if v > 0)
    solid = sum(1 for v in flat if v >= 0.88 * peak) if peak else 0
    return {
        "offgrid": offgrid,
        "levels": levels,
        "ramp": ramp,
        "acutance": round(acut, 4),
        "peak": peak,
        "ink": ink,
        "solid_frac": round(solid / ink, 3) if ink else 0.0,
        "w": len(img[0]) if img else 0,
        "h": len(img),
    }


def to_pgm(img, path, zoom=1):
    h = len(img)
    w = len(img[0]) if h else 0
    with open(path, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (w * zoom, h * zoom))
        for row in img:
            line = bytes(bytearray(v for v in row for _ in range(zoom)))
            for _ in range(zoom):
                f.write(line)


def to_png(img, path, zoom=1, invert=True):
    """Minimal greyscale PNG writer (no deps). invert => ink is dark on white."""
    import zlib

    h = len(img)
    w = len(img[0]) if h else 0
    raw = bytearray()
    for row in img:
        for _ in range(zoom):
            raw.append(0)
            for v in row:
                b = (255 - v) if invert else v
                for _ in range(zoom):
                    raw.append(b)
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    hdr = struct.pack(">IIBBBBB", w * zoom, h * zoom, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", hdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


# ---------------------------------------------------------------- cli

def describe(path):
    f = Bcfnt(path)
    return ("%-28s lineFeed=%-3d cell=%dx%-3d base=%-3d sheets=%d %dx%d cols=%d rows=%d "
            "bytes=%d s_native(exact)=%.4f (shipped ceil)=%.0f"
            % (path.split("/")[-1], f.line_feed, f.cell_w, f.cell_h, f.baseline,
               f.n_sheets, f.sheet_w, f.sheet_h, f.n_cols, f.n_rows,
               len(f.raw), f.s_native_exact, f.s_native_shipped))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        print(describe(p))
