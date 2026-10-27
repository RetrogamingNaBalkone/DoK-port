"""Collect actual QA and timing for exact hardware optimizations after128 rows."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
FOLDER='build/g20a-hw-projection'
VARIANT='hw_v128_projection'
BASE='7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8'
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--copy-rom',action='store_true');args=parser.parse_args()
    build=read(FOLDER+'/'+VARIANT+'/build-report.json');digest=build['rom_sha256'];rom=ROOT/FOLDER/VARIANT/'dok-diagnostic.gba'
    assert sha(rom)==digest==read('build/g20a-hw-fast-final/'+VARIANT+'/build-report.json')['rom_sha256']
    assert read('build/g20a-hw-fast-final/source_control/build-report.json')['rom_sha256']==BASE
    names=('arm','sprites','water','items','combat','ui','flow','ai','ai-combat','interactions','transitions','stress')
    checks={n:read(FOLDER+'/'+n+'.json') for n in names}
    for name,value in checks.items():assert value['status']=='passed' and value['rom_sha256']==digest,name
    assert checks['arm']['scalar_projection_vectors']==32811
    assert len(checks['items']['stats'])==109 and len(checks['items']['pickups'])==58
    assert len(checks['ai-combat']['groups'])==28
    assert checks['water']['phase_cases']==144 and checks['ui']['hardware_ppu_frames']==22
    transitions=checks['transitions']['rows'];assert len(transitions)==282
    assert all(not r['mixed_frames'] and not r['overflows'] for r in transitions)
    exact=read(FOLDER+'/exact396.json');assert len(exact['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 for r in exact['after']['rows'])
    clock=read(FOLDER+'/time.json');assert clock['rom_sha256']==digest and clock['edge_vectors_on_arm']=='passed'
    live={}
    for mode in ('follow','turn120','walking120'):
        result=read(FOLDER+'/'+mode+'.json');assert result['rom_sha256']==digest
        assert len(result['rows'])==6
        for row in result['rows']:
            assert row['time_conservation_error_ticks']==row['hardware_bg_new_overflows']==row['hardware_bg_new_cold_loads']==row['audio']['underruns']==0
            assert row['camera_follows_runtime'] and len(row['samples'])==(24 if mode=='follow' else 120)
            assert all(s['flow_state']==2 and s['ui_state']==0 and s['vblanks']>=2 for s in row['samples'])
            row['minimum_upload_budget_margin_cycles']=min(s['hardware_present'][0]-s['hardware_obj'][2] for s in row['samples'])
            assert row['minimum_upload_budget_margin_cycles']>0
            del row['samples']
        live[mode]=result
    sweep=read(FOLDER+'/sweep.json');assert sweep['rom_sha256']==digest and len(sweep['rows'])==72
    assert sweep['simulation_frozen'] and not sweep['capture_hold_during_samples']
    for row in sweep['rows']:del row['samples']
    assert min(r['presented_fps'] for r in sweep['rows'])<15 # Explicit counterevidence to universal15+.
    baseline={n:read('build/g20a-hw-hot-cover/mode4-'+n+'.json') for n in ('items','combat')}
    for value in baseline.values():assert value['status']=='passed' and value['rom_sha256']==BASE
    originals=[]
    for entry in read('analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if entry['path'].startswith('originals/'):
            assert sha(ROOT/entry['path'])==entry['sha256'];originals.append(entry)
    assert len(originals)==22
    preserved={}
    for report in ('rows','culling','planes'):
        previous=read('analysis/g20a-hardware-'+report+'.json');path=ROOT/previous['artifact']
        assert sha(path)==previous['rom_sha256'];preserved[previous['artifact']]=previous['rom_sha256']
    assert sha(ROOT/'artifacts/gba/DoK-port_pre_alpha.gba')==BASE
    artifact=ROOT/'artifacts/gba/DoK-port_hardware_fast_prototype.gba'
    assert not artifact.exists() or sha(artifact)==digest
    if args.copy_rom:shutil.copyfile(rom,artifact)
    paths=[FOLDER+'/'+n+'.json' for n in (*names,'time','exact396','follow','turn120','walking120','sweep')]
    paths+=['build/g20a-hw-cover4-final/ppu.json','build/g20a-hw-cover4-final/arm.json',
        'build/g20a-hw-face-light/ppu.json','build/g20a-hw-u-track-v3/exact.json',
        'build/g20a-hw-hot-cover/mode4-items.json','build/g20a-hw-hot-cover/mode4-combat.json']
    symbol=build['symbols'];ewram=symbol['__bss_end']-symbol['__data_start'];iwram=symbol['__iwram_end']-symbol['__iwram_start']
    result=dict(task='G20A',date='2026-10-27',status='exact-optimizations-after128-rows',targets_met=False,
        artifact=artifact.relative_to(ROOT).as_posix(),artifact_copied=args.copy_rom,rom_sha256=digest,rom_bytes=rom.stat().st_size,
        build_dir=FOLDER+'/'+VARIANT,exact_rgb_frames=396,
        selected_changes=['HW_COVER4','coverage_edge in IWRAM','HW_DIRECT_RUN','HW_PROJECT_RECIP'],
        ram=dict(ewram=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
        live=live,heading_sweep=sweep,validation={**checks,'clock':clock,
            'actual_transition_video_frames':sum(r['video_frames'] for r in transitions),'clean_repeat_byte_exact':True,
            'effect_opaque_ppu_samples':sum(sum(c['opaque_source_samples']) for v in checks['combat']['independent_instances'] for c in v['captures'])},
        baseline_harness_regressions=baseline,originals_unchanged=originals,preserved_artifacts=preserved,
        build_manifest=read('build/g20a-hw-fast-final/prototype-build.json'),input_reports={p:sha(ROOT/p) for p in paths},
        limitations=['15+ met in selected runtime-camera averages;72 fixed-heading renderer fixtures still include14.931875FPS. Overall goal remains open.',
            'No whole-level minimum FPS, physical GBA or original BIOS verification;24.1/30 not achieved across all worlds.',
            'Artificial all-awake stress remains extremely slow with growing motion debt; passed verifies guards/audio/logic, not frame budget.',
            'Quality remains identical to the128-row prototype, with128 CPU rows stretched to144; original floor affine pixel oracle intentionally omitted.',
            'Direct runs require NO_SCALES; projection acceleration is selected only for denominator1..16384 with exact signed fallback.',
            'Face-light and quotient-tracking trials were not selected; the latter has18 exact image comparisons, not an independent arithmetic acceptance.'])
    (ROOT/'analysis/g20a-hardware-fast.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(digest,'ROM',rom.stat().st_size,'EWRAM',ewram,'IWRAM',iwram)


if __name__=='__main__':main()
