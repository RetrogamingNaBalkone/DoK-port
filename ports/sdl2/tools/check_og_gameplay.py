"""Reproduce OG pickups/fire through a source-map walk and the real SDL main loop.

The replay records logical controls and mouse headings at player callback ticks;
it cannot write positions or grant equipment. AI, collisions, pickups, weapons,
projectiles and animations use the ordinary application callbacks. This short
spawn/money/first-wand/magic route is not a complete level playthrough.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp11-combat'
BUILD = ROOT / 'build/sdl2/windows'
RES = ROOT / 'originals/Old_Gold_3D/OLD_GOLD.RES'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, name, timeout=180):
    start = time.monotonic()
    result = subprocess.run([str(p) for p in command], capture_output=True, text=True, timeout=timeout,
                            env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
    (OUT / (name + '.stdout.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{name}: {result.returncode}\n{result.stdout}\n{result.stderr}')
    return dict(command=[str(p) for p in command], returncode=result.returncode,
                seconds=time.monotonic()-start, stdout=result.stdout, stderr=result.stderr)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    paths = [ROOT / p['sample'] for p in inventory] + [RES, ROOT/'originals/pkr/RUS/PKR.RES', ROOT/'originals/pkr/ENG/PKE.RES']
    before = {str(p.relative_to(ROOT)): sha(p) for p in paths}
    route = OUT / 'og-route.replay'
    tests = [run([BUILD/'dok_combat_route_probe.exe', RES], 'routes-30-60-144'),
             run([BUILD/'dok_combat_route_probe.exe', '--record', route, RES], 'record-route'),
             run([BUILD/'dok_item_tests.exe', RES, ROOT/'originals/pkr/RUS/PKR.RES', ROOT/'originals/pkr/ENG/PKE.RES'], 'placed-items')]
    rows = route.read_text().splitlines()
    first_fire = next(i for i, row in enumerate(rows[1:], 1) if row.split()[7] == '1')
    firing = OUT / 'og-firing.replay'
    firing.write_text('\n'.join(rows[:first_fire+5]) + '\n')
    results = []
    for stage, recording, hud in [('route', route, 'classic'), ('firing', firing, 'desktop')]:
        for backend in ['gl', 'software']:
            name = f'main-{stage}-{backend}'
            user_dir = OUT / name
            user_dir.mkdir(exist_ok=True)
            # Stable test settings; all persistence stays in the evidence folder.
            (user_dir/'settings.ini').write_text('[settings]\nversion=1\n[interface]\nfps=0\n')
            command = [BUILD/'OGDoK.exe', '--res', RES, '--level', 'LEVEL4', '--skip-intro',
                       '--renderer', backend, '--hud', hud, '--size', '800x600', '--user-dir', user_dir,
                       # Stop at the recording endpoint. An explicit --frames
                       # now requests a neutral-input tail for session tests.
                       '--input-replay', recording, '--screenshot', OUT/(name+'.png')]
            result = run(command, name)
            log = (user_dir/'port.log').read_text()
            match = re.search(r'^REPLAY (.*)$', log, re.M)
            assert match, f'{name}: recording did not reach its end\n{log}'
            state = dict(field.split('=', 1) for field in match.group(1).split())
            assert state['current'] == '1' and state['weapons'] == '112,0'
            assert state['score'] == '2' and int(state['hp']) > 0
            if stage == 'route':
                # SP10's native-verified OG bob ends this recording at the -26
                # height clamp; the old -200 baseline omitted original bobbing.
                assert state['pose'] == '8904,-226,15960,90' and state['charge'] == '147' and state['shots'] == '0'
                assert log.count('Picked object 105\n') == 2 and 'Picked object 112\n' in log
                assert 'Picked object 109\n' in log and 'Picked object 108\n' in log
            else:
                assert 0 < int(state['charge']) < 150 and int(state['shots']) > 0
            assert (OUT/(name+'.png')).is_file()
            result.update(name=name, stage=stage, backend=backend, state=state, log=log,
                          screenshot=str((OUT/(name+'.png')).relative_to(ROOT)),
                          screenshot_sha256=sha(OUT/(name+'.png')))
            results.append(result)
            print(name, state, flush=True)
    for stage in ['route', 'firing']:
        states = [r['state'] for r in results if r['stage'] == stage]
        assert states[0] == states[1], f'{stage}: backend changed logical state'
    after = {str(p.relative_to(ROOT)): sha(p) for p in paths}
    assert before == after
    report = dict(tests=tests, processes=results, sha256_before=before, sha256_after=after,
                  executable_sha256=sha(BUILD/'OGDoK.exe'), replay_sha256=sha(route),
                  boundary=__doc__, input='Scripted logical axes/use/fire and mouse headings; fixed 16,666,667 ns per rendered frame; not physical device testing.')
    (OUT/'gameplay-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('OG natural route, 1547 placed items, four real SDL processes PASS; original hashes unchanged')


if __name__ == '__main__':
    main()
