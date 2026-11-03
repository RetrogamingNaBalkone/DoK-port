"""PERF04: paired LEVEL2 ammo room / copper key measurements in mGBA."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'ports/gba/scripts'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--before', type=Path, default=Path('build/gba/releases/gba-beta2/hardware/g22_split_active'))
    ap.add_argument('--after', type=Path, default=Path('build/gba/perf04/final/hardware/perf04_full8'))
    ap.add_argument('--out', type=Path, default=Path('research/evidence/gba-perf04'))
    a = ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    a.out.mkdir(parents=True, exist_ok=True)
    # Native map orientation, zero-based cells. Copper X is at (14,48).
    cells = [(50, 41), (51, 41), (55, 41), (50, 42),
             (14, 48), (14, 47), (14, 49), (15, 49)]
    cases = [f'2:{z*64+x}:{h}' for x, z in cells for h in range(0, 360, 45)]
    commands = []
    for label, build in [('before', a.before), ('after', a.after)]:
        for language in (0, 1):
            argv = [sys.executable, '-X', 'utf8', 'ports/gba/scripts/benchmark_gba_spatial_sweep.py',
                    '--build-dir', str(build), '--report', str(a.out/f'{label}-{language}.json'),
                    '--samples', '30', '--language', str(language), '--verify-output', '--cases', *cases]
            commands.append(argv)
            subprocess.run(argv, check=True)
    (a.out/'commands.json').write_text(json.dumps(commands, indent=2)+'\n', encoding='utf-8')
    import benchmark_battles_gba as battles
    # Real hero/clock/AI, stationary fixture; not a route from the level start.
    battles.CASES = ((2, 50, 41, 270, 'ammo-entrance'),
                    (2, 51, 41, 315, 'ammo-diagonal'),
                    (2, 14, 48, 90, 'copper-key'),
                    (2, 15, 49, 0, 'copper-approach'))
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    for label, build in [('before', a.before), ('after', a.after)]:
        for fire in (False, True):
            result = battles.measure(build, core, 120, (0, 1), fire)
            (a.out/f'{label}-live-{int(fire)}.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    pairs = []
    for language in (0, 1):
        before = json.loads((a.out/f'before-{language}.json').read_text())
        after = json.loads((a.out/f'after-{language}.json').read_text())
        for b, c in zip(before['rows'], after['rows'], strict=True):
            for key in ('cell', 'heading', 'completed_ppu_sha256', 'coverage_sha256',
                        'foreground_sha256', 'completed_renderer_stats'):
                assert b[key] == c[key], (language, b['cell'], b['heading'], key)
            pairs.append(dict(language=language, cell=b['cell'], heading=b['heading'],
                              cpu_before=b['median'], cpu_after=c['median'],
                              fps_before=b['presented_fps'], fps_after=c['presented_fps'],
                              change_percent=100*(c['median']/b['median']-1)))
    result = dict(status='passed', frozen_ppu_coverage_stats_exact=len(pairs), pairs=pairs,
                  scope='mGBA; frozen causal comparisons and separate live stationary fights. No physical GBA claim.')
    (a.out/'paired.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PERF04 paired checks passed', len(pairs), flush=True)


if __name__ == '__main__':
    main()
