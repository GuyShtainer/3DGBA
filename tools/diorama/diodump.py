#!/usr/bin/env python3
"""diodump.py — dump one Pokémon Emerald (BPEE) map straight out of a ROM file into a
phase-31 diorama fixture (+ a composed PNG of the map's real metatile art), using pret's
public symbol map for the ROM data addresses. No emulator involved.

WHY A ROM DUMP AND NOT A LIVE DUMP. Everything the diorama's world module consumes for a map
is STATIC ROM data (layout grid, border, tilesets' tiles/palettes/metatiles/attributes, map
type, connections). The only live pieces are the player/NPC positions and the live copy of the
grid (door animations), which the fixture does not need. So the fixture comes from the file,
deterministically, with symbols as the only "constants" (house rule: every address cites its
source — here the source IS the symbol map).

STRUCT LAYOUTS (pret/pokeemerald include/global.fieldmap.h, fieldmap.h — functional facts about
the game, re-read 2026-09-10):
  MapLayout      {s32 width; s32 height; u16* border; u16* map; Tileset* primary; Tileset* secondary}  24 B
  Tileset        {u8 isCompressed; u8 isSecondary; u8 pad[2]; u32* tiles; u16* palettes;
                  u16* metatiles; u16* metatileAttributes; cb callback}                            24 B
  MapHeader      {MapLayout* mapLayout; MapEvents* events; u8* mapScripts; MapConnections* connections;
                  u16 music; u16 mapLayoutId; u8 regionMapSectionId; u8 cave; u8 weather; u8 mapType;
                  u8 filler[2]; u8 flags; u8 battleType}                                             28 B
  MapConnections {s32 count; MapConnection* connections}; MapConnection {u8 direction; u8 pad[3];
                  s32 offset; u8 mapGroup; u8 mapNum; u8 pad[2]}                                    12 B
  cell u16: metatile id bits 0-9 (0x03FF), collision bits 10-11 (0x0C00), elevation bits 12-15 (0xF000)
  metatile: 8 u16 = 2 layers x 4 quadrants; entry: tile bits 0-9, hflip bit 10, vflip bit 11, pal bits 12-15
  attribute u16 (Emerald): behavior bits 0-7 (0x00FF), layer type bits 12-13 ((a & 0xF000) >> 12)
  NUM_TILES_IN_PRIMARY 512, NUM_METATILES_IN_PRIMARY 512, NUM_PALS_IN_PRIMARY 6, NUM_PALS_TOTAL 13
  Connection directions: 1 south, 2 north, 3 west, 4 east, 5 dive, 6 emerge.

FIXTURE FORMAT (little-endian TLV, magic 'DIOF'):
  'DIOF' u32 version(1) | then sections: u32 tag, u32 size, bytes[size], each:
  'HEAD' s32 w, s32 h, u8 mapType, u8 primIsSecondary(0), u8 pad[2], u32 layoutAddr, u32 headerAddr
  'BORD' 4 x u16            'CELL' w*h x u16 (row-major, y down)
  'ATTP' 512 x u16          'ATTS' 512 x u16          (metatile attributes, primary / secondary)
  'MTLP' 512*8 x u16        'MTLS' 512*8 x u16        (metatile tile entries)
  'TILP' 16384 B            'TILS' 16384 B            (4bpp tiles, decompressed)
  'PALP' 16*16 x u16        'PALS' 16*16 x u16        (BGR555 palettes, whole tables)
  'CONN' n x {u8 dir, u8 group, u8 num, u8 tilesetsMatch, s32 offset, s32 w, s32 h, u32 cellsOff}
  'NCEL' concatenated neighbour cell arrays (u16), each at cellsOff within this section
  'NAME' UTF-8 map name (no NUL)

The fixture and PNG are ROM-DERIVED and git-ignored (never commit; IP policy).
"""
import argparse, json, os, struct, sys

ROM_BASE = 0x08000000

def load_syms(path):
    syms = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) < 4: continue
            addr, kind, size, name = parts[0], parts[1], parts[2], parts[3]
            try: a = int(addr, 16); s = int(size, 16)
            except ValueError: continue
            if name not in syms or (kind == 'g' and syms[name][1] != 'g'):
                syms[name] = (a, kind, s)
    return syms

class Rom:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        code = self.b[0xAC:0xB0].decode('ascii', 'replace')
        if code != 'BPEE':
            sys.exit(f"diodump: ROM code is {code}, only BPEE (Emerald) is supported")
        self.rev = self.b[0xBC]
    def off(self, a):
        if not (ROM_BASE <= a < ROM_BASE + len(self.b)):
            raise ValueError(f"address {a:#010x} outside ROM")
        return a - ROM_BASE
    def u8(self, a):  return self.b[self.off(a)]
    def s8(self, a):  return struct.unpack_from('<b', self.b, self.off(a))[0]
    def u16(self, a): return struct.unpack_from('<H', self.b, self.off(a))[0]
    def u32(self, a): return struct.unpack_from('<I', self.b, self.off(a))[0]
    def s32(self, a): return struct.unpack_from('<i', self.b, self.off(a))[0]
    def bytes(self, a, n): o = self.off(a); return self.b[o:o+n]

