"""Publish G09 only when current source, original evidence and ROM agree."""
import hashlib,json,subprocess
from pathlib import Path
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
build=read('build/g09/build-report.json');rom=sha('build/g09/dok-diagnostic.gba')
assert build['rom_sha256']==rom
names=('gba-verification','renderer-regression','host-verification','time-regression','column-verification','camera-regression','original-verification','level-scenarios','original-level-inventory','startup-profile')
reports={n:read(f'analysis/g09-{n}.json') for n in names}
for n in ('gba-verification','renderer-regression','column-verification','camera-regression','original-verification','level-scenarios'):assert reports[n]['status']=='passed',n
for n in ('gba-verification','renderer-regression','time-regression','column-verification','camera-regression'):assert reports[n]['rom_sha256']==rom,n
assert reports['host-verification']['ARM_ROM']['sha256']==rom
assert reports['startup-profile']['rom']==rom
assert reports['camera-regression']['checked_cameras']==1455
assert all(c['dynamic_scene_pixels_compared']>30000 for c in reports['gba-verification']['captures'])
assert reports['original-verification']['verifier_sha256']==sha('scripts/verify_interactions.py')
for level in reports['original-level-inventory']['levels']:assert sha(level['source'])==level['source_sha256']
for p,digest in reports['original-verification']['artifacts'].items():assert sha(p)==digest,p
for lang,v in reports['original-verification']['languages'].items():
 m=v['manifest'];assert sha(m['sample'])==m['sha256_before']==m['sha256_after']
 assert sha(m['script'])==m['script_sha256']
 assert sha('scripts/analyze-interactions.ps1')==m['driver_sha256']
 assert v['vectors']==6000 and v['crop_vectors']==120
current=json.loads(subprocess.check_output(['build/native/dok-interactions-scenarios.exe','build/gba/assets/assets.bin'],text=True))
assert current==reports['level-scenarios']
previous=read('analysis/g08-review.json');s=build['symbols'];arm=reports['gba-verification'];camera=reports['camera-regression']
stack=max([arm['stack'],camera['stack'],reports['renderer-regression']['stack_before_reset']],key=lambda v:v['high_water_bytes'])
memory=dict(rom_bytes=build['rom_bytes'],rom_remaining=32*1024*1024-build['rom_bytes'],ewram_static_bytes=build['ewram_static_bytes'],ewram_delta_from_g08=build['ewram_static_bytes']-previous['memory']['ewram_static_bytes'],iwram_code_bytes=s['__iwram_code_end']-s['__iwram_start'],iwram_data_bytes=s['__iwram_data_end']-s['__iwram_data_start'],main_stack=stack)
sources=list(dict.fromkeys(list(previous['source_sha256'])+['include/dok/interactions.h','src/interactions.c','tests/interactions_cases.c','tests/test_interactions.c','tools/interactions_probe.c','tools/interactions_scenarios.c','tools/render_tool.c','scripts/InspectInteractions.java','scripts/analyze-interactions.ps1','scripts/verify_interactions.py','scripts/verify_interactions_gba.py','scripts/inventory_interactions.py','scripts/build-gba-interactions.ps1','scripts/report_interactions.py']))
report=dict(task='G09',status='passed',rom_sha256=rom,memory=memory,
 performance=dict(startup_cycles=reports['startup-profile']['total'],worst_static_camera=camera['worst'],live_frame_cycles=[arm['live_frame_min_cycles'],arm['live_frame_max_cycles']],test_batch_frame_max_cycles=arm['test_frame_max_cycles'],target_full_game_fps=30,full_game_performance_verified=False,early_budget_status='400000 cycles deferred by user',visual_steps_per_10000_ticks=30,hero_quantum=416),
 source_sha256={p:sha(p) for p in sources},report_sha256={f'analysis/g09-{n}.json':sha(f'analysis/g09-{n}.json') for n in names},package_sha256=sha('build/gba/assets/assets.bin'),bank_sha256=sha('build/g09/render-columns.bin'),
 native_validation='Windows suites executed; Linux static suite and Thumb module compiled; CMake/MSVC not executed',physical_gba_tested=False,original_bios_tested=False,dos_session_tested=False,
 scope='Door events and crop, class6 base no-op, sensor entry routing, conveyor collision, explicit finish/stat events',
 deliberate_policy='30 visual steps independent of draw; instant G07 translation retained; G18 consumes finish condition instead of writing health=-1')
Path('analysis/g09-review.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'G09 evidence consistent: {rom}; EWRAM {memory["ewram_static_bytes"]}; worst static {camera["worst"]["cycles"]} cycles')
