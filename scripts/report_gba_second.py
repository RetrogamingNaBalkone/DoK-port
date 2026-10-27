"""Bind the second G20A pass to exact-ROM and quality-prototype evidence."""
import json
from pathlib import Path
import shutil

from verify_gba import sha

ROOT=Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def checked(path,digest,oracle=False):
    record=read(path)
    actual=record['after']['rom_sha256'] if oracle else record.get('rom_sha256')
    assert actual==digest,(path,actual,digest)
    if 'status' in record:assert record['status']=='passed',(path,record['status'])
    else:assert record['integrated_renderer'] and record['edge_vectors_on_arm']=='passed'
    return dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),status='passed')


def main():
    root=ROOT/'research/experiments/g20a-second';comparison=read(ROOT/'analysis/g20a-second-prototypes.json')
    variants={};artifacts={}
    for variant,prefix in (('exact_ui','exact-ui'),('world_coarse','world-coarse')):
        build=root/variant;meta=read(build/'build-report.json');digest=sha(build/'dok-diagnostic.gba')
        assert digest==meta['rom_sha256']
        checks={name:checked(root/f'{prefix}-{name}.json',digest)
                for name in ('arm','renderer','sprites','interactions','time','ai_combat','ui','flow','game')}
        interaction=read(root/f'{prefix}-interactions.json')
        assert interaction['original_affine_visual_oracle_checked']==(variant=='exact_ui')
        if variant=='exact_ui':
            checks['oracle']=checked(root/'exact-ui-oracle.json',digest,True)
            assert read(root/'exact-ui-oracle.json')['pixel_exact_frames']==488
        game=read(root/f'{prefix}-game.json')
        variants[variant]=dict(rom_sha256=digest,rom_bytes=meta['rom_bytes'],checks=checks,
                              ewram_static_bytes=meta['ewram_static_bytes'],
                              iwram_static_bytes=meta['symbols']['__iwram_end']-meta['symbols']['__iwram_start'],
                              iwram_stack_bios_reserve=8192,stack=game['stack'],irq_stack=game['irq_stack'],
                              audio_underruns=game['audio_underruns'],
                              native_pixel_exact=variant=='exact_ui',
                              source_snapshot={p.name:sha(p) for p in (build/'source').iterdir() if p.is_file()})
        assert game['audio_underruns']==0
    water_meta=read(root/'world_water/build-report.json')
    water_oracle=read(root/'water-oracle.json')
    assert water_oracle['after']['rom_sha256']==water_meta['rom_sha256']
    assert water_oracle['before']['rom_sha256']==sha(root/'world_ui/dok-diagnostic.gba')
    assert water_oracle['pixel_exact_frames']==128
    water_arm=checked(root/'world-water-arm.json',water_meta['rom_sha256'])
    for variant,record in comparison['frozen'].items():
        assert record['rom_sha256']==sha(root/variant/'dok-diagnostic.gba'),variant
        assert comparison['live'][variant]['rom_sha256']==record['rom_sha256'],variant
    assert sha(root/'baseline/dok-diagnostic.gba')==sha(ROOT/'build/select-menu-fix/dok-diagnostic.gba')
    originals=[r for r in read(ROOT/'analysis/artifact-cleanup-20261026.json')['preserved_sha256'] if r['path'].startswith('originals/')]
    for record in originals:assert sha(ROOT/record['path'])==record['sha256']
    for variant,name in (('exact_ui','DoK-port_exact_ui_v2.gba'),
                         ('world_water','DoK-port_world_spans_prototype.gba'),
                         ('world_coarse','DoK-port_floor_4x2_v2.gba')):
        dest=ROOT/'artifacts/gba'/name;shutil.copy2(root/variant/'dok-diagnostic.gba',dest)
        assert sha(dest)==sha(root/variant/'dok-diagnostic.gba')
        artifacts[variant]=dict(path=dest.relative_to(ROOT).as_posix(),bytes=dest.stat().st_size,sha256=sha(dest))
    dest=ROOT/'artifacts/gba/DoK-port_exact_planes_v1.gba'
    shutil.copy2(ROOT/'research/experiments/g20a-prototypes/exact_planes/dok-diagnostic.gba',dest)
    result=dict(task='G20A',status='second-pass-validated-target-open',targets_fps=[30,24.1,15],targets_met=False,
                conservative_choice='exact_ui',quality_experiment='world_coarse',artifacts=artifacts,variants=variants,
                water_arm_vs_c_exact_frames=128,water_arm=water_arm,original_files_sha_matched=len(originals),
                baseline_rom_reproduced_byte_for_byte=True,
                comparison=dict(path='analysis/g20a-second-prototypes.json',sha256=sha(ROOT/'analysis/g20a-second-prototypes.json')),
                source_manifest=dict(path='research/experiments/g20a-second/prototype-build.json',sha256=sha(root/'prototype-build.json')),
                script_sha256=sha(__file__),
                limitations=['No physical GBA/original BIOS or whole-level playthrough.',
                             'World planes use perspective world UV instead of native affine tile UV.',
                             'Coarse planes sample4x2 displayed pixels; geometry masks/actors remain120x144, HUD240x160.',
                             'The original affine visual oracle is intentionally not applied to coarse interactions; logic checks remain active.',
                             'All-awake AI overload remains; no ticks or callbacks are dropped.',
                             '15+ FPS has not been achieved across all three live start scenes.'])
    (ROOT/'analysis/g20a-second-validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Second G20A pass validated; exact UI488 frames, separate floor4x2 prototype; target remains open.')


if __name__=='__main__':main()