def lz77_decompress(rom, addr):
    """GBA BIOS LZ77 (type 0x10): u32 header (type in bits 4-7, size in bits 8-31), then
    flag bytes (MSB first): 1 = back-reference (len = (b1>>4)+3, disp = ((b1&0xF)<<8|b2)+1)."""
    hdr = rom.u32(addr)
    if (hdr & 0xF0) != 0x10: raise ValueError(f"not LZ77 at {addr:#010x}: {hdr:#010x}")
    size = hdr >> 8
    out = bytearray(); src = rom.off(addr) + 4; b = rom.b
    while len(out) < size:
        flags = b[src]; src += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                b1, b2 = b[src], b[src+1]; src += 2
                ln = (b1 >> 4) + 3; disp = ((b1 & 0xF) << 8 | b2) + 1
                for _ in range(ln): out.append(out[-disp])
            else:
                out.append(b[src]); src += 1
    return bytes(out[:size])

def read_tileset(rom, a):
    ts = {'addr': a, 'isCompressed': rom.u8(a), 'isSecondary': rom.u8(a+1),
          'tiles': rom.u32(a+4), 'palettes': rom.u32(a+8), 'metatiles': rom.u32(a+12),
          'attrs': rom.u32(a+16)}
    if ts['isCompressed']: ts['tileBytes'] = lz77_decompress(rom, ts['tiles'])
    else: ts['tileBytes'] = rom.bytes(ts['tiles'], 512*32)
    if len(ts['tileBytes']) > 512*32: ts['tileBytes'] = ts['tileBytes'][:512*32]
    ts['tileBytes'] = ts['tileBytes'].ljust(512*32, b'\0')
    ts['palBytes'] = rom.bytes(ts['palettes'], 16*16*2)
    ts['mtl'] = list(struct.unpack('<4096H', rom.bytes(ts['metatiles'], 512*8*2)))
    ts['attr'] = list(struct.unpack('<512H', rom.bytes(ts['attrs'], 512*2)))
    return ts

def read_layout(rom, a):
    w, h = rom.s32(a), rom.s32(a+4)
    if not (0 < w <= 512 and 0 < h <= 512): raise ValueError(f"bad layout dims {w}x{h} at {a:#010x}")
    border = list(struct.unpack('<4H', rom.bytes(rom.u32(a+8), 8)))
    cells = list(struct.unpack(f'<{w*h}H', rom.bytes(rom.u32(a+12), w*h*2)))
    return {'addr': a, 'w': w, 'h': h, 'border': border, 'cells': cells,
            'primAddr': rom.u32(a+16), 'secAddr': rom.u32(a+20)}

def read_header(rom, a):
    return {'addr': a, 'layout': rom.u32(a), 'connections': rom.u32(a+12),
            'mapLayoutId': rom.u16(a+18), 'mapType': rom.u8(a+23), 'weather': rom.u8(a+22)}

def map_groups(rom, syms):
    """gMapGroups[] = array of pointers to per-group MapHeader* arrays. pret names the group
    arrays gMapGroup_<Name> (data/maps/map_groups.inc) with size 0 in the .sym, so each group's
    count = (address of the next group array - its own address) / 4, the last one bounded by
    gMapGroups itself (it follows the group arrays in the same file)."""
    base = syms['gMapGroups'][0]
    garr = sorted(a for n, (a, k, s) in syms.items() if n.startswith('gMapGroup_'))
    if not garr: raise SystemExit("diodump: no gMapGroup_* symbols in the .sym")
    bounds = sorted(garr + [base])
    groups = []
    for g in range(64):
        p = rom.u32(base + 4*g)
        if p not in garr: break
        nxt = bounds[bounds.index(p) + 1]
        n = (nxt - p) // 4
        groups.append([rom.u32(p + 4*i) for i in range(n)])
    return groups

def read_connections(rom, hdr):
    ca = hdr['connections']
    if ca == 0: return []
    n = rom.s32(ca); lst = rom.u32(ca+4)
    out = []
    for i in range(max(0, min(n, 32))):
        e = lst + 12*i
        out.append({'dir': rom.u8(e), 'offset': rom.s32(e+4), 'group': rom.u8(e+8), 'num': rom.u8(e+9)})
    return out

