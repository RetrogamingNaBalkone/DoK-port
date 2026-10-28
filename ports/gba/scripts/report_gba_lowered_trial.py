"""Collect the unselected Mode2 lowered-ceiling experiment and counterevidence."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
FOLDER='research/experiments/g20a-hw-lowered-v6'
VARIANT='hw_v128_lowered'
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def minima(rows):return {str(n):min(r['presented_fps'] for r in rows if r['level']==n) for n in (1,2,3)}


def main():
    build=read(FOLDER+'/'+VARIANT+'/build-report.json');digest=build['rom_sha256']
    assert sha(ROOT/FOLDER/VARIANT/'dok-diagnostic.gba')==digest
    repeat=read('research/experiments/g20a-hw-lowered-final/prototype-build.json')
    assert repeat['variants'][VARIANT]['rom_sha256']==digest
    assert repeat['variants']['source_control']['rom_sha256']=='7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8'
    accepted=read('analysis/gba/g20a-hardware-cache.json')
    assert sha(ROOT/accepted['artifact'])==accepted['rom_sha256']
    old_repeat=read('research/experiments/g20a-hw-post-lowered-control/prototype-build.json')
    assert old_repeat['variants']['hw_v128_entity_rect']['rom_sha256']==accepted['rom_sha256']
    for name in ('ui','water'):
        control=read(FOLDER+'/control-'+name+'.json')
        assert control['status']=='passed' and control['rom_sha256']==accepted['rom_sha256']
    checks={n:read(FOLDER+'/'+n+'.json') for n in ('arm','ui','water','flow','sprites','transitions')}
    for n,d in checks.items():assert d['status']=='passed' and d['rom_sha256']==digest,n
    assert checks['arm']['coverage_span_vectors']==1024 and checks['water']['phase_cases']==144
    assert len(checks['transitions']['rows'])==282
    assert all(not r['mixed_frames'] and not r['overflows'] for r in checks['transitions']['rows'])
    exact=read(FOLDER+'/exact396.json');assert len(exact['after']['rows'])==396
    assert all(r['coverage_equal'] and r['different_foreground_rgb_pixels']==r['different_overlay_rgb_pixels']==0 for r in exact['after']['rows'])
    spatial=read(FOLDER+'/spatial.json');assert spatial['rom_sha256']==digest and len(spatial['rows'])==440
    mode4=read(FOLDER+'/transitions-mode4.json')
    old_mode4=read('research/experiments/g20a-hw-lowered-v5/control-transitions-mode4.json')
    assert mode4['rom_sha256']==digest and mode4['status']==old_mode4['status']=='failed'
    assert len(mode4['rows'])==len(old_mode4['rows'])==294
    comparison=[]
    for a,b in zip(old_mode4['rows'],mode4['rows']):
        assert (a['level'],a['language'],a['action'])==(b['level'],b['language'],b['action'])
        assert not b['overflows'] and len(a['mixed_frames'])==len(b['mixed_frames'])
        if b['mixed_frames']:comparison.append(dict(level=b['level'],language=b['language'],action=b['action'],baseline=len(a['mixed_frames']),trial=len(b['mixed_frames'])))
    live={}
    for n in ('follow','turn120','walking120'):
        d=read(FOLDER+'/'+n+'.json');assert d['rom_sha256']==digest
        for r in d['rows']:
            assert r['time_conservation_error_ticks']==r['audio']['underruns']==r['hardware_bg_new_overflows']==r['hardware_bg_new_cold_loads']==0
            del r['samples']
        live[n]=d
    originals=[]
    for entry in read('analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if entry['path'].startswith('originals/'):
            assert sha(ROOT/entry['path'])==entry['sha256'];originals.append(entry)
    assert len(originals)==22
    ui_only=read('research/experiments/g20a-hw-lowered-v2/ui-exact396.json')
    assert len(ui_only['after']['rows'])==396 and all(r['different_rgb_pixels']==0 for r in ui_only['after']['rows'])
    s=build['symbols'];iwram=s['__iwram_end']-s['__iwram_start'];ewram=build['ewram_static_bytes']
    paths=[FOLDER+'/'+n+'.json' for n in (*checks,'exact396','spatial','transitions-mode4','follow','turn120','walking120')]
    paths+=['research/experiments/g20a-hw-lowered-final/prototype-build.json','research/experiments/g20a-hw-lowered-v2/ui-exact396.json',
            'research/experiments/g20a-hw-lowered-v2/ui-transitions.json','research/experiments/g20a-hw-lowered-v5/control-transitions-mode4.json',
            'research/experiments/g20a-hw-lowered-ram/target.json','research/experiments/g20a-hw-surface-clip/target.json',
            FOLDER+'/control-ui.json',FOLDER+'/control-water.json','research/experiments/g20a-hw-post-lowered-control/prototype-build.json']
    result=dict(task='G20A',date='2026-10-27',status='experimental-not-selected',selected=False,targets_met=False,
        build_dir=FOLDER+'/'+VARIANT,rom_sha256=digest,rom_bytes=build['rom_bytes'],artifact=None,previous_selected_artifact=accepted['artifact'],
        clean_repeat_byte_exact=True,ram=dict(ewram=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
        previous_selected_rebuild_byte_exact=True,mode1_ui_water_harness_regressions='passed',
        changes=['Mode2 BG3 at actual inset400/y=-561','native HUD/weapon on normal OBJs',
            'HBlank DMA3 multiplexes four scene OBJs','atomic immediate DMA3 configuration against VBlank IRQ',
            'reuse duplicate BG2 far maps for BG3','native ceiling polygon still publishes coverage',
            'completed front matrices held separately while Mode4 reuses covered[]'],
        live=live,spatial=dict(cases=440,minima=minima(spatial['rows'])),
        quality=dict(cases=396,max_different_rgb_pixels=max(r['different_rgb_pixels'] for r in exact['after']['rows']),
            different_foreground_rgb_pixels=0,different_overlay_rgb_pixels=0,all_coverage_equal=True,ui_only_exact_rgb_cases=396),
        validation={n:dict(status=d['status'],report=FOLDER+'/'+n+'.json') for n,d in checks.items()},
        transition_callbacks=sum(r['video_frames'] for r in checks['transitions']['rows']),
        stricter_mode4_test=dict(status='failed-in-baseline-and-trial',rows=294,comparison=comparison,
            scope='No callbacks discarded; the same counts of intermediate Pause/Resume images remain in selected2775 baseline and trial. This is not a clean transition pass.'),
        originals_unchanged=originals,input_reports={p:sha(ROOT/p) for p in paths},build_manifest=repeat,
        limitations=['Large L1 ceiling-heavy stationary views improve, ordinary live routes regress; this trial does not replace the selected ROM.',
            'UI/flow/sprites/water/ARM and282 ordinary transitions passed; full combat/items/AI/stress acceptance of this backend is not claimed.',
            'Strict Pause/Resume video test exposes pre-existing intermediate images; extra corruption caused by scratch reuse was fixed, but overall test remains failed.',
            'Source-derived32x32/three-LIGHT ceiling changes texture projection/lighting; actual lowered geometry is retained.',
            'No whole-level minimum, physical GBA or original BIOS acceptance.'])
    (ROOT/'analysis/gba/g20a-lowered-ceiling-trial.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(digest,'unselected',minima(spatial['rows']),'ordinary callbacks',result['transition_callbacks'])


if __name__=='__main__':main()
