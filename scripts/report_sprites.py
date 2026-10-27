"""Bind G11 evidence and final ARM/host regressions to current sources and ROM."""
import hashlib,json
from pathlib import Path

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def main():
 build=read('build/g11/build-report.json');rom=Path('build/g11/dok-diagnostic.gba')
 assert sha(rom)==build['rom_sha256']
 names=('playback-host','sprites-host','original-verification','sprites-gba','renderer-regression','interactions-regression','time-regression','camera-regression')
 reports={n:read(f'analysis/g11-{n}.json') for n in names}
 for name,report in reports.items():
  assert report.get('status','passed')=='passed',(name,report.get('status'))
  if 'rom_sha256' in report:assert report['rom_sha256']==build['rom_sha256'],name
 assert reports['camera-regression']['checked_cameras']==1455
 assert reports['camera-regression']['status']=='passed' and not reports['camera-regression']['rejected']
 assert reports['time-regression']['edge_vectors_on_arm']=='passed'
 for name in ('sprites-gba','camera-regression'):
  assert reports[name]['stack']['untouched_margin_bytes']>=512
 for name in ('stack_before_reset','stack_after_reset'):
  assert reports['renderer-regression'][name]['untouched_margin_bytes']>=512
 for language in ('ru','en'):
  manifest=read(f'research/evidence/sprites/{language}/analysis-manifest.json')
  assert manifest['sha256_before']==manifest['sha256_after']==sha(manifest['sample'])
  assert manifest['script_sha256']==sha('scripts/InspectSprites.java')
  assert manifest['driver_sha256']==sha('scripts/analyze-sprites.ps1')
  assert reports['original-verification']['languages'][language]['manifest_sha256']==sha(f'research/evidence/sprites/{language}/analysis-manifest.json')
 assert reports['sprites-gba']['script_sha256']==sha('scripts/verify_sprites_gba.py')
 assert reports['sprites-gba']['automatic']['multiple_steps_per_draw_verified']
 assert reports['sprites-gba']['water_callback']['crop_then_increment_verified']
 assert reports['playback-host']['water_crop_then_increment']
 source_files=[]
 for folder in ('src','include/dok','gba','tests'):
  source_files.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 source_files.extend(Path('scripts')/name for name in ('InspectSprites.java','analyze-sprites.ps1','build-gba.ps1','build-native.ps1','build-gba-sprites.ps1','verify_sprites_original.py','verify_sprites_host.py','verify_sprites_gba.py','report_sprites.py','verify_renderer_gba.py','verify_renderer_early.py','verify_surfaces.py','verify_interactions_gba.py','verify_time_gba.py'))
 source_files.extend((Path('tools/visual_probe.c'),Path('tools/render_tool.c'),Path('CMakeLists.txt')))
 syms=build['symbols'];iwram=syms['__iwram_end']-syms['__iwram_start']
 camera=reports['camera-regression'];renderer=reports['renderer-regression']
 report=dict(task='G11',status='passed',next='G12',rom_sha256=build['rom_sha256'],rom_bytes=rom.stat().st_size,
  package_sha256=sha('build/gba/assets/assets.bin'),ewram_static_bytes=build['ewram_static_bytes'],
  ewram_remaining_bytes=262144-build['ewram_static_bytes'],iwram_static_bytes=iwram,
  iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
  sources={p.as_posix():sha(p) for p in sorted(set(source_files))},
  reports={f'analysis/g11-{n}.json':sha(f'analysis/g11-{n}.json') for n in names},
  artifacts={p:sha(p) for p in ('build/g11/dok-diagnostic.elf','build/g11/dok-diagnostic.gba','build/g11/dok-diagnostic.map','build/native/dok-visual-probe.exe','build/native/dok-render.exe')},
  checks=dict(real_rom_playback=reports['playback-host'],native_billboard_vectors=sum(x['billboard_vectors'] for x in reports['original-verification']['languages'].values()),
    static_scene_sprite_faces=len(reports['sprites-host']['static_scene_sprite_faces']),arm_playback_sequences=len(reports['sprites-gba']['playback']),
    arm_captures=len(reports['sprites-gba']['captures']),full_camera_regression=1455,doors_sensors_conveyor_finish_regression=True,
    rational_clock_regression=True,poisoned_reset_and_language_switch_regression=True),
  performance=dict(startup=renderer['timing'],camera_worst=camera.get('worst'),target30fps_verified=False),
  limits=['GBA physical hardware untested','whole DOS session and complete raster equivalence untested','Linux execution and CMake/MSVC execution untested','positive intervals quantized to333/333/334-tick visual timeline','gameplay attack/death/projectile creation and consequences remain G13/G15','G20 performance optimization remains required'])
 Path('analysis/g11-review.json').write_text(json.dumps(report,indent=2)+'\n')
 print('G11 evidence, current-source hashes, final ROM and all regressions passed')

if __name__=='__main__':main()
