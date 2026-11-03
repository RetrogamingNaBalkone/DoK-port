"""Execute Ghidra-exported original 16-bit x86 fragments against the C++ model.

The fragments start after port I/O and end before storing through the far result
pointer. AX is the latched counter; DS contains original clock globals. This
checks arithmetic, not IRQ delivery, calibration measurement or PIT hardware.
"""
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, __version__ as unicorn_version
from unicorn.x86_const import UC_X86_REG_AX, UC_X86_REG_CS, UC_X86_REG_DS
from unicorn.x86_const import UC_X86_REG_SS, UC_X86_REG_SP, UC_X86_REG_EDX

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    rng = random.Random(0x50505)
    vectors = [(0, 65536, 0, 0, 0), (1, 118, 118, 0, 0),
               (65536, 65536, 0, 65535, 42), (0, 0, 0, 1, 0),
               (1, 119, 0, 0, 0xffffffff)]
    for i in (0, 1, 10000, 10001, 65535, 65536, 0x7fffffff, 0xffffffff):
        for period in (0, 119, 65536, 0xffffffff):
            for counter in (0, 1, 119, 65535):
                vectors.append((i, period, 0, counter, 0xfffffff0))
    vectors += [(rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(16),
                 rng.getrandbits(16), rng.getrandbits(32)) for _ in range(1000)]
    probe = ROOT / 'build/sdl2/windows/dok_clock_probe.exe'
    result = subprocess.run([str(probe)], input=''.join(' '.join(map(str, v)) + '\n' for v in vectors),
                            capture_output=True, text=True, check=True)
    expected = list(map(int, result.stdout.splitlines()))
    if len(expected) != len(vectors):
        raise RuntimeError('Probe returned wrong number of vectors')
    report = dict(scope='PIT arithmetic fragments only; not full clocks or scheduler',
                  unicorn=unicorn_version, vectors_per_profile=len(vectors),
                  probe_sha256=digest(probe), profiles=[])
    for name, globals_offset in [('ru', 0xe70), ('en', 0xf20), ('old-gold', 0xd40)]:
        folder = ROOT / 'research/evidence/source-port/sp05' / name
        manifest = json.loads((folder / 'manifest.json').read_text())
        if digest(folder / 'clock.bin') != manifest['bytes_sha256']:
            raise RuntimeError('Exported bytes changed')
        sample = ROOT / manifest['sample']
        if digest(sample) != manifest['sha256_before']:
            raise RuntimeError('Original hash mismatch')
        code = (folder / 'clock.bin').read_bytes()
        machine = Uc(UC_ARCH_X86, UC_MODE_16)
        machine.mem_map(0, 0x100000)
        machine.mem_write(0x10000, code)
        machine.reg_write(UC_X86_REG_CS, 0x1000)
        machine.reg_write(UC_X86_REG_DS, 0x2000)
        machine.reg_write(UC_X86_REG_SS, 0x3000)
        for index, ((interrupts, period, calibration, counter, base), wanted) in enumerate(zip(vectors, expected)):
            machine.mem_write(0x20000 + globals_offset, struct.pack('<IIIH', interrupts, base, period, calibration))
            machine.reg_write(UC_X86_REG_AX, counter)
            machine.reg_write(UC_X86_REG_SP, 0xff00)
            machine.emu_start(0x10000, 0x10000 + len(code), count=100)
            actual = machine.reg_read(UC_X86_REG_EDX)
            if actual != wanted or machine.reg_read(UC_X86_REG_SP) != 0xff00:
                raise RuntimeError(f'{name} vector {index}: x86={actual}, C++={wanted}')
        if digest(sample) != manifest['sha256_after']:
            raise RuntimeError('Original changed')
        report['profiles'].append(dict(profile=name, passed=len(vectors),
                                       original_sha256=manifest['sha256_before'],
                                       code_sha256=manifest['bytes_sha256']))
        print(name, len(vectors), 'original x86/C++ vectors passed')
    output = ROOT / 'build/sdl2/checks/clock-oracle.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
