"""Bind G15 original/host/ARM evidence to the final ROM and sources."""
import hashlib,json
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
 build=read('build/g15/build-report.json');rom='build/g15/dok-diagnostic.gba';assert sha(rom)==build['rom_sha256']
 names=('host-verification','original-verification','ai-combat-gba','renderer-regression','interactions-regression','time-regression');reports={n:read(f'analysis/g15-{n}.json') for n in names}
 for name,r in reports.items():
  assert r.get('status','passed')=='passed',name
  if 'rom_sha256' in r:assert r['rom_sha256']==build['rom_sha256'],name
 for p,h in reports['host-verification']['sources'].items():assert sha(p)==h,p
 for p,h in reports['host-verification']['artifacts'].items():assert sha(p)==h,p
 assert reports['original-verification']['script_sha256']==sha('scripts/verify_ai_combat_original.py')
 for lang in ('ru','en'):
  path=f'build/ai/{lang}/analysis-manifest.json';m=read(path);assert m['sha256_before']==m['sha256_after']==sha(m['sample']);assert m['script_sha256']==sha('scripts/InspectAI.java');assert m['driver_sha256']==sha('scripts/analyze-ai.ps1')
  o=reports['original-verification']['languages'][lang];assert o['manifest_sha256']==sha(path);assert o['burst_vectors']==648 and len(o['attack_vectors'])==6 and len(o['player_melee_vectors'])==12 and len(o['corpse_vectors'])==4
 arm=reports['ai-combat-gba'];assert arm['script_sha256']==sha('scripts/verify_ai_combat_gba.py');assert len(arm['groups'])==28
 assert {(x['language'],x['group']) for x in arm['groups']}=={(lang,f'E{g:02d}') for lang in (0,1) for g in range(1,15)}
 for row in arm['groups']:
  assert row['melee']['damage']==5 and row['melee']['hit_only_after_animation'];assert row['hurt']['sleeping_first_hit_only_wakes'] and row['hurt']['hurt_state_three'];assert row['death']['completed_once'] and row['death']['passable_persistent_corpse'];assert row['ranged']['threshold_strict']
  ranged=row['ranged']
  if not ranged['limit']:assert ranged['zero_burst_disabled']
  else:assert ranged['series']==list(range(1,ranged['limit']+1)) and ranged['reset_after_limit'] and ranged['projectile_deadline_from_current_ai_time']
 assert arm['extra']['ranged']['solid_wall_blocks_sight'] and arm['extra']['ranged']['front_monster_suppresses_shot'];assert reports['host-verification']['actual_projectile_front_class8'];assert arm['extra']['ranged']['published_projectile_front_suppresses_shot'];assert arm['extra']['ranged']['failed_allocation_still_counts_attempt'];assert arm['extra']['occupied_corpse']['destination_plane']==0
 assert arm['stack']['untouched_margin_bytes']>=512
 assert reports['time-regression']['edge_vectors_on_arm']=='passed'
 for tag in ('stack_before_reset','stack_after_reset'):assert reports['renderer-regression'][tag]['untouched_margin_bytes']>=512
 sources=[]
 for folder in ('src','include/dok','gba','tests'):sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 sources.extend(Path('scripts')/n for n in ('InspectAI.java','analyze-ai.ps1','build-gba.ps1','build-native.ps1','build-gba-ai-combat.ps1','verify_ai_combat_original.py','verify_ai_combat_host.py','verify_ai_combat_gba.py','report_ai_combat.py','verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py'));sources.append(Path('CMakeLists.txt'))
 s=build['symbols'];iwram=s['__iwram_end']-s['__iwram_start'];assert iwram<=24576
 report=dict(task='G15',status='passed',next='G16',rom_sha256=build['rom_sha256'],rom_bytes=Path(rom).stat().st_size,ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining_bytes=262144-build['ewram_static_bytes'],iwram_static_bytes=iwram,iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,sources={p.as_posix():sha(p) for p in sorted(set(sources))},reports={f'analysis/g15-{n}.json':sha(f'analysis/g15-{n}.json') for n in names},artifacts={p:sha(p) for p in ('build/g15/dok-diagnostic.elf','build/g15/dok-diagnostic.gba','build/g15/dok-diagnostic.map')},checks=dict(original_burst=1296,original_melee_hurt=12,original_player_damage=24,original_corpse=8,host_original_parameter_groups=28,arm=arm,renderer_guards_reset_language=True,door_sensor_conveyor_finish=True,rational_clock=True),performance=dict(renderer_timing=reports['renderer-regression']['timing'],target30fps_verified=False),limits=['Physical GBA/original BIOS untested','Full DOS gameplay untested; isolated original branch instructions executed with explicit external boundaries','Exact global DOS visual/AI/projectile owner interleaving not claimed','Linux/CMake/MSVC execution untested; Linux static and Thumb compilation passed','Full1455camera sweep not repeated; targeted static/AI corpse scenarios used','Sound G16 and text UI G17 consumers deferred; gameplay cleanup implemented','Complete game flow G18; 30FPS G20'])
 Path('analysis/g15-review.json').write_text(json.dumps(report,indent=2)+'\n');print('G15 final ROM/source/original/host/ARM acceptance bound successfully')
if __name__=='__main__':main()
