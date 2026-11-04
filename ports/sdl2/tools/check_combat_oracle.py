"""SP11 OG stat/weapon comparisons against freshly exported Ghidra instructions.

HUD, scheduler and animation image lookup are external boundaries. WORD arithmetic,
comparisons, weapon state/frame/shift and cancellation decisions run in Unicorn.
This is not a complete DOS session or a map playthrough.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp11-combat'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Oracle:
    def __init__(self):
        folder = OUT / 'old-gold'
        manifest = json.loads((folder / 'manifest.json').read_text())
        raw = (folder / 'objects.bin').read_bytes()
        assert sha(folder / 'objects.bin') == manifest['bytes_sha256']['objects']
        self.cpu = c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        c.mem_write(0x50000, raw)
        c.hook_add(UC_HOOK_CODE, self.hook)
        self.calls = set()
        self.cancelled = False

    def hook(self, c, address, size, unused):
        if address == 0x5fff0:
            c.emu_stop()
            return
        if c.reg_read(R.UC_X86_REG_CS) == 0x5000:
            return
        self.calls.add(hex(address))
        sp = c.reg_read(R.UC_X86_REG_SP)
        if address == 0x1aadc:  # 1aa1:00cc random frame image lookup.
            ptr = struct.unpack('<I', c.mem_read(0x30000 + sp + 10, 4))[0]
            linear = (ptr >> 16) * 16 + (ptr & 65535)
            c.mem_write(linear, struct.pack('<H', 200))
        if address == 0x2aaf3:  # 2a8d:0223 cancel current event.
            self.cancelled = True
        ip, cs = struct.unpack('<HH', c.mem_read(0x30000 + sp, 4))
        c.reg_write(R.UC_X86_REG_AX, 0)
        c.reg_write(R.UC_X86_REG_DX, 0)
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def run(self, offset, args):
        c = self.cpu
        c.mem_write(0x3f000, struct.pack('<HH', 0xfff0, 0x5000) + args)
        for reg, value in [(R.UC_X86_REG_CS, 0x5000), (R.UC_X86_REG_DS, 0x3000),
                           (R.UC_X86_REG_SS, 0x3000), (R.UC_X86_REG_SP, 0xf000),
                           (R.UC_X86_REG_BP, 0), (R.UC_X86_REG_EFLAGS, 2)]:
            c.reg_write(reg, value)
        c.emu_start(0x50000 + offset, 0, count=20000)
        assert c.reg_read(R.UC_X86_REG_IP) == 0xfff0, 'native function did not return'

    def stats(self, category, current, value, maximum):
        c = self.cpu
        c.mem_write(0x91000, bytes(0x200))
        attrs = [current] * 11
        attrs[2] = attrs[3] = maximum
        c.mem_write(0x9105e, struct.pack('<11h', *attrs))
        c.mem_write(0x91076, struct.pack('<3h', *([current] * 3)))
        c.mem_write(0x910d8, struct.pack('<h', current))
        c.mem_write(0x3358a, struct.pack('<I', 0x90003000))
        c.mem_write(0x93000, bytes(0x100))
        self.run(0x18b9, struct.pack('<Ihh', 0x90001000, category, value))
        return [c.reg_read(R.UC_X86_REG_AX),
                *struct.unpack('<11h', c.mem_read(0x9105e, 22)),
                *struct.unpack('<3h', c.mem_read(0x91076, 6)),
                *struct.unpack('<h', c.mem_read(0x910d8, 2)),
                struct.unpack('<H', c.mem_read(0x9300c, 2))[0]]

    def weapon(self, state, frame, shift):
        c = self.cpu
        c.mem_write(0x91000, bytes(0x200))
        for offset, value in [(0x83, state), (0x50, frame), (0x85, shift), (0x15, 0x1234), (0x52, 416)]:
            c.mem_write(0x91000 + offset, struct.pack('<H', value))
        c.mem_write(0x91048, struct.pack('<I', 0x90004000))
        c.mem_write(0x94000, struct.pack('<H', 3))
        self.cancelled = False
        self.run(0x2840, struct.pack('<I', 0x90001000))
        result = [struct.unpack('<h', c.mem_read(0x91000 + offset, 2))[0] for offset in (0x83, 0x50, 0x85, 0x15)]
        return result + [int(not self.cancelled), 0 if self.cancelled else (200 if state not in (0, 1) else 416)]


def main():
    originals = ['originals/Old_Gold_3D/OG.EXE', 'originals/Old_Gold_3D/OLD_GOLD.RES']
    before = {p: sha(ROOT / p) for p in originals}
    result = subprocess.run([str(ROOT / 'build/sdl2/windows/dok_combat_route_probe.exe'),
                             '--native-matrix', str(ROOT / originals[1])], capture_output=True, text=True, check=True)
    (OUT / 'cpp-matrix.csv').write_text(result.stdout)
    oracle = Oracle()
    vectors = []
    for line in result.stdout.splitlines():
        values = list(map(int, line.split(',')))
        kind, *data = values
        inputs = 4 if kind == 0 else 3
        native = oracle.stats(*data[:inputs]) if kind == 0 else oracle.weapon(*data[:inputs])
        port = data[inputs:]
        vectors.append(dict(kind='stat' if kind == 0 else 'weapon', input=data[:inputs], native=native, port=port, equal=native == port))
    after = {p: sha(ROOT / p) for p in originals}
    assert before == after
    report = dict(count=len(vectors), mismatches=sum(not v['equal'] for v in vectors), vectors=vectors,
                  external_calls=sorted(oracle.calls), sha256_before=before, sha256_after=after,
                  boundary=__doc__)
    (OUT / 'oracle-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"{report['count']} OG native stat/weapon vectors; {report['mismatches']} mismatches; hashes unchanged")
    for vector in vectors:
        if not vector['equal']:
            print(vector)
            break
    assert not report['mismatches']


if __name__ == '__main__':
    main()
