"""Record the partial direct-world-OBJ hardware proof without claiming FPS."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[3]


def main():
    def read(p):
        return json.loads((ROOT/p).read_text(encoding='utf-8-sig'))

    def sha(p):
        return hashlib.sha256((ROOT/p).read_bytes()).hexdigest()

    report_path = Path('research/experiments/g20a-world-obj-bringup-final/report.json')
    manifest_path = Path('research/experiments/g20a-world-obj-native-final/prototype-build.json')
    repeat_path = Path('research/experiments/g20a-world-obj-native-repeat/prototype-build.json')
    r, manifest, repeat = map(read,(report_path,manifest_path,repeat_path))
    variants = manifest['variants']
    for name in ('hw_v128_world_obj_probe','hw_v128_entity_rect'):
        expected = variants[name]['rom_sha256']
        assert repeat['variants'][name]['rom_sha256'] == expected
        for folder in ('g20a-world-obj-native-final','g20a-world-obj-native-repeat'):
            assert sha(Path('research/experiments')/folder/name/'dok-diagnostic.gba') == expected
    control = '2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    assert variants['hw_v128_entity_rect']['rom_sha256'] == r['geometry']['rom_sha256'] == control
    assert variants['hw_v128_world_obj_probe']['rom_sha256'] == r['ppu']['rom_sha256']
    for path, expected in r['scripts'].items():
        assert sha(Path(path)) == expected
    assert sha(Path('build/gba/assets/assets.bin')) == r['asset_package_sha256']
    rows = r['ppu']['rows']
    assert len(rows) == 56 and r['ppu_exact_cases'] == 54
    pressure = [v for v in rows if v['different_native_pixels']]
    assert len(pressure) == 2 and all(v['name']=='masked-behind-cpu' and v['different_native_pixels']==100 for v in pressure)
    mux = [v for v in rows if v['name']=='masked-behind-cpu-multiplex']
    assert len(mux)==2 and all(v['different_native_pixels']==0 for v in mux)
    assert all(v['dispcnt']==(0x3060 if v['hblank_oam_access_enabled'] else 0x3040) for v in rows)
    assert len([v for v in rows if v['name'].startswith('light-')]) == 32
    for v in rows:
        assert sha(Path(v['image'])) == v['image_sha256']
    for im in r['source_images']:
        assert sha(Path(im['lit_pixels_path'])) == im['lit_pixels_sha256']
    plans = {}
    geometry = r['geometry']['rows']
    assert len(geometry) == 34
    for mode in ('full_obj','hybrid','hybrid_multiplex'):
        selected = [v['resource_plans'][mode] for v in geometry]
        plans[mode] = dict(admitted_rectangles=sum(v['admitted_rectangles'] for v in selected),
            failures=dict(sum((Counter(v['failures']) for v in selected),Counter())),
            median_objects=statistics.median(v['objects'] for v in selected),
            median_matrices=statistics.median(v['matrices'] for v in selected),
            median_texture_bytes=statistics.median(v['texture_bytes'] for v in selected))
    preserved={}
    for name, expected in (
        ('DoK-port_hardware_cached_prototype.gba',control),
        ('DoK-port_hardware_rows96_prototype.gba','97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
        ('DoK-port_hardware_rows72_prototype.gba','17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')):
        assert sha(Path('artifacts/gba')/name) == expected
        preserved[name] = expected
    result=dict(task='G20A',status='source-art-bringup-corrected-hblank-access',
        goal_complete=False,performance_build_in_current_goal=False,
        trial_rom_sha256=r['ppu']['rom_sha256'],control_rom_sha256=control,
        core_sha256=r['core_sha256'],byte_exact_repeat=True,
        scope=r['scope'],ppu_exact_cases=54,ppu_pressure_cases=pressure,
        ppu_hblank_multiplex_cases=mux,source_images=r['source_images'],
        geometry_counts=dict(sum((Counter(v['counts']) for v in geometry),Counter())),
        resource_plans=plans,preserved_roms=preserved,
        memory=dict(ewram_bytes=261308,ewram_free_bytes=836,iwram_bytes=24408,
                    iwram_free_before_stack_reserve_bytes=168,stack_reserve_bytes=8192),
        limitations=r['limitations']+[
            'CPU is held with IRQ and PCM disabled during PPU upload fixtures. This is not a gameplay or performance ROM.',
            'Two non-multiplex pressure fixtures lose 100 pixels each; they are explicit failures, not counted as passed.',
            'V4 probe omitted DISPCNT bit5 for HBlank OAM access; its conclusion is superseded. Current native CPU stores enable bit5 and assert it in actual MMIO.',
            'Native hybrid integration and its timing are recorded separately in analysis/gba/g20a-world-obj-native.json.',
            'The same full mixed composition is exact with legal HBlank OAM multiplex; physical GBA not tested.'],
        next_action='Use the separate native hybrid trial for architecture selection; genuine sector clip/span still open.',
        evidence=[dict(path=p.as_posix(),sha256=sha(p)) for p in (report_path,manifest_path,repeat_path)])
    (ROOT/'analysis/gba/g20a-world-obj-bringup.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],ppu_exact=54,ppu_pressure_failures=2,resource_plans=plans),indent=2))


if __name__=='__main__':
    main()
