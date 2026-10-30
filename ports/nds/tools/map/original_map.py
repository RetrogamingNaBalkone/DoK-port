"""Original PKR/PKE automap execution with a translated 256x160 device.

The Ghidra-exported x86 traversal, classes, neighbours, colours and arrow execute
unchanged. Only VGA virtuals, viewport/clip and the two confirmed scale locals
are supplied. Stale hero stack locals are initialized to their eventual values:
this excludes DOS undefined stack pixels (documented NDS correction).
No GBA renderer or engine is imported.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT = Path(__file__).resolve().parents[4]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def normalize_snapshot(snapshot):
    """Accept public packed telemetry or source-research object dictionaries."""
    s = dict(snapshot)
    if 'objects' not in s:
        packed = bytes(s['glyphs'])
        assert len(packed) == 1024
        s['objects'] = [dict(glyph=i, class_id=c, marker=m, monster_state=a)
                        for i, (c,m,a) in enumerate(struct.iter_unpack('<HBB', packed))]
    if 'hero' not in s:
        s['hero'] = dict(x=s['hero_x'], z=s['hero_z'], heading=s['heading'])
    s.setdefault('width', 64)
    s.setdefault('length', 64)
    return s

class OriginalMap:
    def __init__(self, language):
        root = ROOT / 'research/evidence/automap' / language
        manifest = json.loads((root / 'manifest.json').read_text())
        self.sample = ROOT / manifest['sample']
        self.sha_before = sha(self.sample)
        assert self.sha_before == manifest['sha256_before'] == manifest['sha256_after']
        self.ru = language == 'ru'
        self.c = Uc(UC_ARCH_X86, UC_MODE_16)
        self.c.mem_map(0, 0x100000)
        self.c.mem_write(0x50000, (root / ('1098.bin' if self.ru else '1030.bin')).read_bytes())
        self.c.mem_write(0x30000, (root / ('1190.bin' if self.ru else '10d0.bin')).read_bytes())
        self.delta = 0 if self.ru else 0xb0
        self.entry = 0x1290 if self.ru else 0x1374
        self.c.mem_write(0x60000, b'\xcb' * 0x1000)
        self.c.mem_write(0x40000, struct.pack('<H', 0x800))
        for off, stub in ((4, 0x100), (0x14, 0x200), (0x18, 0x300), (0x38, 0x400)):
            self.c.mem_write(0x30800 + off, struct.pack('<HH', stub, 0x6000))
        self.put(0xb48c + self.delta, '<HH', 0, 0x4000)
        self.c.hook_add(UC_HOOK_CODE, self.hook)

    def put(self, off, fmt, *values):
        self.c.mem_write(0x30000 + off, struct.pack(fmt, *values))

    def pixel(self, x, y, colour):
        x -= 64 + self.left
        y -= 7 + self.top
        if 0 <= x < 256 and 0 <= y < 160:
            self.frame[y * 256 + x] = colour

    def hook(self, c, address, size, data):
        if address == 0x50000 + self.entry + 0x10:
            bp = c.reg_read(R.UC_X86_REG_BP)
            self.put(bp - 6, '<hh', self.scale, self.scale)
            self.put(bp - 0x26, '<hh', 7 + self.hero_z * self.scale,
                     64 + (self.width - self.hero_x) * self.scale)
            c.reg_write(R.UC_X86_REG_IP, self.entry + 0x34)
        if c.reg_read(R.UC_X86_REG_CS) in (0x1110, 0x1068):
            c.mem_write(address, b'\xcb')
            return
        if c.reg_read(R.UC_X86_REG_CS) != 0x6000:
            return
        sp = c.reg_read(R.UC_X86_REG_SP)
        args = bytes(c.mem_read(0x30000 + sp + 8, 18))
        op = c.reg_read(R.UC_X86_REG_IP)
        colour = c.mem_read(0x4000e, 1)[0]
        if op == 0x100:
            x, y = struct.unpack_from('<ii', args)
            self.pixel(x, y, args[8])
        elif op in (0x200, 0x300):
            a, b, d = struct.unpack_from('<iii', args)
            colour = args[12]
            if op == 0x200:
                for x in range(min(a, d), max(a, d) + 1):
                    self.pixel(x, b, colour)
            else:
                for y in range(min(b, d), max(b, d) + 1):
                    self.pixel(a, y, colour)
        elif op == 0x400:
            if self.first_rect:
                self.frame[:] = bytes([colour]) * len(self.frame)
                self.first_rect = False
                return
            x, y, x2, y2 = struct.unpack_from('<iiii', args)
            for yy in range(min(y, y2), max(y, y2) + 1):
                for xx in range(min(x, x2), max(x, x2) + 1):
                    self.pixel(xx, yy, colour)

    def draw(self, snapshot, scale, left=0, top=0):
        snapshot = normalize_snapshot(snapshot)
        assert scale in (2, 3, 4, 6, 8)
        self.scale, self.left, self.top = scale, left, top
        self.width = snapshot.get('width', 64)
        length = snapshot.get('length', 64)
        self.hero_x, self.hero_z = snapshot['hero']['x'], snapshot['hero']['z']
        self.frame = bytearray(40960)
        self.first_rect = True
        c = self.c
        world = 0x41000
        c.mem_write(world, bytes(0x800))
        c.mem_write(world, struct.pack('<hhh', self.width, 0, length))
        c.mem_write(world + 0xc, struct.pack('<h', int(snapshot['map_available'])))
        c.mem_write(world + 0x742, struct.pack('<i', self.width))
        objects = {o['glyph']: o for o in snapshot['objects']}
        for glyph in range(256):
            o = objects.get(glyph, {})
            ptr = glyph * 0x100
            data = bytearray(0x100)
            struct.pack_into('<h', data, 0x12, o.get('class_id', 0))
            data[0x1b] = o.get('marker', 0)
            struct.pack_into('<h', data, 0xa2, o.get('monster_state', 0))
            struct.pack_into('<h', data, 0x32, snapshot['hero']['heading'])
            c.mem_write(0x70000 + ptr, bytes(data))
            c.mem_write(world + 0x30e + glyph * 4, struct.pack('<HH', ptr, 0x7000))
            if o.get('class_id') == 1:
                c.mem_write(world + 0x734, struct.pack('<HH', ptr, 0x7000))
        for i, plane in enumerate(snapshot['maps']):
            assert len(plane) == self.width * length
            self.c.mem_write(0x30000 + (0x608a if self.ru else 0x613a) + i * 0x1104, bytes(4096))
            self.c.mem_write(0x30000 + (0x608a if self.ru else 0x613a) + i * 0x1104, bytes(plane))
        self.put(0x9396 if self.ru else 0x9446, '4096s', bytes(4096))
        self.put(0x9396 if self.ru else 0x9446, f'{self.width * length}s', bytes(snapshot['visited']))
        for off, value in ((0x30, 320), (0x34, 200), (0x38, 64 + left),
                           (0x3c, 7 + top), (0x40, 64 + left + 255), (0x44, 7 + top + 159)):
            c.mem_write(0x40000 + off, struct.pack('<i', value))
        self.put(0xf000, '<4H', 0xfff0, 0x5000, 0x1000, 0x4000)
        for reg, val in ((R.UC_X86_REG_DS, 0x3000), (R.UC_X86_REG_SS, 0x3000),
                         (R.UC_X86_REG_CS, 0x5000), (R.UC_X86_REG_SP, 0xf000),
                         (R.UC_X86_REG_BP, 0xf800)):
            c.reg_write(reg, val)
        c.mem_write(0x112d8, b'\xcb')
        c.emu_start(0x50000 + self.entry, 0x5fff0, count=10000000)
        assert c.reg_read(R.UC_X86_REG_SP) == 0xf004
        assert sha(self.sample) == self.sha_before
        return bytes(self.frame)

_CACHE = {}
def original_map(language, snapshot, scale, left=0, top=0):
    if language not in _CACHE:
        _CACHE[language] = OriginalMap(language)
    return _CACHE[language].draw(snapshot, scale, left, top)

def original_map_result(language, snapshot, scale, left=0, top=0):
    """Return scanout and the actual native second-pass visited writes."""
    pixels = original_map(language, snapshot, scale, left, top)
    native = _CACHE[language]
    visited = bytes(native.c.mem_read(0x39396 if native.ru else 0x39446, 4096))
    return pixels, visited

