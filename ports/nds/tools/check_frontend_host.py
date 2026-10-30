"""Build and execute portable N21 controller/settings checks (no emulator)."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n21/host'
EVIDENCE=ROOT/'research/evidence/nds/n21'

def artifact(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def run(args,**kwargs):
    result=subprocess.run([str(arg) for arg in args],cwd=ROOT,text=True,encoding='utf8',
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,**kwargs)
    if result.returncode:raise RuntimeError(result.stdout)
    return result.stdout

def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),
             ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    common=[zig,'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-O2',
            '-I','ports/nds/include','ports/nds/ui/frontend.c','ports/nds/tests/test_frontend.c']
    run([*common,'-o',OUT/'frontend.exe'],env=env)
    stdout=run([OUT/'frontend.exe']);print(stdout.strip())
    run([*common,'-target','x86_64-linux-musl','-static','-o',OUT/'frontend-linux'],env=env)
    checks=int(stdout.split('checks=')[1].split()[0]);size=int(stdout.split('state_bytes=')[1].split()[0])
    report=dict(task='N21',passed=True,checks=checks,state_bytes=size,linux_execution=False,
        compiler=run([zig,'version']).strip(),scope=[
            'All 4095 native key combinations plus touch release gate; held edges and title loading latch',
            'Both languages, all three levels, all eight page sets, intro/pause/back origins and fixture outro/ending',
            'All 256 encode inputs, all 128 single-bit corruptions per encoded record, missing/truncated/extra data'],
        artifacts=[artifact(OUT/'frontend.exe'),artifact(OUT/'frontend-linux')],
        sources=[artifact(ROOT/path) for path in ('ports/nds/include/dok/frontend.h',
            'ports/nds/ui/frontend.c','ports/nds/tests/test_frontend.c','ports/nds/tools/check_frontend_host.py')])
    (EVIDENCE/'host-state-settings.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    return report

if __name__=='__main__':main()
