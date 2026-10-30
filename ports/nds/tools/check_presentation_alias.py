"""N19 RU effective-model alias and preserved N05 gates; N05 reports untouched."""
from __future__ import annotations
import hashlib,json,os,subprocess,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n19/presentation'
EVIDENCE=ROOT/'research/evidence/nds/n19'
COMMON=['ports/nds/core/'+name+'.c' for name in ('assets','sha256','world','balance')]
FLAGS=['-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-O2','-I','ports/nds/include','-I','ports/nds/platform/assets']

def artifact(path):
    path=Path(path);data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    package=ROOT/'build/nds/assets/dok-assets.ndspak'
    watched=[*COMMON,'ports/nds/include/dok/balance.h','ports/nds/include/dok/world.h',
        'ports/nds/tests/test_presentation_alias.c','ports/nds/tests/test_balance.c','ports/nds/tests/test_world.c',
        'ports/nds/platform/assets/file_reader.c','ports/nds/tools/check_presentation_alias.py']
    pins=[artifact(ROOT/p) for p in watched]+[artifact(package)]
    old_reports=[artifact(p) for p in sorted((ROOT/'research/evidence/nds/n05').glob('*.json'))]
    report=dict(task='N19',passed=False,scope='Windows host alias bind and original N05 regression; no ARM9/DOS execution',commands=[])
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    def run(args):
        r=subprocess.run(list(map(str,args)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8')
        report['commands'].append(dict(argv=list(map(str,args)),exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr))
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout
    for name in ('test_presentation_alias','test_balance','test_world'):
        sources=[*COMMON,f'ports/nds/tests/{name}.c']
        if name=='test_presentation_alias':sources+=['ports/nds/platform/assets/file_reader.c']
        run([zig,'cc',*FLAGS,*sources,'-o',OUT/(name+'.exe')])
        output=run([OUT/(name+'.exe'),*([package] if name!='test_world' else [])])
        report[name]=json.loads(output) if name!='test_world' else dict(passed=True,stdout=output.strip())
    assert [artifact(ROOT/p) for p in watched]+[artifact(package)]==pins,'Inputs changed during check'
    assert [artifact(p) for p in sorted((ROOT/'research/evidence/nds/n05').glob('*.json'))]==old_reports,'N05 report changed'
    report['sources_and_package']=pins;report['n05_reports_unchanged']=True
    report['accepted_pre_n19_balance_sources']=[artifact(EVIDENCE/'initial'/p) for p in (
        'ports/nds/core/balance.c','ports/nds/include/dok/balance.h')]
    report['artifacts']=[artifact(p) for p in sorted(OUT.glob('*.exe'))]
    report['passed']=True
    target=EVIDENCE/'presentation-check.json';target.write_text(json.dumps(report,indent=2)+'\n','utf8')
    summary={k:v for k,v in report.items() if k not in ('commands','sources_and_package','artifacts','accepted_pre_n19_balance_sources')}
    summary['report']=artifact(target)
    (ROOT/'analysis/nds-n19-presentation.json').write_text(json.dumps(summary,indent=2)+'\n','utf8')
    print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
