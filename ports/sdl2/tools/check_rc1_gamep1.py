"""Reproduce RC1's Old Gold About crash, or verify its replacement menu flow."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp-rc1-review'
RES = ROOT / 'originals/Old_Gold_3D/OLD_GOLD.RES'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    exe = args.exe.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    before = hashlib.sha256(RES.read_bytes()).hexdigest()
    checks = []

    def run(name, script, extra=(), profile=None, backend='software', baseline=False):
        directory = OUT / name
        directory.mkdir(exist_ok=True)
        profile = profile or directory / 'user'
        ui = directory / 'ui.txt'
        ui.write_text(script, encoding='utf-8')
        cmd = [str(exe), '--hidden', '--verbose', '--renderer', backend,
               '--user-dir', str(profile), '--res', str(RES),
               '--ui-script', str(ui), '--frames', '20', *extra]
        env = dict(os.environ, SDL_AUDIODRIVER='dummy')
        if baseline:
            env['SDL_VIDEODRIVER'] = 'dummy'  # Fatal dialog cannot block unattended reproduction.
        result = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
        log = (profile / 'port.log').read_text(encoding='utf-8')
        (directory / 'port.log').write_text(log, encoding='utf-8')
        checks.append(dict(name=name, command=cmd, returncode=result.returncode))
        if baseline:
            assert result.returncode != 0 and 'Fatal error: Missing resource: GAMEP1' in log, log
        else:
            assert result.returncode == 0 and 'Fatal error' not in log, (name, log, result.stderr)
        return log, profile

    if args.baseline:
        run('baseline-about', '0 key Return\n2 key Return\n4 key Down\n4 key Down\n5 key Return\n', baseline=True)
    else:
        for backend in ('software', 'gl'):
            for locale in ('ru', 'en'):
                name = f'{backend}-{locale}'
                _, profile = run(name + '-save', '2 key F5\n',
                                 ('--level', 'LEVEL4', '--skip-intro', '--locale', locale), backend=backend)
                assert (profile / 'saves/old_gold/quick.sav').is_file()
                log, _ = run(name + '-menu',
                             '0 key Return\n2 key Return\n3 state game_menu\n'
                             '4 key Down\n4 key Down\n5 key Return\n7 state back\n'
                             '8 key Return\n10 key Return\n12 key Return\n14 state story\n'
                             '15 key Return\n18 state started\n',
                             ('--locale', locale), profile, backend)
                states = {m[1]: dict(re.findall(r'(\w+)=([^\s]*)', m[2]))
                          for m in re.finditer(r'^STATE (\w+) (.*)$', log, re.M)}
                assert states['game_menu']['menu'] == '31', states
                assert states['back']['menu'] == '1', states  # Old RC1 row 2 was About; now Back.
                assert states['story']['menu'] == '5', states
                assert 'world' in states['started'] and 'Started Old Gold LEVEL4' in log, log
    assert hashlib.sha256(RES.read_bytes()).hexdigest() == before
    report = dict(exe=str(exe), exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  res_sha256=before, baseline=args.baseline, checks=checks)
    (OUT / ('baseline.json' if args.baseline else 'fixed.json')).write_text(
        json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(checks)} processes; baseline={args.baseline}')


if __name__ == '__main__':
    main()
