"""Game-menu navigation and music lifetime in real SDL processes."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
EXE = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/sdl2/windows/OGDoK.exe'
OUT = ROOT / 'research/evidence/source-port/sp-menu-flow'
RES = [('og', 'originals/Old_Gold_3D/OLD_GOLD.RES', 4, 'old_gold'),
       ('pkr', 'originals/pkr/RUS/PKR.RES', 1, 'pkr'),
       ('pke', 'originals/pkr/ENG/PKE.RES', 1, 'pke')]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    hashes = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for _, p, _, _ in RES}
    checks = []

    def run(name, resource, script, profile=None, extra=(), frames=75):
        directory = OUT / name
        directory.mkdir(exist_ok=True)
        profile = profile or directory / 'user'
        script_path = directory / 'ui.txt'
        script_path.write_text(script, encoding='utf-8')
        command = [str(EXE), '--hidden', '--verbose', '--renderer', 'software',
                   '--user-dir', str(profile), '--res', str(ROOT / resource),
                   '--ui-script', str(script_path), '--frames', str(frames), *extra]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                timeout=60, env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
        log = (profile / 'port.log').read_text(encoding='utf-8')
        (directory / 'port.log').write_text(log, encoding='utf-8')
        assert result.returncode == 0, (name, result.stderr, log)
        states = {m[1]: dict(re.findall(r'(\w+)=([^\s]*)', m[2]))
                  for m in re.finditer(r'^STATE (\w+) (.*)$', log, re.M)}
        checks.append(dict(name=name, command=command, states=states))
        return log, states, profile

    def playing(state):
        assert state['music'] == state['music_active'] == state['music_loop'] == '1', state
        assert 'world' not in state, state

    for name, resource, level, save_dir in RES:
        _, _, profile = run(name + '-save', resource, '2 key F5\n4 state saved\n',
                            extra=('--level', f'LEVEL{level}', '--skip-intro'), frames=6)
        save = profile / 'saves' / save_dir / 'quick.sav'
        assert save.is_file(), save
        for locale in ('ru', 'en'):
            script = ('0 key Return\n2 state title\n3 key Return\n5 state menu\n'
                      '6 key Down\n6 key Down\n7 key Return\n9 state back\n'
                      '10 key Return\n12 key Return\n14 key Return\n16 state story\n'
                      '17 key Escape\n19 state story_back\n20 key Return\n')
            script += ''.join(f'{f} key Return\n' for f in range(22, 60, 2))
            script += '62 state started\n'
            log, states, _ = run(f'{name}-{locale}-new', resource, script,
                                 profile=profile, extra=('--locale', locale))
            for label in ('title', 'menu', 'story', 'story_back'):
                playing(states[label])
            assert states['back']['menu'] == '1' and states['back']['music'] == '0', states
            assert states['menu']['menu'] == states['story_back']['menu'] == '31', states
            assert states['started']['music'] == '0' and 'world' in states['started'], states
            assert len(re.findall(r'^MUSIC ', log, re.M)) == 2, log

        load_script = ('0 key Return\n2 key Return\n4 key Down\n5 key Return\n'
                       '7 state slots\n8 key Return\n10 state entry\n11 key Return\n'
                       '14 state loaded\n')
        log, states, _ = run(name + '-load', resource, load_script, profile=profile, frames=16)
        playing(states['slots']); playing(states['entry'])
        assert states['loaded']['music'] == '0' and 'world' in states['loaded'], states
        assert 'SAVE loaded slot=0' in log, log
        deletion_profile = OUT / (name + '-delete-last') / 'user'
        target = deletion_profile / 'saves' / save_dir / 'quick.sav'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(save.read_bytes())
        deletion_script = ('0 key Return\n2 key Return\n4 key Down\n5 key Return\n'
                           '7 key Return\n9 key Down\n10 key Return\n12 key Up\n'
                           '13 key Return\n15 state deleted\n16 key Escape\n18 state back\n')
        _, states, _ = run(name + '-delete-last', resource, deletion_script,
                           profile=deletion_profile, frames=20)
        playing(states['deleted'])
        assert not target.exists(), target
        assert states['back']['menu'] == '1' and states['back']['music'] == '0', states
        direct_script = ('0 key Return\n2 key Return\n4 state direct\n'
                         '5 key Escape\n7 state back\n')
        # With no usable save, omit the choice and return directly to selection.
        original = save.read_bytes()
        try:
            save.with_suffix('.sav.bak').unlink(missing_ok=True)
            damaged = bytearray(original); damaged[30] ^= 1; save.write_bytes(damaged)
            _, states, _ = run(name + '-bad-load', resource, direct_script,
                               profile=profile, frames=9)
            playing(states['direct'])
            assert states['direct']['menu'] == ('5' if level == 4 else '4'), states
            assert states['back']['menu'] == '1' and states['back']['music'] == '0', states
            save.with_suffix('.sav.bak').write_bytes(original)
            _, states, _ = run(name + '-backup', resource,
                               '0 key Return\n2 key Return\n4 state menu\n',
                               profile=profile, frames=6)
            assert states['menu']['menu'] == '31', states
            playing(states['menu'])
        finally:
            save.write_bytes(original)
            save.with_suffix('.sav.bak').unlink(missing_ok=True)

        for mode in ('empty', 'disabled'):
            isolated = OUT / f'{name}-{mode}' / 'user'
            isolated.mkdir(parents=True, exist_ok=True)
            (isolated / 'settings.ini').write_text(
                '[settings]\nversion=1\n[gameplay]\nsaves_enabled=' + ('0' if mode == 'disabled' else '1') + '\n')
            if mode == 'disabled':
                target = isolated / 'saves' / save_dir / 'quick.sav'
                target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(original)
            _, states, _ = run(name + '-' + mode, resource, direct_script,
                               profile=isolated, frames=9)
            playing(states['direct'])
            assert states['direct']['menu'] == ('5' if level == 4 else '4'), states
            assert states['back']['menu'] == '1' and states['back']['music'] == '0', states

    for route in ('title', 'menu', 'direct'):
        # A real three-game catalogue, with and without a loadable save.
        profile = OUT / ('multi-' + route) / 'user'
        profile.mkdir(parents=True, exist_ok=True)
        (profile / 'settings.ini').write_text('[settings]\nversion=1\n[gameplay]\nsaves_enabled=' + ('0' if route == 'direct' else '1') + '\n')
        if route == 'menu':
            for name, _, _, save_dir in RES:
                target = profile / 'saves' / save_dir / 'quick.sav'
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((OUT / (name + '-save') / 'user/saves' / save_dir / 'quick.sav').read_bytes())
        script = '0 key Return\n2 key Down\n3 key Return\n'
        if route != 'title':
            script += '5 key Return\n'
        script += '7 state selected\n8 key Escape\n10 state back\n'
        _, states, _ = run('multi-' + route, RES[0][1], script, profile=profile,
                           extra=('--res', str(ROOT / RES[1][1]), '--res', str(ROOT / RES[2][1])), frames=12)
        playing(states['selected'])
        assert states['back']['menu'] == '2' and states['back']['music'] == '0', states

    assert hashes == {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in hashes}
    (OUT / 'report.json').write_text(json.dumps(dict(exe=str(EXE), checks=checks,
        resource_sha256=hashes), indent=2) + '\n', encoding='utf-8')
    print(f'{len(checks)} menu/music processes PASS; original RES unchanged')


if __name__ == '__main__':
    main()
