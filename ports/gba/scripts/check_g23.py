"""Reproduce G23 attribution, separate production timing and sampling probes.

Build first with build_gba_frontend and prototype_gba_bsp as documented in G23.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
from check_g22 import live, CASES, ROOT
from verify_gba import sha


def main():
    os.chdir(ROOT)
    out=ROOT/'research/evidence/gba-g23';out.mkdir(parents=True,exist_ok=True)
    build=ROOT/'build/gba/g23/control/hardware/g22_split_active'
    profile=ROOT/'build/gba/g23/profile/g23_kernel_profile'
    assert sha(build/'dok-diagnostic.gba')=='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
    cases=CASES+['2:3039:90','3:1623:0','3:1940:270']
    commands=[]
    def run(label,script,b=build,*extra):
        cmd=[sys.executable,'-X','utf8',str(ROOT/'ports/gba/scripts'/script),'--build-dir',str(b),
             '--report',str(out/(label+'.json')),*map(str,extra)]
        commands.append(cmd)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        with (ROOT/'build/gba/g23'/(label+'.log')).open('w') as log:
            subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(label,'passed',flush=True)
    for mode,flags in [('warm',()),('cold',('--cold',)),('moving',('--moving',)),('battle',('--battles',))]:
        run(mode+'-work','audit_g23.py',build,*flags)
    for lang in (0,1):
        for mode,b,flags in [('warm',build,()),('cold',build,('--cold-geometry',)),('profile',profile,('--profile',))]:
            run(f'{mode}-{lang}','benchmark_gba_spatial_sweep.py',b,'--samples',12,'--language',lang,'--cases',*cases,*flags)
    run('profile-ppu','benchmark_gba_hardware.py',profile,'--reference-build',build,'--languages',0,1)
    run('battles','benchmark_battles_gba.py',build,'--samples',120,'--fire')
    (out/'live.json').write_text(json.dumps(live(build),indent=2)+'\n')
    print('live passed',flush=True)


if __name__=='__main__':main()
