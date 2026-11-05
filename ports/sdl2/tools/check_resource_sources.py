"""SP03: exact duplicate classification and real-process CLI source selection."""
import hashlib
import json
import struct
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/sdl2/checks/resource-sources'
APP = ROOT / 'build/sdl2/windows/dok_port.exe'
CORE = ROOT / 'build/sdl2/windows/dok_core_tests.exe'
PROFILES = [ROOT / p for p in ('originals/Old_Gold_3D/OLD_GOLD.RES',
            'originals/pkr/RUS/PKR.RES', 'originals/pkr/ENG/PKE.RES')]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in PROFILES}
    core = subprocess.run([str(CORE), *map(str, PROFILES)], capture_output=True, text=True, check=True)
    report = {'core': core.stdout, 'cases': [], 'sha256_before': before}
    variant = bytearray(PROFILES[1].read_bytes())
    # Change one palette index in a valid standalone image, preserving all formats.
    for i in range(struct.unpack_from('<H', variant)[0]):
        at = 2 + i * 30
        size, offset = struct.unpack_from('<II', variant, at + 16)
        if size >= 18:
            width, height = struct.unpack_from('<HH', variant, offset)
            if width and height and width * height == size - 18 and struct.unpack_from('<I', variant, offset + 10)[0] == size - 18:
                variant[offset + size - 1] ^= 1
                break
    else:
        raise AssertionError('No image mutation candidate')
    changed = OUT / 'variant.res'
    broken = OUT / 'broken.res'
    copied = OUT / 'identical.res'
    changed.write_bytes(variant)
    broken.write_bytes(b'bad')
    copied.write_bytes(PROFILES[1].read_bytes())

    def run(name, args, code=0, required=(), absent=()):
        user = OUT / name
        result = subprocess.run([str(APP), '--hidden', '--renderer', 'software', '--size', '320x200',
                                 '--frames', '2', '--user-dir', str(user), *map(str, args)],
                                capture_output=True, text=True, timeout=45)
        log = (user / 'port.log').read_text(encoding='utf-8')
        assert result.returncode == code, (name, result.returncode, result.stderr, log)
        for text in required:
            assert text in log + result.stderr, (name, text, log, result.stderr)
        for text in absent:
            assert text not in log, (name, text, log)
        report['cases'].append({'name': name, 'exit': result.returncode, 'log': log, 'stderr': result.stderr})
        return log

    try:
        run('duplicate', ['--res', PROFILES[1], '--res', PROFILES[1], '--level', 'LEVEL1', '--skip-intro'],
            required=('Duplicate ignored', 'Started Kremlin PKR'), absent=('Source selection required',))
        run('ambiguous', ['--res', PROFILES[1], '--res', changed],
            required=('Source selection required',), absent=('Started',))
        run('alias_choice', ['--res', PROFILES[1], '--res', copied, '--res', changed,
                            '--res-source', copied, '--level', 'LEVEL1', '--skip-intro'],
            required=('Selected source:', 'Started Kremlin PKR'))
        run('selected', ['--res', PROFILES[1], '--res', changed, '--res-source', changed,
                         '--level', 'LEVEL1', '--skip-intro'], required=('Selected source:', 'Started Kremlin PKR'))
        run('bad_choice', ['--res', PROFILES[1], '--res-source', broken], code=1,
            required=('--res-source must name an admitted --res source',), absent=('Started',))
        run('isolated_error', ['--res', broken, '--res', PROFILES[2], '--level', 'LEVEL1', '--skip-intro'],
            required=('Invalid RES directory', 'Started Kremlin PKE'))
        run('other_dataset', ['--res', PROFILES[1], '--res', changed, '--res', PROFILES[2],
                              '--level', 'LEVEL1', '--skip-intro'], required=('Started Kremlin PKE',), absent=('Started Kremlin PKR',))
        run('empty', [], absent=('Started',))
        for name, dropped in (('drop_duplicate', PROFILES[1]), ('drop_variant', changed)):
            script = OUT / (name + '.txt')
            script.write_text(f'0 key Escape\n1 state before\n1 drop {dropped}\n2 state after\n', encoding='utf-8')
            log = run(name, ['--res', PROFILES[1], '--level', 'LEVEL1', '--skip-intro',
                             '--frames', '4', '--ui-script', script], required=('Resource load deferred: active world',))
            states = re.findall(r'world=([^\n]+)', log)
            assert len(states) == 2 and states[0] == states[1], (name, states)
    finally:
        changed.unlink(missing_ok=True)
        broken.unlink(missing_ok=True)
        copied.unlink(missing_ok=True)
    after = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in PROFILES}
    assert before == after
    report['sha256_after'] = after
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: three profile resource tests; {len(report['cases'])} application scenarios; original RES hashes unchanged")


if __name__ == '__main__':
    main()
