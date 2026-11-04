"""Record raw SDL-key/mouse firing after IDKFA; a passing check is not a bug fix.

User settings are read-only and copied into private evidence profiles. Hidden
presentation disables fullscreen in the copy. SDL audio always uses dummy.
Initial tests use the normal clock and input path; post-pickup tests first walk
the recorded original route, then switch to raw SDL input without changing pose.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp11-idkfa-r2'
RES = ROOT / 'originals/Old_Gold_3D/OLD_GOLD.RES'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def code(frame):
    return ''.join(f'{frame} key {key}\n' for key in 'IDKFA')


def states(log):
    return {name: dict(re.findall(r'(\w+)=([^ ]+)', fields))
            for name, fields in re.findall(r'^STATE (\S+) (.*)$', log, re.M)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include-r1', action='store_true')
    parser.add_argument('--exe', type=Path, default=ROOT/'build/sdl2/windows/dok_port.exe')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    prefs = Path(os.environ['APPDATA'])/'DoK-port/SourcePort/settings.ini'
    settings = prefs.read_bytes()
    original_settings_hash = sha(prefs)
    # Only the private copy is changed: fullscreen cannot be meaningful hidden.
    copied_settings = re.sub(rb'(?m)^fullscreen=1\r?$', b'fullscreen=0', settings)
    before = {str(p.relative_to(ROOT)): sha(p) for p in [RES, ROOT/'originals/Old_Gold_3D/OG.EXE']}
    reports = []
    core = subprocess.run([str(ROOT/'build/sdl2/windows/dok_idkfa_tests.exe'), str(RES)],
                          capture_output=True, text=True, check=True)
    (OUT/'core.log').write_text(core.stdout + core.stderr)
    rows = (ROOT/'research/evidence/source-port/sp11-combat/og-route.replay').read_text().splitlines()
    first_fire = next(i for i, row in enumerate(rows[1:], 1) if row.split()[7] == '1')
    pickup_rows = rows[:first_fire]
    prefix_tick = int(pickup_rows[-1].split()[0])
    prefix_frame = math.ceil(prefix_tick * 119000000000 / (1193182 * 16666667))
    prefix = OUT/'natural-pickup.replay'
    prefix.write_text('\n'.join(pickup_rows)+'\n')
    for natural in [False, True]:
        for backend in ['gl', 'software']:
            name = ('natural-' if natural else 'initial-') + backend
            user = OUT/name
            user.mkdir(exist_ok=True)
            (user/'settings.ini').write_bytes(copied_settings)
            base = prefix_frame + 12 if natural else 0
            script = code(base) + (f'{base} live_input on\n' if natural else '')
            script += f'{base+20} state granted\n{base+30} keydown Left Ctrl\n{base+170} state ctrl-held\n{base+180} keyup Left Ctrl\n'
            script += code(base+200)
            script += f'{base+230} mouse_down 1\n{base+370} state mouse-held\n{base+380} mouse_up 1\n'
            script += f'{base+390} keydown 2\n{base+391} keyup 2\n{base+460} mouse_down 1\n{base+590} state second-held\n{base+600} mouse_up 1\n'
            # Ordinary elapsed time is not the fixed replay clock. Leave enough
            # wall-clock time for idle/hide/show before testing the second wand.
            frame_scale = 1 if natural else 5
            if frame_scale != 1:
                script = ''.join(str(int(line.split(' ', 1)[0])*frame_scale)+' '+line.split(' ', 1)[1]+'\n' for line in script.splitlines())
            script_path = OUT/(name+'.script')
            script_path.write_text(script)
            command = [args.exe, '--res', RES, '--level', 'LEVEL4', '--skip-intro',
                       '--renderer', backend, '--hidden', '--user-dir', user, '--ui-script', script_path,
                       '--frames', str((base+620)*frame_scale), '--screenshot', OUT/(name+'.png')]
            if natural:
                command += ['--input-replay', prefix]
            result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                                    env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=120)
            log = (user/'port.log').read_text()
            (OUT/(name+'.stdout.log')).write_text(result.stdout+result.stderr)
            snapshot = states(log)
            assert result.returncode == 0, result.stderr
            assert log.count('\nIDKFA\n') == 2, log
            assert 'Audio driver: dummy' in log and 'Diagnostic hidden presentation:' in log
            charges = [int(snapshot[key]['charge']) for key in ['granted', 'ctrl-held', 'mouse-held', 'second-held']]
            assert charges[0] > charges[1] > charges[2] > charges[3] > 0, (name, charges, log)
            assert int(snapshot['second-held']['weapon']) == 2
            assert int(snapshot['second-held']['gun_events']) > 0
            reports.append(dict(name=name, command=[str(x) for x in command], returncode=result.returncode,
                                state=snapshot, stdout=result.stdout, stderr=result.stderr, log=log,
                                mode='Recorded ordinary route then raw SDL keys/mouse' if natural else 'Raw SDL keys/mouse with ordinary elapsed clock'))
            print(name, 'charge', charges, 'raw Ctrl/left mouse/second wand fired', flush=True)
    if args.include_r1:
        exe = ROOT/'artifacts/sdl2/windows-playtest-20261104-sp08-sp16-r1/dok_port.exe'
        original_exe_hash = sha(exe)
        user = OUT/'r1-readonly-artifact'
        user.mkdir(exist_ok=True)
        (user/'settings.ini').write_bytes(settings)
        script_path = OUT/'r1-raw.script'
        script_path.write_text(code(0)+'50 state granted\n100 keydown Left Ctrl\n2700 state ctrl-held\n2800 keyup Left Ctrl\n')
        command = [exe, '--res', RES, '--level', 'LEVEL4', '--skip-intro', '--renderer', 'software',
                   '--user-dir', user, '--ui-script', script_path, '--frames', '3000']
        result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                                env=dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software'), timeout=120)
        log = (user/'port.log').read_text()
        snapshot = states(log)
        assert result.returncode == 0 and int(snapshot['ctrl-held']['charge']) < int(snapshot['granted']['charge'])
        assert sha(exe) == original_exe_hash
        reports.append(dict(name='r1-readonly-artifact', executable_sha256=original_exe_hash,
                            command=[str(x) for x in command], returncode=result.returncode, state=snapshot, log=log,
                            boundary='Unmodified r1 artifact, ordinary clock/raw Ctrl. Dummy VIDEO prevents any desktop window; not a real GL/focus test.'))
        print('unchanged r1 raw Ctrl fired; dummy video/audio', flush=True)
    after = {str(p.relative_to(ROOT)): sha(p) for p in [RES, ROOT/'originals/Old_Gold_3D/OG.EXE']}
    assert before == after and sha(prefs) == original_settings_hash
    report = dict(user_symptom_status='OPEN: refusal after IDKFA not reproduced; no gameplay fix claimed',
                  runs=reports, core_scenarios=21, core_log=core.stdout,
                  source_settings_sha256_before=original_settings_hash, source_settings_sha256_after=sha(prefs),
                  copied_setting_change='fullscreen=0 only, in private copies for hidden current-app checks',
                  original_sha256_before=before, original_sha256_after=after,
                  executable_sha256=sha(args.exe), boundary=__doc__)
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Recorded checks passed; user IDKFA symptom remains OPEN, not a claimed fix.')


if __name__ == '__main__':
    main()
