"""N17 gates, always write fresh N17 evidence; never rerun old task drivers."""
import json,os,sys
from pathlib import Path
from check_items import ROOT,artifact,run
from check_combat import SOURCES
from check_interactions import FLAGS
OUT=ROOT/'build/nds/n17';EVIDENCE=ROOT/'research/evidence/nds/n17'
def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';report=dict(task='N17',passed=False,tests={},commands=[])
    preserved=[]
    for name in ('nds-n14-items.json','nds-n15-combat.json','nds-n16-ai-movement.json'):
        accepted=json.loads((ROOT/'analysis'/name).read_text());assert accepted['passed']
        for row in accepted['artifacts']:assert artifact(ROOT/row['path'])['sha256']==row['sha256'],row['path'];preserved.append(row)
    for name in ('test_ai_combat','ai_combat_dump','test_ai','test_combat','test_sprites'):
        extra=['ports/nds/tests/ai_combat_cases.c'] if name=='test_ai_combat' else ['ports/nds/tests/ai_cases.c'] if name=='test_ai' else ['ports/nds/tests/combat_cases.c'] if name=='test_combat' else []
        cmd=[zig,'cc',*FLAGS,*SOURCES,'ports/nds/core/ai.c',*extra,f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')];run(cmd,env=env);report['commands'].append(list(map(str,cmd)))
        if name.startswith('test_'):
            args=[OUT/(name+'.exe')]+([ROOT/'build/nds/assets/dok-assets.ndspak'] if name!='test_sprites' else [])
            if name=='test_ai_combat':args+=[EVIDENCE/'callbacks.jsonl']
            report['tests'][name]=json.loads(run(args))
    cmd=[zig,'cc',*FLAGS,'-target','x86_64-linux-musl','-static',*SOURCES,'ports/nds/core/ai.c','ports/nds/tests/ai_combat_cases.c','ports/nds/tests/test_ai_combat.c','-o',OUT/'test_ai_combat-linux']
    run(cmd,env=env);report['commands'].append(list(map(str,cmd)));report['linux_execution']=False
    report['native']=json.loads(run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_ai_combat.py']))
    report['preserved_artifacts']=len(preserved)
    for row in preserved:assert artifact(ROOT/row['path'])['sha256']==row['sha256'],row['path']
    report['sources']=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc','.py')];report['passed']=True
    (EVIDENCE/'host-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
