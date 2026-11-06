"""SP-OG-BOBBING: real-process camera, simulation and menu checks."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess
from compiler_cache import ROOT
from experiment_cleanup import temporary_workspace

SAMPLES = [('old_gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', 4),
           ('kremlin_pkr', 'originals/pkr/RUS/PKR.RES', 1),
           ('kremlin_pke', 'originals/pkr/ENG/PKE.RES', 1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT/'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--out', type=Path, default=ROOT/'research/evidence/source-port/sp-og-bobbing')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    originals = {res: hashlib.sha256((ROOT/res).read_bytes()).hexdigest() for _, res, _ in SAMPLES}
    checks = []
    scratch = ROOT/'build/sdl2/checks'
    scratch.mkdir(parents=True, exist_ok=True)
    with temporary_workspace(scratch, 'bobbing-') as work:

        def run(name, dataset, res, level, backend, setting, script='', replay=True, locale='ru', size='640x480'):
            profile = work/name
            profile.mkdir()
            ini = '[settings]\nversion=1\n[interface]\nfps=0\n'
            if setting is not None:
                ini += f'[gameplay]\ncamera_bobbing={setting}\n'
            (profile/'settings.ini').write_text(ini, encoding='utf-8')
            events = profile/'events.txt'
            events.write_text(script, encoding='utf-8')
            shot = profile/'frame.png'
            cmd = [str(args.app.resolve()), '--hidden', '--user-dir', str(profile), '--res', str(ROOT/res),
                   '--renderer', backend, '--level', f'LEVEL{level}', '--skip-intro', '--size', size,
                   '--software-mode', '320x200', '--locale', locale, '--ui-script', str(events),
                   '--screenshot', str(shot)]
            if replay:
                recording = profile/'input.replay'
                recording.write_text(f'DOK_INPUT_REPLAY 1 {dataset} LEVEL{level} 0\n' + ''.join(
                    f'{416*t} 0 1 0 0 0 0 0 0 -1 0 0\n' for t in range(1, 13)), encoding='utf-8')
                cmd += ['--input-replay', str(recording)]
            else:
                cmd += ['--frames', '14']
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=60,
                                  env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
            log = (profile/'port.log').read_text(encoding='utf-8')
            assert proc.returncode == 0, (name, proc.stderr, log)
            frame = shot.read_bytes()
            state = next((line for line in log.splitlines() if line.startswith('REPLAY ')), '')
            config = configparser.ConfigParser()
            config.read(profile/'settings.ini')
            checks.append(dict(name=name, command=cmd, sha256=hashlib.sha256(frame).hexdigest(), state=state))
            return frame, state, config

        for dataset, res, level in SAMPLES:
            for backend in ('gl', 'software'):
                prefix = dataset+'-'+backend
                native = run(prefix+'-on', dataset, res, level, backend, 1)
                stable = run(prefix+'-off', dataset, res, level, backend, 0)
                legacy = run(prefix+'-legacy', dataset, res, level, backend, None)
                assert native[1] and native[1] == stable[1] == legacy[1], (prefix, 'Simulation changed')
                assert native[0] == legacy[0], (prefix, 'Legacy default changed')
                assert (native[0] != stable[0]) == (dataset == 'old_gold'), (prefix, 'Camera scope wrong')
            print(dataset, 'camera scope / simulation / legacy default PASS', flush=True)

        # Menu route from pause: Settings -> Gameplay -> Bobbing. Save both locales.
        menu = ('1 key Escape\n2 key Down\n3 key Return\n4 key Down\n5 key Down\n'
                '6 key Down\n7 key Return\n8 key Down\n9 key Down\n10 key Right\n')
        for locale in ('ru', 'en'):
            for size in ('320x200', '1280x720'):
                name = 'menu-'+locale+'-'+size
                frame, _, config = run(name, *SAMPLES[0], 'gl', 1, menu, False, locale, size)
                assert config['gameplay']['camera_bobbing'] == '0', name
                (args.out/(name+'.png')).write_bytes(frame)

    assert originals == {res: hashlib.sha256((ROOT/res).read_bytes()).hexdigest() for _, res, _ in SAMPLES}
    report = dict(status='PASS', app_sha256=hashlib.sha256(args.app.read_bytes()).hexdigest(),
                  originals_sha256=originals, checks=checks)
    (args.out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(len(checks), 'processes PASS; temporary profiles and movement frames removed')


if __name__ == '__main__':
    main()
