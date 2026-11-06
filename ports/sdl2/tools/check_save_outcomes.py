"""SP17: F9 after natural death/Game Over, using established logical-input routes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import configparser

ROOT = Path(__file__).resolve().parents[3]
ROUTES = ROOT / 'research/evidence/source-port/sp16-flow/routes'


def read_note(path):
    raw = path.read_bytes()
    return raw.decode("utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--app', type=Path, default=ROOT / 'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--dataset', choices=['ru', 'en', 'og'])
    parser.add_argument('--backend', choices=['gl', 'software'])
    parser.add_argument('--outcome', choices=['death', 'session'])
    parser.add_argument('--saves-disabled', action='store_true', help='Verify authenticity switch blocks F9 after death/Game Over.')
    parser.add_argument('--out', type=Path, default=ROOT / 'research/evidence/source-port/sp17-outcomes')
    args = parser.parse_args()
    out = args.out / ('run-' + str(time.time_ns()))
    out.mkdir(parents=True)
    rows = []
    for dataset, resource, level in [('ru', 'originals/pkr/RUS/PKR.RES', 1),
                                     ('en', 'originals/pkr/ENG/PKE.RES', 1),
                                     ('og', 'originals/Old_Gold_3D/OLD_GOLD.RES', 4)]:
        if args.dataset and dataset != args.dataset:
            continue
        for outcome in ('death', 'session'):
            if args.outcome and outcome != args.outcome:
                continue
            replay = ROUTES / f'{outcome}-{dataset}.replay'
            source_log = read_note(ROUTES / f'{outcome}-{dataset}.log')
            death_frame = int(re.findall(r'DIED frames=(\d+)', source_log)[-1])
            for backend in ('gl', 'software'):
                if args.backend and backend != args.backend:
                    continue
                name = f'{dataset}-{outcome}-{backend}'
                folder = out / name
                folder.mkdir()
                script = [] if args.saves_disabled else ['0 key F5', '1 state saved']
                # The F5 snapshot skips exactly one replay frame. Existing retry
                # presses are shifted by one; the final death is not confirmed.
                if outcome == 'session':
                    old = read_note(Path(str(replay) + '.script'))
                    script += [f'{int(frame)+(0 if args.saves_disabled else 1)} {action} {value}'
                               for frame, action, value in (line.split(' ', 2) for line in old.splitlines())
                               if int(frame) < death_frame]
                script += [f'{death_frame+4} state outcome',
                           f'{death_frame+5} shot {folder}/outcome.png',
                           f'{death_frame+6} key F9',
                           f'{death_frame+8} state loaded']
                path = folder / 'events.txt'
                path.write_text('\n'.join(script) + '\n', encoding='utf-8')
                command = [str(args.app.resolve()), '--hidden', '--res', str(ROOT / resource),
                           '--level', 'LEVEL' + str(level), '--skip-intro', '--input-replay', str(replay),
                           '--frames', str(death_frame+12), '--ui-script', str(path),
                           '--user-dir', str(folder/'profile'), '--renderer', backend, '--size', '640x480',
                           '--screenshot', str(folder/'loaded.png')]
                before = hashlib.sha256((ROOT/resource).read_bytes()).hexdigest()
                if args.saves_disabled:
                    seed=folder/'seed.txt';seed.write_text('0 key F5\n',encoding='utf-8')
                    seed_command=command.copy();seed_command[seed_command.index('--ui-script')+1]=str(seed);seed_command[seed_command.index('--frames')+1]='2'
                    # Seed a real valid save, then disable saving in the same profile.
                    seed_command.remove('--input-replay');seed_command.remove(str(replay))
                    seeded=subprocess.run(seed_command,cwd=ROOT,capture_output=True,env=dict(os.environ,SDL_AUDIODRIVER='dummy'),timeout=60)
                    assert seeded.returncode==0
                    save=next((folder/'profile/saves').glob('*/quick.sav'));save_hash=hashlib.sha256(save.read_bytes()).hexdigest()
                    settings=configparser.ConfigParser();settings.read(folder/'profile/settings.ini')
                    for section in ('settings','gameplay'):
                        if not settings.has_section(section):settings.add_section(section)
                    settings['settings']['version']='1';settings['gameplay']['saves_enabled']='0'
                    with (folder/'profile/settings.ini').open('w',encoding='utf-8') as stream: settings.write(stream)
                process = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                         env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=240)
                log = (folder/'profile/port.log').read_text(encoding='utf-8')
                (folder/'port.log').write_text(log, encoding='utf-8')
                assert process.returncode == 0, (name, process.stderr, log)
                states = {label: dict(re.findall(r'(\w+)=(\S+)', body))
                          for label, body in re.findall(r'^STATE (\S+) (.*)$', log, re.M)}
                dead, loaded = states['outcome'], states['loaded']
                assert dead['hp'] == '0' and dead['phase'] == ('1' if outcome == 'death' else '2'), (name, states)
                if args.saves_disabled:
                    assert loaded['hp']==dead['hp'] and loaded['phase']==dead['phase'] and loaded['menu']==dead['menu'],(name,states)
                    assert 'SAVE loaded' not in log and 'SAVE written' not in log
                    assert hashlib.sha256(save.read_bytes()).hexdigest()==save_hash
                else:
                    assert int(loaded['hp']) > 0 and loaded['lives'] == '3' and loaded['phase'] == '0', (name, states)
                    assert loaded['message_key'] == 'port.loaded' and 'SAVE loaded slot=0' in log
                    assert log.count('SAVE written slot=0') == 1
                assert hashlib.sha256((ROOT/resource).read_bytes()).hexdigest() == before
                rows.append(dict(name=name, command=command, resource_sha256=before,
                                 replay_sha256=hashlib.sha256(replay.read_bytes()).hexdigest(), states=states))
                print(name, 'PASS', flush=True)
    (out/'report.json').write_text(json.dumps(dict(status='PASS', saves_enabled=not args.saves_disabled, app_sha256=hashlib.sha256(args.app.read_bytes()).hexdigest(), checks=rows), ensure_ascii=False, indent=2), encoding='utf-8')
    print(out)


if __name__ == '__main__':
    main()
