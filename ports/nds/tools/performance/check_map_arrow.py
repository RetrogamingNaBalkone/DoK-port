"""Check arrow-only map updates against the frozen production full raster."""
import json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact
from map.compiler_cache import compiler_environment

def main():
    out=ROOT/'build/nds/n23/map-arrow-check';out.mkdir(parents=True,exist_ok=True)
    baseline=ROOT/'research/evidence/nds/n23/candidates/animation26-ru/sources/ports/nds/ui/map_raster.c'
    old=out/'baseline.c'
    old.write_bytes(baseline.read_bytes()+b'\nvoid baseline_rect(uint8_t *o,int32_t x,int32_t y,int32_t r,int32_t b,uint8_t c){rect(o,x,y,r,b,c);}\n')
    flags=[str(Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include','-DN23_MAP_VERTICAL=1','-DN23_MAP_FLOOR_RUNS=1','-DN23_MAP_DIRECT_SEEN=1']
    env=compiler_environment(ROOT);commands=[];results={}
    def run(args):
        commands.append(list(map(str,args)))
        return subprocess.check_output(args,cwd=ROOT,env=env,text=True,stderr=subprocess.STDOUT)
    run(flags+['-Ddok_map_raster=baseline_dok_map_raster','-c',str(old),'-o',str(out/'baseline.o')])
    run(flags+['ports/nds/tests/map_unroll_equivalence.c',str(out/'baseline.o'),'-o',str(out/'pixels.exe')])
    results['pixels']=json.loads(run([str(out/'pixels.exe')]))
    for audit in (0,1):
        for sector in (0,1):
            exe=out/f'runtime-{audit}-{sector}.exe'
            run(flags+['-DN21P_HOST_TEST=1',f'-DN21P_AUDIT={audit}',f'-DN23_MAP_SECTOR_DIRTY={sector}',
                       '-DN23_MAP_BUTTON_CACHE=1','-Iports/nds/tests/map_runtime_stub',
                       'ports/nds/platform/map/runtime.c','ports/nds/ui/map_control.c','ports/nds/ui/map_raster.c',
                       'ports/nds/tools/performance/map_buttons_test.c','-o',str(exe)])
            results[f'runtime-{audit}-{sector}']=run([str(exe)]).strip()
    report=dict(passed=True,baseline=artifact(baseline),results=results,commands=commands,
        sources=[artifact(ROOT/p)for p in ('ports/nds/ui/map_raster.c','ports/nds/include/dok/map_raster.h',
        'ports/nds/platform/map/runtime.c','ports/nds/platform/map/runtime.h','ports/nds/tests/test_map_runtime.c',
        'ports/nds/tests/map_unroll_equivalence.c','ports/nds/tools/performance/map_buttons_test.c',
        'ports/nds/tools/performance/check_map_arrow.py')])
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(results))
if __name__=='__main__':main()
