"""N18 real-package stream/ownership tests with a deterministic fake consumer.

This gate does not execute ARM7 or validate acoustic timing. The separate
emulator capture gate verifies the actual IPC/timer/sound backend.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/nds/n18/host'
EVIDENCE = ROOT / 'research/evidence/nds/n18'

def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    zig = Path.home() / 'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),
               ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    sources = ['ports/nds/tests/test_audio_runtime.c', 'ports/nds/core/assets.c',
               'ports/nds/core/sha256.c', 'ports/nds/core/contracts.c',
               'ports/nds/core/balance.c', 'ports/nds/core/world.c',
               'ports/nds/core/audio_codec.c']
    common = [str(zig), 'cc', '-std=c11', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
              '-Wno-misleading-indentation', '-O2', '-g', '-ffunction-sections',
              '-fdata-sections', '-Wl,--gc-sections', '-Iports/nds/tests/audio_host',
              '-Iports/nds/include']
    report = {'task': 'N18', 'passed': False, 'commands': [], 'tests': [],
              'scope': 'Production asset/runtime code; deterministic fake audio consumer. No ARM7/timer/acoustic validation.'}
    for language in (1, 2):
        executable = OUT / f'audio-runtime-{language}.exe'
        command = [*common, f'-DSPRITE_LANGUAGE={language}', *sources, '-o', str(executable)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        report['commands'].append(command)
        result = subprocess.run([str(executable), str(ROOT/'build/nds/assets/dok-assets.ndspak')],
                                cwd=ROOT, check=True, capture_output=True, text=True)
        report['tests'].append(json.loads(result.stdout))
        linux = [*common, '-target', 'x86_64-linux-musl', '-static',
                 f'-DSPRITE_LANGUAGE={language}', *sources,
                 '-o', str(OUT/f'audio-runtime-{language}-linux')]
        subprocess.run(linux, cwd=ROOT, env=env, check=True)
        report['commands'].append(linux)
    report['linux_cross_builds'] = 2
    report['linux_execution'] = False
    report['sources'] = [{'path': p, 'sha256': hashlib.sha256((ROOT/p).read_bytes()).hexdigest()}
                         for p in sources + ['ports/nds/platform/audio/runtime.c',
                                             'ports/nds/platform/audio/runtime.h',
                                             'ports/nds/platform/audio/arm9.h',
                                             'ports/nds/platform/audio/audio_ipc.h',
                                             'ports/nds/tests/audio_host/nds.h',
                                             'ports/nds/tools/check_audio_host.py']]
    report['passed'] = True
    (EVIDENCE/'host-audio-runtime.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print(json.dumps(report['tests'], indent=2))

if __name__ == '__main__':
    main()
