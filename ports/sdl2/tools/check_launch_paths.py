"""SP23: real-process profile, CLI isolation, persistence and relocation checks."""
import argparse
from contextlib import contextmanager
import hashlib
import itertools
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/sdl2/checks/launch-paths'
PROFILES = [ROOT / p for p in ('originals/Old_Gold_3D/OLD_GOLD.RES',
            'originals/pkr/RUS/PKR.RES', 'originals/pkr/ENG/PKE.RES')]


@contextmanager
def workspace():
    # mkdir with inherited Windows ACLs: tempfile's private mode excludes the
    # managed sandbox identity on this host.
    path = OUT / ('run-' + uuid.uuid4().hex)
    path.mkdir()
    try:
        yield path
    finally:
        assert path.resolve().is_relative_to(OUT.resolve())
        shutil.rmtree(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT / 'build/sdl2/windows/OGDoK.exe')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    hashes = lambda: {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in PROFILES}
    before = hashes()
    report = {'cases': [], 'sha256_before': before}
    with workspace() as temporary:
        work = Path(temporary)
        binary = work / 'portable folder'; binary.mkdir()
        shutil.copy2(args.app, binary / 'OGDoK.exe')
        shutil.copy2(args.app.parent / 'SDL2.dll', binary / 'SDL2.dll')
        cwd = work / 'other cwd'; cwd.mkdir()
        env = dict(os.environ, SDL_AUDIODRIVER='dummy')

        def run(name, options=(), profile=None, code=0, required=(), absent=(), executable=None, verbose=True):
            exe = executable or binary / 'OGDoK.exe'
            command = [str(exe), '--hidden', '--renderer', 'software', '--size', '320x200', '--frames', '3']
            if verbose:
                command += ['--verbose']
            if profile is not None:
                command += ['--user-dir', str(profile)]
            command += list(map(str, options))
            result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, encoding='utf-8', timeout=30)
            target = profile if profile is not None else exe.parent
            if not target.is_absolute():
                target = cwd / target
            log_file = target / 'port.log'
            log = log_file.read_text(encoding='utf-8') if log_file.is_file() else ''
            text = log + result.stderr
            assert result.returncode == code, (name, result.returncode, text)
            for value in required:
                assert value in text, (name, value, text)
            for value in absent:
                assert value not in log, (name, value, log)
            report['cases'].append({'name': name, 'exit': result.returncode, 'log': log, 'stderr': result.stderr})
            return log

        # Explicit profiles bypass both the marker and the platform directory.
        marker = binary / 'PORTABLE.any.extension'; marker.write_text('ignored')
        quiet = work / 'quiet'
        assert run('errors-only-clean', profile=quiet, verbose=False) == ''
        run('verbose-opt-in', profile=quiet, required=('Profile mode:', 'Clean shutdown;'))
        assert run('verbose-does-not-persist', profile=quiet, verbose=False) == ''
        assert run('errors-only-gameplay', ['--res', PROFILES[0], '--level', 'LEVEL4', '--skip-intro'],
                   profile=quiet, verbose=False) == ''
        run('errors-only-resource', ['--res', work / 'missing.res'], profile=quiet,
            verbose=False, required=('missing.res',), absent=('Profile mode:', 'Clean shutdown;'))
        run('errors-only-fatal', ['--input-replay', work / 'missing.replay'], profile=quiet,
            verbose=False, code=1, required=('Fatal error:',), absent=('Profile mode:',))
        assert run('errors-only-truncate', profile=quiet, verbose=False) == ''
        run('portable', required=('Profile mode: portable', 'datasets=0'))
        run('explicit-wins', profile=Path('relative profile'), required=('Profile mode: explicit', 'datasets=0'))
        assert (cwd / 'relative profile/settings.ini').is_file()
        run('unicode-profile', ['--res', os.path.relpath(PROFILES[0], cwd)],
            profile=Path('\u041f\u0440\u043e\u0444\u0438\u043b\u044c with spaces'), required=('datasets=1',))
        for name in ('portable', 'portable.txt'):
            marker.unlink(); marker = binary / name; marker.touch()
            run('marker-' + name, required=('Profile mode: portable',))
        # F12 follows the selected profile, never the process working directory.
        capture_script = work / 'capture.txt'; capture_script.write_text('0 key F12\n', encoding='utf-8')
        for name, profile in [('portable', None), ('explicit', Path('Screenshot profile')), ('unicode', Path('Снимки with spaces'))]:
            run('screenshot-' + name, ['--ui-script', capture_script], profile=profile, required=('Screenshot saved:',))
            target = binary if profile is None else cwd / profile
            captures = list((target / 'screenshots').glob('*.png'))
            assert len(captures) == 1 and captures[0].read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), name
            assert not (cwd / 'screenshots').exists(), 'F12 wrote into cwd'
        run('screenshot-cli', ['--screenshot', 'explicit.png'], required=('Screenshot saved:',))
        assert (cwd / 'explicit.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        # Each nonempty combination, renamed files, any order and invalid CLI.
        for n in range(1, 4):
            for combo in itertools.combinations(PROFILES, n):
                options = list(itertools.chain.from_iterable(('--res', p) for p in reversed(combo)))
                run('combination-' + ''.join(str(PROFILES.index(p)) for p in combo), options,
                    required=(f'datasets={n}',))
        renamed = work / 'renamed.payload'; shutil.copy2(PROFILES[0], renamed)
        run('renamed', ['--res', renamed], required=('Loaded Old Gold', 'datasets=1'))
        for name, options in [('four-res', ['--res', renamed] * 4), ('missing-res', ['--res']),
                              ('empty-res', ['--res', '']), ('missing-user', ['--user-dir']),
                              ('unknown', ['--unknown'])]:
            result = subprocess.run([str(binary / 'OGDoK.exe'), '--hidden', *map(str, options)],
                                    cwd=cwd, env=env, capture_output=True, encoding='utf-8', timeout=30)
            assert result.returncode == 1 and result.stderr, (name, result)
            report['cases'].append({'name': name, 'exit': result.returncode, 'stderr': result.stderr})

        # Autodiscovery is limited to the executable's immediate directory.
        (binary / 'data').mkdir(); shutil.copy2(PROFILES[1], binary / 'data/PKR.RES')
        shutil.copy2(PROFILES[2], cwd / 'PKE.RES')
        run('no-data-or-cwd', required=('datasets=0',))
        shutil.copy2(PROFILES[1], binary / 'game.ReS')
        run('auto-exe', required=('Loaded Kremlin PKR', 'datasets=1'))
        run('cli-excludes-auto', ['--res', renamed], required=('datasets=1',), absent=('Loaded Kremlin PKR',))

        # Persist menu/drop paths, including a non-ASCII internal filename.
        internal = binary / '\u0420\u0435\u0441\u0443\u0440\u0441\u044b.payload'
        shutil.copy2(PROFILES[0], internal)
        script = work / 'drop.txt'; script.write_text(f'0 drop {internal}\n1 key F3\n', encoding='utf-8')
        run('drop-internal', ['--ui-script', script], required=('datasets=2',))
        ini = binary / 'settings.ini'
        settings_text = ini.read_text(encoding='utf-8')
        assert internal.name.encode().hex() in settings_text, settings_text
        assert str(internal).encode().hex() not in settings_text
        snapshot = ini.read_bytes()
        run('cli-excludes-stored', ['--res', PROFILES[2]], required=('datasets=1',), absent=('Loaded Old Gold', 'Loaded Kremlin PKR'))
        assert snapshot == ini.read_bytes(), 'CLI modified persistent settings'
        run('restore-stored', required=('Loaded Old Gold', 'datasets=2'))
        lost = work / 'lost-profile'; lost.mkdir()
        lost_path = str(work / 'missing.res').encode().hex()
        (lost / 'settings.ini').write_text(f'[settings]\nversion=1\n[resources]\ncount=1\npath0={lost_path}\nchoice1={lost_path}\n')
        run('replace-missing', ['--ui-script', script], profile=lost, required=('Loaded Old Gold',))
        assert lost_path not in (lost / 'settings.ini').read_text()
        # Legacy absolute paths remain readable; a manual copy preserves all fields.
        external_script = work / 'external.txt'; external_script.write_text(f'0 drop {renamed}\n', encoding='utf-8')
        run('external-absolute', ['--ui-script', external_script])
        assert str(renamed).encode().hex() in ini.read_text(encoding='utf-8')
        manual = work / 'manual'; manual.mkdir(); shutil.copy2(ini, manual / 'settings.ini')
        run('manual-copy', profile=manual, required=('Loaded Old Gold',))
        assert (manual / 'settings.ini').read_bytes() == ini.read_bytes()
        (binary / 'game.ReS').unlink()
        relocated = work / '\u041f\u0435\u0440\u0435\u043d\u0435\u0441\u0435\u043d\u043e folder'; binary.rename(relocated)
        run('relocated', executable=relocated / 'OGDoK.exe', required=('Loaded Old Gold', 'datasets=1'))
        binary = relocated
        # A bad file remains intact on idle, and is backed up before an explicit edit.
        bad = work / 'bad-profile'; bad.mkdir(); corrupt = b'[settings]\nversion=999\n'
        (bad / 'settings.ini').write_bytes(corrupt)
        run('corrupt-idle', profile=bad)
        assert (bad / 'settings.ini').read_bytes() == corrupt
        edit = work / 'edit.txt'; edit.write_text('0 key F3\n')
        run('corrupt-edit', ['--ui-script', edit], profile=bad, required=('Original settings preserved:',))
        assert (bad / 'settings.ini.bak').read_bytes() == corrupt
        assert (bad / 'settings.ini').read_bytes() != corrupt
        # Failed replacement must preserve both previous settings and unrelated data.
        blocked = work / 'blocked'; blocked.mkdir()
        (blocked / 'settings.ini').write_bytes(corrupt)
        (blocked / 'settings.ini.tmp').mkdir()
        (blocked / 'settings.ini.tmp/keep').write_text('keep')
        run('write-failure', ['--ui-script', edit], profile=blocked, required=('Settings save failed:',))
        assert (blocked / 'settings.ini').read_bytes() == corrupt
        assert (blocked / 'settings.ini.tmp/keep').read_text() == 'keep'
        not_directory = work / 'profile-file'; not_directory.write_text('keep')
        run('profile-unavailable', profile=not_directory, code=1)
        assert not_directory.read_text() == 'keep'

    assert before == hashes(), 'Original RES changed'
    report['sha256_after'] = hashes()
    report['executable_sha256'] = hashlib.sha256(args.app.read_bytes()).hexdigest()
    (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: {len(report['cases'])} launch scenarios; original hashes unchanged")


if __name__ == '__main__':
    main()
