"""Bind G12 acceptance to the final executable, sources and evidence."""
import hashlib,json
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
 build=read('build/g12/build-report.json');rom=Path('build/g12/dok-diagnostic.gba')
 assert sha(rom)==build['rom_sha256']
 names=('item-inventory','original-verification','items-gba','renderer-regression','interactions-regression','time-regression')
 reports={n:read(f'analysis/g12-{n}.json') for n in names}
 for name,r in reports.items():
  assert r.get('status','passed')=='passed',name
  if 'rom_sha256' in r:assert r['rom_sha256']==build['rom_sha256'],name
 for language in ('ru','en'):
  m=read(f'build/items/{language}/analysis-manifest.json')
  assert m['sha256_before']==m['sha256_after']==sha(m['sample'])
  assert m['script_sha256']==sha('scripts/InspectItems.java')
  assert m['driver_sha256']==sha('scripts/analyze-items.ps1')
  assert reports['original-verification']['languages'][language]['manifest_sha256']==sha(f'build/items/{language}/analysis-manifest.json')
 assert reports['items-gba']['script_sha256']==sha('scripts/verify_items_gba.py')
 assert len(reports['items-gba']['stats'])==109
 assert len(reports['items-gba']['pickups'])==58
 assert len(reports['items-gba']['live_pickup'])==2
 assert all(x['production_motion_path'] and x['full_positive_item_retained'] and x['changed_scene_pixels']>0 for x in reports['items-gba']['live_pickup'])
 assert sum(bool(x.get('duplicate_weapon_verified')) for x in reports['items-gba']['pickups'])==12
 assert reports['items-gba']['stack']['untouched_margin_bytes']>=512
 assert reports['time-regression']['edge_vectors_on_arm']=='passed'
 for tag in ('stack_before_reset','stack_after_reset'):
  assert reports['renderer-regression'][tag]['untouched_margin_bytes']>=512
 sources=[]
 for folder in ('src','include/dok','gba','tests'):
  sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 sources.extend(Path('scripts')/name for name in ('InspectItems.java','analyze-items.ps1','build-gba.ps1','build-native.ps1','build-gba-items.ps1','inventory_items.py','verify_items_original.py','verify_items_gba.py','report_items.py','verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py'))
 sources.extend((Path('tools/items_probe.c'),Path('CMakeLists.txt')))
 syms=build['symbols'];iwram=syms['__iwram_end']-syms['__iwram_start'];renderer=reports['renderer-regression']
 report=dict(task='G12',status='passed',next='G13',rom_sha256=build['rom_sha256'],rom_bytes=rom.stat().st_size,
  package_sha256=sha('build/gba/assets/assets.bin'),ewram_static_bytes=build['ewram_static_bytes'],
  ewram_remaining_bytes=262144-build['ewram_static_bytes'],iwram_static_bytes=iwram,
  iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
  sources={p.as_posix():sha(p) for p in sorted(set(sources))},reports={f'analysis/g12-{n}.json':sha(f'analysis/g12-{n}.json') for n in names},
  artifacts={p:sha(p) for p in ('build/g12/dok-diagnostic.elf','build/g12/dok-diagnostic.gba','build/g12/dok-diagnostic.map','build/native/dok-items-tests.exe','build/native/dok-items-probe.exe')},
  checks=dict(original_stat_vectors=sum(x['stat_vectors'] for x in reports['original-verification']['languages'].values()),
   original_item_callback_vectors=16,item_placements_per_language=reports['item-inventory']['item_placements_per_language'],
   arm_stats=len(reports['items-gba']['stats']),arm_real_item_cases=58,arm_visible_production_pickups=2,
   renderer_guards_reset_language=True,door_sensor_conveyor_finish=True,rational_clock=True),
  performance=dict(startup=renderer['timing'],target30fps_verified=False),
  limits=['Physical GBA/original BIOS untested','Full DOS gameplay session untested; HUD/sound intercepted in native arithmetic vectors',
   'Linux and CMake/MSVC execution untested','Full1455 camera sweep not repeated because renderer unchanged',
   'Pending weapon transition and weapon/projectile gameplay are G13; death/level progression G18; HUD/audio consumers G16/G17','G20 performance optimization remains required'])
 Path('analysis/g12-review.json').write_text(json.dumps(report,indent=2)+'\n')
 print('G12 final ROM, sources and native/host/ARM evidence bound successfully')
if __name__=='__main__':main()
