"""C9 exact signed32 merge intermediates after the existing range guard."""
from __future__ import annotations
from compiler_cache import compiler_environment
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/scene-merge'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def function(text,name):
    a=text.index(name+'(');start=text.rfind('\n',0,a)+1;b=text.index('{',a);depth=1;i=b+1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=EVIDENCE/'pre-merge/pins.json'
    archived=json.loads(manifest.read_text('utf8'))['source_pins']
    for row in archived:assert pin(ROOT/row['archived'])['sha256']==row['sha256']
    row=next(r for r in archived if r['path']=='ports/nds/render/scene.c')
    old=ROOT/row['archived'];current=ROOT/row['path'];old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    reversed_text=new_text.replace('static int32_t component(','static int64_t component(',1)
    reversed_text=reversed_text.replace('int32_t aw[5],bw[5],height[3],cross[3];','int64_t aw[5],bw[5],height[3],cross[3];',1)
    reversed_text=reversed_text.replace('(int64_t)aw[i]*bw[axis]!=(int64_t)bw[i]*aw[axis]','aw[i]*bw[axis]!=bw[i]*aw[axis]',1)
    assert reversed_text==old_text
    unchanged=('dok_render_trig','dok_render_bounds','visible','dok_render_scene_prepare','dok_render_scene_release')
    for name in unchanged:assert function(old_text,name)==function(new_text,name)
    names=('component','vertex_range','dok_render_merge')+unchanged
    function_pins={name:hashlib.sha256(function(new_text,name).encode('utf8')).hexdigest() for name in names}
    sources=['ports/nds/render/scene.c','ports/nds/render/geometry.c','ports/nds/include/dok/render_scene.h',
             'ports/nds/include/dok/render_packet.h','ports/nds/core/world.c','ports/nds/core/sha256.c',
             'ports/nds/tests/scene_merge_equivalence.c','ports/nds/tests/scene_prepare_equivalence.c',
             'ports/nds/tools/map/check_scene_merge.py']
    before=[pin(ROOT/p) for p in sources]
    env=compiler_environment(ROOT)
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include','-Ibuild/nds/n07/generated']
    exports=('dok_render_trig','dok_render_scene_init','dok_render_merge','dok_render_bounds','dok_render_scene_prepare','dok_render_scene_release')
    common=['ports/nds/render/geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c']
    commands=[flags+['-D'+n+'=baseline_'+n for n in exports]+['-c',str(old),'-o',str(OUT/'baseline.o')],
              flags+['ports/nds/tests/scene_merge_equivalence.c','ports/nds/render/scene.c']+common+[str(OUT/'baseline.o'),'-o',str(OUT/'scene_merge_equivalence.exe')],
              flags+['ports/nds/tests/scene_prepare_equivalence.c','ports/nds/render/scene.c']+common+[str(OUT/'baseline.o'),'-o',str(OUT/'scene_prepare_equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    numeric=json.loads(subprocess.check_output([str(OUT/'scene_merge_equivalence.exe')],cwd=ROOT,text=True));assert numeric['passed']
    print('merge API',numeric,flush=True)
    previous=EVIDENCE/'scene-visibility-equivalence.json';c8=json.loads(previous.read_text('utf8'));assert c8['passed']
    rows=[];inputs=[]
    for prior in c8['scene_rows']:
        level=prior['level'];poses=ROOT/prior['inputs']['path'];assert pin(poses)==prior['inputs']
        fixture=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        inputs.extend([pin(poses)]+[pin(p) for p in fixture])
        result=json.loads(subprocess.check_output([str(OUT/'scene_prepare_equivalence.exe')]+list(map(str,fixture)),input=poses.read_text('utf8'),cwd=ROOT,text=True))
        assert result['passed'] and result['scenes']==prior['scenes'];result['level']=level;rows.append(result)
        print('merge fullscene',level,result,flush=True)
    assert before==[pin(ROOT/p) for p in sources]
    snapshot=EVIDENCE/'c9-merge/ports/nds/render/scene.c';snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_bytes(current.read_bytes())
    result=dict(task='N21P C9 exact merge intermediates',passed=True,source_pins=before,
                source_pins_before_after_equal=True,archived_C8_source=pin(old),current_C9_snapshot=pin(snapshot),
                function_body_sha256=function_pins,unchanged_functions=list(unchanged),
                range_proof=dict(xyz_difference_max=8191,uv_difference_max=268431360,
                    xyz_product_max=67092481,cross_max=134184962,uv_collinearity_products='explicit int64 preserved'),
                numeric=numeric,scene_rows=rows,full_scenes=sum(r['scenes'] for r in rows),
                normalized_bytes_compared=sum(r['normalized_bytes_compared'] for r in rows),
                scope='Only range-proven component return/intermediate storage and XYZ cross products became signed32. UV collinearity products remain signed64. C8 visibility/trig/bounds/prepare unchanged. Host profile default0.',
                C8_frustum_proof=pin(previous),inputs=inputs,commands=commands,
                binaries=[pin(OUT/p) for p in ('baseline.o','scene_merge_equivalence.exe','scene_prepare_equivalence.exe')])
    report=EVIDENCE/'scene-merge-equivalence.json';report.write_text(json.dumps(result,indent=2)+'\n','utf8')
    bridge=dict(task='N21P C9 scene merge source bridge',passed=True,scope='Only C8-to-C9 merge helper range-proven integer widths; all C8 private visibility and original trig/bounds unchanged.',
                original_compiled_path=row['path'],archived_original=pin(old),current_equivalent_source=pin(current),
                equivalence_proof=pin(report),C8_frustum_proof=pin(previous),
                C8_source_bridge=pin(EVIDENCE/'scene-source-supersession.json'),
                unchanged_function_body_sha256={n:function_pins[n] for n in unchanged},native_replay=False,old_C8_reports_modified=False)
    (EVIDENCE/'scene-merge-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,API_calls=numeric['API_calls'],full_scenes=result['full_scenes'])))
if __name__=='__main__':main()