DIR_NAMES = {1:'south', 2:'north', 3:'west', 4:'east', 5:'dive', 6:'emerge'}

def compose_metatile_raw(mid, prim, sec):
    """Like compose_metatile but returns the 256 RAW BGR555 colour words (no RGB888 conversion) —
    the stage at which the C atlas composer is differential-tested."""
    ts = prim if mid < 512 else sec
    base = (mid if mid < 512 else mid - 512) * 8
    pal_tables = (prim['palBytes'], sec['palBytes'])
    backdrop = struct.unpack_from('<H', prim['palBytes'], 0)[0]
    px = [backdrop] * 256
    for layer in (0, 1):
        for q in range(4):
            e = ts['mtl'][base + layer*4 + q]
            tile, hf, vf, pal = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            tb = prim['tileBytes'] if tile < 512 else sec['tileBytes']
            to = (tile if tile < 512 else tile - 512) * 32
            palb = pal_tables[0] if pal < 6 else pal_tables[1]
            qx, qy = (q & 1) * 8, (q >> 1) * 8
            for y in range(8):
                for x in range(8):
                    sx = 7 - x if hf else x; sy = 7 - y if vf else y
                    byte = tb[to + sy*4 + (sx >> 1)]
                    ci = (byte & 0xF) if (sx & 1) == 0 else (byte >> 4)
                    if ci == 0: continue
                    px[(qy + y) * 16 + qx + x] = struct.unpack_from('<H', palb, pal*32 + ci*2)[0]
    return px

def bgr555(v):
    return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31)

def compose_metatile(mid, prim, sec):
    """16x16 RGB pixels of metatile `mid` with both layers flattened (top over bottom; bottom
    colour-0 = backdrop = primary palette 0 colour 0). Returns list of 256 (r,g,b)."""
    ts = prim if mid < 512 else sec
    base = (mid if mid < 512 else mid - 512) * 8
    pal_tables = (prim['palBytes'], sec['palBytes'])
    backdrop = bgr555(struct.unpack_from('<H', prim['palBytes'], 0)[0])
    px = [backdrop] * 256
    for layer in (0, 1):
        for q in range(4):
            e = ts['mtl'][base + layer*4 + q]
            tile, hf, vf, pal = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            tb = prim['tileBytes'] if tile < 512 else sec['tileBytes']
            to = (tile if tile < 512 else tile - 512) * 32
            palb = pal_tables[0] if pal < 6 else pal_tables[1]
            qx, qy = (q & 1) * 8, (q >> 1) * 8
            for y in range(8):
                for x in range(8):
                    sx = 7 - x if hf else x; sy = 7 - y if vf else y
                    byte = tb[to + sy*4 + (sx >> 1)]
                    ci = (byte & 0xF) if (sx & 1) == 0 else (byte >> 4)
                    if ci == 0 and layer == 1: continue       # top layer colour 0 = transparent
                    if ci == 0: continue                        # bottom colour 0 = backdrop (pre-filled)
                    c = struct.unpack_from('<H', palb, pal*32 + ci*2)[0]
                    px[(qy + y) * 16 + qx + x] = bgr555(c)
    return px

def render_png(path, lay, prim, sec, overlay=None):
    from PIL import Image
    w, h = lay['w'], lay['h']
    img = Image.new('RGB', (w*16, h*16))
    cache = {}
    put = img.putpixel
    for ty in range(h):
        for tx in range(w):
            cell = lay['cells'][ty*w + tx]; mid = cell & 0x3FF
            if mid not in cache: cache[mid] = compose_metatile(mid, prim, sec)
            px = cache[mid]
            for y in range(16):
                for x in range(16):
                    put((tx*16 + x, ty*16 + y), px[y*16 + x])
            if overlay == 'collision' and (cell & 0x0C00):
                for y in range(16):
                    for x in range(16):
                        if (x + y) % 4 == 0: put((tx*16 + x, ty*16 + y), (255, 0, 0))
    img.save(path)

def sec_bytes(tag, payload):
    return struct.pack('<4sI', tag.encode(), len(payload)) + payload

