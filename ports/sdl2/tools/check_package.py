"""Launch the delivered Windows directory with isolated settings and original RES."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--hidden', action='store_true', help='Use diagnostic hidden windows (r2 or later).')
    args = parser.parse_args()
    package = args.package.resolve()
    exe = package / 'dok_port.exe'
    required = {'dok_port.exe', 'SDL2.dll', 'README.md', 'NOTICES.txt',
                'SDL2-LICENSE.txt', 'Play.cmd', 'Play-OpenGL.cmd', 'Play-Software.cmd'}
    assert required <= {p.name for p in package.iterdir()}
    assert not list(package.rglob('*.res')) and not list(package.rglob('*.RES'))
    readme = (package / 'README.md').read_text(encoding='utf-8')
    assert 'Старое золото' in readme and '????' not in readme and '\ufffd' not in readme
    out = ROOT / 'research/evidence/source-port/sp08-sp16-checkpoint' / package.name
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    results = []
    for dataset, res, level in [
        ('old-gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', 'LEVEL4'),
        ('ru', 'originals/pkr/RUS/PKR.RES', 'LEVEL1'),
        ('en', 'originals/pkr/ENG/PKE.RES', 'LEVEL1'),
        ('no-res', None, None),
    ]:
        for backend in ['gl', 'software']:
            name = dataset + '-' + backend
            profile = out / name
            picture = out / (name + '.png')
            command = [str(exe), '--user-dir', str(profile), '--renderer', backend,
                       '--size', '1024x768' if backend == 'software' else '1280x720',
                       '--locale', 'en' if backend == 'software' else 'ru',
                       '--frames', '5', '--screenshot', str(picture)]
            if args.hidden:
                command += ['--hidden']
            if res:
                command += ['--res', str(ROOT / res), '--level', level, '--skip-intro']
            process = subprocess.run(command, cwd=package, env=env, capture_output=True,
                                     text=True, encoding='utf-8', timeout=30)
            log = (profile / 'port.log').read_text(encoding='utf-8')
            assert process.returncode == 0 and picture.is_file(), (name, process.stderr)
            assert 'Clean shutdown;' in log and 'OpenGL fallback:' not in log, (name, log)
            assert ('Started ' in log) if res else 'datasets=0' in log
            results.append(dict(name=name, command=command, exit_code=process.returncode,
                                screenshot=str(picture.relative_to(ROOT)), log=log))
            print(name, 'PASS', flush=True)
    report = dict(package=str(package.relative_to(ROOT)), processes=results,
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  readme_utf8=True, original_resources_included=False,
                  boundary='Packaged executable smoke checks, not full playthroughs.')
    (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n',
                                     encoding='utf-8')


if __name__ == '__main__':
    main()
