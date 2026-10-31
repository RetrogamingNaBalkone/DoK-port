"""N23 private door constructor against the frozen N22 full scene implementation."""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact
OUT=ROOT/'build/nds/n23/door-check'
E=ROOT/'research/evidence/nds/n23'

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'map'))
from compiler_cache import compiler_environment


def main():
    OUT.mkdir(parents=True,exist_ok=True);E.mkdir(parents=True,exist_ok=True)
    old=ROOT/'research/evidence/nds/n22/candidates/candidate-partial02-ru/sources/ports/nds/render/door_render.c'
    current=ROOT/'ports/nds/render/door_render.c'
    env=compiler_environment(ROOT)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',
        '-DN21P_PROFILE=0','-DN21P_GEOMETRY_PROFILE=0','-DGAMEPLAY_GEOMETRY_CACHE=0',
        '-Iports/nds/include','-Iports/nds/render','-Ibuild/nds/n07/generated']
    exports=('dok_door_geometry_build','dok_door_scene_prepare')
    commands=[flags+['-D'+name+'=baseline_'+name for name in exports]+['-c',str(old),'-o',str(OUT/'baseline.o')],
        flags+['-DN23_FUSED_CULL=1','-DN23_DOOR_INPLACE=1','ports/nds/tests/door_lazy_equivalence.c','ports/nds/render/door_render.c','ports/nds/render/scene.c',
            'ports/nds/render/geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c',str(OUT/'baseline.o'),'-o',str(OUT/'equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    prior=json.loads((ROOT/'research/evidence/nds/n21p/scene-visibility-equivalence.json').read_text('utf8'))
    rows=[];inputs=[]
    for row in prior['scene_rows']:
        poses=ROOT/row['inputs']['path'];assert artifact(poses)==row['inputs']
        level=row['level'];fixtures=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        inputs.extend([artifact(poses),*[artifact(path)for path in fixtures]])
        output=subprocess.check_output([str(OUT/'equivalence.exe'),*map(str,fixtures)],input=poses.read_text('utf8'),cwd=ROOT,text=True)
        result=json.loads(output.splitlines()[-1]);assert result['passed'];rows.append(dict(level=level,**result))
    report=dict(task='N23',passed=True,baseline=artifact(old),candidate=artifact(current),rows=rows,inputs=inputs,commands=commands,
        scope='Full normalized staging bytes, pointers, flags, UV, counts and errors from actual frozen/current C scene functions')
    (E/'door-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report))
if __name__=='__main__':main()
