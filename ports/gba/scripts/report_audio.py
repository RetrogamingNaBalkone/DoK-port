"""Bind G16 evidence and regressions to frozen ROM/source/artifacts."""

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
 build=read('build/gba/g16/build-report.json');rom='build/gba/g16/dok-diagnostic.gba';assert sha(rom)==build['rom_sha256']
 names=('host-verification','original-verification','audio-gba','renderer-regression','interactions-regression','time-regression','ai-combat-regression','listening')
 reports={n:read(f'analysis/gba/g16-{n}.json') for n in names}
 for name,r in reports.items():
  assert r.get('status','passed')==('confirmed_by_user' if name=='listening' else 'passed'),name
  if name!='listening' and 'rom_sha256' in r:assert r['rom_sha256']==build['rom_sha256'],name
 for p,h in reports['host-verification']['sources'].items():assert sha(p)==h,p
 for p,h in reports['host-verification']['artifacts'].items():assert sha(p)==h,p
 assert reports['original-verification']['script_sha256']==sha('ports/gba/scripts/verify_audio_original.py')
 for lang in ('ru','en'):
  path=f'research/evidence/audio/{lang}/analysis-manifest.json';m=read(path)
  assert m['sha256_before']==m['sha256_after']==sha(m['sample'])
  assert m['script_sha256']==sha('tools/ghidra/InspectAudio.java') and m['driver_sha256']==sha('tools/ghidra/analyze-audio.ps1')
  o=reports['original-verification']['languages'][lang];assert o['manifest_sha256']==sha(path)
  assert o['base_policy_cases']==7 and o['wrapper_mutations']==2 and o['backend_latch_cases']==4
 assert reports['audio-gba']['script_sha256']==sha('ports/gba/scripts/verify_audio_gba.py')
 assert reports['audio-gba']['human_listening_verified']
 audio=reports['audio-gba'];assert len(audio['all_sounds'])==23
 assert audio['final']['underruns']==0 and audio['irq_stack']['untouched_margin_bytes']>=32
 assert audio['identity']['same_pcm_distinct_sound_objects'] and audio['identity']['other_owner_stop_preserved_current']
 assert audio['projectile_identity']['same_pcm_distinct_clone_identities'] and audio['projectile_identity']['removal_of_other_clone_preserved_voice']
 assert audio['level_change_stopped_voice'] and audio['language_preserved_active_voice'] and audio['rejected_loop_preserved_once']
 for sound in audio['all_sounds']:
  assert sound['correlation']['coefficient']>0.95 and sound['correlation']['whole_recording_coefficient']>0.95
  assert abs(sound['observed_seconds']-sound['expected_hardware_seconds'])<0.05
  assert sound['renderer_frames']>0
 costs=audio['integrated_cost'];assert costs['timer_quantum_cycles']==64
 assert len({costs[k]['framebuffer_sha256'] for k in ('silent','active22050','active11025')})==1
 def captures(value):
  if isinstance(value,dict):
   if str(value.get('path','')).endswith('.wav') and 'sha256' in value:assert sha(value['path'])==value['sha256'],value['path']
   for child in value.values():captures(child)
  elif isinstance(value,list):
   for child in value:captures(child)
 captures(reports['audio-gba'])
 assert sha('build/gba/g16/audio-check/listened-short-loop.wav')=='81d9ab296af0ce1fa91a9afa75ad2b931a3c88184dc87b4837812906efeda94b'
 for tag in ('stack_before_reset','stack_after_reset'):assert reports['renderer-regression'][tag]['untouched_margin_bytes']>=512
 assert reports['time-regression']['edge_vectors_on_arm']=='passed'
 sources=[]
 for folder in ('ports/gba/src','ports/gba/include/dok','ports/gba/platform','ports/gba/tests'):sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 sources.extend(script_path(n) for n in ('InspectAudio.java','analyze-audio.ps1','build-gba.ps1','build-native.ps1','build-gba-audio.ps1','verify_audio_original.py','verify_audio_host.py','verify_audio_gba.py','report_audio.py','verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py','verify_ai_combat_gba.py'));sources.append(Path('CMakeLists.txt'))
 symbols=build['symbols'];iwram=symbols['__iwram_end']-symbols['__iwram_start'];assert iwram<=24576
 report=dict(task='G16',status='passed',next='G17',rom_sha256=build['rom_sha256'],rom_bytes=Path(rom).stat().st_size,
  ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining_bytes=262144-build['ewram_static_bytes'],iwram_static_bytes=iwram,iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
  sources={p.as_posix():sha(p) for p in sorted(set(sources))},reports={f'analysis/gba/g16-{n}.json':sha(f'analysis/gba/g16-{n}.json') for n in names},
  artifacts={p:sha(p) for p in ('build/gba/g16/dok-diagnostic.elf','build/gba/g16/dok-diagnostic.gba','build/gba/g16/dok-diagnostic.map')},
  checks=dict(native_policy_cases=26,host_policy=reports['host-verification']['stdout'],arm_audio=reports['audio-gba'],rational_clock=True,door_sensor_conveyor_finish=True,ai_combat=reports['ai-combat-regression']),
  performance=dict(renderer_timing=reports['renderer-regression']['timing'],target30fps_verified=False),
  limits=['Physical GBA/original BIOS and full DOS playback untested','User listened to short-loop mGBA output, not all23 resources; exact listened artifact predates identity-only fix','Numerical output comparison is not Sound Blaster hardware equivalence','Refill cycles cover C fill/rearm only; complete IRQ cost is not directly instrumented','Linux/ARM policy cross compilation passed; Linux/MSVC/CMake execution untested','Full game flow G18 and 30FPS G20 remain separate tasks'])
 Path('analysis/gba/g16-review.json').write_text(json.dumps(report,indent=2)+'\n');print('G16 final ROM/source/native/host/ARM/listening evidence bound')
if __name__=='__main__':main()
