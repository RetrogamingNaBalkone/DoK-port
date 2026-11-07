"""Verify the opt-in hit setting through menus, restart and quickload."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp-optional-hits'
RES = ('originals/pkr/RUS/PKR.RES', 'originals/pkr/ENG/PKE.RES', 'originals/Old_Gold_3D/OLD_GOLD.RES')
TOGGLE = ('1 key Escape\n2 key Down\n3 key Return\n4 key Down\n4 key Down\n4 key Down\n'
          '5 key Return\n6 key Down\n6 key Down\n6 key Down\n7 key Return\n9 state toggled\n'
          '10 key Escape\n11 key Escape\n12 key Escape\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    args = parser.parse_args()
    checks = []
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in RES}
    for dataset, resource in enumerate(RES):
        for backend in ('software', 'gl'):
            directory = OUT / f'{dataset}-{backend}'
            directory.mkdir(parents=True, exist_ok=True)
            profile = directory / 'user'

            def run(name, script):
                ui = directory / (name + '.txt')
                ui.write_text(script, encoding='utf-8')
                cmd = [str(args.exe.resolve()), '--hidden', '--verbose', '--renderer', backend,
                       '--locale', 'ru' if backend == 'gl' else 'en', '--user-dir', str(profile),
                       '--res', str(ROOT / resource), '--level', 'LEVEL4' if dataset == 2 else 'LEVEL1',
                       '--skip-intro', '--ui-script', str(ui), '--frames', '20']
                p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=30,
                                   env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
                log = (profile / 'port.log').read_text(encoding='utf-8')
                (directory / (name + '.log')).write_text(log, encoding='utf-8')
                assert p.returncode == 0 and 'OpenGL fallback:' not in log, (cmd, log, p.stderr)
                states = dict(re.findall(r'^STATE (\w+) .*?boundary_hits=(\d+)', log, re.M))
                config = configparser.ConfigParser()
                config.read(profile / 'settings.ini', encoding='utf-8')
                checks.append(dict(command=cmd, states=states, boundary_hits=config['gameplay']['boundary_hits']))
                return states, config, log

            # Each completed previous run leaves the flag disabled.
            states, config, log = run('enable-load', '0 state native\n' + TOGGLE +
                                     '13 key F5\n15 key F9\n17 state loaded\n')
            assert states == {'native': '0', 'toggled': '1', 'loaded': '1'}, states
            assert config['gameplay']['boundary_hits'] == '1' and 'SAVE loaded slot=0' in log, log
            states, _, _ = run('restart', '2 state restarted\n')
            assert states == {'restarted': '1'}, states
            states, config, _ = run('disable', TOGGLE)
            assert states == {'toggled': '0'} and config['gameplay']['boundary_hits'] == '0', states
    assert before == {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in RES}
    (OUT / 'process-report.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(), resources=before, checks=checks), indent=2), encoding='utf-8')
    print(f'{len(checks)} opt-in/menu/restart/quickload processes PASS')


if __name__ == '__main__':
    main()
