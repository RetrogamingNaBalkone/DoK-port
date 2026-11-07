"""Exercise real RES drops while a selected game's story is open."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp-rc1-review/resource-refresh'
SOURCES = ('originals/Old_Gold_3D/OLD_GOLD.RES', 'originals/pkr/RUS/PKR.RES', 'originals/pkr/ENG/PKE.RES')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--expect-failure', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in SOURCES}
    results = []
    for dataset, source in enumerate(SOURCES):
        # Differing, structurally valid source: change one README character in a
        # local test copy. Original resources remain byte-for-byte unchanged.
        data = bytearray((ROOT / source).read_bytes())
        for i in range(struct.unpack_from('<H', data)[0]):
            at = 2 + i * 30
            if data[at:at + 16].split(b'\0')[0] == (b'INTRO4B' if dataset == 0 else b'README'):
                _, offset = struct.unpack_from('<II', data, at + 16)
                data[offset] ^= 1
                break
        else:
            raise AssertionError('Story fixture missing')
        variant = OUT / f'different-{dataset}.RES'
        variant.write_bytes(data)
        for fallback in (True, False):
            for backend in (('software',) if args.expect_failure else ('software', 'gl')):
                name = f'{dataset}-{int(fallback)}-{backend}-' + ('before' if args.expect_failure else 'after')
                directory = OUT / name
                directory.mkdir(exist_ok=True)
                profile = directory / 'user'
                ui = directory / 'ui.txt'
                script = '0 key Return\n'
                if fallback:
                    if dataset:
                        script += '1 key Down\n'
                    script += '2 key Return\n4 key Return\n'
                else:
                    script += '4 key Return\n'
                script += f'5 state story\n6 drop {variant}\n8 state refreshed\n9 key Return\n11 state reentered\n'
                ui.write_text(script, encoding='utf-8')
                cmd = [str(args.exe.resolve()), '--hidden', '--verbose', '--renderer', backend,
                       '--user-dir', str(profile), '--res', str(ROOT / source)]
                if fallback:
                    cmd += ['--res', str(ROOT / SOURCES[1 if dataset == 0 else 0])]
                cmd += ['--ui-script', str(ui), '--frames', '13']
                env = dict(os.environ, SDL_AUDIODRIVER='dummy')
                if args.expect_failure:
                    env['SDL_VIDEODRIVER'] = 'dummy'
                p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
                log = (profile / 'port.log').read_text(encoding='utf-8')
                (directory / 'port.log').write_text(log, encoding='utf-8')
                if args.expect_failure:
                    assert p.returncode != 0, (name, log)
                else:
                    assert p.returncode == 0 and 'OpenGL fallback:' not in log, (name, log, p.stderr)
                    states = dict(re.findall(r'^STATE (\w+) .*?menu=(\d+)', log, re.M))
                    assert states['story'] == ('5' if dataset == 0 else '4'), states
                    assert states['refreshed'] == '1', states
                    assert states['reentered'] == ('3' if fallback else '17'), states
                results.append(dict(name=name, command=cmd, returncode=p.returncode, stderr=p.stderr))
    assert before == {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in SOURCES}
    (OUT / ('before.json' if args.expect_failure else 'after.json')).write_text(json.dumps(
        dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(), sources=before, checks=results), indent=2), encoding='utf-8')
    print(f'PASS: {len(results)} resource-refresh processes; expect_failure={args.expect_failure}')


if __name__ == '__main__':
    main()
