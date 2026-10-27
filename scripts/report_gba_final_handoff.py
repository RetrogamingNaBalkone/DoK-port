"""Verify final G20A evidence; completion of handoff is not an FPS pass."""
import json
from pathlib import Path

from audit_gba_final_costs import CONTROL, ROOT
from verify_gba import sha


def main():
    read = lambda p: json.loads((ROOT/p).read_text(encoding='utf-8-sig'))
    audit = read('analysis/g20a-final-costs.json')
    manifest_path = 'research/experiments/g20a-final-costs/prototype-build.json'
    manifest = read(manifest_path)
    assert sha(ROOT/manifest_path) == audit['manifest_sha256']
    assert sha(ROOT/'scripts/audit_gba_final_costs.py') == audit['script_sha256']
    assert sha(ROOT/'build/gba/assets/assets.bin') == audit['source_post_feasibility']['package_sha256']
    assert sha(ROOT/'research/experiments/g20a-final-costs/prelit.bin') == audit['source_post_feasibility']['bank_sha256']
    assert sha(ROOT/'research/experiments/g20a-final-costs/exact-offsets.bin') == audit['source_post_feasibility']['tables_sha256']
    assert all(sha(ROOT/p) == value for p, value in manifest['sources'].items())
    for name in ('hw_v128_entity_rect','hw_v128_final_class_profile'):
        assert sha(ROOT/'research/experiments/g20a-final-costs'/name/'dok-diagnostic.gba') == manifest['variants'][name]['rom_sha256']
    assert manifest['variants']['hw_v128_entity_rect']['rom_sha256'] == CONTROL
    assert audit['profile']['rom_sha256'] == manifest['variants']['hw_v128_final_class_profile']['rom_sha256']
    assert len(audit['profile']['rows']) == 5 and not audit['profile']['forced_pcm']
    old = read('analysis/g20a-hardware-cache.json')
    shear = read('analysis/g20a-sector-shear-trial.json')
    assert old['rom_sha256'] == shear['control_rom_sha256'] == CONTROL
    assert old['clean_repeat_byte_exact'] and shear['byte_exact_repeat']
    assert not shear['performance_build_in_current_goal']
    for item in shear['evidence']:
        assert sha(ROOT/item['path']) == item['sha256']
    for name, expected in shear['preserved_roms'].items():
        assert sha(ROOT/'artifacts/gba'/name) == expected
    spatial = read('research/experiments/g20a-hw-entity-rect-v2/spatial.json')
    assert spatial['rom_sha256'] == CONTROL and len(spatial['rows']) == 440
    heavy = read('research/experiments/g20a-sector-shear-v4/control-target.json')
    live = read('research/experiments/g20a-sector-shear-v4/hw_v128_entity_rect-live24.json')
    assert heavy['rom_sha256'] == live['rom_sha256'] == CONTROL
    assert all(row['audio']['underruns'] == 0 for row in heavy['rows']+live['rows'])
    assert all(row['time_conservation_error_ticks'] == 0 for row in live['rows'])
    minima = {str(level): min(r['presented_fps'] for r in spatial['rows'] if r['level']==level)
              for level in (1,2,3)}
    inputs = ['analysis/g20a-final-costs.json', manifest_path,
              'analysis/g20a-hardware-cache.json','analysis/g20a-row-quality-trial.json',
              'analysis/g20a-sector-shear-trial.json',
              'research/experiments/g20a-hw-entity-rect-v2/spatial.json',
              'research/experiments/g20a-sector-shear-v4/control-target.json',
              'research/experiments/g20a-sector-shear-v4/hw_v128_entity_rect-live24.json',
              'analysis/g20a-dark-distance-audit.json',
              'docs/gba-optimization-handoff.md']
    result = dict(task='G20A-current-goal-final-handoff',
        status='handoff-complete-fps-targets-not-met',
        script_sha256=sha(__file__), current_goal_boundary_complete=True,
        performance_build_created=False, release_quality_selected=False,
        g20_budget_complete=False, g21_release_complete=False,
        selected_rom_sha256=CONTROL, spatial_poses=440, spatial_minimum_fps=minima,
        fps_targets_universally_met=dict(satisfactory_15=False, good_internal_24_1=False, ideal_30=False),
        architecture_comparison='Planned representative comparison closed;2775 selected. '
            'Not an absolute GBA hardware limit proof.',
        transparent_column_check='Source run storage/reconstruction feasible, native sparse-post '
            'renderer unmeasured and deferred. No FPS gain inferred from asset sparsity.',
        current_sources_byte_exact_control=True,
        heavy_control=[{k:r[k] for k in ('level','cell','heading','median','presented_fps')} for r in heavy['rows']],
        latest_live24=[{k:r[k] for k in ('level','language','presented_fps')} for r in live['rows']],
        remaining_costs=audit['profile'],
        preserved_roms=shear['preserved_roms'],
        limitations=['No physical GBA/original BIOS acceptance.',
            'Instrumented attribution includes setup/coverage/texels and ROM relocation; not production FPS.',
            'Frozen spatial scenes do not establish whole-game minima or live AI budget.',
            'Known294 Pause/Resume mixed-frame failure also occurs on2775; artificial all-awake overload remains.',
            'No draw-distance/AI rebalance or Performance profile implemented.'],
        evidence=[dict(path=p,sha256=sha(ROOT/p)) for p in inputs])
    (ROOT/'analysis/g20a-final-handoff.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(result['status'], minima)


if __name__ == '__main__':
    main()
