"""Publish G08 provenance and limits only for matching successful evidence."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib
import json
from pathlib import Path
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
build=read('build/gba/g08/build-report.json');rom=sha('build/gba/g08/dok-diagnostic.gba')
assert build['rom_sha256']==rom
reports={n:read(f'analysis/gba/g08-{n}.json') for n in ('gba-verification','renderer-regression',
    'host-verification','time-regression','column-verification','camera-regression','collision-evidence','startup-profile')}
for n in ('gba-verification','renderer-regression','time-regression','column-verification','camera-regression'):
    assert reports[n]['rom_sha256']==rom,('stale evidence',n)
for n in ('gba-verification','renderer-regression','column-verification','camera-regression','collision-evidence'):
    assert reports[n]['status']=='passed',n
assert reports['host-verification']['ARM_ROM']['sha256']==rom
assert reports['startup-profile']['rom']==rom
assert reports['camera-regression']['checked_cameras']==1455
for case in reports['collision-evidence']['cases']:
    m=case['manifest'];assert sha(m['sample'])==m['sha256_before']==m['sha256_after']
    assert m['script_sha256']==sha('tools/ghidra/InspectCollision.java')
    assert m['driver_sha256']==sha('tools/ghidra/analyze-collision.ps1')
assert reports['collision-evidence']['script_sha256']==sha('ports/gba/scripts/verify_collision.py')
for path,digest in reports['collision-evidence']['artifacts'].items():assert sha(path)==digest
previous=read('analysis/gba/g07-review.json')
sources=list(dict.fromkeys(list(previous['source_sha256'])+[
    'ports/gba/include/dok/collision.h','ports/gba/src/collision.c','ports/gba/tests/collision_cases.c','ports/gba/tests/test_collision.c',
    'ports/gba/tools/collision_probe.c','ports/gba/tools/collision_replay.c','tools/ghidra/InspectCollision.java',
    'tools/ghidra/analyze-collision.ps1','ports/gba/scripts/verify_collision.py','ports/gba/scripts/verify_collision_gba.py',
    'ports/gba/scripts/build-gba-collision.ps1','ports/gba/scripts/report_collision.py']))
s=build['symbols'];arm=reports['gba-verification'];camera=reports['camera-regression']
stack=max([arm['stack'],camera['stack'],reports['renderer-regression']['stack_before_reset'],
    reports['renderer-regression']['stack_after_reset']],key=lambda x:x['high_water_bytes'])
memory=dict(rom_bytes=build['rom_bytes'],rom_remaining=32*1024*1024-build['rom_bytes'],
    ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining=256*1024-build['ewram_static_bytes'],
    ewram_delta_from_g07=build['ewram_static_bytes']-previous['memory']['ewram_static_bytes'],
    iwram_code_bytes=s['__iwram_code_end']-s['__iwram_start'],
    iwram_data_bytes=s['__iwram_data_end']-s['__iwram_data_start'],main_stack=stack)
performance=dict(startup_cycles=reports['startup-profile']['total'],worst_camera=camera['worst'],
    live_motion_max_cycles=arm['motion_max_cycles'],
    live_full_frame_cycles=[arm['full_frame_min_cycles'],arm['full_frame_max_cycles']],
    test_batch_max_cycles=arm['test_batch_max_cycles'],
    target_full_game_fps=30,full_game_performance_verified=False,early_budget=400000,
    early_budget_status='deferred by user',motion_quantum_native_ticks=416)
report=dict(task='G08',status='passed',rom_sha256=rom,memory=memory,performance=performance,
    source_sha256={p:sha(p) for p in sources},
    report_sha256={f'analysis/gba/g08-{n}.json':sha(f'analysis/gba/g08-{n}.json') for n in reports},
    asset_bank_sha256=sha('build/gba/g08/render-columns.bin'),package_sha256=sha('build/gba/assets/assets.bin'),
    native_validation='Windows suite executed; Linux static suite and Thumb module compiled',
    physical_gba_tested=False,original_bios_tested=False,dos_session_tested=False,
    preserved_policy='G07 instant translation start/stop/reversal; original turn acceleration',
    safety_changes='Reject out-of-world, undefined glyph, excessive substep or invalid footprint result; doors remain closed until G09')
Path('analysis/gba/g08-review.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'G08 evidence consistent: {rom}; EWRAM {memory["ewram_static_bytes"]}; worst {performance["worst_camera"]["cycles"]} cycles')
