"""Reproduce/fix lost short SDL fire/weapon presses without holding the buttons.

All application processes are hidden, audio is dummy, and real settings are
read-only. Only fullscreen is disabled in private settings copies. R2 is an
immutable before-control; --after can point at a private executable copy.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT/'research/evidence/source-port/sp11-idkfa-r3'
RES = ROOT/'originals/Old_Gold_3D/OLD_GOLD.RES'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshots(log):
    return {name: dict(re.findall(r'(\w+)=([^ ]+)', fields))
            for name, fields in re.findall(r'^STATE (\S+) (.*)$', log, re.M)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, default=ROOT/'artifacts/sdl2/windows-playtest-20261104-sp08-sp16-r2/dok_port.exe')
    parser.add_argument('--after', type=Path, default=ROOT/'build/sdl2/windows/OGDoK.exe')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    prefs = Path(os.environ['APPDATA'])/'DoK-port/SourcePort/settings.ini'
    settings = prefs.read_bytes()
    source_hash = sha(prefs)
    copied = re.sub(rb'(?m)^fullscreen=1\r?$', b'fullscreen=0', settings)
    original_hashes = {str(p.relative_to(ROOT)): sha(p) for p in [RES, ROOT/'originals/Old_Gold_3D/OG.EXE']}
    control_hash = sha(args.before)
    code = ''.join(f'0 key {key}\n' for key in 'IDKFA')
    script = OUT/'tap-repro.script'
    script.write_text(code+'350 state ready\n400 key Left Ctrl\n650 state after-ctrl-tap\n'
                      '700 mouse_down 1\n700 mouse_up 1\n950 state after-mouse-tap\n'
                      '1000 key 2\n1500 state after-2-tap\n1600 keydown W\n2100 keyup W\n2200 state moved\n')
    rows = []

    def run(exe, name, backend, events, frames, replay=None):
        user = OUT/name
        user.mkdir(exist_ok=True)
        (user/'settings.ini').write_bytes(copied)
        command = [exe, '--res', RES, '--level', 'LEVEL4', '--skip-intro', '--renderer', backend,
                   '--hidden', '--user-dir', user, '--ui-script', events, '--frames', str(frames),
                   '--screenshot', OUT/(name+'.png')]
        if replay:
            command += ['--input-replay', replay, '--replay-live-input']
        result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                                env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=120)
        log = (user/'port.log').read_text()
        assert result.returncode == 0, result.stderr
        assert 'Audio driver: dummy' in log and 'Diagnostic hidden presentation:' in log
        row = dict(name=name, executable_sha256=sha(exe), command=[str(x) for x in command],
                   returncode=result.returncode, states=snapshots(log), log=log,
                   screenshot=str((OUT/(name+'.png')).relative_to(ROOT)))
        rows.append(row)
        return row['states']

    for before in [True, False]:
        for backend in ['gl', 'software']:
            state = run(args.before if before else args.after, ('before-r2-' if before else 'after-')+backend,
                        backend, script, 2250)
            charge = [int(state[key]['charge']) for key in ['ready', 'after-ctrl-tap', 'after-mouse-tap']]
            assert charge == ([300, 300, 300] if before else [300, 299, 298])
            assert int(state['after-2-tap']['weapon']) == (1 if before else 2)
            assert state['moved']['pose'] != state['ready']['pose']
            assert int(state['moved']['ticks']) > int(state['ready']['ticks'])
            assert state['moved']['menu'] == '0'
            print(('BEFORE' if before else 'AFTER'), backend, charge, 'weapon', state['after-2-tap']['weapon'], 'walking PASS', flush=True)

    neutral = OUT/'tick-gap.replay'
    neutral.write_text('DOK_INPUT_REPLAY 1 old_gold LEVEL4 0\n'+
                       ''.join(f'{416*n} 83 0 0 0 0 0 0 0 -1 0 0\n' for n in range(1, 101)))
    gaps = OUT/'tick-gap.script'
    gaps.write_text(code+'0 key Left Ctrl\n10 state unready-press-consumed\n'
                    '11 key Left Ctrl\n12 state between-player-ticks\n14 state next-player-tick\n'
                    '30 state no-repeat\n41 key Left Ctrl\n42 key Escape\n50 state paused\n'
                    '51 key Escape\n60 state resumed-no-stale-fire\n62 keydown 2\n90 state held-2-progress\n95 keyup 2\n'
                    '130 state held-2-completed\n150 keydown Left Ctrl\n200 keyup Left Ctrl\n210 state held-fire\n')
    for backend in ['gl', 'software']:
        state = run(args.after, 'gap-reset-'+backend, backend, gaps, 220, neutral)
        assert int(state['unready-press-consumed']['charge']) == 300
        assert int(state['between-player-ticks']['charge']) == 300
        assert int(state['next-player-tick']['charge']) == 299
        assert int(state['no-repeat']['charge']) == 299
        assert int(state['resumed-no-stale-fire']['charge']) == 299
        assert state['paused']['menu'] != '0' and state['resumed-no-stale-fire']['menu'] == '0'
        assert state['held-2-progress']['weapon'] == '2'
        assert state['held-2-completed']['weapon'] == '2' and state['held-2-completed']['gun_state'] == '7'
        assert int(state['held-fire']['charge']) < 299
        print('AFTER gap/reset', backend, 'single next-tick attempt, no readiness replay, pause reset, hold PASS', flush=True)
    assert sha(args.before) == control_hash and sha(prefs) == source_hash
    assert original_hashes == {str(p.relative_to(ROOT)): sha(p) for p in [RES, ROOT/'originals/Old_Gold_3D/OG.EXE']}
    report = dict(reproduced='DOWN+UP within one render frame loses fire/weapon selection in r2 while walking/ticks work.',
                  correction='Preserve press edges through SDL sampling and one nearest player-tick attempt; keep held fire, clear on lifecycle/input reset.',
                  runs=rows, before_executable_sha256=control_hash, after_executable_sha256=sha(args.after),
                  source_settings_sha256_before=source_hash, source_settings_sha256_after=sha(prefs),
                  originals_sha256=original_hashes,
                  limitations=['Scripted raw SDL events; not a capture of the physical user event timings.',
                               'This proves a matching failure mechanism, not every possible cause of the user report.',
                               'Fixed-clock gap/reset checks are separate from ordinary-clock before/after checks.'])
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Four before/after ordinary-clock and two tick-gap/reset actual processes PASS; r2/preferences/originals unchanged.')


if __name__ == '__main__':
    main()
