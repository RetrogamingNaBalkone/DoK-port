"""Execute exact N23 source-lease packing and map button lifetime checks."""
import json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact
OUT=ROOT/'build/nds/n23/layout-check'
E=ROOT/'research/evidence/nds/n23'

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'map'))
from compiler_cache import compiler_environment


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    env=compiler_environment(ROOT)
    flags=[str(Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include','-Ibuild/nds/n07/generated']
    sources=['ports/nds/tools/performance/compact_source_test.c','ports/nds/render/materials.c','ports/nds/core/assets.c','ports/nds/core/world.c','ports/nds/core/balance.c','ports/nds/core/sha256.c','ports/nds/platform/assets/file_reader.c']
    old='research/evidence/nds/n22/candidates/candidate-partial02-ru/sources/ports/nds/render/materials.c'
    buttons=['ports/nds/tools/performance/map_buttons_test.c','ports/nds/platform/map/runtime.c','ports/nds/ui/map_control.c','ports/nds/ui/map_raster.c']
    inputs=[ROOT/p for p in sources+buttons+[old,'ports/nds/tests/test_map_runtime.c','ports/nds/tests/map_runtime_stub/nds.h']]
    inputs+=list((ROOT/'ports/nds/include/dok').glob('*.h'))+[ROOT/'ports/nds/platform/map/runtime.h',Path(__file__)]
    before=[artifact(p)for p in inputs]
    commands=[flags+['-Ddok_render_world_load=baseline_dok_render_world_load','-c',old,'-o',str(OUT/'old.o')],
        flags+['-DN23_COMPACT_SOURCE=1',*sources,str(OUT/'old.o'),'-o',str(OUT/'compact.exe')],
        flags+['-DN21P_HOST_TEST=1','-DN21P_AUDIT=0','-DN23_MAP_BUTTON_CACHE=1','-DN23_MAP_SECTOR_DIRTY=1','-DN23_MAP_FLOOR_RUNS=1','-DN23_MAP_DIRECT_SEEN=1','-Iports/nds/tests/map_runtime_stub','-Iports/nds/platform/map','-Iports/nds/platform/render_probe','-Iports/nds/platform/assets','-Iports/nds/platform/audio',*buttons,'-o',str(OUT/'buttons.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    reports=[]
    for name,args in [('compact',['build/nds/assets/dok-assets.ndspak']),('buttons',[])]:
        output=subprocess.check_output([str(OUT/(name+'.exe')),*args],cwd=ROOT,text=True)
        result=json.loads(output.splitlines()[-1]);assert result['passed']
        result.update(task='N23',sources=before,commands=commands,binary=artifact(OUT/(name+'.exe')))
        if name=='compact':result['package']=artifact(ROOT/args[0])
        reports.append((name,result))
    assert before==[artifact(p)for p in inputs]
    for name,result in reports:
        result['source_inputs_unchanged']=True
        (E/('compact-source-check.json'if name=='compact'else'map-buttons-check.json')).write_text(json.dumps(result,indent=2)+'\n','utf8')
        print(json.dumps({k:v for k,v in result.items()if k not in('sources','commands')}))
if __name__=='__main__':main()
