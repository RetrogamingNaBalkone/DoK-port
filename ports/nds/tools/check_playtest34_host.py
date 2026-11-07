"""N23 playtest fixes: fresh host evidence without historical artifact gates."""
import json,os,sys
from pathlib import Path
from check_combat import SOURCES
from check_interactions import FLAGS
from check_items import ROOT,run

def main():
    out=ROOT/'build/nds/n23/playtest34-host';out.mkdir(parents=True,exist_ok=True)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    report={}
    for name,extra in [('test_combat',['combat_cases.c']),('test_ai_combat',['ai_combat_cases.c']),('test_playtest34_hits',[])]:
        command=[zig,'cc',*FLAGS,*SOURCES,'ports/nds/core/ai.c',*[f'ports/nds/tests/{p}' for p in extra],f'ports/nds/tests/{name}.c','-o',out/(name+'.exe')]
        run(command,env=env)
        report[name]=json.loads(run([out/(name+'.exe'),ROOT/'build/nds/assets/dok-assets.ndspak']))
        print(name,report[name],flush=True)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    return report
if __name__=='__main__':main()
