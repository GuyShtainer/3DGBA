#!/usr/bin/env python3
"""dioref.py — PYTHON REFERENCE MODEL of the phase-31 diorama world rules, run over a diodump
fixture. It exists to (1) produce golden class grids / structure tables for the C module's host
tests (differential testing: two independent implementations of the same written rules), (2) draw
a class-colour overlay over the real map art so the massing can be eyeballed, and (3) draw a quick
oblique "cardboard" 3D preview of the extruded map — the intended look, before any C exists.

Rules implemented EXACTLY as written in docs/phase31-diorama/RESEARCH-classification.md §3–§4
(clean-room idea spec; nothing here was read from any reference code). Behaviour constants come
from pret's public include/constants/metatile_behaviors.h (an enum, parsed at runtime).

Usage: dioref.py FIXTURE.bin [--beh /tmp/pret/metatile_behaviors.h] [--grid] [--overlay out.png]
                 [--preview out.png] [--golden out.json] [--rom ROM]   (rom needed for art in PNGs)
"""
import argparse, json, re, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

VOID, FLAT, DECAL, LOW, LEDGE, WALL, ROOF, FURNITURE, BED, TABLE, COUNTER, SIGN, WATER, VEG = range(14)
HMAX = 6   # structure height cap (our rule): adjacent solid scenery fuses into buildings; nothing real is taller
NAMES = ['VOID','FLAT','DECAL','LOW','LEDGE','WALL','ROOF','FURNITURE','BED','TABLE','COUNTER','SIGN','WATER','VEG']
CHARS = ' .:LlWRfbtcs~T'
COLORS = {VOID:(0,0,0), FLAT:None, DECAL:(255,255,0), LOW:(255,128,0), LEDGE:(255,0,255), WALL:(255,0,0),
          ROOF:(0,0,255), FURNITURE:(0,255,255), BED:(255,0,128), TABLE:(128,0,255), COUNTER:(0,128,255),
          SIGN:(255,255,255), WATER:(0,64,255), VEG:(0,160,0)}
MAP_TYPE_INDOOR, MAP_TYPE_SECRET_BASE = 8, 9

def load_behaviors(path):
    names = {}; val = 0
    for line in open(path):
        m = re.match(r'\s*(MB_[A-Z0-9_]+)\s*(=\s*(0x[0-9A-Fa-f]+|\d+))?\s*,?', line)
        if not m: continue
        if m.group(3): val = int(m.group(3), 0)
        names[m.group(1)] = val; val += 1
    return names

