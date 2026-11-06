"""Reproduce the repeated-switch freeze and verify switching plus fire recover."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp22-weapon-handoff-r5'
BEFORE = ROOT / 'research/experiments/sp22-weapon-handoff-r5/before.exe'
AFTER = ROOT / 'build/sdl2/windows/OGDoK.exe'
PROFILES = [('old_gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', 'LEVEL4'),
            ('kremlin_pkr', 'originals/pkr/RUS/PKR.RES', 'LEVEL1'),
            ('kremlin_pke', 'originals/pkr/ENG/PKE.RES', 'LEVEL1')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    originals = {path: sha(ROOT / path) for _, path, _ in PROFILES}
    before_hash = sha(BEFORE)
    rows = []
    for dataset, path, level in PROFILES:
        replay = OUT / (dataset+'.replay')
        replay.write_text(f'DOK_INPUT_REPLAY 1 {dataset} {level} 0\n' +
                          ''.join(f'{n*416} 83 0 0 0 0 0 0 0 -1 0 0\n' for n in range(1, 601)))
        script = OUT / (dataset+'.script')
        script.write_text(''.join(f'0 key {c}\n' for c in 'IDKFA') +
                          ''.join(f'{n} key 1\n' for n in range(100, 601)) +
                          '750 state settled\n800 mouse_down 1\n800 mouse_up 1\n900 state fired\n'
                          '920 key 2\n1040 state switched\n1080 key Left Ctrl\n1200 state fired_again\n')
        for backend in ('gl', 'software'):
            for label, exe in [('before', BEFORE), ('after', AFTER)]:
                user = OUT / f'{dataset}-{backend}-{label}'
                user.mkdir(exist_ok=True)
                (user/'settings.ini').write_text('[settings]\nversion=1\n[audio]\nmaster=0\n[interface]\nfps=0\n')
                (user/'port.log').unlink(missing_ok=True)
                command = [str(exe), '--hidden', '--renderer', backend, '--size', '640x480', '--res', str(ROOT/path),
                           '--level', level, '--skip-intro', '--user-dir', str(user), '--input-replay', str(replay),
                           '--replay-live-input', '--ui-script', str(script), '--frames', '1220']
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                        env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=90)
                log = (user/'port.log').read_text(encoding='utf-8')
                assert result.returncode == 0, (label, dataset, backend, result.stderr, log)
                states = {name: dict(re.findall(r'(\w+)=([^\s]+)', fields))
                          for name, fields in re.findall(r'^STATE (\w+) (.*)$', log, re.M)}
                settled, fired, switched, again = [states[k] for k in ('settled', 'fired', 'switched', 'fired_again')]
                if label == 'before':
                    assert settled['gun_state'] == '0' and settled['gun_events'] == '0', (dataset, backend, states)
                    assert again['charge'] == settled['charge'] and again['weapon'] == settled['weapon'], states
                else:
                    # A final request during a busy animation can leave a pending
                    # slot; the invariant here is a live, ready gun and recovery.
                    assert settled['gun_state'] == '7' and settled['gun_events'] == '1', states
                    assert int(fired['charge']) < int(settled['charge']), states
                    assert switched['weapon'] != settled['weapon'] and switched['gun_state'] == '7', states
                    assert int(again['charge']) < int(fired['charge']), states
                rows.append(dict(dataset=dataset, backend=backend, label=label, states=states, command=command, exit_code=0))
                print(dataset, backend, label, 'PASS', flush=True)
    assert sha(BEFORE) == before_hash and originals == {path: sha(ROOT/path) for _, path, _ in PROFILES}
    (OUT/'report.json').write_text(json.dumps(dict(before_sha256=before_hash, after_sha256=sha(AFTER),
                                                 originals_sha256=originals, processes=rows), indent=2)+'\n')


if __name__ == '__main__':
    main()