def write_fixture(path, name, hdr, lay, prim, sec, conns, neighbours):
    out = bytearray(b'DIOF' + struct.pack('<I', 1))
    out += sec_bytes('HEAD', struct.pack('<iiBBBBII', lay['w'], lay['h'], hdr['mapType'], 0, 0, 0, lay['addr'], hdr['addr']))
    out += sec_bytes('BORD', struct.pack('<4H', *lay['border']))
    out += sec_bytes('CELL', struct.pack(f"<{len(lay['cells'])}H", *lay['cells']))
    out += sec_bytes('ATTP', struct.pack('<512H', *prim['attr']))
    out += sec_bytes('ATTS', struct.pack('<512H', *sec['attr']))
    out += sec_bytes('MTLP', struct.pack('<4096H', *prim['mtl']))
    out += sec_bytes('MTLS', struct.pack('<4096H', *sec['mtl']))
    out += sec_bytes('TILP', prim['tileBytes'])
    out += sec_bytes('TILS', sec['tileBytes'])
    out += sec_bytes('PALP', prim['palBytes'])
    out += sec_bytes('PALS', sec['palBytes'])
    ncel = bytearray(); conn = bytearray()
    for c, nb in zip(conns, neighbours):
        if nb is None:
            conn += struct.pack('<BBBBiiiI', c['dir'], c['group'], c['num'], 0, c['offset'], 0, 0, 0)
            continue
        conn += struct.pack('<BBBBiiiI', c['dir'], c['group'], c['num'], 1 if nb['match'] else 0,
                            c['offset'], nb['w'], nb['h'], len(ncel))
        ncel += struct.pack(f"<{nb['w']*nb['h']}H", *nb['cells'])
    out += sec_bytes('CONN', bytes(conn))
    out += sec_bytes('NCEL', bytes(ncel))
    out += sec_bytes('NAME', name.encode())
    open(path, 'wb').write(out)
    return len(out)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom', required=True)
    ap.add_argument('--sym', default='/tmp/pret/pokeemerald.sym')
    ap.add_argument('--map', required=True, help='pret map header symbol, e.g. LittlerootTown')
    ap.add_argument('--out', help='fixture path (.bin)')
    ap.add_argument('--png', help='composed map art (.png)')
    ap.add_argument('--overlay', choices=['none', 'collision'], default='none')
    ap.add_argument('--json', help='provenance sidecar (.json)')
    ap.add_argument('--atlas', help="golden atlas: 'DIOA' u32 ver | 1024 x 256 u16 raw BGR555 composed metatiles")
    a = ap.parse_args()
    syms = load_syms(a.sym); rom = Rom(a.rom)
    if a.map not in syms: sys.exit(f"diodump: no symbol {a.map} in {a.sym}")
    hdr = read_header(rom, syms[a.map][0])
    lay = read_layout(rom, hdr['layout'])
    prim = read_tileset(rom, lay['primAddr']); sec = read_tileset(rom, lay['secAddr'])
    conns = read_connections(rom, hdr)
    groups = map_groups(rom, syms)
    neighbours = []
    for c in conns:
        if c['dir'] in (5, 6) or c['group'] >= len(groups) or c['num'] >= len(groups[c['group']]):
            neighbours.append(None); continue
        nh = read_header(rom, groups[c['group']][c['num']]); nl = read_layout(rom, nh['layout'])
        neighbours.append({'w': nl['w'], 'h': nl['h'], 'cells': nl['cells'],
                           'match': nl['primAddr'] == lay['primAddr'] and nl['secAddr'] == lay['secAddr'],
                           'layoutAddr': nl['addr']})
    solid = sum(1 for c in lay['cells'] if c & 0x0C00)
    ids = sorted({c & 0x3FF for c in lay['cells']})
    info = {'map': a.map, 'rom_rev': rom.rev, 'header': f"{hdr['addr']:#010x}", 'layout': f"{lay['addr']:#010x}",
            'w': lay['w'], 'h': lay['h'], 'mapType': hdr['mapType'], 'weather': hdr['weather'],
            'primary': f"{lay['primAddr']:#010x}", 'secondary': f"{lay['secAddr']:#010x}",
            'primCompressed': prim['isCompressed'], 'secCompressed': sec['isCompressed'],
            'cells': lay['w']*lay['h'], 'solidCells': solid, 'distinctMetatiles': len(ids),
            'secondaryIdsUsed': sum(1 for i in ids if i >= 512),
            'connections': [{**c, 'dirName': DIR_NAMES.get(c['dir'], '?'),
                             'neighbour': None if nb is None else {'w': nb['w'], 'h': nb['h'], 'tilesetsMatch': nb['match']}}
                            for c, nb in zip(conns, neighbours)]}
    if a.out:
        info['fixtureBytes'] = write_fixture(a.out, a.map, hdr, lay, prim, sec, conns, neighbours)
    if a.png:
        render_png(a.png, lay, prim, sec, None if a.overlay == 'none' else a.overlay)
    if a.atlas:
        ab = bytearray(b'DIOA' + struct.pack('<I', 1))
        for mid in range(1024): ab += struct.pack('<256H', *compose_metatile_raw(mid, prim, sec))
        open(a.atlas, 'wb').write(ab); info['atlasBytes'] = len(ab)
    if a.json:
        json.dump(info, open(a.json, 'w'), indent=1)
    print(json.dumps(info, indent=1))

if __name__ == '__main__':
    main()
