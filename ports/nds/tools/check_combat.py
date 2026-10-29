"""N15 host and compatibility gates; source instruction oracle is separate."""
import json,os,sys
from pathlib import Path
from check_items import ROOT,artifact,run
from check_interactions import CORE,FLAGS
OUT=ROOT/'build/nds/n15';EVIDENCE=ROOT/'research/evidence/nds/n15'
SOURCES=[*CORE,'ports/nds/core/items.c','ports/nds/core/combat.c','ports/nds/render/sprites.c','ports/nds/render/sprite_assets.c','ports/nds/render/weapon_overlay.c']
def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';report=dict(task='N15',passed=False,commands=[],tests={})
    for name in ('test_combat','combat_dump','collision_dump','test_items','test_collision','test_collision_routes'):
        extra=['ports/nds/tests/combat_cases.c'] if name=='test_combat' else ['ports/nds/tests/items_cases.c'] if name=='test_items' else ['ports/nds/tests/collision_cases.c'] if name=='test_collision' else ['ports/nds/tests/collision_routes.c'] if name=='test_collision_routes' else []
        cmd=[zig,'cc',*FLAGS,*SOURCES,*extra,f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')];run(cmd,env=env);report['commands'].append(list(map(str,cmd)))
        if name.startswith('test_'):
            report['tests'][name]=json.loads(run([OUT/(name+'.exe')]+([ROOT/'build/nds/assets/dok-assets.ndspak'] if name in ('test_combat','test_items','test_collision_routes') else [])))
    cmd=[zig,'cc',*FLAGS,'-target','x86_64-linux-musl','-static',*SOURCES,'ports/nds/tests/combat_cases.c','ports/nds/tests/test_combat.c','-o',OUT/'test_combat-linux']
    run(cmd,env=env);report['commands'].append(list(map(str,cmd)));report['linux_execution']=False
    report['native']=json.loads(run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_combat.py']))
    old=EVIDENCE/'original-collision.json'
    collision=json.loads(old.read_text()) if old.exists() else {}
    if collision.get('probe',{}).get('sha256')!=artifact(OUT/'collision_dump.exe')['sha256']:
        run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_combat_collision.py','--probe',OUT/'collision_dump.exe','--output',old])
    report['collision_oracle']=artifact(old)
    report['sources']=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and p.suffix in ('.c','.h','.py','.inc')]
    report['passed']=True;(EVIDENCE/'host-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
