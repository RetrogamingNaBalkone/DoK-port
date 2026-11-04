"""Frozen production vs cached native record order, dynamic sprites and visited."""
import json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact
from map.compiler_cache import compiler_environment

def main():
    out=ROOT/'build/nds/n23/static-discovery-check';out.mkdir(parents=True,exist_ok=True)
    base=ROOT/'research/evidence/nds/n23/candidates/mapcache31-ru/sources/ports/nds/ui/map_discovery.c'
    flags=[str(Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',
        '-Iports/nds/include','-Ibuild/nds/n07/generated','-DN23_DISCOVERY_BITSET=1','-DN23_DISCOVERY_SORT=1']
    env=compiler_environment(ROOT);commands=[];results=[]
    def run(command):
        commands.append(list(map(str,command)))
        r=subprocess.run(command,cwd=ROOT,env=env,text=True,capture_output=True)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout
    names=['dok_map_discovery_bounds','dok_map_record_geometry','dok_map_record_sprite','dok_map_discovery_records','dok_map_discovery_update']
    run(flags+['-D'+n+'=baseline_'+n for n in names]+['-c',str(base),'-o',str(out/'baseline.o')])
    common=['ports/nds/render/sprites.c','ports/nds/render/scene.c','ports/nds/render/geometry.c',
        'ports/nds/core/world.c','ports/nds/core/sha256.c','ports/nds/ui/map_discovery.c','ports/nds/tests/test_discovery_static_cache.c']
    harness=(ROOT/'ports/nds/tests/test_discovery_static_cache.c').read_text('utf8')
    derived='#include "static_geometry.h"\nstatic _Alignas(32) unsigned char geometry_backing[32768];\n'+harness
    needle='    for(unsigned n=0;n<256u;++n){';assert derived.count(needle)==1
    derived=derived.replace(needle,'    uint32_t used;\n    if(!dok_static_geometry_load(&world,bindings,geometry_backing,sizeof(geometry_backing),&used))return 19;\n'+needle)
    generated=out/'static-harness.c';generated.write_text(derived,'utf8')
    for capacity,static in ((256,0),(8,0),(256,1)):
        exe=out/f'cache-{capacity}-{static}.exe'
        selected=common if not static else [p for p in common if not p.endswith('test_discovery_static_cache.c')]+[str(generated),'ports/nds/render/static_geometry.c']
        run(flags+[f'-DDOK_MAP_STATIC_RECORDS={capacity}',f'-DN23_STATIC_GEOMETRY={static}','-Iports/nds/render',*selected,str(out/'baseline.o'),'-o',str(exe)])
        for level in (1,2,3):
            result=json.loads(run([str(exe),str(ROOT/f'build/nds/n07/geometry-level{level}.cp866'),str(ROOT/f'build/nds/n07/geometry-bindings{level}.bin')]))
            assert result['passed'];results.append(dict(level=level,capacity=capacity,static_geometry=static,**result))
    report=dict(passed=True,results=results,commands=commands,baseline=artifact(base),
        sources=[artifact(ROOT/p)for p in common+['ports/nds/render/static_geometry.c','ports/nds/render/static_geometry.h','ports/nds/include/dok/map_discovery.h','ports/nds/tools/performance/check_static_discovery_cache.py']],generated_harness=artifact(generated))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(results))
if __name__=='__main__':main()
