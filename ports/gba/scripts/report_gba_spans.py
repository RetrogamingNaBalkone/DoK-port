"""Validate and deliver the G20A shared-plane/ceiling-side checkpoint."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import json
from pathlib import Path
import shutil
import statistics

from report_gba_second import ROOT, read, checked
from verify_gba import sha


def main():
    root=ROOT/'research/experiments/g20a-sides';comparison=read(root/'comparison.json')
    variants={};artifacts={}
    for variant,prefix,native in (('sides_exact','exact',True),('sides_coarse','coarse',False)):
        build=root/variant;meta=read(build/'build-report.json');digest=sha(build/'dok-diagnostic.gba')
        assert digest==meta['rom_sha256']==comparison['frozen'][variant]['rom_sha256']==comparison['live'][variant]['rom_sha256']
        assert digest==sha(ROOT/'research/experiments/g20a-sides-rebuild'/variant/'dok-diagnostic.gba')
        checks={name:checked(root/f'{prefix}-{name}.json',digest)
                for name in ('renderer','sprites','interactions','time','ai_combat','ui','flow','game')}
        checks['arm']=checked(root/f'{variant}-arm.json',digest)
        checks['oracle']=checked(root/f'{prefix}-oracle.json',digest,True)
        oracle=read(root/f'{prefix}-oracle.json');assert oracle['pixel_exact_frames']==488
        reference=ROOT/('build/gba/select-menu-fix' if native else 'research/experiments/g20a-second/world_coarse')/'dok-diagnostic.gba'
        assert oracle['before']['rom_sha256']==sha(reference)
        assert read(root/f'{prefix}-interactions.json')['original_affine_visual_oracle_checked']==native
        game=read(root/f'{prefix}-game.json');assert game['audio_underruns']==0
        ratios=[b['median']/a['median'] for b,a in zip(oracle['before']['rows'],oracle['after']['rows'])]
        iwram=meta['symbols']['__iwram_end']-meta['symbols']['__iwram_start']
        variants[variant]=dict(rom_sha256=digest,rom_bytes=meta['rom_bytes'],checks=checks,
            native_pixel_exact=native,oracle_reference_sha256=sha(reference),exact_frames=488,
            paired_cpu_speedup_median=statistics.median(ratios),
            ewram_static_bytes=meta['ewram_static_bytes'],iwram_static_bytes=iwram,
            ewram_free_bytes=262144-meta['ewram_static_bytes'],iwram_free_bytes=32768-8192-iwram,
            stack=game['stack'],irq_stack=game['irq_stack'],audio_underruns=0,
            sources={p.name:sha(p) for p in (build/'source').iterdir() if p.is_file()})
    assert sha(root/'baseline/dok-diagnostic.gba')==sha(ROOT/'build/gba/select-menu-fix/dok-diagnostic.gba')
    originals=[r for r in read(ROOT/'analysis/artifact-cleanup-20261026.json')['preserved_sha256'] if r['path'].startswith('originals/')]
    for record in originals:assert sha(ROOT/record['path'])==record['sha256']
    second_path=ROOT/'analysis/gba/g20a-second-validation.json';second=read(second_path)
    world_artifact=second['artifacts']['world_water']
    assert sha(ROOT/world_artifact['path'])==world_artifact['sha256']
    for source,name in (('g20a-second/exact_ui','DoK-port_exact_ui_v2.gba'),
                        ('g20a-second/world_coarse','DoK-port_floor_4x2_v2.gba'),
                        ('g20a-sides/sides_exact','DoK-port_optimized_prototype.gba'),
                        ('g20a-sides/sides_coarse','DoK-port_floor_4x2_prototype.gba')):
        origin=ROOT/'research/experiments'/source/'dok-diagnostic.gba';dest=ROOT/'artifacts/gba'/name
        shutil.copy2(origin,dest);assert sha(dest)==sha(origin)
        artifacts[name]=dict(path=dest.relative_to(ROOT).as_posix(),bytes=dest.stat().st_size,sha256=sha(dest))
    negative={}
    for folder in ('g20a-third','g20a-raster','g20a-eager','g20a-masked'):
        path=ROOT/'research/experiments'/folder/'comparison.json';trial=read(path)
        negative[folder]=dict(report_sha256=sha(path),scope='Nine frozen cameras and six live PCM fixtures only; unselected.',
            variants={v:dict(rom_sha256=r['rom_sha256'],cpu_speedup_median=r['cpu_speedup_median'],
                frozen_cycles=[row['median'] for row in r['rows']],
                changed_scene_pixels=[row['changed_scene_pixels'] for row in r['rows']],
                live_fps=[row['presented_fps'] for row in trial['live'][v]['rows']])
                      for v,r in trial['frozen'].items() if v!='baseline'})
    output=ROOT/'analysis/gba/g20a-span-prototypes.json';shutil.copy2(root/'comparison.json',output)
    attribution=ROOT/'analysis/gba/g20a-span-attribution.json';shutil.copy2(root/'coarse-attribution.json',attribution)
    reviewed=[root/'sides_exact/L1-median.png',root/'sides_coarse/L1-median.png',root/'sides_coarse/L2-worst.png']
    result=dict(task='G20A',status='validated-checkpoint-target-open',targets_fps=[30,24.1,15],targets_met=False,
        conservative_choice='sides_exact',quality_prototype='sides_coarse',variants=variants,artifacts=artifacts,
        measurements=dict(path=output.relative_to(ROOT).as_posix(),sha256=sha(output)),
        attribution=dict(path=attribution.relative_to(ROOT).as_posix(),sha256=sha(attribution),instrumented=True),
        negative_experiments=negative,baseline_reproduced_byte_for_byte=True,original_files_sha_matched=len(originals),
        retained_world_span_prototype=dict(**world_artifact,validation_path=second_path.relative_to(ROOT).as_posix(),
                                          validation_sha256=sha(second_path)),
        isolated_rebuild_byte_identical=True,
        reviewed_native_pngs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in reviewed],
        source_manifest=dict(path='research/experiments/g20a-sides/prototype-build.json',sha256=sha(root/'prototype-build.json')),
        script_sha256=sha(__file__),limitations=[
            'No physical GBA/original BIOS or whole-level playthrough.',
            'Live start fixtures are not whole-level minimum FPS;14.93 is below15.',
            'Coarse floor/ceiling UV is perspective world mapping instead of native affine tile interpolation.',
            'Coarse floor/ceiling samples4x2 displayed pixels; actors/doors/walls remain120x144, HUD240x160.',
            'Coarse488-frame comparison proves equivalence to the previous coarse prototype, not the original.',
            'Coarse interaction logic checks retain an explicit skip of the original affine visual oracle.',
            'All-awake overload persists; simulation debt and callbacks are retained.',
            'Standalone23-sound correlation suite was not repeated; integrated PCM22050 underruns0.'])
    (ROOT/'analysis/gba/g20a-span-validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('G20A spans checkpoint validated; exact488 native frames, coarse488 relative frames;15+ target remains open.')


if __name__=='__main__':main()
