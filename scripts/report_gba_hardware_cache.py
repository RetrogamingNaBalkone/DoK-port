"""Verify and collect the measured geometry-cache/entity raster prototype."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
FOLDER='research/experiments/g20a-hw-entity-rect-v2'
VARIANT='hw_v128_entity_rect'
BASE='7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8'

def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def minima(rows):return {str(n):min(r['presented_fps'] for r in rows if r['level']==n) for n in (1,2,3)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy-rom',action='store_true');args=parser.parse_args()
    build=read(FOLDER+'/'+VARIANT+'/build-report.json');digest=build['rom_sha256']
    rom=ROOT/FOLDER/VARIANT/'dok-diagnostic.gba';assert sha(rom)==digest
    repeat=read('research/experiments/g20a-hw-entity-final/prototype-build.json')
    assert repeat['variants'][VARIANT]['rom_sha256']==digest
    assert repeat['variants']['source_control']['rom_sha256']==BASE
    names=('arm','sprites','water','ui','flow','ai','ai-combat','items','combat','cache','transitions','interactions','stress')
    checks={n:read(FOLDER+'/'+n+'.json') for n in names}
    for name,value in checks.items():assert value['status']=='passed' and value['rom_sha256']==digest,name
    assert checks['arm']['rectangle_raster_vectors']==512
    assert checks['arm']['geometry_copy_vectors']==1024 and checks['arm']['geometry_equal_vectors']==2048
    assert checks['cache']['cases']==48
    assert len(checks['items']['stats'])==109 and len(checks['items']['pickups'])==58
    assert len(checks['ai-combat']['groups'])==28 and checks['water']['phase_cases']==144
    transitions=checks['transitions']['rows'];assert len(transitions)==282
    assert all(not r['mixed_frames'] and not r['overflows'] for r in transitions)
    exact=read(FOLDER+'/exact396.json');assert len(exact['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 for r in exact['after']['rows'])
    clock=read(FOLDER+'/time.json');assert clock['rom_sha256']==digest and clock['edge_vectors_on_arm']=='passed'
    live={}
    for mode in ('follow','turn120','walking120','standing120'):
        result=read(FOLDER+'/'+mode+'.json');assert result['rom_sha256']==digest and len(result['rows'])==6
        for row in result['rows']:
            assert row['time_conservation_error_ticks']==row['audio']['underruns']==row['hardware_bg_new_overflows']==row['hardware_bg_new_cold_loads']==0
            assert row['camera_follows_runtime'] and len(row['samples'])==(24 if mode=='follow' else 120)
            assert all(s['flow_state']==2 and s['ui_state']==0 and s['vblanks']>=2 for s in row['samples'])
            row['minimum_upload_budget_margin_cycles']=min(s['hardware_present'][0]-s['hardware_obj'][2] for s in row['samples'])
            assert row['minimum_upload_budget_margin_cycles']>0
            del row['samples']
        live[mode]=result
    sweep=read(FOLDER+'/sweep.json');assert sweep['rom_sha256']==digest and len(sweep['rows'])==72
    spatial=read(FOLDER+'/spatial.json');reference=read(FOLDER+'/reference-spatial.json')
    assert spatial['rom_sha256']==digest and len(spatial['rows'])==len(reference['rows'])==440
    assert not spatial['capture_hold_during_samples'] and spatial['simulation_frozen'] and not spatial['instrumented']
    for before,after in zip(reference['rows'],spatial['rows']):
        assert tuple(before[k] for k in ('level','cell','heading'))==tuple(after[k] for k in ('level','cell','heading'))
        assert before['ppu_sha256']==after['ppu_sha256']
    assert min(r['presented_fps'] for r in spatial['rows'])<15
    profile=read(FOLDER+'/profile.json');assert profile['rom_sha256']==digest and profile['instrumented']
    originals=[]
    for entry in read('analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if entry['path'].startswith('originals/'):
            assert sha(ROOT/entry['path'])==entry['sha256'];originals.append(entry)
    assert len(originals)==22
    preserved={}
    for name in ('fast','rows','culling','planes'):
        previous=read('analysis/g20a-hardware-'+name+'.json')
        assert sha(ROOT/previous['artifact'])==previous['rom_sha256']
        preserved[previous['artifact']]=previous['rom_sha256']
    assert sha(ROOT/'artifacts/gba/DoK-port_pre_alpha.gba')==BASE
    artifact=ROOT/'artifacts/gba/DoK-port_hardware_cached_prototype.gba'
    assert not artifact.exists() or sha(artifact)==digest
    if args.copy_rom:shutil.copyfile(rom,artifact)
    paths=[FOLDER+'/'+n+'.json' for n in (*names,'time','exact396','follow','turn120','walking120','standing120','sweep','spatial','reference-spatial','profile')]
    paths+=['research/experiments/g20a-hw-entity-final/prototype-build.json']
    s=build['symbols'];iwram=s['__iwram_end']-s['__iwram_start'];ewram=build['ewram_static_bytes']
    result=dict(task='G20A',date='2026-10-27',status='cache-and-entity-raster-accepted-prototype',targets_met=False,
        artifact=artifact.relative_to(ROOT).as_posix(),artifact_copied=args.copy_rom,rom_sha256=digest,rom_bytes=rom.stat().st_size,
        build_dir=FOLDER+'/'+VARIANT,clean_repeat_byte_exact=True,source_control_sha256=BASE,
        selected_changes=['HW_NONZERO_COLUMN','HW_COMPACT_ZERO','HW_SURFACE_SPLIT','HW_LINEAR_ROM','HW_DIRECT_GAPS',
            'HW_STATIC_GEOMETRY','HW_EDGE_RAM_CALL','HW_INTERVAL_EWRAM','HW_ENTITY_PRELIT','HW_MASKED_BATCH','HW_RECT_RASTER'],
        ram=dict(ewram=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
        live=live,heading_sweep=dict(cases=72,minima=minima(sweep['rows']),simulation_frozen=True),
        spatial_sweep=dict(cases=440,reference_rom_sha256=reference['rom_sha256'],minima=minima(spatial['rows']),
            reference_minima=minima(reference['rows']),ppu_equal_cases=440,simulation_frozen=True,
            worst=sorted(spatial['rows'],key=lambda r:r['presented_fps'])[:16]),
        instrumented_profile=profile,
        validation=dict(checks={n:dict(status=v['status'],report=FOLDER+'/'+n+'.json',sha256=sha(ROOT/FOLDER/(n+'.json'))) for n,v in checks.items()},
            rectangle_raster_vectors=512,geometry_copy_vectors=1024,geometry_equal_vectors=2048,
            exact_rgb_frames=396,exact_spatial_ppu_frames=440,cache_mutation_cases=48,
            actual_transition_video_frames=sum(r['video_frames'] for r in transitions),transition_cases=282,mixed_frames=0,
            stack=checks['stress']['stack'],irq_stack=checks['stress']['irq_stack'],clock_edge_vectors='passed'),
        input_reports={p:sha(ROOT/p) for p in paths},originals_unchanged=originals,preserved_artifacts=preserved,
        build_manifest=repeat,
        limitations=['Goal remains open: reachable stationary spatial views still have9.9546FPS L1/L2 and14.9319FPS L3, without live AI cost.',
            'No whole-level minimum or physical GBA/original BIOS acceptance. Current visual baseline is the earlier128-row hardware renderer.',
            'Geometry reuse benefits camera-stable frames only; doors, moving actors and instance geometry are rebuilt normally.',
            'All-awake stress passed guards/audio/logic but remains extremely slow with simulation debt; it is not a frame-budget pass.',
            'Instrumented per-primitive profile includes timer overhead; it is distinct from uninstrumented FPS rows.',
            'Transition harness now settles a previous action before recording the next; no video callbacks after a mutation are discarded.'])
    (ROOT/'analysis/g20a-hardware-cache.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(digest,'ROM',rom.stat().st_size,'EWRAM',ewram,'IWRAM',iwram,'spatial minimum',minima(spatial['rows']))


if __name__=='__main__':main()
