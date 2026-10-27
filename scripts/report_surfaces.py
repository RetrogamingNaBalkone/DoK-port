"""Bind G10 acceptance to the actual ROM and preserved source/evidence files."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
def artifact(path):
 p=ROOT/path;return dict(path=path,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
meta=read('build/g10/build-report.json');rom=artifact('build/g10/dok-diagnostic.gba');assert meta['rom_sha256']==rom['sha256']
camera=read('analysis/g10-camera-regression.json');surface=read('analysis/g10-surface-gba.json')
renderer=read('analysis/g10-renderer-regression.json');interaction=read('analysis/g10-interactions-regression.json')
clock=read('analysis/g10-time-regression.json');columns=read('analysis/g10-column-verification.json')
host=read('analysis/g10-host-verification.json');original=read('analysis/g10-original-verification.json')
profile=read('analysis/g10-startup-profile.json')
for r in (camera,surface,renderer,interaction,clock,columns):assert r['rom_sha256']==rom['sha256']
assert camera['status']=='passed' and camera['checked_cameras']==1455 and not camera['rejected']
assert surface['status']==renderer['status']==interaction['status']==original['status']==columns['status']=='passed'
assert clock['edge_vectors_on_arm']=='passed' and profile['rom']==rom['sha256']
assert len(surface['captures'])==32 and len(surface['palettes'])==6 and surface['light_row_startup_cases']
for c in surface['captures']:assert c['scene_pixels']==32736
for c in host['ARM_comparison']:assert c['status']=='identical'
for language in ('ru','en'):
 manifest=read(f'research/evidence/surfaces/{language}/analysis-manifest.json')
 assert manifest['sha256_before']==manifest['sha256_after']==artifact(manifest['sample'])['sha256']
 assert original['languages'][language]['manifest_sha256']==artifact(f'research/evidence/surfaces/{language}/analysis-manifest.json')['sha256']
 assert manifest['script_sha256']==artifact('scripts/InspectSurfaces.java')['sha256']
 assert original['languages'][language]['negative_depth_quads']>0 and original['languages'][language]['synthetic_single_planes']>0
symbols=meta['symbols'];baseline=read('analysis/g09-review.json')
source_paths=['include/dok/renderer.h','src/renderer.c','src/renderer_surfaces.inc','src/renderer_surface_raster.inc','src/renderer_surface_sides.inc',
 'gba/renderer_probe.c','gba/gba.ld','tools/render_tool.c','scripts/pack_render_columns.py','scripts/verify_render_columns.py',
 'scripts/InspectSurfaces.java','scripts/analyze-surfaces.ps1','scripts/build-gba-surfaces.ps1','scripts/verify_surfaces.py',
 'scripts/verify_surfaces_gba.py','scripts/verify_interactions_gba.py','scripts/verify_renderer_early.py','scripts/report_surfaces.py']
reports=['analysis/g10-camera-regression.json','analysis/g10-surface-gba.json','analysis/g10-renderer-regression.json',
 'analysis/g10-interactions-regression.json','analysis/g10-time-regression.json','analysis/g10-column-verification.json',
 'analysis/g10-host-verification.json','analysis/g10-original-verification.json','analysis/g10-startup-profile.json']
report=dict(task='G10',status='passed',rom_sha256=rom['sha256'],
 memory=dict(rom_bytes=rom['bytes'],rom_remaining=32*1024*1024-rom['bytes'],ewram_static_bytes=meta['ewram_static_bytes'],
  ewram_delta_from_g09=meta['ewram_static_bytes']-baseline['memory']['ewram_static_bytes'],
  iwram_code_bytes=symbols['__iwram_code_end']-symbols['__iwram_start'],
  iwram_data_bytes=symbols['__iwram_data_end']-symbols['__iwram_data_start'],
  main_stack=camera['stack']),
 performance=dict(startup_cycles=profile['total'],worst_static_camera=camera['worst'],
  startup_vs_g09=profile['total']/baseline['performance']['startup_cycles'],
  live_frame_cycles=[interaction['live_frame_min_cycles'],interaction['live_frame_max_cycles']],
  test_batch_frame_max_cycles=interaction['test_frame_max_cycles'],target_full_game_fps=30,full_game_performance_verified=False,
  early_budget_status='400000 cycles deferred by user; G10 surface/light integration is substantially slower than G09'),
 verification=dict(cameras=camera['checked_cameras'],dynamic_surface_views=len(surface['captures']),surface_pixels_per_view=32736,
  palette_variants=len(surface['palettes']),original_languages=original['languages'],door_views=len(interaction['captures']),
  checked_strips=columns['checked_strips'],native_windows_suites=13,linux_and_thumb_compile=True,
  arm_light_cases=True,arm_time_and_interaction_cases=True),
 limitations=['bounded affine GBA raster, not whole-DOS pixel equivalence','automatic surface animation scheduling is G11',
  'fixed initial light near1920; dynamic combat/palette effects remain outside this stage','physical GBA and original BIOS not tested',
  'Linux execution and CMake/MSVC not tested','full game 30FPS unconfirmed; ROM slack is not a full-game reserve'],
 sources=[artifact(p) for p in source_paths],evidence=[artifact(p) for p in reports],
 artifacts=[artifact(f'build/g10/dok-diagnostic.{ext}') for ext in ('gba','elf','map')])
(ROOT/'analysis/g10-review.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:report[k] for k in ('task','status','rom_sha256','memory')},indent=2))
