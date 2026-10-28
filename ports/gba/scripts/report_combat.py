"""Bind G13 acceptance to the executable, source, original evidence and ARM runs."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from research_paths import script_path

import hashlib,json
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
 build=read('build/gba/g13/build-report.json');rom='build/gba/g13/dok-diagnostic.gba';assert sha(rom)==build['rom_sha256']
 names=('host-verification','original-verification','combat-gba','renderer-regression','interactions-regression','time-regression')
 reports={n:read(f'analysis/gba/g13-{n}.json') for n in names}
 for name,r in reports.items():
  assert r.get('status','passed')=='passed',name
  if 'rom_sha256' in r:assert r['rom_sha256']==build['rom_sha256'],name
 for lang in ('ru','en'):
  path=f'research/evidence/combat/{lang}/analysis-manifest.json';m=read(path)
  assert m['sha256_before']==m['sha256_after']==sha(m['sample'])
  assert m['script_sha256']==sha('tools/ghidra/InspectCombat.java') and m['driver_sha256']==sha('tools/ghidra/analyze-combat.ps1')
  assert reports['original-verification']['languages'][lang]['manifest_sha256']==sha(path)
 assert reports['time-regression']['edge_vectors_on_arm']=='passed'
 assert reports['combat-gba']['script_sha256']==sha('ports/gba/scripts/verify_combat_gba.py')
 combat=reports['combat-gba']
 assert len(combat['flights'])==4 and {(x['language'],x['weapon']) for x in combat['flights']}=={(0,1),(0,2),(1,1),(1,2)}
 assert all(x['empty_rejected'] and x['low_charge_shot'] for x in combat['flights'])
 pool=combat['pool'];assert 0<pool['capacity']<=256 and pool['failed_allocation_preserved_charge'] and pool['failed_allocation_changed_readiness'] and pool['first_free_reused']==0 and pool['all_events_unlinked_after_clear']
 assert len(combat['independent_instances'])==2
 for x in combat['independent_instances']:
  assert x['simultaneous_different_frames_verified'] and x['independent_texture_slots']==[256,257]
  assert len(x['captures'])==2 and all(min(capture['opaque_source_samples'])>0 for capture in x['captures'])
  assert x['captures'][0]['framebuffer_sha256']!=x['captures'][1]['framebuffer_sha256']
 collisions={x['kind']:x for x in combat['collisions']};assert len(collisions)==5
 for kind in ('wall','door-closed','monster'):assert collisions[kind]['cleaned_up'] and collisions[kind]['effect_starts_at_frame']==1
 assert collisions['door-open']['passed_through'] and collisions['monster']['damage_on_contact']==14
 assert all(collisions['damage-events'][k] for k in ('sleeping_contact_wakes_without_damage','dead_health_clamped','ai_state_preserved','same_class_immune'))
 assert combat['dynamic_occupancy']['production_actor_collision_verified'] and combat['dynamic_occupancy']['clear_released_occupancy']
 first=combat['first_pickup_show'];assert first['ordinary_pickup_registered'] and first['fire_rejected_during_show'] and first['show_states']==[[1,2],[1,1],[5,0],[7,0]]
 assert combat['weapon_transition']['unavailable_weapon_rejected'] and combat['weapon_transition']['states']==[[0,1,1],[0,2,1],[4,3,1],[1,2,2],[1,1,2],[5,0,2],[7,0,2]]
 assert len(combat['shot_series'])==2 and {x['weapon'] for x in combat['shot_series']}=={1,2}
 assert all(x['shots']==10 and x['active_after_each_shot']==0 and x['all_events_and_occupancy_released'] for x in combat['shot_series'])
 live=combat['live_input'];assert live['gba_key_mask']==1 and live['native_fire_command']==512 and live['charge_after_shot']==86 and live['real_l_r_weapon_cycle'] and live['laser_shots']>=1 and live['laser_charge']==100-4*live['laser_shots']
 assert combat['stack']['untouched_margin_bytes']>=512
 assert reports['host-verification']['script_sha256']==sha('ports/gba/scripts/verify_combat_host.py')
 for p,h in reports['host-verification']['sources'].items():assert sha(p)==h,p
 for p,h in reports['host-verification']['artifacts'].items():assert sha(p)==h,p
 for tag in ('stack_before_reset','stack_after_reset'):assert reports['renderer-regression'][tag]['untouched_margin_bytes']>=512
 sources=[]
 for folder in ('ports/gba/src','ports/gba/include/dok','ports/gba/platform','ports/gba/tests'):
  sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 sources.extend(script_path(n) for n in ('InspectCombat.java','analyze-combat.ps1','build-gba.ps1','build-native.ps1','build-gba-combat.ps1','verify_combat_original.py','verify_combat_host.py','verify_combat_gba.py','report_combat.py','verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py'))
 sources.append(Path('CMakeLists.txt'));s=build['symbols'];iwram=s['__iwram_end']-s['__iwram_start']
 report=dict(task='G13',status='passed',next='G14',rom_sha256=build['rom_sha256'],rom_bytes=Path(rom).stat().st_size,
  package_sha256=sha('build/gba/assets/assets.bin'),ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining_bytes=262144-build['ewram_static_bytes'],
  iwram_static_bytes=iwram,iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
  sources={p.as_posix():sha(p) for p in sorted(set(sources))},reports={f'analysis/gba/g13-{n}.json':sha(f'analysis/gba/g13-{n}.json') for n in names},
  artifacts={p:sha(p) for p in ('build/gba/g13/dok-diagnostic.elf','build/gba/g13/dok-diagnostic.gba','build/gba/g13/dok-diagnostic.map','build/gba/host/dok-combat-tests.exe')},
  checks=dict(original_readiness=20,original_slots=72,original_spawn_copy=2,original_damage_decay=2012,original_monster_damage=336,
   host_combat=True,arm_combat=reports['combat-gba'],renderer_guards_reset_language=True,door_sensor_conveyor_finish=True,rational_clock=True),
  performance=dict(renderer_timing=reports['renderer-regression']['timing'],target30fps_verified=False),
  limits=['Physical GBA/original BIOS untested','Full DOS gameplay session untested; isolated original x86 instructions executed',
   'Linux/CMake/MSVC execution untested; Linux/Thumb compilation checked','Full1455 camera sweep not repeated; targeted static and new instance-image tests used',
   'AI consumers of WAKE/DAMAGE/DEATH/RECLAIM are G14/G15; sound/HUD G16/G17; hero death G18','Weapon overlay uses original images with GBA placement; not a DOS pixel-equivalence claim','G20 performance optimization remains required'])
 Path('analysis/gba/g13-review.json').write_text(json.dumps(report,indent=2)+'\n');print('G13 final ROM/source/original/host/ARM acceptance bound successfully')
if __name__=='__main__':main()
