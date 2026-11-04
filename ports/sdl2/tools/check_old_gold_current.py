"""Audit OG current magnitude and nominal cadence against Ghidra x86 exports.

Open geometry isolates speed from the documented bank-collision adaptation.
This is not a wall-clock DOSBox capture or a complete player-loop equivalence.
"""
import json
import struct
import subprocess
from pathlib import Path

from check_interaction_oracle import Oracle, sha
from check_movement_oracle import Original

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp15-ui/r5-water'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    folder = ROOT / 'research/evidence/source-port/sp10-interactions/ghidra/old-gold'
    manifest = json.loads((folder / 'manifest.json').read_text())
    sample = ROOT / manifest['sample']
    before = sha(sample)
    assert before == manifest['sha256_before'] == manifest['sha256_after']
    code = (folder / 'objects.bin').read_bytes()
    data = (folder / 'vtables.bin').read_bytes()
    # Constructor schedules the descriptor at DS:0486 with interval 0x1a0.
    assert code[0x1321:0x1327] == bytes.fromhex('66 68 a0 01 00 00')
    assert code[0x1327:0x132a] == bytes.fromhex('b8 86 04')
    assert struct.unpack_from('<4H', data, 0x486 - 0x400) == (0x1e16, 0x1ae1, 0, 0)
    model = json.loads(subprocess.run([str(ROOT / 'build/sdl2/windows/dok_inspect.exe'), '--json',
                                     str(ROOT / 'originals/Old_Gold_3D/OLD_GOLD.RES')],
                                    capture_output=True, text=True, check=True).stdout)['levels'][0]
    original = Original('old-gold', model)
    # Ensure the executable movement bytes are exactly the newly exported ASM sample.
    assert original.raw['actor'] == code
    surface = Oracle(1)
    side = original.side
    x, z = side * 32 + side // 2, side * 32 + side // 2
    checks, vectors = [], []
    for category in (100, 101, 102):
        for value in (-100, 100):
            surface.surface(category, value)
            for count in (1, 24):
                original.reset_maps(empty=True)
                actor = bytearray(0xde)
                struct.pack_into('<iii', actor, 0x26, x, original.spawn['y'], z)
                actor[0x5c] = original.hero
                original.cpu.mem_write(0x4e000, bytes(actor))
                for _ in range(count):
                    original.call(0x18b9, struct.pack('<Ihh', 0x4000e000, category, value), 0x5000)
                pose = struct.unpack('<iii', original.cpu.mem_read(0x4e026, 12))
                expected = (x + (0 if category == 102 else value * count), z + (value * count if category == 102 else 0))
                assert (pose[0], pose[2]) == expected
                vectors.append(f'5 {x} {z} {category} {value} {count}\n')
                checks.append(dict(category=category, value=value, calls=count, native_pose=expected))
                if count == 24:
                    for fps in (30, 60, 144):
                        vectors.append(f'6 {x} {z} {category} {value} {fps}\n')
                        checks.append(dict(category=category, value=value, calls=count, fps=fps, native_pose=expected))
    result = subprocess.run([str(ROOT / 'build/sdl2/windows/dok_movement_probe.exe'),
                             str(ROOT / 'originals/Old_Gold_3D/OLD_GOLD.RES'), 'LEVEL4'],
                            input=''.join(vectors), text=True, capture_output=True, check=True)
    host = [tuple(map(int, line.split(','))) for line in result.stdout.splitlines()]
    assert host == [c['native_pose'] for c in checks], (host, checks)
    assert sha(sample) == before
    report = dict(sample=manifest['sample'], sha256_before=before, sha256_after=sha(sample),
                  address_system='Ghidra MZ segment:offset, not file offset or DPMI selector',
                  interval=416, tick_hz=1193182/119, nominal_calls_per_second=1193182/119/416,
                  cell_side=side, nominal_cells_per_second=100*(1193182/119/416)/side,
                  placed_surface_definitions=[o for o in model['objects'] if o['class']==12],
                  checks=checks,
                  boundary='Native surface arguments + native stat/collision in open geometry; DOS frame delay and bank collision differ explicitly')
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS {len(checks)} current vectors; nominal {report["nominal_cells_per_second"]:.3f} cells/s; original unchanged')


if __name__ == '__main__':
    main()
