"""N23 promoted floor/column/prefix-seen raster: old/new guards, full rasters and original x86 pixels."""
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact
OUT=ROOT/'build/nds/n23/map-check'
E=ROOT/'research/evidence/nds/n23'

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'map'))
from compiler_cache import compiler_environment


def main():
    OUT.mkdir(parents=True,exist_ok=True);E.mkdir(parents=True,exist_ok=True)
    old=ROOT/'research/evidence/nds/n22/candidates/candidate-partial02-ru/sources/ports/nds/ui/map_raster.c'
    current=ROOT/'ports/nds/ui/map_raster.c'
    env=compiler_environment(ROOT)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-DN23_MAP_VERTICAL=1','-DN23_MAP_FLOOR_RUNS=1','-DN23_MAP_DIRECT_SEEN=1','-Iports/nds/include']
    baseline=OUT/'baseline.c'
    baseline.write_bytes(old.read_bytes()+b'\nvoid baseline_rect(uint8_t *out,int32_t x1,int32_t y1,int32_t x2,int32_t y2,uint8_t c){rect(out,x1,y1,x2,y2,c);}\n')
    commands=[flags+['-Ddok_map_raster=baseline_dok_map_raster','-c',str(baseline),'-o',str(OUT/'baseline.o')],
              flags+['ports/nds/tests/map_unroll_equivalence.c',str(OUT/'baseline.o'),'-o',str(OUT/'equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    differential=json.loads(subprocess.check_output([str(OUT/'equivalence.exe')],cwd=ROOT,text=True));assert differential['passed']
    checker=ROOT/'ports/nds/tools/check_map_raster.py';code=checker.read_text('utf8')
    replacements=[("ROOT = Path(__file__).resolve().parents[3]",'ROOT = Path('+repr(ROOT.as_posix())+')'),
                  ("sys.path.insert(0, str(Path(__file__).parent / 'map'))","sys.path.insert(0, str(ROOT / 'ports/nds/tools/map'))"),
                  ("build = ROOT / 'build/nds/n20/host'","build = ROOT / 'build/nds/n23/map-check/native'")]
    replacements.append(("'ports/nds/ui/map_raster.c','-o'","'-DN23_MAP_VERTICAL=1','-DN23_MAP_FLOOR_RUNS=1','-DN23_MAP_DIRECT_SEEN=1','ports/nds/ui/map_raster.c','-o'"))
    for before,after in replacements:assert code.count(before)==1;code=code.replace(before,after)
    execution=OUT/'native_checker.py';execution.write_text(code,'utf8')
    native_path=E/'map-native.json'
    with (OUT/'native.log').open('w',encoding='utf8')as log:
        subprocess.run([sys.executable,'-B','-X','utf8',str(execution),'--out',str(native_path.relative_to(ROOT))],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    native=json.loads(native_path.read_text('utf8'));assert native['passed']and native['checks']==954
    report=dict(task='N23',passed=True,baseline=artifact(old),candidate=artifact(current),differential=differential,
        native_report=artifact(native_path),native_checker=artifact(checker),execution=artifact(execution),
        path_and_feature_replacements=replacements,commands=commands)
    (E/'map-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report))
if __name__=='__main__':main()
