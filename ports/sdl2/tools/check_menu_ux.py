"""SP15: real menu/HUD readbacks, map pause and resource selection/restart."""
import configparser
import hashlib
import json
import os
import sys
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
EXE = ROOT / 'build/sdl2/windows/dok_port.exe'
OUT = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT / 'research/evidence/source-port/sp15-ui'
RES = [ROOT / path for path in ('originals/Old_Gold_3D/OLD_GOLD.RES',
                               'originals/pkr/RUS/PKR.RES', 'originals/pkr/ENG/PKE.RES')]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in RES}
    checks = []

    def run(name, args=(), script='', frames=8, profile=None, cwd=ROOT, reset=True):
        directory = OUT / name
        directory.mkdir(exist_ok=True)
        profile = profile or directory / 'user'
        profile.mkdir(exist_ok=True)
        if reset:
            (profile / 'settings.ini').write_text('[settings]\nversion=1\n[interface]\nfps=0\n[audio]\nmaster=0\n')
        path = directory / 'script.txt'
        path.write_text(script, encoding='utf-8')
        command = [str(EXE), '--hidden', '--user-dir', str(profile), '--ui-script', str(path),
                   '--frames', str(frames), '--screenshot', str(directory / 'frame.png'), *map(str, args)]
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=60, env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
        log = (profile / 'port.log').read_text(encoding='utf-8')
        (directory / 'port.log').write_text(log, encoding='utf-8')
        assert result.returncode == 0, (name, result.stdout, result.stderr, log)
        checks.append(dict(name=name, command=command, exit=0))
        return log, profile

    # The same physical text size at 4:3 and 16:9 heights; all editions, no abbreviations.
    for locale in ('ru', 'en'):
        run('selection-' + locale, ['--renderer', 'software', '--size', '1280x720', '--locale', locale,
                                   *sum((['--res', p] for p in RES), [])], '0 key Return\n', frames=3)
    for profile, res, level in [('og', RES[0], 'LEVEL4'), ('ru', RES[1], 'LEVEL1')]:
        for backend in ('gl', 'software'):
            for size in ('1280x720', '960x720'):
                run(f'hud-{profile}-{backend}-{size}',
                    ['--renderer', backend, '--res', res, '--level', level, '--skip-intro',
                     '--hud', 'desktop', '--size', size],
                    ''.join('0 key ' + char + '\n' for char in 'IDKFA'), frames=30)
        run('hud-' + profile + '-no-message',
            ['--renderer', 'software', '--res', res, '--level', level, '--skip-intro',
             '--hud', 'desktop', '--size', '1280x720'], frames=4)

    log, _ = run('map-pause', ['--renderer', 'software', '--res', RES[1], '--level', 'LEVEL1', '--skip-intro'],
                 '1 keydown Tab\n2 state open\n3 state still\n3 shot ' + str(OUT / 'map-open.png') + '\n4 keyup Tab\n5 key Tab\n6 state resumed\n')
    states = {m[1]: dict(re.findall(r'(\w+)=([^\s]*)', m[2])) for m in re.finditer(r'^STATE (\w+) (.*)$', log, re.M)}
    for key in ('world', 'ticks', 'pose', 'hp', 'charge', 'maps'):
        assert states['open'][key] == states['still'][key], (key, states)
    assert states['resumed']['menu'] == '0' and states['resumed']['world'] == states['open']['world']

    # A valid image-payload variant retains its own source and must be explicitly chosen.
    fixture = ROOT / 'build/sdl2/checks/sp15-resource-fixture'
    fixture.mkdir(parents=True, exist_ok=True)
    variant = bytearray(RES[1].read_bytes())
    for i in range(struct.unpack_from('<H', variant)[0]):
        at = 2 + i * 30
        size, offset = struct.unpack_from('<II', variant, at + 16)
        if size >= 18:
            width, height = struct.unpack_from('<HH', variant, offset)
            if width and height and width * height == size - 18 and struct.unpack_from('<I', variant, offset + 10)[0] == size - 18:
                variant[offset + size - 1] ^= 1
                break
    chosen = fixture / 'variant.res'
    added = fixture / 'data.res'
    chosen.write_bytes(variant)
    added.write_bytes(RES[1].read_bytes())
    try:
        # Primary Add row is first; keyboard navigation skips three read-only statuses.
        log, user = run('choose-source', ['--renderer', 'software', '--res', RES[1], '--res', chosen],
                        '0 key Down\n1 key Down\n2 key Return\n3 key Down\n4 key Down\n5 key Return\n6 state selected\n')
        cfg = configparser.ConfigParser()
        cfg.read(user / 'settings.ini')
        assert bytes.fromhex(cfg['resources']['choice2']).decode() == str(chosen), cfg['resources']
        restart, _ = run('source-restart', ['--renderer', 'software', '--level', 'LEVEL1', '--skip-intro'],
                         profile=user, reset=False, frames=3)
        assert 'Started Kremlin PKR LEVEL1' in restart

        # Browse a real directory and admit a file without CLI --res.
        # Hide the second variant so this folder contains exactly one candidate.
        chosen.rename(fixture / 'variant.hidden')
        log, user = run('browse-add', ['--renderer', 'software'],
                        '0 key Return\n1 key Return\n2 key Down\n2 key Down\n3 key Return\n5 state admitted\n',
                        cwd=fixture)
        assert 'Loaded Kremlin PKR' in log, log
        cfg.read(user / 'settings.ini')
        assert int(cfg['resources']['count']) == 1
        restart, _ = run('browse-restart', ['--renderer', 'software', '--level', 'LEVEL1', '--skip-intro'],
                         profile=user, reset=False, frames=3)
        assert 'Started Kremlin PKR LEVEL1' in restart
    finally:
        for path in (chosen, fixture / 'variant.hidden', added):
            path.unlink(missing_ok=True)

    assert before == {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in RES}
    report = dict(task='SP15', processes=checks, map_preserves_world=True,
                  source_choice_restart=True, browser_add_restart=True, originals_sha256=before)
    (OUT / 'process-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: {len(checks)} real processes; map pause, source choice and browser restart')


if __name__ == '__main__':
    main()
