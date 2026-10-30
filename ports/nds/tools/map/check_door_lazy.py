"""C10 lazy in-place door compaction copies; exact old/new scene replay."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_scene_merge import function

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/door-lazy'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=EVIDENCE/'pre-door-lazy/pins.json';old_pin=json.loads(archive.read_text('utf8'))['source_pins'][0]
    old=ROOT/old_pin['archived'];current=ROOT/old_pin['path'];assert pin(old)['sha256']==old_pin['sha256']
    old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    unchanged=('nearest','intersection','dok_door_geometry_build','source_quad','source_door_class','changed')
    for name in unchanged:assert function(old_text,name)==function(new_text,name),name
    sources=['ports/nds/render/door_render.c','ports/nds/render/scene.c','ports/nds/render/geometry.c',
             'ports/nds/include/dok/door_render.h','ports/nds/include/dok/render_scene.h',
             'ports/nds/core/world.c','ports/nds/core/sha256.c','ports/nds/tests/door_lazy_equivalence.c',
             'ports/nds/tools/map/check_door_lazy.py','ports/nds/tools/map/check_scene_merge.py']
    before=[pin(ROOT/p) for p in sources]
    env=os.environ.copy();env['ZIG_LOCAL_CACHE_DIR']=str(OUT/'zig-local-cache');env['ZIG_GLOBAL_CACHE_DIR']=str(OUT/'zig-global-cache')
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',
           '-DN21P_PROFILE=0','-DN21P_GEOMETRY_PROFILE=0','-Iports/nds/include','-Ibuild/nds/n07/generated']
    commands=[flags+['-Ddok_door_geometry_build=baseline_dok_door_geometry_build',
                     '-Ddok_door_scene_prepare=baseline_dok_door_scene_prepare','-c',str(old),'-o',str(OUT/'baseline.o')],
              flags+['ports/nds/tests/door_lazy_equivalence.c','ports/nds/render/door_render.c',
                     'ports/nds/render/scene.c','ports/nds/render/geometry.c','ports/nds/core/world.c',
                     'ports/nds/core/sha256.c',str(OUT/'baseline.o'),'-o',str(OUT/'door_lazy_equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    c8=json.loads((EVIDENCE/'scene-visibility-equivalence.json').read_text('utf8'));rows=[];inputs=[]
    for prior in c8['scene_rows']:
        level=prior['level'];poses=ROOT/prior['inputs']['path'];assert pin(poses)==prior['inputs']
        fixture=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        inputs.extend([pin(poses)]+[pin(p) for p in fixture])
        result=json.loads(subprocess.check_output([str(OUT/'door_lazy_equivalence.exe')]+list(map(str,fixture)),
            input=poses.read_text('utf8'),cwd=ROOT,text=True));assert result['passed'] and result['poses']==prior['scenes']
        result['level']=level;rows.append(result);print('door lazy level',level,result,flush=True)
    assert before==[pin(ROOT/p['path']) for p in before]
    snapshot=EVIDENCE/'c10-door/ports/nds/render/door_render.c';snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_bytes(current.read_bytes())
    result=dict(task='N21P C10 lazy door copies',passed=True,source_pins=before,source_pins_before_after_equal=True,
                archived_original=pin(old),current_snapshot=pin(snapshot),
                unchanged_function_body_sha256={n:hashlib.sha256(function(new_text,n).encode('utf8')).hexdigest() for n in unchanged},
                proof='Read and write quad indices are ordered during stable compaction, destination never exceeds source. Same-address unchanged records need no write; distinct records are non-overlapping. Crop vertices are written only after the unchanged crop validation succeeds. No early copy occurs on a failing quad.',
                phase0_shortcut_added=False,all_crop_validation_preserved=True,
                rows=rows,poses=sum(r['poses'] for r in rows),scene_cases=sum(r['scene_cases'] for r in rows),
                focused_cases=sum(r['focused_cases'] for r in rows),error_returns=sum(r['error_returns'] for r in rows),
                cropped_outputs=sum(r['cropped_outputs'] for r in rows),hidden_outputs=sum(r['hidden_outputs'] for r in rows),
                normalized_bytes_compared=sum(r['normalized_bytes_compared'] for r in rows),inputs=inputs,
                scope='Entire normalized scene, counts, status, output pointer and error side effects: every936 N07/12880 observed runtime camera at phase0 and cycling all30 phases, plus invalid extent/phase/revision/epoch, NULL phases, ready-open resample, mixed late failure, class4 and degenerate merged-door refusal fixtures. Initial LEVEL geometry; not mutable live world replay.',
                profile_timer='One cpuGetTiming pair immediately after successful base prepare, including NULL-phase success and failed lease release; only N21P_PROFILE && N21P_GEOMETRY_PROFILE. Host proof disables profiling.',
                commands=commands,binary=pin(OUT/'door_lazy_equivalence.exe'))
    report=EVIDENCE/'door-lazy-equivalence.json';report.write_text(json.dumps(result,indent=2)+'\n','utf8')
    bridge=dict(task='N21P C10 door source bridge',passed=True,original_compiled_path=old_pin['path'],
                archived_original=pin(old),current_equivalent_source=pin(current),equivalence_proof=pin(report),
                unchanged_function_body_sha256=result['unchanged_function_body_sha256'],
                scope='Only quad copy elision on identical addresses or stable compaction; crop/native geometry helper and every phase validation unchanged. Diagnostic timer is conditional.',
                native_replay=False)
    (EVIDENCE/'door-lazy-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps({k:result[k] for k in ('passed','poses','scene_cases','error_returns','cropped_outputs','hidden_outputs','normalized_bytes_compared')}))
if __name__=='__main__':main()
