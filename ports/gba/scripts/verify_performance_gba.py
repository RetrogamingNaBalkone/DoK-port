"""G20 paired, pixel-exact camera sweep and complete-frame timing in mGBA.

Frozen simulation isolates renderer transformations; live integrated workloads
are measured separately. The G18 ROM is an optimization regression reference,
not evidence of whole-session DOS equivalence.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct

from verify_gba import sha
from verify_renderer_gba import BUDGET, stack_usage
from verify_sprites_gba import Probe, load_json


def cases(step):
    result = []
    for level in range(3):
        model = load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json')
        actor = model['objects'][model['player_index']-1]['actor']
        # Runtime JSON stores initial native scalar fields as a dictionary.
        pose = [actor['x'], actor['y'], actor['z']]
        for heading in range(0, 360, step):
            result.append(dict(name=f'L{level+1}-start-{heading}', level=level,
                               camera=pose+[heading]))
        for name, camera in (
                ('near-x', [pose[0]//480*480+1, pose[1], pose[2], 45]),
                ('near-z', [pose[0], pose[1], pose[2]//480*480+1, 135]),
                ('low', [1, -300, 1, 225]),
                ('high', [30719, -300, 30719, 45])):
            result.append(dict(name=f'L{level+1}-{name}', level=level, camera=camera))
    # Preserve the four proven visible water views from G10; all original
    # shift directions and representative crop phases enter the paired oracle.
    for water in load_json('analysis/gba/g10-surface-gba.json')['selections']:
        for phase in (0,1,9,17):
            result.append(dict(name=f'L2-water-{water["glyph"]}-{phase}',level=1,
                               camera=water['camera'],crop_glyph=water['glyph'],crop_phase=phase))
    return result


def run(build, core, selected, samples, output, expected=None):
    probe = Probe(core, build)
    rows = []
    try:
        probe.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        for symbol in ('g_motion_probe', 'g_visual_probe', 'g_combat_probe', 'g_ai_probe', 'g_camera_capture_hold'):
            probe.write(probe.symbols[symbol], struct.pack('<I', 1))
        rollover='surface_epoch' in probe.symbols
        if rollover:probe.write(probe.symbols['surface_epoch'],struct.pack('<I',0x7fffe))
        # Force a pristine level after the boot setup on both ROMs.
        probe.camera(1, 15120, -300, 22800, 90)
        current = 1
        for language in (0, 1):
            probe.language(language)
            for case in selected:
                level = case['level']
                if level != current:
                    current = level
                probe.camera(level, *case['camera'])
                if 'crop_glyph' in case:
                    model=load_json('research/evidence/runtime/ru/LEVEL2-runtime.json')
                    for index,obj in enumerate(model['objects']):
                        if obj['glyph'] not in (60,62,123,125):continue
                        phase=case['crop_phase'] if obj['glyph']==case['crop_glyph'] else 0
                        address=probe.symbols['g_render_world']+4+index*36
                        probe.write(address+2,struct.pack('<H',phase))
                        probe.write(address+8,struct.pack('<h',phase))
                        known=probe.object_words(index)[6]
                        probe.write(address+12,struct.pack('<H',(known|8)&~1))
                probe.present()
                values, intervals, stages = [], [], []
                previous = probe.diag()['present_vblank']
                for _ in range(samples):
                    d = probe.present()
                    values.append(d['last_cycles'])
                    intervals.append((d['present_vblank']-previous)&0xffffffff)
                    previous = d['present_vblank']
                    t = probe.words('g_render_stages', 7)
                    stages.append([t[0]]+[b-a for a, b in zip(t, t[1:])])
                raw = probe.frontend.read(0x06000000+d['visible_page']*0xa000, 38400)
                key = f'{language}-{case["name"]}'
                if expected is None:
                    (output/(key+'.idx')).write_bytes(raw)
                else:
                    before = (expected/(key+'.idx')).read_bytes()
                    # HUD counters are included: every displayed index must match.
                    assert raw == before, (key, 'pixel mismatch', sum(a != b for a,b in zip(raw,before)))
                rows.append(dict(**case, language=language, samples=values,
                                 minimum=min(values), median=statistics.median(values), maximum=max(values),
                                 stage_medians=dict(zip(('logic', 'resources', 'geometry', 'sort', 'raster', 'ui', 'copy'),
                                                       map(statistics.median, zip(*stages)))),
                                 vblank_intervals=intervals,
                                 capture_hold_enabled=True,
                                 framebuffer_sha256=hashlib.sha256(raw).hexdigest(),
                                 faces=d['face_count'], pixels=d['pixels']))
            print(f'G20 cameras {build} language={language}: {len(selected)} passed', flush=True)
        return dict(rom_sha256=sha(probe.rom), rows=rows, stack=stack_usage(probe.frontend),
                    surface_cache_epoch_rollover_exercised=rollover)
    finally:
        probe.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, default=Path('build/gba/g20'))
    p.add_argument('--reference-build', type=Path, default=Path('build/gba/g18'))
    p.add_argument('--reference-rom-sha256',default='46e4bfa412dd1abb6b729d4a9ed014802e99430b1dbc47ffb70d2de077eeb3c0')
    p.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    p.add_argument('--report', type=Path, default=Path('analysis/gba/g20-cameras.json'))
    p.add_argument('--step', type=int, default=30)
    p.add_argument('--samples', type=int, default=3)
    a = p.parse_args()
    assert 1 <= a.step <= 90 and a.samples >= 2
    assert sha(a.reference_build/'dok-diagnostic.gba')==a.reference_rom_sha256,'Frozen reference ROM changed'
    output = a.build_dir/'performance-reference'
    output.mkdir(parents=True, exist_ok=True)
    selected = cases(a.step)
    before = run(a.reference_build, a.core, selected, a.samples, output)
    after = run(a.build_dir, a.core, selected, a.samples, output, output)
    result = dict(task='G20', status='passed', scope='frozen-simulation paired camera regression',
                  script_sha256=sha(__file__), core_sha256=sha(a.core), budget_cycles=BUDGET,
                  before=before, after=after, pixel_exact_frames=len(after['rows']),
                  note='Capture hold adds a VBlank outside the CPU timer; these intervals are not normal gameplay FPS.',
                  full_game_performance_verified=False, physical_gba_tested=False)
    a.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'G20 paired cameras passed: {len(after["rows"])} frames; report {a.report}')


if __name__ == '__main__':
    main()
