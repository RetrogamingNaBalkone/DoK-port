"""C9 instrumentation-only bridge; leave accepted C8/C9 proofs immutable."""
from __future__ import annotations
from compiler_cache import compiler_environment
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_scene_merge import function

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/scene-profile'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def without_profile(text):
    result=[];depth=0
    for line in text.splitlines(keepends=True):
        directive=line.strip()
        if not depth and directive=='#if N21P_PROFILE':depth=1;continue
        if depth:
            if directive.startswith('#if'):depth+=1
            elif directive=='#endif':depth-=1
            continue
        result.append(line)
    assert depth==0
    return ''.join(result)
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    old=EVIDENCE/'c9-merge/ports/nds/render/scene.c';current=ROOT/'ports/nds/render/scene.c'
    c9path=EVIDENCE/'scene-merge-equivalence.json';c9=json.loads(c9path.read_text('utf8'))
    assert c9['passed'] and pin(old)==c9['current_C9_snapshot']
    old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    stripped=without_profile(new_text)
    stripped=stripped.replace('            int source_visible=visible(source,camera,&dot);\n            if(!source_visible)',
                              '            if(!visible(source,camera,&dot))',1)
    stripped=stripped.replace('                int merged=dok_render_merge(&scene->quads[previous[kind]],&shifted,\n                     &scene->quads[previous[kind]]);\n                if(merged)',
                              '                if(dok_render_merge(&scene->quads[previous[kind]],&shifted,\n                     &scene->quads[previous[kind]]))',1)
    assert stripped==old_text,'Unexpected non-instrumentation scene change'
    names=('component','same_vertex','vertex_range','dok_render_merge','visible','dok_render_trig','dok_render_bounds','dok_render_scene_release')
    for name in names:assert function(old_text,name)==function(new_text,name)
    header=ROOT/'ports/nds/include/dok/render_scene.h'
    archived_header=EVIDENCE/'initial/ports/nds/include/dok/render_scene.h'
    expected=next(p for p in c9['source_pins'] if p['path']=='ports/nds/include/dok/render_scene.h')
    assert pin(archived_header)['sha256']==expected['sha256']
    assert without_profile(header.read_text('utf8'))==archived_header.read_text('utf8')
    before=[pin(p) for p in (current,header,ROOT/'ports/nds/render/geometry.c',
                            ROOT/'ports/nds/tests/scene_prepare_equivalence.c',Path(__file__),
                            ROOT/'ports/nds/tools/map/check_scene_merge.py')]
    env=compiler_environment(ROOT)
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-DN21P_PROFILE=0',
           '-Iports/nds/include','-Ibuild/nds/n07/generated']
    exports=('dok_render_trig','dok_render_scene_init','dok_render_merge','dok_render_bounds','dok_render_scene_prepare','dok_render_scene_release')
    commands=[flags+['-D'+n+'=baseline_'+n for n in exports]+['-c',str(old),'-o',str(OUT/'baseline.o')],
              flags+['ports/nds/tests/scene_prepare_equivalence.c','ports/nds/render/scene.c',
                     'ports/nds/render/geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c',
                     str(OUT/'baseline.o'),'-o',str(OUT/'scene_profile_equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    c8=json.loads((EVIDENCE/'scene-visibility-equivalence.json').read_text('utf8'));rows=[]
    for prior in c8['scene_rows']:
        level=prior['level'];poses=ROOT/prior['inputs']['path'];assert pin(poses)==prior['inputs']
        fixture=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        result=json.loads(subprocess.check_output([str(OUT/'scene_profile_equivalence.exe')]+list(map(str,fixture)),
            input=poses.read_text('utf8'),cwd=ROOT,text=True));assert result['passed'] and result['scenes']==prior['scenes']
        result['level']=level;rows.append(result)
    assert before==[pin(ROOT/p['path']) for p in before]
    report=dict(task='N21P C9 scene instrumentation bridge',passed=True,source_pins=before,
                archived_algorithm_source=pin(old),archived_profile0_header=pin(archived_header),
                C9_merge_proof=pin(c9path),unchanged_function_body_sha256={n:hashlib.sha256(function(new_text,n).encode('utf8')).hexdigest() for n in names},
                profile0_algorithm_text_equal_after_local_result_normalization=True,
                profile0_header_text_equal=True,profile0_full_scenes=sum(r['scenes'] for r in rows),
                normalized_bytes_compared=sum(r['normalized_bytes_compared'] for r in rows),scene_rows=rows,
                scope='Only N21P_PROFILE diagnostic cpuGetTiming calls and conditional16-byte existing scene tail. Visibility and merge helper bodies unchanged; host PROFILE0 full scene outputs byte-identical. This does not claim zero instrumentation overhead.',
                commands=commands,binary=pin(OUT/'scene_profile_equivalence.exe'),old_C8_C9_reports_modified=False)
    target=EVIDENCE/'scene-profile-equivalence.json';target.write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,profile0_full_scenes=report['profile0_full_scenes'],normalized_bytes_compared=report['normalized_bytes_compared'])))
if __name__=='__main__':main()
