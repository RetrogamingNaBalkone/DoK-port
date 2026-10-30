"""Fail-closed N21P cadence, native-equivalence, input, LCD and lifetime report."""
import math
from pathlib import Path
from archive_ui_performance import ROOT,E,read,artifact
import json

STAGES=('entry_audio','input','simulation','geometry','sprites','discovery','overlay','effects',
 'submit_including_wait','wait_vblank','stream_commit','hud','map','post_map_audio','telemetry',
 'cpu_excluding_wait','map_snapshot','map_raster','map_postvisited','map_buttons','map_dma',
 'map_vram_audit','map_publish','map_total')
def distribution(values):
    values=sorted(values);assert values
    return dict(samples=len(values),mean=sum(values)/len(values),median=values[(len(values)-1)//2],
        p95=values[math.ceil(len(values)*.95)-1],p99=values[math.ceil(len(values)*.99)-1],maximum=values[-1])
def compact_metrics(result):
    return {k:v for k,v in result.items() if k not in ('cases','cold_loading')}
def require_accepted_report(report,path):
    assert report.get('passed') is True,path
    assert report.get('acceptance_eligible') is not False,('Diagnostic-only report',path)
    if 'steady_metrics' in report:
        steady=report['steady_metrics']
        assert steady.get('budget_passed') is True,('Frontend cadence budget failed',path)
        assert steady.get('intervals',0)>0,('No frontend steady intervals',path)
        assert steady.get('checked_one_second_windows',0)>0,('No frontend one-second windows',path)
        assert steady.get('missing_profiles')==[],('Incomplete frontend stage coverage',path)
def variant(target,*,require_budget):
    runtime=read(target/'emulator/result.json');profile=read(target/'joint-profile/result.json')
    manifest=read(target/'build-manifest.json')
    require_accepted_report(runtime,target/'emulator/result.json')
    require_accepted_report(profile,target/'joint-profile/result.json')
    assert runtime['fat_unchanged']
    assert len(runtime['cases'])==63 and runtime['last_observation']['total_frames']==12880
    assert runtime['rom_sha256']==artifact(target/'dok_nds_gameplay.nds')['sha256']==profile['rom_sha256']
    assert runtime['elf_sha256']==artifact(target/'dok_nds_gameplay.elf')['sha256']==profile['elf_sha256']
    if require_budget:assert runtime['history']['budget_passed'],target
    memory=runtime['memory'];assert memory['sdk_heap_available']>=196608 and memory['reserve_bytes']==655360 and memory['closure_bytes']==3670016
    ids=set(map(int,runtime['history']['cases']))
    normal=[row for row in runtime['history_records'] if row['case_id'] in ids and row['flags']&8 and not row['flags']&1]
    by_frame={row['frame_id']:row for row in profile['rows']}
    assert len(by_frame)==len(profile['rows'])
    covered=[by_frame[row['frame_id']] for row in normal if row['frame_id'] in by_frame]
    assert len(covered)==len(normal),('Missing normal stage records',target,len(covered),len(normal))
    hz=runtime['last_observation']['timer_hz'];assert hz==33513982
    stages={name:distribution([row[name]*1000/hz for row in covered]) for name in STAGES}
    profile_abis=sorted({row['abi'] for row in covered})
    if profile_abis in ([2],[3]):
        fine_names=('geometry_build','geometry_cull','geometry_merge','geometry_other' if profile_abis==[2] else 'geometry_doors')
        for name in fine_names:
            stages[name]=distribution([row[name]*1000/hz for row in covered])
        if profile_abis==[3]:
            residual=[row['geometry']-sum(row[name] for name in fine_names) for row in covered]
            assert min(residual)>=0
            stages['geometry_residual']=distribution([value*1000/hz for value in residual])
    redraws=sum(row['map_raster']>0 for row in covered)
    gpu=dict(maximum_hardware_polygons=max(row['hardware_polygons'] for row in normal),
        maximum_hardware_vertices=max(row['hardware_vertices'] for row in normal),
        maximum_texture_bytes=max(row['texture_bytes'] for row in normal),maximum_palette_bytes=max(row['palette_bytes'] for row in normal))
    stacks=[]
    for path in (target/'obj').glob('*.su'):
        for line in path.read_text().splitlines():
            fields=line.split('\t')
            if len(fields)==3:stacks.append(dict(function=fields[0],bytes=int(fields[1]),kind=fields[2]))
    result=dict(target=target.relative_to(ROOT).as_posix(),manifest=artifact(target/'build-manifest.json'),
        rom=artifact(target/'dok_nds_gameplay.nds'),elf=artifact(target/'dok_nds_gameplay.elf'),
        runtime=artifact(target/'emulator/result.json'),profile=artifact(target/'joint-profile/result.json'),
        history=compact_metrics(runtime['history']),live_routes=compact_metrics(runtime['live_routes']),
        all_awake=compact_metrics(runtime['all_awake']),audit_stress=compact_metrics(runtime['all_frame_state_stress']),
        memory=memory,normal_stage_samples=len(covered),normal_intervals=len(normal),normal_stage_coverage=len(covered)/len(normal),
        stage_ms=stages,profile_abis=profile_abis,normal_map_raster_updates=redraws,normal_map_skipped=len(covered)-redraws,
        gpu=gpu,largest_individual_stack_frames=sorted(stacks,key=lambda row:row['bytes'],reverse=True)[:8],
        stack_scope='Compiler per-function .su only, not physical call-chain/IRQ high-water; N24 remains open',
        audit=manifest['audit'])
    return result
def main():
    acceptance=read(E/'root-acceptance.json');assert acceptance['passed'] is True
    names=('host-dirty-map.json','map-word-arm-proof.json','discovery-equivalence.json',
        'discovery-native.json','discovery-clip-edges.json','discovery-validation.json','discovery-math.json',
        'geometry-clear-equivalence.json','geometry-source-supersession.json',
        'scene-visibility-equivalence.json','scene-source-supersession.json',
        'scene-merge-equivalence.json','scene-merge-source-supersession.json',
        'scene-profile-equivalence.json','scene-fine-flag-supersession.json',
        'door-lazy-equivalence.json','door-lazy-source-supersession.json',
        'geometry-inplace-equivalence.json','geometry-inplace-source-supersession.json',
        'field54-prepared-equivalence.json','field54-prepared-native.json','field54-source-supersession.json',
        'backend-strip-equivalence.json','backend-typed-equality/ARM-proof.json',
        'backend-strip-packed-equivalence.json','backend-packed/ARM-proof.json',
        'overlay-blank-equivalence.json','overlay-blank-ARM-layout.json','hotcode-placement.json',
        'hotcode-variant-layout.json','hotcode-root-verification.json','stream-clean-equivalence.json',
        'map-cull-equivalence.json','map-cull-native.json',
        'map-range-equivalence.json','map-range-native.json',
        'map-flush-source-supersession.json',
        'map-count-source-supersession.json',
        'resource-audit/root-verification.json','oracle-review/root-verification.json','supersession.json')
    if any(Path(target).name.startswith('candidate16-') for target in acceptance['targets']):names+=('final-tool-companion.json',)
    gates={name:read(E/name) for name in names};assert all(row['passed'] for row in gates.values())
    dirty=gates['host-dirty-map.json'];assert dirty['checks_per_mode']>=16948549 and len(dirty['native_followup_vectors'])==2
    assert gates['map-word-arm-proof.json']['no_libc_memcmp_in_object']
    assert all(row['first_pixel']==86 and row['followup_pixel']==12 and row['changed_pixels']==13 for row in dirty['native_followup_vectors'])
    assert gates['discovery-equivalence.json']['differential_cases']==1536
    assert gates['discovery-equivalence.json']['persistent_bytes']==0 and gates['discovery-equivalence.json']['workspace_bytes']==46532
    assert gates['discovery-native.json']['complete_source_gate'] and gates['discovery-native.json']['source_pins_before_after_equal']
    assert gates['discovery-clip-edges.json']['native_constructor_vectors']==2880
    assert gates['discovery-validation.json']['validation_cases']==45736
    assert gates['discovery-validation.json']['error_cases']==661
    assert gates['discovery-validation.json']['dead_field_vectors']==24576
    assert gates['discovery-math.json']['actual_C_sar_vectors']==6855608
    assert gates['discovery-math.json']['actual_C_rotation_vectors']==604800
    assert gates['scene-visibility-equivalence.json']['full_scenes']==13816
    assert gates['scene-visibility-equivalence.json']['source_pins_before_after_equal']
    assert gates['field54-prepared-equivalence.json']['workspace_bytes']==47556
    assert gates['field54-prepared-equivalence.json']['scene_prepared_bytes']==1024
    assert gates['field54-prepared-equivalence.json']['invalid_global_no_field_read_cases']==30
    assert gates['field54-prepared-native.json']['source_pins_before_after_equal']
    assert gates['field54-source-supersession.json']['workspace']==47556
    assert gates['backend-strip-packed-equivalence.json']['numeric']['decode_states']==512
    assert gates['backend-packed/ARM-proof.json']['static_saving_bytes']==6144
    assert gates['overlay-blank-equivalence.json']['numeric']['frames']==4096
    assert gates['overlay-blank-ARM-layout.json']['old_bytes']==gates['overlay-blank-ARM-layout.json']['new_bytes']
    assert gates['hotcode-placement.json']['itcm']['bytes']<=32768
    for relative in acceptance['reports']:
        report=read(ROOT/relative);require_accepted_report(report,relative)
    pairs=[];variants={}
    for pair in acceptance['profile_pairs']:
        before,after=ROOT/pair['before'],ROOT/pair['after']
        a,b=variant(before,require_budget=False),variant(after,require_budget=not read(after/'build-manifest.json')['audit'])
        for item in (a,b):variants[item['target']]=item
        pairs.append(dict(language=pair.get('language'),before=a['target'],after=b['target'],
            fps_before=a['history']['virtual_fps'],fps_after=b['history']['virtual_fps'],
            relative_fps_gain=b['history']['virtual_fps']/a['history']['virtual_fps']-1))
    for relative in acceptance.get('standalone_profiles',[]):
        item=variant(ROOT/relative,require_budget=False);variants[item['target']]=item
    normal=[value for value in variants.values() if not value['audit'] and value['history']['budget_passed']]
    assert len(normal)>=2
    archive=read(E/'final-archive.json');assert archive['passed']
    for group in ('sources','builds','generated','reports','artifacts'):
        for pair in archive[group]:
            assert artifact(ROOT/pair['source']['path'])==pair['source']
            assert artifact(ROOT/pair['archive']['path'])==pair['archive']
            assert pair['source']['sha256']==pair['archive']['sha256']
    result=dict(task='N21P',status='ready-emulator',passed=True,decision='continue to N22 only after this requested iteration stops',
        gates={name:artifact(E/name) for name in names},root_acceptance=artifact(E/'root-acceptance.json'),
        variants=variants,profile_pairs=pairs,archive=artifact(E/'final-archive.json'),
        joint_performance_budget_passed=True,goal_60_passed=all(row['history']['goal_60_passed'] for row in normal),
        map_policy='No raster/DMA on unchanged bitmap; dynamic dirty updates at12Hz deadline, immediate actual gesture/button changes, input every presentation; source disclosure remains exact',
        physical_hardware=False,listening_deferred_by_user=True,next_task='N22',
        scope='DS-mode emulator with accumulated fixture/input routes and actual production frontend. Full FINISH/death gameflow remains N22. Cadence derives from complete new-presentation history; CPU stage coverage is published separately.',
        limits=['Physical DS/DSLite/flashcart, IRQ/LCD/touch latency and stack high-water remain N24',
            'Subjective RU/EN sound and music listening deferred to N24 by explicit user decision',
            'Expensive audit and artificial all-awake are separate from normal production cadence',
            'No game saves; only language settings may be written',
            'Nested CPU stages are not additive: discovery belongs to sprites and detailed map stages belong to map'])
    (ROOT/'analysis/nds-n21p-performance.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,pairs=pairs)))
if __name__=='__main__':main()
