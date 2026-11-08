"""Real-process Unix smoke checks; optional originals stay local."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--res', type=Path, action='append', default=[])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--expect-api', choices=['OPENGL', 'GLES2'], default='OPENGL')
    args = parser.parse_args()
    exe = args.exe.resolve()
    resources = [p.resolve() for p in args.res]
    before = {str(p): digest(p) for p in resources}
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix='ogdok-') as temporary:
        work = Path(temporary)
        program = work / 'Игра с пробелами'
        program.mkdir()
        shutil.copy2(exe, program / 'OGDoK')
        app = program / 'OGDoK'
        app.chmod(0o755)

        def run(name, renderer='gl', res=(), extra=(), mode='explicit'):
            profile = work / name
            profile.mkdir()
            env = dict(os.environ, SDL_AUDIODRIVER='dummy', XDG_DATA_HOME=str(profile / 'xdg'))
            timing = out / f'{name}.csv'
            env['DOK_FRAME_TIMINGS'] = str(timing)
            command = [str(app), '--hidden', '--verbose', '--frames', '3', '--renderer', renderer,
                       '--size', '640x480', '--screenshot', str(out / f'{name}.png')]
            if mode == 'explicit':
                command += ['--user-dir', str(profile)]
            for path in res:
                command += ['--res', str(path)]
            command += list(extra)
            process = subprocess.run(command, cwd=work, env=env, text=True, capture_output=True, timeout=90)
            log_path = profile / 'port.log' if mode == 'explicit' else program / 'port.log' if mode == 'portable' else profile / 'xdg/DoK-port/SourcePort/port.log'
            log = log_path.read_text(encoding='utf-8') if log_path.exists() else ''
            (out / f'{name}.log').write_text(process.stdout + process.stderr + log, encoding='utf-8')
            if process.returncode or 'Clean shutdown' not in log or not (out / f'{name}.png').exists():
                raise RuntimeError(f'{name}: failed; see {out / (name + ".log")}')
            if f'Profile mode: {mode}' not in log:
                raise RuntimeError(f'{name}: wrong profile')
            if renderer == 'gl':
                if 'OpenGL fallback' in log or 'OGDoK graphics:' not in process.stderr:
                    raise RuntimeError(f'{name}: GL fell back or no graphics diagnostics')
                if args.expect_api == 'GLES2' and 'OpenGL ES' not in process.stderr:
                    raise RuntimeError(f'{name}: expected GLES')
            if len(timing.read_text().splitlines()) != 4:
                raise RuntimeError(f'{name}: incomplete frame telemetry')
            results.append(dict(name=name, exit_code=process.returncode, screenshot_sha256=digest(out / f'{name}.png')))

        for renderer in ['gl', 'software']:
            run(f'empty-{renderer}', renderer)
            if resources:
                run(f'all-{renderer}', renderer, resources)
            for i, path in enumerate(resources):
                level = 'LEVEL4' if 'gold' in path.name.lower() else 'LEVEL1'
                run(f'game-{i}-{renderer}', renderer, [path], ['--level', level, '--skip-intro'])
        run('platform-profile', mode='platform')
        (program / 'portable').touch()
        run('portable-profile', mode='portable')
        run('explicit-over-portable')
        (program / 'portable').unlink()
        run('missing-resource', res=[work / 'missing.res'])
        if 'datasets=0' not in (out / 'missing-resource.log').read_text(encoding='utf-8'):
            raise RuntimeError('Missing resource was accepted')
        if resources:
            lower = program / 'ресурсы с пробелами.res'
            shutil.copyfile(resources[0], lower)
            run('lowercase-auto')
            # Assert that the lower-case extension was actually discovered.
            if 'datasets=1' not in (out / 'lowercase-auto.log').read_text(encoding='utf-8'):
                raise RuntimeError('Lowercase resource not discovered')
        invalid = subprocess.run([str(app), '--hidden', '--user-dir', str(work / 'bad'), '--renderer', 'invalid'],
                                 env=dict(os.environ, SDL_AUDIODRIVER='dummy'), capture_output=True, timeout=30)
        if invalid.returncode == 0:
            raise RuntimeError('Invalid renderer accepted')
    after = {str(p): digest(p) for p in resources}
    if before != after:
        raise RuntimeError('Original resource changed')
    (out / 'report.json').write_text(json.dumps(dict(executable_sha256=digest(exe), api=args.expect_api,
        processes=results, resources_before=before, resources_after=after,
        limitations='Hidden SDL processes; no real audio, physical input, suspend or full playthrough.'), indent=2) + '\n', encoding='utf-8')
    print(f'{len(results)} Unix process checks PASS')


if __name__ == '__main__':
    main()
