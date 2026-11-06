"""Real-process menu checks; private profiles never touch the user's settings."""
import configparser
import json
import os
from pathlib import Path
import re
import subprocess
from compiler_cache import ROOT
from experiment_cleanup import temporary_workspace

EXE = ROOT / 'build/sdl2/windows/OGDoK.exe'
OUT = ROOT / 'build/sdl2/checks/frontend-process'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with temporary_workspace(OUT, 'run-') as run_dir:
        check(run_dir)


def check(run_dir):

    def run(name, script, args=(), profile=None, env=None, frames=30):
        profile = profile or run_dir / name
        path = run_dir / (name + '.txt')
        path.write_text(script, encoding='utf-8')
        run_env = {**os.environ, **(env or {}), 'SDL_AUDIODRIVER': 'dummy'}
        subprocess.run([str(EXE), '--hidden', '--verbose', '--user-dir', str(profile), '--ui-script', str(path),
                        '--frames', str(frames), *args], cwd=ROOT, env=run_env, check=True, timeout=45)
        log = (profile / 'port.log').read_text(encoding='utf-8')
        (run_dir / (name + '.log')).write_text(log, encoding='utf-8')
        return log, profile

    log, profile = run('session', '''2 key Escape
3 state before
4 key Down
5 key Return
6 key Return
7 key Right
8 key Down
9 key Down
10 key Right
11 key Down
13 key Return
15 state staged
16 key Return
18 state kept
19 key Escape
20 key Escape
21 key Escape
23 state resumed
''', ['--res', 'originals/pkr/RUS/PKR.RES', '--level', 'LEVEL1', '--skip-intro',
      '--renderer', 'gl', '--locale', 'en', '--size', '1280x720'])
    states = {m[1]: m[2] for m in re.finditer(r'^STATE (\w+) (.+)$', log, re.M)}
    def world_state(label):
        return states[label].split('world=', 1)[1]
    assert world_state('before') == world_state('staged') == world_state('kept')
    assert 'renderer=0 size=640x400' in states['kept']
    assert states['resumed'].split('world=')[1].split()[0] == states['before'].split('world=')[1].split()[0]
    cfg = configparser.ConfigParser()
    cfg.read(profile / 'settings.ini', encoding='utf-8')
    assert cfg['video']['renderer'] == '0' and cfg['video']['software_mode'] == '1'
    restart, _ = run('restart', '2 state restarted\n', profile=profile, frames=4)
    assert 'renderer=0 size=640x400' in restart

    # A declined mode is never persisted; CLI settings remain session-only.
    rollback, _ = run('rollback', '''1 key Down
2 key Return
3 key Return
4 key Right
5 key Down
6 key Down
7 key Right
8 key Down
9 key Down
10 key Return
12 key Escape
14 state reverted
''', profile=profile, frames=16)
    assert 'STATE reverted renderer=0 size=640x400' in rollback
    cfg.read(profile / 'settings.ini', encoding='utf-8')
    assert cfg['video']['renderer'] == '0'
    for name, args in [('no-res', []), ('bad-res', ['--res', 'missing.RES'])]:
        log, _ = run(name, f'1 key Return\n3 state resources\n4 shot {run_dir / (name + ".png")}\n',
                     ['--renderer', 'software', '--locale', 'ru', *args], frames=6)
        assert 'datasets=0' in log and 'menu=17' in log
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    fallback, _ = run('fallback', '2 state fallback\n', ['--renderer', 'gl'], env=env, frames=4)
    assert 'OpenGL fallback:' in fallback and 'renderer=0' in fallback
    report = {'paused_world_preserved': True, 'window_resized': '640x400',
              'settings_restart': True, 'video_cancel': True, 'no_resources': True,
              'invalid_resources': True, 'gl_failure_fallback': True, 'temporary_profiles': 'removed on exit'}
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