def read_fixture(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'DIOF', 'not a DIOF fixture'
    i = 8; sec = {}
    while i < len(b):
        tag = b[i:i+4].decode(); n = struct.unpack_from('<I', b, i+4)[0]
        sec[tag] = b[i+8:i+8+n]; i += 8 + n
    w, h, mapType = struct.unpack_from('<iiB', sec['HEAD'], 0)
    f = {'w': w, 'h': h, 'mapType': mapType, 'name': sec['NAME'].decode(),
         'cells': struct.unpack(f'<{w*h}H', sec['CELL']),
         'attp': struct.unpack('<512H', sec['ATTP']), 'atts': struct.unpack('<512H', sec['ATTS']),
         'mtlp': struct.unpack('<4096H', sec['MTLP']), 'mtls': struct.unpack('<4096H', sec['MTLS']),
         'tilp': sec['TILP'], 'tils': sec['TILS'],
         'conns': []}
    cb = sec['CONN']; ncel = sec['NCEL']
    for k in range(len(cb) // 20):
        d, g, n, match, off, nw, nh, coff = struct.unpack_from('<BBBBiiiI', cb, k*20)
        cells = struct.unpack_from(f'<{nw*nh}H', ncel, coff) if nw and nh else ()
        f['conns'].append({'dir': d, 'offset': off, 'w': nw, 'h': nh, 'match': match, 'cells': cells})
    f['sec'] = sec
    return f

class World:
    """Instances: 0 = current map at origin; first-ring neighbours at their connection origin."""
    def __init__(self, f, beh):
        self.f = f; self.B = beh
        self.inst = [{'ox': 0, 'oy': 0, 'w': f['w'], 'h': f['h'], 'cells': f['cells'], 'tex': True}]
        for c in f['conns']:
            if c['dir'] in (5, 6) or not c['w']: continue
            if   c['dir'] == 2: o = (c['offset'], -c['h'])           # north
            elif c['dir'] == 1: o = (c['offset'], f['h'])            # south
            elif c['dir'] == 3: o = (-c['w'], c['offset'])           # west
            elif c['dir'] == 4: o = (f['w'], c['offset'])            # east
            else: continue
            self.inst.append({'ox': o[0], 'oy': o[1], 'w': c['w'], 'h': c['h'], 'cells': c['cells'], 'tex': bool(c['match'])})
        self.indoor = f['mapType'] in (MAP_TYPE_INDOOR, MAP_TYPE_SECRET_BASE)
        B = beh
        self.water = {B[n] for n in B if (('WATER' in n and 'WATERFALL' not in n) or 'CURRENT' in n or n == 'MB_NO_SURFACING' or n == 'MB_WATERFALL')
                      and n not in ('MB_SHALLOW_WATER', 'MB_PUDDLE')}
        self.grass = {B[n] for n in ('MB_TALL_GRASS', 'MB_LONG_GRASS', 'MB_LONG_GRASS_SOUTH_EDGE', 'MB_ASHGRASS') if n in B}
        self.jump = {B[n] for n in B if n.startswith('MB_JUMP_')}
        self.doors = {B[n] for n in B if 'DOOR' in n}
        self.shelves = {B[n] for n in ('MB_BOOKSHELF', 'MB_POKEMART_SHELF', 'MB_POKEMON_CENTER_BOOKSHELF', 'MB_LIBRARY_SHELVES') if n in B}
        self.pc, self.tv, self.counter = B['MB_PC'], B['MB_TELEVISION'], B['MB_COUNTER']

    def lookup(self, x, y):
        for k, i in enumerate(self.inst):
            lx, ly = x - i['ox'], y - i['oy']
            if 0 <= lx < i['w'] and 0 <= ly < i['h']:
                return k, i['cells'][ly * i['w'] + lx]
        return -1, 0
    def col(self, x, y):
        k, c = self.lookup(x, y); return (c >> 10) & 3 if k >= 0 else 0
    def blank(self, mid):
        f = self.f
        mtl = f['mtlp'] if mid < 512 else f['mtls']; base = (mid if mid < 512 else mid - 512) * 8
        for q in range(8):
            e = mtl[base + q]; tile = e & 0x3FF
            tb = f['tilp'] if tile < 512 else f['tils']; to = (tile if tile < 512 else tile - 512) * 32
            if any(tb[to:to + 32]): return False
        return True
    def beh(self, mid):
        a = self.f['attp'][mid] if mid < 512 else self.f['atts'][mid - 512]
        return a & 0xFF

    def classify(self, x, y):
        k, c = self.lookup(x, y)
        if k < 0: return VOID
        mid, solid = c & 0x3FF, (c >> 10) & 3
        b = self.beh(mid)
        if self.indoor:
            # D3 (2026-09-10): the research's metatile-ID table was checked against the real
            # GenericBuilding and BrendansMaysHouse sheets and does NOT correspond -> v1 indoor rules
            # are BEHAVIOUR-based only, plus one tileset-agnostic VOID rule: a metatile whose 8 tile
            # entries all reference an all-zero (blank) tile renders as pure backdrop (the black
            # filler outside a room) and is not meshed at all.
            if self.blank(mid): return VOID
            if b == self.pc or b == self.tv: return FURNITURE
            if b in self.shelves: return WALL
            if b == self.counter: return COUNTER
            if b in self.doors: return WALL
        else:
            if b in self.water: return WATER
            if b in self.grass: return FLAT
            if b in self.jump: return LEDGE
        if solid:
            # VEG (our own rule, 2026-09-10): a solid cell whose metatile id repeats two rows away
            # in a solid cell is part of a PERIODIC band (tree lines, hedges, rock fields). Such
            # bands must not become one tall structure (a 32-row tree line is a cliff, not a
            # forest): each cell is its own 2-tall block with same-class side culling.
            if not self.indoor:
                for dy in (-4, -3, -2, -1, 1, 2, 3, 4):   # period 1..4 (trees, hedges, cliff bands)
                    k2, c2 = self.lookup(x, y + dy)
                    if k2 >= 0 and ((c2 >> 10) & 3) and (c2 & 0x3FF) == mid: return VEG
            if self.col(x, y - 1): return WALL
            if self.col(x, y + 1): return ROOF
            return LOW
        if self.indoor and self.col(x, y - 1) and self.col(x + 1, y) and self.col(x - 1, y):
            return WALL
        return FLAT

    def class_grid(self, k=0):
        i = self.inst[k]
        return [[self.classify(i['ox'] + x, i['oy'] + y) for x in range(i['w'])] for y in range(i['h'])]

    def structures(self):
        """BFS 4-neighbourhood over WALL/ROOF cells across all instances; explicit queue."""
        cls = {}
        for i in self.inst:
            for y in range(i['h']):
                for x in range(i['w']):
                    wx, wy = i['ox'] + x, i['oy'] + y
                    cls[(wx, wy)] = self.classify(wx, wy)
        seen = set(); structs = []
        for (wx, wy) in sorted(cls, key=lambda p: (p[1], p[0])):
            if (wx, wy) in seen or cls[(wx, wy)] not in (WALL, ROOF): continue
            q = [(wx, wy)]; seen.add((wx, wy)); members = []
            while q:
                p = q.pop(0); members.append(p)
                for d in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                    n = (p[0] + d[0], p[1] + d[1])
                    if n in cls and n not in seen and cls[n] in (WALL, ROOF):
                        seen.add(n); q.append(n)
            height = 1
            for (mx, my) in members:
                hcount = 0; yy = my
                while cls.get((mx, yy)) in (WALL, ROOF):
                    hcount += 1
                    if cls[(mx, yy)] == ROOF: break
                    yy -= 1
                height = max(height, hcount)
            height = min(height, HMAX)
            xs = [m[0] for m in members]; ys = [m[1] for m in members]
            structs.append({'n': len(members), 'height': height, 'bbox': [min(xs), min(ys), max(xs), max(ys)]})
        return cls, structs

def grid_text(g):
    return '\n'.join(''.join(CHARS[c] for c in row) for row in g)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fixture'); ap.add_argument('--beh', default='/tmp/pret/metatile_behaviors.h')
    ap.add_argument('--grid', action='store_true'); ap.add_argument('--overlay'); ap.add_argument('--preview')
    ap.add_argument('--golden'); ap.add_argument('--rom'); ap.add_argument('--pitch', type=float, default=40.0)
    a = ap.parse_args()
    f = read_fixture(a.fixture); W = World(f, load_behaviors(a.beh))
    g = W.class_grid(0); cls, structs = W.structures()
    from collections import Counter
    cnt = Counter(c for row in g for c in row)
    summary = {'map': f['name'], 'w': f['w'], 'h': f['h'], 'indoor': W.indoor, 'instances': len(W.inst),
               'counts': {NAMES[k]: v for k, v in sorted(cnt.items())},
               'structures': len(structs), 'maxHeight': max([s['height'] for s in structs] + [0])}
    print(json.dumps(summary))
    if a.grid: print(grid_text(g))
    if a.golden:
        ordered = sorted(structs, key=lambda s: (s['bbox'][1], s['bbox'][0], s['bbox'][3], s['bbox'][2]))
        sets = {'water': sorted(W.water), 'grass': sorted(W.grass), 'jump': sorted(W.jump), 'doors': sorted(W.doors),
                'shelves': sorted(W.shelves), 'pc': W.pc, 'tv': W.tv, 'counter': W.counter, 'hmax': HMAX}
        json.dump({**summary, 'grid': [''.join(CHARS[c] for c in row) for row in g], 'structs': ordered,
                   'behaviourSets': sets, 'instances_origin': [(i['ox'], i['oy'], i['w'], i['h'], i['tex']) for i in W.inst]},
                  open(a.golden, 'w'), indent=1)
        # binary twin for the C host suite: 'DIOG' u32 ver=1 | s32 w h | u8 class[w*h] (instance 0, row-major)
        # | u32 nStructs | per struct: s32 minx miny maxx maxy, u32 n, u32 height  (sorted as above)
        gb = bytearray(b'DIOG' + struct.pack('<Iii', 1, f['w'], f['h']))
        gb += bytes(c for row in g for c in row)
        gb += struct.pack('<I', len(ordered))
        for st in ordered: gb += struct.pack('<iiiiII', *st['bbox'], st['n'], st['height'])
        open(a.golden.replace('.json', '.bin'), 'wb').write(gb)
    if a.overlay or a.preview:
        import diodump as D
        from PIL import Image
        rom = D.Rom(a.rom); syms = D.load_syms('/tmp/pret/pokeemerald.sym')
        hdr = D.read_header(rom, syms[f['name']][0]); lay = D.read_layout(rom, hdr['layout'])
        prim, sec = D.read_tileset(rom, lay['primAddr']), D.read_tileset(rom, lay['secAddr'])
        cache = {}
        def art(mid):
            if mid not in cache:
                px = D.compose_metatile(mid, prim, sec); im = Image.new('RGB', (16, 16)); im.putdata(px); cache[mid] = im
            return cache[mid]
        if a.overlay:
            im = Image.new('RGB', (f['w']*16, f['h']*16))
            for y in range(f['h']):
                for x in range(f['w']):
                    t = art(f['cells'][y*f['w']+x] & 0x3FF).copy()
                    col = COLORS[g[y][x]]
                    if col: t = Image.blend(t, Image.new('RGB', (16, 16), col), 0.45)
                    im.paste(t, (x*16, y*16))
            im.save(a.overlay)
        if a.preview:
            import math
            p = math.radians(a.pitch); c, s = math.cos(p), math.sin(p)
            # world tile (x, z) with box height hgt: top face y-range [z*16c - hgt*16s, (z+1)*16c - hgt*16s]
            # front face [(z+1)*16c - hgt*16s, (z+1)*16c]. Painter: z ascending. Heights in tiles.
            H = {}   # (x,z) -> (height, structure?)
            sid = {}
            for si, st in enumerate(structs): pass
            # per-cell height: structure members = structure height; LOW/LEDGE .4, COUNTER .7, TABLE .5, FURNITURE 1, BED .35, SIGN 1(flat card), WATER -.1, else 0
            memb = {}
            for si, st in enumerate(structs):
                pass
            # rebuild membership by re-running BFS cheaply: use bbox+class test (approximation only for the preview)
            hmap = {}
            for (wx, wy), k in cls.items():
                hmap[(wx, wy)] = {LOW: .4, LEDGE: .4, COUNTER: .7, TABLE: .5, FURNITURE: 1.0, BED: .35, SIGN: 1.0, WATER: -.1, VEG: 2.0}.get(k, 0.0)
            # structure heights: assign by BFS again
            seen = set()
            for (wx, wy), k in sorted(cls.items(), key=lambda kv: (kv[0][1], kv[0][0])):
                if (wx, wy) in seen or k not in (WALL, ROOF): continue
                q = [(wx, wy)]; seen.add((wx, wy)); mem = []
                while q:
                    pt = q.pop(0); mem.append(pt)
                    for d in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                        n = (pt[0]+d[0], pt[1]+d[1])
                        if n in cls and n not in seen and cls[n] in (WALL, ROOF): seen.add(n); q.append(n)
                hh = 1
                for (mx, my) in mem:
                    cnt2 = 0; yy = my
                    while cls.get((mx, yy)) in (WALL, ROOF):
                        cnt2 += 1
                        if cls[(mx, yy)] == ROOF: break
                        yy -= 1
                    hh = max(hh, cnt2)
                hh = min(hh, HMAX)
                for m in mem: hmap[m] = float(hh)
            xs = [k[0] for k in cls]; zs = [k[1] for k in cls]
            x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
            maxh = max(hmap.values()) if hmap else 1
            Wpx = (x1 - x0 + 1) * 16; Hpx = int((z1 - z0 + 1) * 16 * c + maxh * 16 * s) + 2
            im = Image.new('RGB', (Wpx, Hpx), (20, 24, 40))
            def cell_art(wx, wz):
                k, cc = W.lookup(wx, wz)
                if k < 0 or not W.inst[k]['tex']: return Image.new('RGB', (16, 16), (40, 40, 48))
                return art(cc & 0x3FF)
            base = maxh * 16 * s
            for wz in range(z0, z1 + 1):
                for wx in range(x0, x1 + 1):
                    if (wx, wz) not in cls or cls[(wx, wz)] == VOID: continue
                    k = cls[(wx, wz)]; hgt = hmap[(wx, wz)]
                    sx = (wx - x0) * 16
                    top_h = max(1, int(round(16 * c)))
                    top_y = base + (wz - z0) * 16 * c - hgt * 16 * s
                    tex = cell_art(wx, wz - int(hgt) + 1) if (k in (WALL, ROOF) and hgt >= 1) else cell_art(wx, wz)
                    im.paste(tex.resize((16, top_h)), (sx, int(round(top_y))))
                    if hgt > 0:
                        fy1 = base + (wz - z0 + 1) * 16 * c       # ground line of this row
                        lvl = 0; remaining = hgt
                        while remaining > 1e-6:
                            frac = min(1.0, remaining)
                            fh = max(1, int(round(16 * s * frac)))
                            fa = cell_art(wx, wz - lvl) if k in (WALL, ROOF, VEG) else Image.blend(cell_art(wx, wz), Image.new('RGB', (16, 16), (0, 0, 0)), 0.45)
                            im.paste(fa.resize((16, fh)), (sx, int(round(fy1 - (lvl + frac) * 16 * s))))
                            remaining -= frac; lvl += 1
            im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
            im.save(a.preview)

if __name__ == '__main__':
    main()
