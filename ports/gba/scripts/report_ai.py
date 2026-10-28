"""Bind G14 source, original evidence, host, final ROM and ARM acceptance."""

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
 build=read('build/gba/g14/build-report.json');rom='build/gba/g14/dok-diagnostic.gba';assert sha(rom)==build['rom_sha256']
 names=('host-verification','original-verification','ai-gba','renderer-regression','interactions-regression','time-regression');reports={n:read(f'analysis/gba/g14-{n}.json') for n in names}
 for name,r in reports.items():
  assert r.get('status','passed')=='passed',name
  if 'rom_sha256' in r:assert r['rom_sha256']==build['rom_sha256'],name
 for p,h in reports['host-verification']['sources'].items():assert sha(p)==h,p
 for p,h in reports['host-verification']['artifacts'].items():assert sha(p)==h,p
 for lang in ('ru','en'):
  path=f'research/evidence/ai/{lang}/analysis-manifest.json';m=read(path);assert m['sha256_before']==m['sha256_after']==sha(m['sample']);assert m['script_sha256']==sha('tools/ghidra/InspectAI.java');assert m['driver_sha256']==sha('tools/ghidra/analyze-ai.ps1');assert reports['original-verification']['languages'][lang]['manifest_sha256']==sha(path)
 assert reports['ai-gba']['script_sha256']==sha('ports/gba/scripts/verify_ai_gba.py')
 arm=reports['ai-gba'];assert len(arm['perception'])==40 and len(arm['traces'])==2
 assert all(x['repeat_equal'] and x['duplicate_glyph_preserved'] for x in arm['traces'])
 assert len(arm['viewed_wake'])==2 and all(x['live_consideration_path'] and x['valid_display_before_first_callback'] for x in arm['viewed_wake'])
 owners=arm['combat_owners'];assert owners['sleeping_hit_woke_without_damage'] and owners['reclaimed_monster_owner_removed_before_projectile_reuse'] and owners['reused_glyph']==129
 assert arm['stack']['untouched_margin_bytes']>=512
 assert reports['time-regression']['edge_vectors_on_arm']=='passed'
 for tag in ('stack_before_reset','stack_after_reset'):assert reports['renderer-regression'][tag]['untouched_margin_bytes']>=512
 sources=[]
 for folder in ('ports/gba/src','ports/gba/include/dok','ports/gba/platform','ports/gba/tests'):sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
 sources.extend(script_path(n) for n in ('InspectAI.java','analyze-ai.ps1','build-gba.ps1','build-native.ps1','build-gba-ai.ps1','verify_ai_original.py','verify_ai_host.py','verify_ai_gba.py','report_ai.py','verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py'));sources.append(Path('CMakeLists.txt'))
 s=build['symbols'];iwram=s['__iwram_end']-s['__iwram_start']
 report=dict(task='G14',status='passed',next='G15',rom_sha256=build['rom_sha256'],rom_bytes=Path(rom).stat().st_size,ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining_bytes=262144-build['ewram_static_bytes'],iwram_static_bytes=iwram,iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
 sources={p.as_posix():sha(p) for p in sorted(set(sources))},reports={f'analysis/gba/g14-{n}.json':sha(f'analysis/gba/g14-{n}.json') for n in names},artifacts={p:sha(p) for p in ('build/gba/g14/dok-diagnostic.elf','build/gba/g14/dok-diagnostic.gba','build/gba/g14/dok-diagnostic.map')},checks=dict(original_heading=2098,original_perception=400,original_course=50,host_real_RES_replay=True,host_four_corners=True,host_combat_reclaim=True,arm=arm,renderer_guards_reset_language=True,door_sensor_conveyor_finish=True,rational_clock=True),performance=dict(renderer_timing=reports['renderer-regression']['timing'],target30fps_verified=False),limits=['Physical GBA/original BIOS untested','Full DOS gameplay untested; isolated original heading/getter/counter instructions executed','FPU table ctor and full native active callback/scheduler closure not executed as a whole','GBA getter sampling uses previous-render consideration lease in simulation time; exact DOS render/event interleaving not claimed','Linux/CMake/MSVC execution untested; Linux static and Thumb compilation passed','Full1455camera sweep not repeated; targeted static/AI sprite scenarios used','Attack/hurt/death G15, sound G16, HUD G17, complete game flow G18; 30FPS G20'])
 Path('analysis/gba/g14-review.json').write_text(json.dumps(report,indent=2)+'\n');print('G14 final ROM/source/original/host/ARM acceptance bound successfully')
if __name__=='__main__':main()
