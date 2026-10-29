"""Build NDS gameplay gates with the pinned shared SDK, no installation."""
import json,os,shutil,sys
from pathlib import Path
from check_items import ROOT,artifact,run
def build(task):
    out=ROOT/f'build/nds/{task}/arm9';out.mkdir(parents=True,exist_ok=True);(out/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',out/'generated/render_trig.inc')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile."+task+'\n'
    log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    (out/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(k+' '+v in log for k,v in lock['packages'].items())
    report=dict(task=task,packages=lock['packages'],artifacts=[artifact(p) for p in out.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc')])
    (out/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['artifacts']))
if __name__=='__main__':build(sys.argv[1])
