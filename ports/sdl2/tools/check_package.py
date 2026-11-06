"""Launch the delivered Windows directory with isolated settings and original RES."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import struct

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--hidden', action='store_true', help='Use diagnostic hidden windows (r2 or later).')
    args = parser.parse_args()
    package = args.package.resolve()
    exe = package / 'OGDoK.exe'
    required = {'OGDoK.exe', 'SDL2.dll', 'README.txt', 'LICENSES.txt'}
    assert required <= {p.name for p in package.iterdir()}
    assert not list(package.glob('*.cmd')) and not list(package.glob('*.bat'))
    assert not (package / 'NOTICES.txt').exists() and not (package / 'SDL2-LICENSE.txt').exists()
    licenses = (package / 'LICENSES.txt').read_text(encoding='utf-8')
    sdl_license = (Path(os.environ['LOCALAPPDATA']) / 'Programs/SDL2/SDL2-2.32.10/LICENSE.txt').read_text(encoding='utf-8')
    assert sdl_license in licenses and 'CC BY 4.0' in licenses
    pe = exe.read_bytes()
    header = struct.unpack_from('<I', pe, 0x3c)[0]
    assert pe[header:header+4] == b'PE\0\0'
    assert struct.unpack_from('<H', pe, header+24+68)[0] == 2, 'Game must use Windows GUI subsystem'
    resources = {'OLD_GOLD.RES': 'originals/Old_Gold_3D/OLD_GOLD.RES',
                 'PKR.RES': 'originals/pkr/RUS/PKR.RES',
                 'PKE.RES': 'originals/pkr/ENG/PKE.RES'}
    bundled = any((package / name).exists() for name in resources)
    if bundled:
        for name, source in resources.items():
            assert (package / name).read_bytes() == (ROOT / source).read_bytes(), name
    readme = (package / 'README.txt').read_text(encoding='utf-8')
    assert 'Старое золото' in readme and '????' not in readme and '\ufffd' not in readme
    assert not (package / 'README.md').exists()
    assert not any(line.startswith(('|', '#', '```')) for line in readme.splitlines())
    assert 'Границы плейтеста' not in readme and 'паспорт' not in readme
    assert readme == (ROOT / 'ports/sdl2/README.txt').read_text(encoding='utf-8')
    assert 'Командная строка' in readme and 'Диагностические параметры' in readme
    source = (ROOT / 'ports/sdl2/include/dok/launch.hpp').read_text(encoding='utf-8')
    import re
    options = set(re.findall(r'"(--[a-z-]+)"', source)) - {'--', '--aspect', '--fov'}
    assert all(option in readme for option in options), options
    out = ROOT / 'research/evidence/source-port/sp08-sp16-checkpoint' / package.name
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    results = []
    for dataset, res, level in [
        ('old-gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', 'LEVEL4'),
        ('ru', 'originals/pkr/RUS/PKR.RES', 'LEVEL1'),
        ('en', 'originals/pkr/ENG/PKE.RES', 'LEVEL1'),
        ('bundled-auto' if bundled else 'no-res', None, None),
    ]:
        for backend in ['gl', 'software']:
            name = dataset + '-' + backend
            profile = out / name
            picture = out / (name + '.png')
            command = [str(exe), '--verbose', '--user-dir', str(profile), '--renderer', backend,
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
            assert ('Started ' in log) if res else ('datasets=3' if bundled else 'datasets=0') in log
            results.append(dict(name=name, command=command, exit_code=process.returncode,
                                screenshot=str(picture.relative_to(ROOT)), log=log))
            print(name, 'PASS', flush=True)
    report = dict(package=str(package.relative_to(ROOT)), processes=results,
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  readme_utf8=True, original_resources_included=bundled, windows_gui_subsystem=True, batch_launchers=False,
                  boundary='Packaged executable smoke checks, not full playthroughs.')
    (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n',
                                     encoding='utf-8')


if __name__ == '__main__':
    main()
