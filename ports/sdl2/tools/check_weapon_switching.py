"""Check live 1/2 and wheel toggles in both backends, using a fixed clock."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'research/evidence/source-port/sp22-weapons-r4'
EXE = ROOT / 'build/sdl2/windows/OGDoK.exe'
PROFILES = [('old_gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', 'LEVEL4'),
            ('kremlin_pkr', 'originals/pkr/RUS/PKR.RES', 'LEVEL1'),
            ('kremlin_pke', 'originals/pkr/ENG/PKE.RES', 'LEVEL1')]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    original_hashes = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for _, path, _ in PROFILES}
    rows = []
    for dataset, path, level in PROFILES:
        replay = OUT / (dataset + '.replay')
        replay.write_text(f'DOK_INPUT_REPLAY 1 {dataset} {level} 0\n' +
                          ''.join(f'{416*n} 83 0 0 0 0 0 0 0 -1 0 0\n' for n in range(1, 601)))
        script = OUT / (dataset + '.script')
        commands = ''.join(f'0 key {c}\n' for c in 'IDKFA') + '80 state ready\n'
        for i, action in enumerate(['key 1', 'key 1', 'key 2', 'key 2', 'wheel 1', 'wheel 1', 'wheel -1', 'wheel -1']):
            frame = 100 + i * 140
            commands += f'{frame} {action}\n{frame+120} state switch{i}\n'
        script.write_text(commands)
        for backend in ('gl', 'software'):
            user = OUT / f'{dataset}-{backend}'
            user.mkdir(exist_ok=True)
            (user / 'settings.ini').write_text('[settings]\nversion=1\n[audio]\nmaster=0\n[interface]\nfps=0\n')
            command = [str(EXE), '--hidden', '--renderer', backend, '--size', '640x480', '--res', str(ROOT / path),
                       '--level', level, '--skip-intro', '--verbose', '--user-dir', str(user), '--input-replay', str(replay),
                       '--replay-live-input', '--ui-script', str(script), '--frames', '1220']
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                    env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=90)
            log = (user / 'port.log').read_text(encoding='utf-8')
            assert result.returncode == 0, (dataset, backend, result.stderr, log)
            states = {name: dict(re.findall(r'(\w+)=([^\s]+)', fields))
                      for name, fields in re.findall(r'^STATE (\w+) (.*)$', log, re.M)}
            initial = int(states['ready']['weapon'])
            assert initial in (1, 2), states
            for i in range(8):
                state = states[f'switch{i}']
                assert int(state['weapon']) == (3-initial if i % 2 == 0 else initial), (dataset, backend, i, states)
                assert state['menu'] == '0' and state['pending'] == state['weapon'], (dataset, backend, i, state)
            rows.append(dict(dataset=dataset, backend=backend, command=command, states=states, exit_code=0))
            print(dataset, backend, '1/1/2/2/up/up/down/down PASS', flush=True)
    assert original_hashes == {path: hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for _, path, _ in PROFILES}
    (OUT / 'report.json').write_text(json.dumps(dict(processes=rows, originals_sha256=original_hashes), indent=2)+'\n')


if __name__ == '__main__':
    main()
