"""Collect native evidence for the full foreground column prototype (G20A)."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main():
    def read(p):
        return json.loads((ROOT / p).read_text(encoding='utf-8-sig'))

    def sha(p):
        return hashlib.sha256((ROOT / p).read_bytes()).hexdigest()

    folder = Path('research/experiments/g20a-columns-v6')
    paths = [folder / n for n in ('prototype-build.json', 'target.json',
             'ppu18-live.json', 'profile.json', 'visibility.json', 'control-live24.json')]
    paths += [Path('research/experiments/g20a-columns-repeat-final/prototype-build.json'),
              Path('research/experiments/g20a-hw-entity-rect-v2/spatial.json')]
    manifest, target, ppu, profile, visibility, live, repeat, baseline = map(read, paths)
    variants = manifest['variants']
    trial = variants['hw_v128_columns']['rom_sha256']
    control = variants['hw_v128_entity_rect']['rom_sha256']
    assert trial == 'e9bae2d9268db5c74674c7dd2eab8e8511d1ea2d84d039122c745e5b54804b22'
    assert control == '2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    for name in ('hw_v128_columns', 'hw_v128_entity_rect'):
        assert variants[name]['rom_sha256'] == repeat['variants'][name]['rom_sha256']
        assert sha(folder / name / 'dok-diagnostic.gba') == variants[name]['rom_sha256']
        assert sha(Path('research/experiments/g20a-columns-repeat-final') / name / 'dok-diagnostic.gba') == variants[name]['rom_sha256']
    stream = 'bfdba7d0455199e5599ebea9bf795abefdaff0c730f63db4b848e71a12825184'
    assert repeat['variants']['hw_v128_stream_bsp']['rom_sha256'] == stream
    assert sha(Path('research/experiments/g20a-columns-repeat-final/hw_v128_stream_bsp/dok-diagnostic.gba')) == stream
    assert target['rom_sha256'] == profile['rom_sha256'] == ppu['after']['rom_sha256'] == visibility['after']['rom_sha256'] == trial
    assert baseline['rom_sha256'] == live['rom_sha256'] == ppu['before']['rom_sha256'] == visibility['before']['rom_sha256'] == control
    assert ppu['live']['rom_sha256'] == trial
    assert visibility['status'] == 'passed'
    assert visibility['script_sha256'] == sha(Path('ports/gba/scripts/verify_gba_columns.py'))
    assert visibility['cold_reset_after_freeze'] and ppu['frozen_initial_world_reset']
    assert not profile['forced_pcm']
    key = lambda r: (r['level'], r['cell'], r['heading'])
    bases = {key(r): r for r in baseline['rows']}
    rows = [dict(level=r['level'], cell=r['cell'], heading=r['heading'],
                 cpu_ratio=r['median'] / bases[key(r)]['median'],
                 baseline_fps=bases[key(r)]['presented_fps'], fps=r['presented_fps'])
            for r in target['rows']]
    assert len(rows) == 5 and all(r['cpu_ratio'] > 1 for r in rows)
    poses = visibility['after']['rows']
    columns = [c for r in poses for c in r['columns']]
    assert len(poses) == 36 and len(columns) == 108
    assert sum(c['checked_rows'] for c in columns) == 13752
    assert len(ppu['after']['rows']) == 18 and len(ppu['live']['rows']) == len(live['rows']) == 6
    for r in target['rows'] + ppu['live']['rows'] + live['rows']:
        assert r['audio']['underruns'] == 0 and r['sound_active_frames'] > 0
    for r in ppu['live']['rows'] + live['rows']:
        assert r['time_conservation_error_ticks'] == 0
    preserved = {}
    for name, expected in (
        ('DoK-port_hardware_cached_prototype.gba', control),
        ('DoK-port_hardware_rows96_prototype.gba', '97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
        ('DoK-port_hardware_rows72_prototype.gba', '17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')):
        assert sha(Path('artifacts/gba') / name) == expected
        preserved[name] = expected
    short = lambda rs: [dict(level=r['level'], language=r['language'], fps=r['presented_fps'],
                             max_motion_debt=r['max_motion_debt']) for r in rs]
    result = dict(task='G20A', status='native-column-prototype-not-selected', goal_complete=False,
        trial_rom_sha256=trial, control_rom_sha256=control, core_sha256=target['core_sha256'],
        byte_exact_repeat=True, prior_stream_rom_preserved=stream, heavy_cases=rows,
        ppu=dict(cases=18, coverage_exact=sum(r['coverage_equal'] for r in ppu['after']['rows']),
                 max_changed_pixels=max(r['different_rgb_pixels'] for r in ppu['after']['rows']),
                 max_overlay_changes=max(r['different_overlay_rgb_pixels'] for r in ppu['after']['rows'])),
        visibility=dict(poses=36, column_snapshots=108, checked_rows=13752,
                        excluded_shared_boundary_rows=72, scope=visibility['scope'],
                        stack=visibility['after']['stack']),
        live24=dict(before=short(live['rows']), after=short(ppu['live']['rows'])),
        profile=profile, memory=dict(ewram_bytes=261280, ewram_free_bytes=864,
            iwram_bytes=20976, iwram_free_before_stack_reserve_bytes=3600, stack_reserve_bytes=8192),
        preserved_roms=preserved, evidence=[dict(path=p.as_posix(), sha256=sha(p)) for p in paths],
        limitations=[
            'Five heavy PCM cases, 18 frozen PPU views, 36 visibility poses and six short live runs; no full 440-column sweep or full gameplay acceptance.',
            'Specific per-row nearest-depth/owner DDA implementation; not a Doom sector top/bottom clip and visplane renderer.',
            'World-space ceiling and wall UV/raster rounding differ from the old affine path; exact RGB is not the acceptance criterion.',
            'Profile uses instrumentation without forced PCM; do not compare attributed totals directly with target PCM cycles.',
            'Live motion debt is retained, not proof of sufficient simulation speed; no physical GBA acceptance.',
            'V1-V5 superseded: reciprocal depth equality fixed, startup state cold-reset after freeze, original envelope restored without a 6400 cap.',
            'No draw-distance or AI activation changes. Full mutated-map/fallback and moving-transition QA not accepted.'],
        next_action='Direct hardware world OBJ and genuine sector clip/span architecture before local tuning; static image cache lower priority.')
    (ROOT / 'analysis/gba/g20a-columns-trial.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(heavy_cases=rows, ppu=result['ppu'], visibility=result['visibility']), indent=2))


if __name__ == '__main__':
    main()
