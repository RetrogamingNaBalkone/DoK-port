"""N16 host/ARM-ready movement gates; N15 inputs are preserved."""
import json,os,sys
from pathlib import Path
from check_items import ROOT,artifact,run
from check_combat import SOURCES
from check_interactions import FLAGS
OUT=ROOT/'build/nds/n16';EVIDENCE=ROOT/'research/evidence/nds/n16'
def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';report=dict(task='N16',passed=False,tests={},commands=[])
    for name in ('test_ai','ai_dump','test_combat','combat_dump'):
        extra=['ports/nds/tests/ai_cases.c'] if name=='test_ai' else ['ports/nds/tests/combat_cases.c'] if name=='test_combat' else []
        cmd=[zig,'cc',*FLAGS,*SOURCES,'ports/nds/core/ai.c',*extra,f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')];run(cmd,env=env);report['commands'].append(list(map(str,cmd)))
        if name.startswith('test_'):report['tests'][name]=json.loads(run([OUT/(name+'.exe'),ROOT/'build/nds/assets/dok-assets.ndspak']))
    cmd=[zig,'cc',*FLAGS,'-target','x86_64-linux-musl','-static',*SOURCES,'ports/nds/core/ai.c','ports/nds/tests/ai_cases.c','ports/nds/tests/test_ai.c','-o',OUT/'test_ai-linux']
    run(cmd,env=env);report['commands'].append(list(map(str,cmd)));report['linux_execution']=False
    report['native']=json.loads(run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_ai.py']))
    report['sources']=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc','.py')];report['passed']=True
    (EVIDENCE/'host-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
