"""Check Windows DPI awareness of the actual port process, not just SDL flags.

Does not change the user's display settings. Physical scale changes and moving
between monitors remain a separate hardware check.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--out', type=Path, default=ROOT / 'research/evidence/source-port/sp21-dpi')
    args = parser.parse_args()
    exe, out = args.exe.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shcore = ctypes.WinDLL('shcore')  # This probe requires Windows 8.1+.
    query = shcore.GetProcessDpiAwareness
    query.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_int)]
    query.restype = ctypes.c_long
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    # Do not let an inherited SDL override mask the application defaults.
    env.pop('SDL_WINDOWS_DPI_AWARENESS', None)
    env.pop('SDL_WINDOWS_DPI_SCALING', None)
    results = []
    for backend in ('gl', 'software'):
        for size in ('1280x720', '1707x960', '1920x1080', '2560x1440'):
            name = backend + '-' + size
            profile = out / name
            profile.mkdir(exist_ok=True)
            picture = profile / 'menu.png'
            command = [str(exe), '--hidden', '--verbose', '--user-dir', str(profile),
                       '--renderer', backend, '--size', size, '--locale', 'ru',
                       '--res', str(ROOT / 'originals/pkr/RUS/PKR.RES'),
                       '--frames', '12', '--screenshot', str(picture)]
            levels = set()
            with subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE) as process:
                # Sample until exit: the early process may still be unaware
                # before main/SDL_Init runs. Keep all observed states.
                deadline = time.monotonic() + 40
                while process.poll() is None and time.monotonic() < deadline:
                    awareness = ctypes.c_int(-1)
                    if query(wintypes.HANDLE(int(process._handle)), ctypes.byref(awareness)) == 0:
                        levels.add(awareness.value)
                    time.sleep(0.005)
                if process.poll() is None:
                    process.kill()
                    raise AssertionError((name, 'Process timeout'))
                stdout, stderr = process.communicate()
            log = (profile / 'port.log').read_text(encoding='utf-8')
            assert process.returncode == 0 and 'Clean shutdown;' in log, (name, stderr.decode(errors='replace'), log)
            assert 'OpenGL fallback:' not in log, (name, log)
            data = picture.read_bytes()
            assert data[:8] == b'\x89PNG\r\n\x1a\n'
            dimensions = struct.unpack_from('>II', data, 16)
            assert dimensions == tuple(map(int, size.split('x'))), (name, dimensions)
            result = dict(name=name, command=command, awareness_levels=sorted(levels),
                          screenshot_dimensions=dimensions, per_monitor_aware=2 in levels)
            results.append(result)
            print(name, 'PASS' if result['per_monitor_aware'] else 'FAIL: DPI unaware', flush=True)
    report = dict(exe=str(exe), exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  processes=results, passed=all(r['per_monitor_aware'] for r in results),
                  boundary='Process awareness and native readbacks; no Windows scale settings changed; no physical mixed-DPI claim.')
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert report['passed'], 'Windows would bitmap-scale this process; see report.json'


if __name__ == '__main__':
    main()
