"""Check repeated IDKFA through SDL input in the delivered Windows package."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp-idkfa-refill'
EXE = ROOT / 'artifacts/sdl2/windows-playtest-ogdok/OGDoK.exe'
PROFILES = [('old_gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', 'LEVEL4'),
            ('kremlin_pkr', 'originals/pkr/RUS/PKR.RES', 'LEVEL1'),
            ('kremlin_pke', 'originals/pkr/ENG/PKE.RES', 'LEVEL1')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    originals = {path: sha(ROOT/path) for _, path, _ in PROFILES}
    rows = []
    for dataset, path, level in PROFILES:
        replay = OUT / (dataset+'.replay')
        replay.write_text(f'DOK_INPUT_REPLAY 1 {dataset} {level} 0\n' +
                          ''.join(f'{416*n} 83 0 0 0 0 0 0 0 -1 0 0\n' for n in range(1, 401)))
        script = OUT / (dataset+'.script')
        code = lambda frame: ''.join(f'{frame} key {c}\n' for c in 'IDKFA')
        script.write_text(code(0)+'100 state full\n120 key Left Ctrl\n180 state spent\n'+
                          code(200)+'220 state refilled\n'+code(240)+'260 state repeated\n')
        for backend in ('gl', 'software'):
            user = OUT / f'{dataset}-{backend}'
            user.mkdir(exist_ok=True)
            (user/'settings.ini').write_text('[settings]\nversion=1\n[audio]\nmaster=0\n')
            command = [str(EXE), '--hidden', '--renderer', backend, '--size', '640x480',
                       '--res', str(ROOT/path), '--level', level, '--skip-intro', '--verbose', '--user-dir', str(user),
                       '--input-replay', str(replay), '--replay-live-input', '--ui-script', str(script), '--frames', '280']
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                    env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=45)
            log = (user/'port.log').read_text(encoding='utf-8')
            assert result.returncode == 0, (result.stderr, log)
            states = {name: dict(re.findall(r'(\w+)=([^\s]+)', fields))
                      for name, fields in re.findall(r'^STATE (\w+) (.*)$', log, re.M)}
            full, spent, refilled, repeated = [states[k] for k in ('full', 'spent', 'refilled', 'repeated')]
            assert int(spent['charge']) < int(full['charge']), states
            assert refilled['charge'] == repeated['charge'] == full['charge'], states
            assert len({s['weapon'] for s in states.values()}) == 1, states
            assert log.count('\nIDKFA\n') == 3, log
            rows.append(dict(dataset=dataset, backend=backend, states=states, command=command))
            print(dataset, backend, 'refill PASS', flush=True)
    assert originals == {path: sha(ROOT/path) for _, path, _ in PROFILES}
    (OUT/'report.json').write_text(json.dumps(dict(executable_sha256=sha(EXE),
        originals_sha256=originals, processes=rows), indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
