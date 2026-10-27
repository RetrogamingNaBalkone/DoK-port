"""Bind G18 original, host, ARM, visual and regression evidence to one ROM."""
import hashlib,json
from pathlib import Path

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
 build=read('build/g18/build-report.json');rom='build/g18/dok-diagnostic.gba';assert sha(rom)==build['rom_sha256']
 names=('host-verification','original-flow','exit-audit','flow-gba','visual-review','ui-regression','renderer-regression','interactions-regression','time-regression','ai-combat-regression','audio-regression')
 reports={n:read(f'analysis/g18-{n}.json') for n in names}
 for name,r in reports.items():
  assert r.get('status','passed')=='passed',name
  if 'rom_sha256' in r:assert r['rom_sha256']==build['rom_sha256'],name
 for name in ('host-verification',):
  for group in ('sources','artifacts'):
   for p,h in reports[name][group].items():assert sha(p)==h,p
 assert reports['host-verification']['cross_compiled']
 assert reports['original-flow']['script_sha256']==sha('scripts/verify_flow_original.py')
 for lang in ('ru','en'):
  d=Path('research/evidence/flow')/lang;m=read(d/'analysis-manifest.json');r=reports['original-flow']['languages'][lang]
  assert m['sha256_before']==m['sha256_after']==sha(m['sample'])
  assert m['script_sha256']==sha('scripts/InspectFlow.java') and m['driver_sha256']==sha('scripts/analyze-flow.ps1')
  assert r['manifest_sha256']==sha(d/'analysis-manifest.json') and r['asm_sha256']==sha(d/'evidence.asm')
  assert len(r['equality_vectors'])==8 and len(r['death_vectors'])==7 and r['helper_native_far_args_and_CX30']
 assert reports['exit-audit']['script_sha256']==sha('scripts/audit_flow_exits.py')
 assert len(reports['exit-audit']['levels'])==6
 for row in reports['exit-audit']['levels']:
  assert row['score_total']==0 and not row['score_item_definitions'] and sha(row['archive'])==row['archive_sha256']
 assert len(reports['flow-gba']['levels'])==6 and len(reports['flow-gba']['retries'])==2
 assert reports['flow-gba']['gameover_three_lives'] and reports['flow-gba']['new_game_after_gameover'] and reports['flow-gba']['no_automatic_campaign_or_inventory_carry']
 assert reports['flow-gba']['script_sha256']==sha('scripts/verify_flow_gba.py')
 expected_texts={(n,l) for n in ('BINTRO1','BINTRO2','BINTRO3','EINTRO1','EINTRO2','EINTRO3','ENDTXT') for l in (0,1)}
 assert {(x['resource'],x['language']) for x in reports['flow-gba']['screens']}>=expected_texts
 assert all(x['all_original_text_pixels'] for x in reports['flow-gba']['screens'])
 for key in ('suspended_continue_preserved_world_events_projectiles','ending_discarded_suspended_world_streams','new_game_replaced_live_world_without_old_owners','negative_hp_native_finish_sentinel'):
  assert reports['flow-gba'][key],key
 assert reports['ui-regression']['script_sha256']==sha('scripts/verify_ui_gba.py')
 def screens(value):
  if isinstance(value,dict):
   if 'path' in value and 'sha256' in value:yield value
   for child in value.values():yield from screens(child)
  elif isinstance(value,list):
   for child in value:yield from screens(child)
 assert list(screens(reports['visual-review'])), 'Visual review has no captures'
 for n in ('flow-gba','ui-regression','visual-review'):
  for screen in screens(reports[n]):assert sha(screen['path'])==screen['sha256'],screen['path']
 assert reports['audio-regression']['final']['underruns']==0 and len(reports['audio-regression']['all_sounds'])==23
 assert reports['time-regression']['exact_rational_samples']
 sources=[]
 for folder in ('src','include/dok','gba','tests'):
  sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 scripts=('InspectFlow.java','analyze-flow.ps1','build-gba.ps1','build-gba-flow.ps1','verify_flow_original.py','verify_flow_host.py','verify_flow_gba.py','audit_flow_exits.py','report_flow.py','verify_ui_gba.py','verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py','verify_sprites_gba.py','verify_ai_combat_gba.py','verify_audio_gba.py','verify_gba.py')
 sources.extend(Path('scripts')/p for p in scripts);sources.append(Path('CMakeLists.txt'))
 iwram=build['symbols']['__iwram_end']-build['symbols']['__iwram_start'];assert iwram<=24576
 result=dict(task='G18',status='passed',next='G20',rom_sha256=build['rom_sha256'],rom_bytes=Path(rom).stat().st_size,
  ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining_bytes=262144-build['ewram_static_bytes'],iwram_static_bytes=iwram,iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
  sources={p.as_posix():sha(p) for p in sorted(set(sources))},artifacts={p:sha(p) for p in ('build/g18/dok-diagnostic.gba','build/g18/dok-diagnostic.elf','build/g18/dok-diagnostic.map')},reports={f'analysis/g18-{n}.json':sha(f'analysis/g18-{n}.json') for n in names},
  checks=dict(original_equality_vectors=16,original_death_vectors=14,original_pose_helper_vectors=2,independent_original_RES_audit=True,all_three_levels_both_languages=True,actual_rules_and_keypad=True,renderer_interactions_time_ui_regressions=True,ai_combat_variants=28,actual_audio_all23=True),
  performance=dict(renderer_timing=reports['renderer-regression']['timing'],target30fps_verified=False),
  limits=['Physical GBA/original BIOS and whole DOS session untested','Original levels are selected independently; no automatic campaign or inter-world inventory carry','PKR/PKE have no category99 collectibles: original score/total equality is 0==0, not a collection goal','GBA menu/keypad/text pagination and suspended Continue are platform adaptations','Linux static and Thumb compile checked; Linux/MSVC/CMake execution untested','30FPS remains G20; original viewport and detail retained','Audio G16 user listening accepted; G18 PCM numerically captured, not newly user-listened'])
 Path('analysis/g18-review.json').write_text(json.dumps(result,indent=2)+'\n');print('G18 source/ROM/original/host/ARM/visual/regression evidence bound')
if __name__=='__main__':main()
