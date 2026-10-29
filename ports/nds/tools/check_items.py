"""N14 strict C host/original instruction gate. ARM9 execution is separate."""
from __future__ import annotations
import hashlib,json,os,subprocess,sys
from pathlib import Path
from check_interactions import CORE,FLAGS
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n14';EVIDENCE=ROOT/'research/evidence/nds/n14'
def artifact(p):
    p=Path(p);return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def run(args,**kw):
    r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',**kw)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    return r.stdout
def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';report=dict(task='N14',passed=False,commands=[])
    for name in ('test_items','items_dump'):
        sources=[*CORE,'ports/nds/core/items.c',f'ports/nds/tests/{name}.c']
        if name=='test_items':sources+=['ports/nds/tests/items_cases.c']
        cmd=[zig,'cc',*FLAGS,*sources,'-o',OUT/(name+'.exe')];run(cmd,env=env);report['commands'].append(list(map(str,cmd)))
        if name=='test_items':report['host']=json.loads(run([OUT/(name+'.exe'),ROOT/'build/nds/assets/dok-assets.ndspak']))
    cmd=[zig,'cc',*FLAGS,'-target','x86_64-linux-musl','-static',*CORE,'ports/nds/core/items.c','ports/nds/tests/test_items.c','ports/nds/tests/items_cases.c','-o',OUT/'test_items-linux']
    run(cmd,env=env);report['commands'].append(list(map(str,cmd)))
    report['native']=json.loads(run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_items.py']))
    report['sources']=[artifact(ROOT/p) for p in (*CORE,'ports/nds/core/items.c','ports/nds/include/dok/items.h','ports/nds/tests/items_cases.c','ports/nds/tests/items_cases.h','ports/nds/tests/test_items.c','ports/nds/tests/items_dump.c','ports/nds/tools/check_items.py','ports/nds/tools/audit_items.py')]
    report['passed']=True;report['linux_execution']=False
    (EVIDENCE/'host-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('sources','commands')},indent=2))
if __name__=='__main__':main()
