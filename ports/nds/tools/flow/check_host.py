"""Independent N22 host invariants; native source slices have a separate gate."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools/map'))
from compiler_cache import compiler_environment

def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size)

def main():
    out=ROOT/'build/nds/n22/host';out.mkdir(parents=True,exist_ok=True)
    evidence=ROOT/'research/evidence/nds/n22/host';evidence.mkdir(parents=True,exist_ok=True)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    sources=['ports/nds/core/gameflow.c','ports/nds/ui/frontend.c','ports/nds/core/events.c',
             'ports/nds/platform/gameplay_probe/gameplay.c','ports/nds/tools/flow/test_flow_host.c']
    sources += ['ports/nds/core/'+name+'.c' for name in ('contracts','input','controls','motion','camera',
                'time','collision','interactions','interactions_move','assets','memory','sha256','world','balance','items','combat','ai')]
    sources += ['ports/nds/render/'+name+'.c' for name in ('scene','geometry','materials','door_render','sprites','sprite_assets','weapon_overlay')]
    sources += ['ports/nds/platform/assets/file_reader.c']
    command=[zig,'cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-g',
             '-ffunction-sections','-fdata-sections','-DGAMEPLAY_FLOW=1','-DGAMEPLAY_UI=0','-DGAMEPLAY_AUDIO=0',
             '-Iports/nds/include','-Iports/nds/platform/gameplay_probe','-Ibuild/nds/n07/generated',
             '-Iports/nds/platform/assets',*sources,'-o',out/'flow-host.exe']
    process=subprocess.run(list(map(str,command)),cwd=ROOT,env=compiler_environment(ROOT),capture_output=True,text=True)
    if process.returncode:raise RuntimeError(process.stdout+process.stderr)
    process=subprocess.run([str(out/'flow-host.exe')],cwd=ROOT,capture_output=True,text=True)
    if process.returncode:raise RuntimeError(process.stdout+process.stderr)
    test=json.loads(process.stdout)
    headers=sorted((ROOT/'ports/nds/include').rglob('*.h'))
    headers += sorted((ROOT/'ports/nds/platform/gameplay_probe').glob('*.h'))
    headers += [ROOT/'build/nds/n07/generated/render_trig.inc']
    report=dict(task='N22',passed=True,command=list(map(str,command)),test=test,
                scope='Windows host: scalar/full-state retry/controller; no ARM9 or DOS-session claim',
                inputs=[pin(ROOT/p) for p in sources]+[pin(__file__)]+[pin(p) for p in headers],binary=pin(out/'flow-host.exe'))
    (evidence/'check.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(test))

if __name__=='__main__':main()
