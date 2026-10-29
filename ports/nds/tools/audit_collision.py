"""N10 original PKR/PKE collision instructions; source evidence, not a DOS session.

Ghidra-exported bytes execute in 16-bit Unicorn with valid relocated far data
pointers. Resolver and footprint callees execute without replacements. The
wrapper's far world-call is redirected to identical exported bytes at an
isolated address. No GBA engine or NDS collision model computes expectations.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/nds/n10'
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

SAMPLES = {
    'ru': ('originals/pkr/RUS/PKR.EXE', '12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'),
    'en': ('originals/pkr/ENG/PKE.EXE', 'cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b'),
}
LAYOUT = {
    'ru': dict(actor='1068', world='1098', clear=0x448, resolve=0x901,
               candidate=0xe13, wrapper=0xeb1, world_pointer=0x266a,
               mmap=0x608a, trig=0x1180, motion=0xa85, clock=0x1130,
               read=0x26f, stop=0x2b6, start=0x239),
    'en': dict(actor='1020', world='1030', clear=0x52c, resolve=0x9e5,
               candidate=0xef7, wrapper=0x13bb, world_pointer=0x271a,
               mmap=0x613a, trig=0x10c0, motion=0xf8f, clock=0x1078,
               read=0x1905, stop=0x194c, start=0x18cf),
}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=sha(path.read_bytes()))


def sources():
    result = {lang: sha((ROOT / path).read_bytes()) for lang, (path, _) in SAMPLES.items()}
    if any(result[lang] != expected for lang, (_, expected) in SAMPLES.items()):
        raise ValueError('Original executable SHA-256 differs')
    return result


def destination(path):
    path = Path(path).absolute()
    if '..' in path.parts or path == OUT or not path.is_relative_to(OUT):
        raise ValueError('Only N10 evidence output is allowed')
    for ancestor in [path, *path.parents]:
        if ancestor == ROOT.parent:
            break
        if ancestor.is_symlink() or (ancestor.exists() and ancestor.is_junction()):
            raise ValueError('Output alias rejected')
    if path.resolve() != path or (path.exists() and (not path.is_file() or path.stat().st_nlink != 1)):
        raise ValueError('Output is not an unaliased regular file')
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def fixture(language, level):
    # This is the independently recovered original Stage6 runtime model, not
    # any GBA engine output. Also compare its maps/definitions with accepted
    # N05 independent source parse (before the selected NDS monster profile).
    path = ROOT / f'research/evidence/runtime/{language}/LEVEL{level}-runtime.json'
    model = json.loads(path.read_text(encoding='utf-8'))
    nds_path = ROOT / f'research/evidence/nds/n05/oracle/level{level}-lang{1 if language == "ru" else 2}.json'
    nds = json.loads(nds_path.read_text(encoding='utf-8'))
    assert model['settings'] == nds['base']['settings']
    for obj, other in zip(model['objects'], nds['base']['objects'], strict=True):
        assert (obj['glyph'], obj['class'], obj['flags']) == (other['glyph'], other['class_id'], other['flags'])
    # EN localization follows the RU geometry in N05; original EN is separately
    # executed here on its own world. Match N05 only where its source is RU.
    if language == 'ru':
        assert model['maps'] == nds['world']['maps']
    width, _, height, side, _, _, _, radius = model['settings']
    flags, classes, known = [0] * 256, [0] * 256, [0] * 256
    for obj in model['objects']:
        glyph = obj['glyph']
        flags[glyph], classes[glyph], known[glyph] = obj['flags'], obj['class'], 1
    hero = model['objects'][model['player_index'] - 1]
    maps = [bytearray.fromhex(plane) for plane in model['maps'][:3]]
    maps[0] = maps[0].replace(bytes([hero['glyph']]), b' ')
    used = set(maps[0]) | set(maps[1])
    assert all(known[glyph] for glyph in used)
    return dict(width=width, height=height, side=side, radius=radius,
                glyph=hero['glyph'], flags=flags, classes=classes, known=known,
                maps=maps, pose=hero['actor'], pins=[pin(path), pin(nds_path)])


class Original:
    def __init__(self, language, f):
        self.language, self.p, self.f = language, LAYOUT[language], f
        self.cpu = c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        folder = OUT / 'original' / language
        manifest_path = folder / 'analysis-manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
        assert manifest['read_only'] is True
        assert manifest['sha256_before'] == manifest['sha256_after'] == SAMPLES[language][1]
        for role, path in [('script', ROOT / 'tools/ghidra/ExportNdsCollision.java'),
                           ('driver', ROOT / 'ports/nds/tools/export_collision_evidence.ps1')]:
            assert manifest[role + '_sha256'] == sha(path.read_bytes())
        self.pins = [pin(manifest_path), pin(folder / 'evidence.asm'), pin(folder / 'automatic.c')]
        for role, address in [('actor', 0x50000), ('world', 0x60000)]:
            path = folder / (self.p[role] + '.bin')
            raw = path.read_bytes()
            assert sha(raw) == manifest['binary_sha256'][self.p[role]]
            c.mem_write(address, raw)
            self.pins.append(pin(path))
        header = bytearray(0x750)
        struct.pack_into('<3h', header, 0, f['width'], 1, f['height'])
        struct.pack_into('<h', header, 10, f['radius'])
        struct.pack_into('<h', header, 0x738, f['side'])
        struct.pack_into('<i', header, 0x742, f['width'])
        for glyph in range(256):
            off = 0x1000 + glyph * 0x80
            struct.pack_into('<HH', header, 0x30e + glyph * 4, off, 0x8000)
            obj = bytearray(0x80)
            struct.pack_into('<H', obj, 0x12, f['classes'][glyph])
            struct.pack_into('<H', obj, 0x15, f['flags'][glyph])
            c.mem_write(0x80000 + off, bytes(obj))
        c.mem_write(0x90000, bytes(header))
        c.mem_write(0x30000 + self.p['world_pointer'], struct.pack('<HH', 0, 0x9000))
        self.reset_maps()
        ram_path = ROOT / 'research/evidence/reference/ru-03/level1-memory.bin'
        ram = ram_path.read_bytes()
        assert sha(ram) == 'c8ff1347518aae86af31b8ed2c6f6443c4c63c9157b4a52bf76d53c7f5c95629'
        c.mem_write(self.p['trig'] * 16, ram[0x1ca9b0:0x1ca9b0 + 722 * 4])
        self.pins.append(pin(ram_path))
        self.stopped = False
        self.elapsed = 0
        self.queries, self.substeps = [], []
        c.hook_add(UC_HOOK_CODE, self.hook)

    def reset_maps(self):
        for plane, data in enumerate(self.f['maps']):
            self.cpu.mem_write(0x30000 + self.p['mmap'] + plane * 4356, bytes(data))

    def counter(self, glyph, value):
        self.cpu.mem_write(0x81000 + glyph * 0x80 + 0x56, struct.pack('<H', value))

    def reg(self, name):
        return self.cpu.reg_read(getattr(R, 'UC_X86_REG_' + name))

    def hook(self, c, address, size, unused):
        if address in (0x6fff0, 0x5fff0):
            self.stopped = True
            c.emu_stop()
        elif address in [self.p['clock'] * 16 + self.p[key] for key in ('read', 'stop', 'start')]:
            sp = self.reg('SP')
            ip, cs = struct.unpack('<HH', c.mem_read(0x30000 + sp, 4))
            if address == self.p['clock'] * 16 + self.p['read']:
                c.reg_write(R.UC_X86_REG_AX, self.elapsed & 65535)
                c.reg_write(R.UC_X86_REG_DX, self.elapsed >> 16)
            c.reg_write(R.UC_X86_REG_SP, sp + 4)
            c.reg_write(R.UC_X86_REG_CS, cs)
            c.reg_write(R.UC_X86_REG_IP, ip)
        elif address == int(self.p['world'], 16) * 16 + self.p['candidate']:
            # Relocate only control transfer, preserving original arguments,
            # return address, actual instructions and all memory side effects.
            x, y, z = struct.unpack('<iii', c.mem_read(0x40126, 12))
            self.substeps.append(dict(x=x, y=y, z=z))
            c.reg_write(R.UC_X86_REG_CS, 0x6000)
            c.reg_write(R.UC_X86_REG_IP, self.p['candidate'])
        elif address == 0x60000 + self.p['clear']:
            self.queries.append(struct.unpack('<iih', c.mem_read(0x30000 + self.reg('SP') + 8, 10)))

    def call(self, entry, args, segment=0x6000):
        c = self.cpu
        for name, value in [('SS', 0x3000), ('CS', segment), ('SP', 0xf000), ('BP', 0xf800), ('DS', 0x3000), ('ES', 0x8000)]:
            c.reg_write(getattr(R, 'UC_X86_REG_' + name), value)
        c.mem_write(0x3f000, struct.pack('<HH', 0xfff0, segment) + args)
        self.stopped = False
        self.queries = []
        c.emu_start(segment * 16 + entry, 0xfffff, count=100000)
        if not self.stopped or self.reg('SP') != 0xf004:
            raise AssertionError(('Native did not return', self.language, hex(entry), hex(self.reg('CS')), hex(self.reg('IP'))))

    def query(self, vector):
        op, x, z, nx, nz = vector
        if op == 0:
            self.call(self.p['clear'], struct.pack('<HHiiH', 0, 0x9000, x, z, self.f['radius'] & 65535))
            return (self.reg('AX'), x, z)
        self.cpu.mem_write(0x40200, struct.pack('<ii', x, z))
        self.call(self.p['resolve'], struct.pack('<HHHHHHii', 0, 0x9000, 0x200, 0x4000, 0x204, 0x4000, nx, nz))
        return (0, *struct.unpack('<ii', self.cpu.mem_read(0x40200, 8)))

    def move(self, x, z, distance, heading, strafe=0, *, y=-300, upper=-274, lower=-326):
        actor = bytearray(0x100)
        struct.pack_into('<iii', actor, 0x26, x, y, z)
        struct.pack_into('<h', actor, 0x32, heading)
        struct.pack_into('<hh', actor, 0x9e, upper, lower)
        actor[0x5c] = self.f['glyph']
        self.cpu.mem_write(0x40100, bytes(actor))
        self.substeps = []
        self.call(self.p['wrapper'], struct.pack('<HHhH', 0x100, 0x4000, distance, strafe), 0x5000)
        x, result_y, z = struct.unpack('<iii', self.cpu.mem_read(0x40126, 12))
        bounds = struct.unpack('<hh', self.cpu.mem_read(0x4019e, 4))
        assert (result_y, *bounds) == (y, upper, lower)
        size = self.f['width'] * self.f['height']
        mmap = bytes(self.cpu.mem_read(0x30000 + self.p['mmap'], size))
        counters = [struct.unpack('<H', self.cpu.mem_read(0x81000 + glyph * 0x80 + 0x56, 2))[0] for glyph in range(256)]
        return dict(x=x, z=z, y=result_y, bounds=bounds, mmap_sha256=sha(mmap), counters=counters,
                    substeps=self.substeps, queries=self.queries)

    def run_motion(self, values):
        from audit_movement import FIELDS
        data = bytearray(0x100)
        for name, offset, code in FIELDS:
            struct.pack_into('<' + code, data, offset, values[name])
        data[0x5c] = self.f['glyph']
        self.cpu.mem_write(0x40100, bytes(data))
        self.elapsed = values['elapsed']
        self.substeps = []
        self.call(self.p['motion'], struct.pack('<HH', 0x100, 0x4000), 0x5000)
        raw = self.cpu.mem_read(0x40100, 0x100)
        result = {name: struct.unpack_from('<' + code, raw, offset)[0] for name, offset, code in FIELDS}
        return dict(state=result, moved=self.reg('AX'), substeps=len(self.substeps))


def vectors(f, rng):
    result = []
    side = f['side']
    for row in range(f['height']):
        for col in range(f['width']):
            x, z = col * side + side // 2, row * side + side // 2
            result.extend([(0, x, z, 0, 0), (1, x, z, x + rng.randint(-180, 180), z + rng.randint(-180, 180))])
    # Both signs, exact equal magnitudes, branch centers, cell-edge r/r+1,
    # signed quotient low-WORD wrap and arithmetic wrap around int32 limits.
    coords = [-2147483648, -2147483647, -2147483410, -15728640, -481, -480, -239, -1,
              0, 237, 238, 239, 240, 241, 242, 479, 480, 481, 30719, 30720,
              15728640, 2147483409, 2147483646, 2147483647]
    for x in coords:
        for z in coords:
            result.append((0, x, z, 0, 0))
    for _ in range(1536):
        x = rng.randint(-side, f['width'] * side + side)
        z = rng.randint(-side, f['height'] * side + side)
        dx = rng.choice([-180, -120, -1, 0, 1, 120, 180])
        dz = rng.choice([-180, -120, -1, 0, 1, 120, 180, dx, -dx])
        result.append((1, x, z, x + dx, z + dz))
    return result


def fixture_wire(f):
    # Explicit LE wire format, independent of host structure layout.
    data = struct.pack('<5i', f['width'], f['height'], f['side'], f['radius'], f['glyph'])
    data += struct.pack('<256H', *f['flags']) + struct.pack('<256H', *f['classes'])
    data += bytes(f['known']) + b''.join(bytes(plane) for plane in f['maps'])
    return data


def c_queries(exe, f, cases):
    data = fixture_wire(f) + b''.join(struct.pack('<5i', *case) for case in cases)
    raw = subprocess.run([str(exe)], input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
    if len(raw) != len(cases) * 12:
        raise AssertionError(('C query transport length', len(raw), len(cases)))
    return list(struct.iter_unpack('<3i', raw))


def c_wrappers(exe, f, walk):
    records = [(2 + item['input'][4], *item['input'][:4]) for item in walk]
    data = fixture_wire(f) + b''.join(struct.pack('<5i', *case) for case in records)
    process = subprocess.run([str(exe)], input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    raw = process.stdout
    cells = f['width'] * f['height']
    stride = 32 + cells + 1024
    assert len(raw) == stride * len(walk), ('C wrapper transport length', len(raw), stride * len(walk))
    exact, guarded = 0, []
    for index, item in enumerate(walk):
        status, x, z, requested, committed, adjusted, rejected, reason = struct.unpack_from('<8i', raw, index * stride)
        mmap = raw[index * stride + 32:index * stride + 32 + cells]
        counters = list(struct.unpack_from('<256I', raw, index * stride + 32 + cells))
        source = item['result']
        same = (x, z, sha(mmap), counters) == (source['x'], source['z'], source['mmap_sha256'], source['counters'])
        if status == 0 and reason == 0:
            assert same, ('Unexplained C/native wrapper difference', index, item['input'], [x, z, sha(mmap)], source)
            assert requested == committed == 3
            exact += 1
        else:
            # Runtime guards are deliberately outside the native resolver.
            # Each case gets fresh identical maps/counters, preventing an
            # earlier guarded pose from masking a later native disagreement.
            raise AssertionError(('Unexpected runtime guard in frozen source-domain wrapper walk',
                                  index, item['input'], status, reason, rejected))
    return dict(exact=exact, guarded=guarded, isolated_fixture_reset=True)


def controlled(language, base, probe=None):
    """Execute intentional counter/geometry fixtures, with no expected model."""
    f = copy.deepcopy(base)
    size = f['width'] * f['height']
    f['maps'] = [bytearray(b' ' * size) for _ in range(3)]
    x = z = 5 * f['side'] + f['side'] // 2
    cell = 5 * f['width'] + 5
    door = next(g for g in range(256) if f['known'][g] and f['classes'][g] == 7)
    wall = next(g for g in range(256) if f['known'][g] and f['classes'][g] == 2 and not f['flags'][g] & 16)
    result = []
    # Height/FCMap variations do not affect this geometry path. These do not
    # run the full hero callback, which DOES inspect FCMap gameplay effects.
    for variant, y, upper, lower in [('baseline', -300, -274, -326),
                                     ('high-y', 2000000000, 32767, -32768),
                                     ('low-y', -2000000000, -1, 1)]:
        f['maps'][2] = bytearray(bytes([wall if variant != 'baseline' else 32]) * size)
        native = Original(language, f)
        value = native.move(x, z, 480, 45, y=y, upper=upper, lower=lower)
        result.append(dict(case='height-fcmap-' + variant, result=value))
    assert len({(r['result']['x'], r['result']['z'], r['result']['mmap_sha256'], tuple(r['result']['counters'])) for r in result}) == 1
    f['maps'][2] = bytearray(b' ' * size)
    f['flags'][door] |= 16
    f['maps'][1][cell] = door
    for initial, expected in [(0, 1), (1, 1), (32768, 32768), (65535, 65535)]:
        native = Original(language, f)
        native.counter(door, initial)
        value = native.move(x, z, 0, 90)
        assert value['counters'][door] == expected
        result.append(dict(case='stationary-door', initial_counter=initial, result=value))
    f['maps'][1][cell] = 32
    f['maps'][1][cell + 1] = door
    native = Original(language, f)
    native.counter(door, 65535)
    value = native.move(x, z, 324, 90)
    assert value['x'] // f['side'] == 6 and value['counters'][door] == 0
    result.append(dict(case='destination-door-wrap', initial_counter=65535, result=value))
    # Source footprint clamps the lookup rather than the coordinate itself.
    f['maps'][1][cell + 1] = 32
    native = Original(language, f)
    outside = [(0, -1000, z, 0, 0), (1, 100, z, -80, z),
               (1, 30600, z, 30780, z)]
    outputs = [native.query(case) for case in outside]
    assert outputs[0][0] == 1 and outputs[1][1] < f['radius'] and outputs[2][1] + f['radius'] >= f['width'] * f['side']
    result.append(dict(case='native-open-edge-outside', inputs=outside, outputs=outputs,
                       nds_difference='Bounded actor admission keeps its old pose at the real edge'))
    # Native wrapper erases even a foreign glyph at the actor center. NDS
    # ownership preflight refuses this corrupted input before mutation.
    f['maps'][0][cell] = wall
    native = Original(language, f)
    value = native.move(x, z, 0, 90)
    assert bytes(native.cpu.mem_read(0x30000 + native.p['mmap'] + cell, 1)) == bytes([f['glyph']])
    result.append(dict(case='native-overwrites-foreign-occupancy', source_glyph=wall,
                       result=value, nds_difference='Bounded actor preflight refuses foreign old-cell glyph'))
    if probe:
        # The port admits a bounded initial pose, then keeps it when a source
        # resolver would publish an outside footprint. Check bytes, not hashes
        # alone, and require exactly the documented guard/status.
        for kind in ('edge', 'foreign'):
            f['maps'][0] = bytearray(b' ' * size)
            old_x = f['radius'] + 1 if kind == 'edge' else x
            if kind == 'foreign': f['maps'][0][cell] = wall
            request=(2,old_x,z,-720 if kind=='edge' else 120,90)
            raw=subprocess.run([str(probe)],input=fixture_wire(f)+struct.pack('<5i',*request),stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stdout
            assert len(raw)==32+size+1024
            status,px,pz,requested,committed,adjusted,rejected,reason=struct.unpack_from('<8i',raw)
            counts=list(struct.unpack_from('<256I',raw,32+size))
            expected=bytearray(f['maps'][0])
            if kind=='edge':
                assert status==0 and requested==committed==rejected==3 and reason==1
                expected[(z//f['side'])*f['width']+old_x//f['side']]=f['glyph']
            else:
                assert status==3 and requested==committed==rejected==reason==0
            assert (px,pz)==(old_x,z) and raw[32:32+size]==expected and counts==[0]*256
            result.append(dict(case='nds-explicit-'+kind+'-guard',input=request,status=status,
                               reason_mask=reason,requested=requested,committed=committed,rejected=rejected,
                               exact_old_pose=True,exact_mmap=True,exact_all_counters=True))
    return result


def scalar_widths(language, base, probe):
    """Broader public source-query arithmetic domain, outside actor admission."""
    values=[-2147483648,-2147483647,-15728640,-481,-1,0,1,479,480,15728640,2147483646,2147483647]
    records=[]
    for side,radius in [(1,-32768),(2,-1),(3,0),(479,1),(480,238),(32767,32767)]:
        f=copy.deepcopy(base);f['side']=side;f['radius']=radius
        original=Original(language,f)
        cases=[]
        for i,x in enumerate(values):
            for j,z in enumerate(values):
                cases.append((0,x,z,0,0))
                cases.append((1,x,z,values[(i+j+1)%len(values)],values[(i*3+j+2)%len(values)]))
        expected=[original.query(case) for case in cases]
        if probe:
            actual=c_queries(probe,f,cases)
            for case,source,port in zip(cases,expected,actual,strict=True):
                assert source==port, ('Source scalar width mismatch',language,side,radius,case,source,port)
        trace=b''.join(struct.pack('<8i',*case,*value) for case,value in zip(cases,expected,strict=True))
        records.append(dict(side=side,radius=radius,cases=len(cases),trace_sha256=sha(trace)))
    return dict(language=language,fixtures=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path)
    parser.add_argument('--output', type=Path, default=OUT / 'original-collision.json')
    args = parser.parse_args()
    before = sources()
    reports, pins, special, widths = [], [], [], []
    rng = random.Random(0x4e3130)
    for language in ('ru', 'en'):
        for level in (1, 2, 3):
            f = fixture(language, level)
            original = Original(language, f)
            cases = vectors(f, rng)
            expected = []
            for case in cases:
                expected.append(original.query(case))
            actual = c_queries(args.probe, f, cases) if args.probe else None
            if actual is not None:
                for case, source, port in zip(cases, expected, actual, strict=True):
                    assert source == port, (language, level, case, source, port)
            stream = b''.join(struct.pack('<8i', *case, *value) for case, value in zip(cases, expected, strict=True))
            # A source pose walk exercises map/counter writes and three
            # quarter-substeps. Each wrapper gets fresh identical map/counter
            # backing, so defensive NDS differences cannot propagate silently.
            pose = f['pose']
            x, z = pose['x'], pose['z']
            walk = []
            for index in range(192):
                distance = rng.choice((-720, -480, -240, -120, -4, -1, 0, 1, 4, 120, 240, 480, 720))
                heading, strafe = rng.randrange(361), rng.randrange(2)
                original.reset_maps()
                for glyph in range(256):
                    original.counter(glyph, 0)
                record = original.move(x, z, distance, heading, strafe)
                walk.append(dict(input=[x, z, distance, heading, strafe], result=record))
                x, z = record['x'], record['z']
            comparison = c_wrappers(args.probe, f, walk) if args.probe else None
            reports.append(dict(language=language, level=level, cases=len(cases), query_trace_sha256=sha(stream),
                                wrapper_calls=len(walk), wrapper_substeps=sum(len(item['result']['substeps']) for item in walk),
                                final_pose=[x, z], comparison=comparison, walk=walk))
            pins += f['pins'] + original.pins
            if level == 1:
                special.append(dict(language=language, fixtures=controlled(language, f, args.probe)))
                widths.append(scalar_widths(language,f,args.probe))
            print(f'N10 original {language} level{level}: {len(cases)} queries, {len(walk)} wrappers', flush=True)
    after = sources()
    assert before == after
    unique = {item['path']: item for item in pins}
    result = dict(task='N10', status='passed' if args.probe else 'original-execution-only',
                  oracle='Unmodified original instructions from fresh read-only Ghidra exports',
                  unicorn=unicorn.__version__, address_system='Ghidra NE loader; valid real-mode relocation for execution',
                  original_sha256_before=before, original_sha256_after=after, reports=reports,
                  controlled=special,
                  scalar_widths=widths,
                  driver=pin(Path(__file__)),
                  inputs=list(unique.values()), probe=pin(args.probe.absolute()) if args.probe else None,
                  probe_source=pin(ROOT / 'ports/nds/tests/collision_dump.c'),
                  trig=pin(ROOT / 'build/nds/n10/generated/render_trig.inc'),
                  limitations=['Not a full live DOS/DPMI session', 'No x87 trig initializer replay; pinned original runtime trig table',
                               'Door state machine not executed; native WORD occupancy counters execute',
                               'Geometry queries ignore FCMap/y/bounds; full hero callback still handles FCMap class12/13 effects'])
    destination(args.output).write_bytes((json.dumps(result, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8'))


if __name__ == '__main__':
    main()
