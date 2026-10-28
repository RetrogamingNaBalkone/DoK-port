"""G07 original x86 arithmetic oracle. Clock and collisions are controlled.

Uses Ghidra-exported original instructions, never WASDify payloads. Does not
claim to execute a complete DOS session. Shared Unicorn installation is read-only.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def s32(n):
    return (n + 2**31) % 2**32 - 2**31


class Original:
    def __init__(self, language):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
        from unicorn import x86_const as reg
        self.reg = reg
        self.lang = language
        self.ru = language == 'ru'
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.world = Uc(UC_ARCH_X86, UC_MODE_16)
        self.directory = ROOT/'research/evidence/movement'/language
        for cpu in (self.cpu, self.world):
            cpu.mem_map(0, 0x100000)
            cpu.reg_write(reg.UC_X86_REG_SS, 0x3000)
        self.cpu.mem_write(0x50000, (self.directory/'actor-segment.bin').read_bytes())
        self.world.mem_write(0x60000, (self.directory/'world-segment.bin').read_bytes())
        text = (ROOT/'ports/gba/src/renderer_trig.inc').read_text()
        self.trig = list(map(int, re.findall(r'-?\d+', text.split('{', 1)[1])))
        assert len(self.trig) == 722
        trig_selector = 0x1180 if self.ru else 0x10c0
        self.world.mem_write(trig_selector*16, struct.pack('<722i', *self.trig))
        self.clock_base = (0x1130 if self.ru else 0x1078)*16
        self.clock_read = 0x26f if self.ru else 0x1905
        self.clock_stop = 0x2b6 if self.ru else 0x194c
        self.clock_start = 0x239 if self.ru else 0x18cf
        self.entry = 0xa85 if self.ru else 0xf8f
        self.translate = 0xeb1 if self.ru else 0x13bb
        self.world_entry = 0xe13 if self.ru else 0xef7
        self.collision = 0x901 if self.ru else 0x9e5
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)
        self.world.hook_add(UC_HOOK_CODE, self.world_hook)
        self.substeps = 0

    def get(self, cpu, name):
        return cpu.reg_read(getattr(self.reg, 'UC_X86_REG_'+name))

    def set(self, cpu, name, value):
        cpu.reg_write(getattr(self.reg, 'UC_X86_REG_'+name), value)

    def ret(self, cpu):
        sp = self.get(cpu, 'SP')
        ip, cs = struct.unpack('<HH', cpu.mem_read(0x30000+sp, 4))
        self.set(cpu, 'SP', sp+4)
        self.set(cpu, 'CS', cs)
        self.set(cpu, 'IP', ip)

    def world_hook(self, cpu, address, size, context):
        if address == 0x60000+self.collision:
            sp = self.get(cpu, 'SP')
            # far return, world pointer, x pointer, z pointer, desired x/z
            xoff, xseg, zoff, zseg, x, z = struct.unpack('<HHHHii', cpu.mem_read(0x30000+sp+8, 16))
            cpu.mem_write(xseg*16+xoff, struct.pack('<i', x))
            cpu.mem_write(zseg*16+zoff, struct.pack('<i', z))
            self.ret(cpu)

    def displace(self, data, distance, strafe):
        cpu = self.world
        cpu.mem_write(0x40100, bytes(data))
        cpu.mem_write(0x4013c, struct.pack('<i', distance))
        self.set(cpu, 'CS', 0x6000)
        self.set(cpu, 'SP', 0x8000)
        self.set(cpu, 'BP', 0x9000)
        cpu.mem_write(0x38000, struct.pack('<7H', 0xfff0, 0x6000, 0, 0, 0x126, 0x4000, strafe))
        cpu.emu_start(0x60000+self.world_entry, 0x6fff0, count=5000)
        assert self.get(cpu, 'SP') == 0x8004
        self.substeps += 1
        return cpu.mem_read(0x40126, 12)

    def hook(self, cpu, address, size, context):
        if address in (self.clock_base+self.clock_read, self.clock_base+self.clock_stop,
                       self.clock_base+self.clock_start):
            if address == self.clock_base+self.clock_read:
                self.set(cpu, 'AX', self.dt & 65535)
                self.set(cpu, 'DX', self.dt >> 16)
            self.ret(cpu)
        elif address == 0x50000+self.translate:
            sp = self.get(cpu, 'SP')
            _, _, distance, strafe = struct.unpack('<HHhH', cpu.mem_read(0x30000+sp+4, 8))
            data = bytearray(cpu.mem_read(0x40100, 0xde))
            # Native wrapper is documented by its Ghidra SAR AX,2 / loop 1..<4.
            for _ in range(3):
                data[0x26:0x32] = self.displace(data, distance >> 2, strafe)
            struct.pack_into('<i', data, 0x3c, distance)
            cpu.mem_write(0x40100, bytes(data))
            self.ret(cpu)

    def run(self, v):
        f, self.dt, x, y, z, heading, angle, speed, aa, ab, command, flags, remembered, step_distance, policy = v
        if policy:
            if f & 0x30 == 0x30:
                f &= ~0x30
            if f & 0x180000 == 0x180000:
                f &= ~0x180000
            aa = 10000 if f & 0x180030 else 0
            remembered = f if f & 0x180030 else 0
        data = bytearray(0xde)
        for off, fmt, value in ((0x1c,'I',f|flags),(0x26,'i',x),(0x2a,'i',y),(0x2e,'i',z),
                                (0x32,'h',heading),(0x34,'h',angle),(0x38,'i',speed),
                                (0x3c,'i',step_distance),(0x90,'i',aa),(0x94,'i',ab),
                                (0x82,'h',command),(0x9a,'I',remembered)):
            struct.pack_into('<'+fmt, data, off, value)
        self.cpu.mem_write(0x40100, bytes(data))
        self.set(self.cpu,'CS',0x5000)
        self.set(self.cpu,'SP',0x8000)
        self.set(self.cpu,'BP',0x9000)
        self.cpu.mem_write(0x38000,struct.pack('<4H',0xfff0,0x5000,0x100,0x4000))
        self.cpu.emu_start(0x50000+self.entry,0x5fff0,count=10000)
        assert self.get(self.cpu,'SP') == 0x8004
        data = self.cpu.mem_read(0x40100,0xde)
        fields = [(0x26,'i'),(0x2a,'i'),(0x2e,'i'),(0x32,'h'),(0x7c,'h'),(0x7e,'h'),
                  (0x90,'i'),(0x94,'i'),(0x82,'h'),(0x98,'h'),(0x3c,'i'),(0x1c,'i'),(0x9a,'i')]
        return [struct.unpack_from('<'+fmt,data,off)[0] for off,fmt in fields]+[self.get(self.cpu,'AX')]


def host(vectors):
    payload=''.join(' '.join(str(n & 0xffffffff) for n in v)+'\n' for v in vectors)
    result=subprocess.run([str(ROOT/'build/gba/host/dok-movement-probe.exe')],input=payload,
                          text=True,capture_output=True,check=True)
    return [list(map(int,line.split())) for line in result.stdout.splitlines()]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unicorn-dir',type=Path,default=Path('C:/Source/wasdify/build/research-deps'))
    args=parser.parse_args()
    sys.path.insert(0,str(args.unicorn_dir))
    import unicorn
    rng=random.Random(7)
    vectors=[]
    for heading in (0,1,89,90,179,180,269,270,358,359):
        for f in (0,4,8,12,16,32,48,0x80000,0x100000,0x180000,20,24,0x2000|16,0x4000000|16):
            for dt in (0,100,416,832,1248,20000):
                vectors.append([f,dt,15120,-300,22800,heading,5,120,10000,10000,0,0,16,0,0])
    for _ in range(1500):
        f=rng.choice([0,4,8,12,16,32,0x80000,0x100000,0x80014,0x102008,0x2000|16,0x4000000|32])
        vectors.append([f,rng.choice([1,100,416,1000,0x7fffffff,0x80000000,0xffffffff]),
                        rng.choice([-2**31,2**31-1,rng.randrange(-100000,100000)]),-300,
                        rng.choice([-2**31,2**31-1,22800]),rng.randrange(360),5,
                        0 if f&12 else rng.choice([120,0,-1,32767,2**31-1,-2**31]),
                        rng.choice([0,10000,2**31-1,-2**31]),rng.choice([0,10000,2**31-1,-2**31]),
                        rng.randrange(2),0,rng.choice([16,32,0x80000,0x100000]),0,0])
    # Corrupt heading/angle fields: native single-word wrap is preserved, but no
    # unsafe table access is requested. Free-flight guards those lookups separately.
    for heading in (-32768,-1,32767):
        for angle in (-32768,-1,32767):
            for f in (4,8,12):
                vectors.append([f,416,0,-300,0,heading,angle,0,0,10000,0,0,0,0,0])
    corrected=[]
    for _ in range(300):
        v=[rng.choice([0,16,32,48,0x80000,0x100000,0x180000,0x2000|16]),416,
           15120,-300,22800,rng.randrange(360),5,120,rng.randrange(10001),0,0,0,16,0,1]
        corrected.append(v)
    vectors+=corrected
    expected=host(vectors)
    languages=[]
    for language in ('ru','en'):
        inventory=json.loads((ROOT/f'analysis/originals/inventory-{language}.json').read_text())
        sample=ROOT/inventory['file'];before=sha(sample)
        assert before==inventory['sha256']
        oracle=Original(language)
        for index,(v,want) in enumerate(zip(vectors,expected)):
            got=oracle.run(v)
            assert got==want,(language,index,v,want,got)
        assert sha(sample)==before
        manifest=json.loads((oracle.directory/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['script_sha256']==sha(ROOT/'tools/ghidra/InspectMovement.java')
        languages.append(dict(language=language,sample=inventory['file'],sha256_before=before,sha256_after=sha(sample),
                              cases=len(vectors),executed_world_substeps=oracle.substeps,
                              actor_export_sha256=sha(oracle.directory/'actor-segment.bin'),
                              world_export_sha256=sha(oracle.directory/'world-segment.bin'),
                              listing_sha256=sha(oracle.directory/'evidence.asm'),ghidra_manifest=manifest))
    report=dict(task='G07',status='passed',unicorn_version=unicorn.__version__,languages=languages,
                original_vectors=len(vectors)-len(corrected),corrected_policy_vectors=len(corrected),
                controlled_clock=True,collision='intercepted: accept x/z destination; no map or door writes',
                translate_wrapper='intercepted: SAR16 distance,2; three world calls; final signed distance',
                original_trig='G04 verified table used as runtime input; no x87 initializer executed here',
                trig_sha256=sha(ROOT/'ports/gba/src/renderer_trig.inc'),dos_game_session=False,
                script_sha256=sha(Path(__file__)))
    (ROOT/'analysis/gba/g07-movement-evidence.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (ROOT/'research/evidence/movement/reference-vectors.json').write_text(json.dumps(dict(vectors=vectors,expected=expected)))
    print(f'G07 original x86 arithmetic: {len(vectors)} vectors per language passed (clock/collision controlled)')


if __name__=='__main__':
    main()
