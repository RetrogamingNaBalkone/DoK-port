"""Compare loaded level geometry and native disclosure with accepted constructors."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from audit_pipeline import ROOT,pin

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label',required=True)
    args=parser.parse_args();assert args.label.replace('-','').isalnum()
    out=ROOT/'build/nds/n23'/args.label;out.mkdir(exist_ok=True)
    report_path=ROOT/'research/evidence/nds/n23'/(args.label+'-host.json');assert not report_path.exists()
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(out/'zig-cache'))
    common=['ports/nds/render/sprites.c','ports/nds/render/scene.c','ports/nds/render/geometry.c',
        'ports/nds/render/static_geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c']
    sources=common+['ports/nds/render/static_geometry.h','ports/nds/ui/map_discovery.c',
        'ports/nds/tools/performance/topology_invariant.c','ports/nds/tests/map_discovery_equivalence.c',
        'ports/nds/tools/performance/check_static_geometry.py']
    before=[pin(ROOT/p)for p in sources]
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include',
        '-Iports/nds/render','-Ibuild/nds/n07/generated','-DN23_DISCOVERY_BITSET=1','-DN23_DISCOVERY_SORT=1']
    commands=[]
    def build(extra):
        command=[str(zig),'cc',*flags,*extra];commands.append(command)
        subprocess.run(command,cwd=ROOT,env=env,capture_output=True,check=True)
    names=['dok_map_discovery_bounds','dok_map_record_geometry','dok_map_record_sprite',
        'dok_map_discovery_records','dok_map_discovery_update']
    originals=[]
    for directory,prefix,pins_file in [('initial','baseline','discovery-pins.json'),
        ('pre-compact','candidate4','pins.json'),('pre-math','candidate5','pins.json')]:
        root=ROOT/'research/evidence/nds/n21p'/directory
        for item in json.loads((root/pins_file).read_text())['source_pins']:
            assert pin(ROOT/item['archived'])['sha256']==item['sha256']
        source=root/'ports/nds/ui/map_discovery.c';originals.append(pin(source))
        # Old headers precede current ones for the historical workspace ABI.
        command=[str(zig),'cc','-I'+str(root/'ports/nds/include'),*flags,
            *['-D'+name+'='+prefix+'_'+name for name in names],'-c',str(source),'-o',str(out/(prefix+'.o'))]
        commands.append(command);subprocess.run(command,cwd=ROOT,env=env,capture_output=True,check=True)
    harness=(ROOT/'ports/nds/tests/map_discovery_equivalence.c').read_text()
    needle='    for(n=0;n<512u;++n) {';assert harness.count(needle)==1
    harness='#include "static_geometry.h"\nstatic _Alignas(32) unsigned char static_backing[32768];\n'+harness
    harness=harness.replace(needle,'    uint32_t static_bytes;\n    if(!dok_static_geometry_load(&world,bindings,static_backing,sizeof(static_backing),&static_bytes))return 19;\n'+needle)
    derived=out/'disclosure_equivalence.c';derived.write_text(harness)
    build(['-DN23_STATIC_GEOMETRY=1',str(derived),'ports/nds/ui/map_discovery.c',*common,
        *[str(out/(p+'.o'))for p in ('baseline','candidate4','candidate5')],'-o',str(out/'disclosure.exe')])
    build(['-DN23_STATIC_GEOMETRY=1','-DDOK_TEST_STATIC_GEOMETRY=1',
        'ports/nds/tools/performance/topology_invariant.c','ports/nds/render/static_geometry.c',
        'ports/nds/render/geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c','-o',str(out/'geometry.exe')])
    results=[]
    for level in (1,2,3):
        inputs=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        rows={}
        for binary in ('geometry','disclosure'):
            run=subprocess.run([out/(binary+'.exe'),*inputs],cwd=ROOT,capture_output=True,text=True,check=True)
            rows[binary]=[json.loads(line)for line in run.stdout.splitlines()]
            assert all(row.get('passed',True)for row in rows[binary])
        results.append(dict(level=level,inputs=[pin(p)for p in inputs],**rows))
        print(json.dumps(results[-1]),flush=True)
    assert before==[pin(ROOT/p)for p in sources]
    report=dict(task='N23',passed=True,scope=__doc__,sources=before,historical_sources=originals,
        derived_harness=pin(derived),commands=commands,results=results,
        limitations=['Host timings are not ARM timings. Current C12 material/working-set validation is the separate emulator gate.'])
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=True,report=pin(report_path))))
if __name__=='__main__':main()
