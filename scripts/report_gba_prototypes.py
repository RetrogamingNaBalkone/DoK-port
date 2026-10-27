"""Bind G20A evidence to immutable ROMs and deliver separate local prototypes."""
import hashlib
import json
from pathlib import Path
import shutil

from verify_gba import sha

ROOT=Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def main():
    out=ROOT/'build/g20a-prototypes'
    builds=read(out/'prototype-build.json')
    comparison=read(ROOT/'analysis/g20a-prototypes.json')
    best=read(out/'exact_planes/build-report.json')
    digest=best['rom_sha256']
    assert sha(out/'exact_planes/dok-diagnostic.gba')==digest
    checks={}
    for name in ('oracle','arm','renderer','sprites','interactions','time','ai_combat','ui','flow','game'):
        path=out/f'exact-planes-{name}.json';record=read(path)
        if name=='oracle':
            actual=record['after']['rom_sha256'];assert record['pixel_exact_frames']==488
        else:
            actual=record.get('rom_sha256',record.get('rom'))
            if isinstance(actual,dict):actual=actual.get('sha256')
        if actual is not None:assert actual==digest,(name,actual,digest)
        if name=='time':
            assert record['integrated_renderer'] and record['edge_vectors_on_arm']=='passed'
        else:assert record['status']=='passed',(name,record['status'])
        checks[name]=dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),status='passed')
    for variant,record in comparison['frozen'].items():
        assert record['rom_sha256']==sha(out/variant/'dok-diagnostic.gba'),variant
    for variant,record in comparison['live'].items():
        assert record['rom_sha256']==sha(out/variant/'dok-diagnostic.gba'),variant
    original_checks=[]
    for record in read(ROOT/'analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if not record['path'].startswith('originals/'):continue
        assert sha(ROOT/record['path'])==record['sha256'],record['path']
        original_checks.append(record)
    artifacts={}
    for variant,name in (('exact_planes','DoK-port_exact_planes_v1.gba'),
                         ('bsp_setup','DoK-port_BSP_prototype.gba')):
        dest=ROOT/'artifacts/gba'/name;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(out/variant/'dok-diagnostic.gba',dest)
        assert sha(dest)==sha(out/variant/'dok-diagnostic.gba')
        artifacts[variant]=dict(path=dest.relative_to(ROOT).as_posix(),bytes=dest.stat().st_size,sha256=sha(dest))
    game=read(out/'exact-planes-game.json')
    result=dict(task='G20A',status='validated-prototypes-performance-target-open',
                current_best='exact_planes',rom_sha256=digest,artifacts=artifacts,checks=checks,
                pixel_exact_frames=488,scene_columns=120,scene_height=144,ui_width=240,ui_height=160,
                quality_reduced_in_best=False,ai_rules_changed=False,ticks_dropped=False,
                targets_fps=[30,24.1,15],targets_met=False,
                memory=dict(rom_bytes=best['rom_bytes'],ewram_static_bytes=best['ewram_static_bytes'],
                            ewram_free=262144-best['ewram_static_bytes'],
                            iwram_static_bytes=best['symbols']['__iwram_end']-best['symbols']['__iwram_start'],
                            iwram_stack_bios_reserve=8192,
                            iwram_free_before_stack=24576-(best['symbols']['__iwram_end']-best['symbols']['__iwram_start']),
                            stack=game['stack'],irq_stack=game['irq_stack']),
                integrated_audio_underruns=game['audio_underruns'],
                all_awake_worst_cycles=max(r['maximum'] for r in game['rows']),
                original_files_matching_preexisting_inventory=original_checks,
                comparison=dict(path='analysis/g20a-prototypes.json',sha256=sha(ROOT/'analysis/g20a-prototypes.json')),
                scripts={p.relative_to(ROOT).as_posix():sha(p) for p in [
                    ROOT/'scripts/prototype_gba_bsp.py',ROOT/'scripts/benchmark_gba_prototypes.py',
                    ROOT/'scripts/report_gba_prototypes.py',ROOT/'scripts/verify_performance_arm.py',
                    ROOT/'scripts/verify_sprites_gba.py',ROOT/'scripts/verify_ui_gba.py',ROOT/'scripts/verify_interactions_gba.py']},
                limitations=['No physical GBA or original BIOS test.',
                             'Live FPS fixtures are not whole-level playthroughs.',
                             'BSP and procedural variants are not selected defaults.',
                             'The five-band and low-resolution options do not justify their image cost.',
                             'All-awake simulation overload remains; rendering changes do not solve it.'])
    (ROOT/'analysis/g20a-prototype-validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Validated G20A prototypes: '+digest+'; target15+ FPS remains open.')


if __name__=='__main__':main()
