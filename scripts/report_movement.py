"""Publish G07 provenance/memory only from matching, successful ROM evidence."""
import hashlib
import json
from pathlib import Path

sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
build=read('build/g07/build-report.json');rom=sha('build/g07/dok-diagnostic.gba')
assert build['rom_sha256']==rom
reports={name:read(f'analysis/g07-{name}.json') for name in
         ('gba-verification','renderer-regression','host-verification','time-regression',
          'column-verification','camera-regression','movement-evidence','startup-profile')}
for name in ('gba-verification','renderer-regression','time-regression','column-verification','camera-regression'):
    assert reports[name]['rom_sha256']==rom,('stale ROM evidence',name)
for name in ('gba-verification','renderer-regression','column-verification','camera-regression','movement-evidence'):
    assert reports[name]['status']=='passed',name
assert reports['host-verification']['ARM_ROM']['sha256']==rom
assert reports['startup-profile']['rom']==rom
assert reports['camera-regression']['checked_cameras']==1455
for language in reports['movement-evidence']['languages']:
    assert sha(language['sample'])==language['sha256_before']==language['sha256_after']
    assert language['ghidra_manifest']['script_sha256']==sha('scripts/InspectMovement.java')
assert reports['movement-evidence']['script_sha256']==sha('scripts/verify_movement.py')
sources=['include/dok/movement.h','src/movement.c','src/renderer_trig.inc','gba/movement_probe.c',
         'gba/renderer_probe.c','tests/movement_cases.c','tests/test_movement.c','tools/movement_probe.c',
         'scripts/InspectMovement.java','scripts/analyze-movement.ps1','scripts/verify_movement.py',
         'scripts/verify_movement_gba.py','scripts/build-gba-movement.ps1','scripts/report_movement.py',
         'scripts/build-gba.ps1','scripts/build-native.ps1','CMakeLists.txt']
previous=read('analysis/g06a-provenance.json')
sources=list(dict.fromkeys(sources+list(previous['source_sha256'])))
s=build['symbols'];arm=reports['gba-verification'];camera=reports['camera-regression']
stack=max([arm['stack'],camera['stack'],reports['renderer-regression']['stack_before_reset'],
           reports['renderer-regression']['stack_after_reset']],key=lambda x:x['high_water_bytes'])
baseline=read('analysis/g06a-memory-budget.json')
memory=dict(rom_bytes=build['rom_bytes'],rom_remaining=32*1024*1024-build['rom_bytes'],
            ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining=256*1024-build['ewram_static_bytes'],
            ewram_delta_from_g06a=build['ewram_static_bytes']-baseline['ewram_static_bytes'],
            iwram_code_bytes=s['__iwram_code_end']-s['__iwram_start'],
            iwram_data_bytes=s['__iwram_data_end']-s['__iwram_data_start'],
            iwram_remaining_below_stack=s['__iwram_start']+24576-s['__iwram_end'],
            main_stack=stack,stack_bios_reserve=8192)
performance=dict(startup_cycles=reports['startup-profile']['total'],worst_camera=camera['worst'],
                 live_motion_max_cycles=max(x['held']['max_cycles'] for x in arm['live_buttons']),
                 live_full_frame_cycles=[arm['full_frame_min_cycles'],arm['full_frame_max_cycles']],
                 target_full_game_fps=30,full_game_performance_verified=False,
                 early_budget=400000,early_budget_status='deferred by user; no new graphics reduction',
                 motion_quantum_native_ticks=416,motion_nominal_hz=1193182/119/416,
                 scope='CPU timer includes time/input/motion/render/HUD/VRAM copy; presentation wait excluded')
report=dict(task='G07',status='passed',rom_sha256=rom,memory=memory,performance=performance,
            source_sha256={p:sha(p) for p in sources},
            report_sha256={f'analysis/g07-{name}.json':sha(f'analysis/g07-{name}.json') for name in reports},
            asset_bank_sha256=sha('build/g07/render-columns.bin'),
            package_sha256=sha('build/gba/assets/assets.bin'),
            compiler='Shared Zig 0.13.0 / bundled LLD18.1.6',
            native_validation='Windows suite executed; Linux static binary and ARM7TDMI Thumb module compiled',
            physical_gba_tested=False,original_bios_tested=False,dos_session_tested=False,
            correction='User requested instant translation start/stop/reversal, no release coast; turn acceleration retained')
Path('analysis/g07-review.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(f'G07 evidence consistent: {rom}; EWRAM {memory["ewram_static_bytes"]}; worst {performance["worst_camera"]["cycles"]} cycles')
