"""G26 fixed-transport cost probes; no adaptive threshold experiment."""
import json, os, subprocess, sys
from pathlib import Path
from check_g22 import ROOT, CASES
from verify_gba import sha

OUT=ROOT/'research/evidence/gba-g26'
CONTROL=ROOT/'build/gba/g26/control/hardware/g22_split_active'

def main():
    os.chdir(ROOT);OUT.mkdir(parents=True,exist_ok=True)
    assert sha(CONTROL/'dok-diagnostic.gba')=='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
    commands=[]
    def run(name,script,build,*args):
        cmd=[sys.executable,'-X','utf8',str(ROOT/'ports/gba/scripts'/script),'--build-dir',str(build),'--report',str(OUT/(name+'.json')),*map(str,args)]
        commands.append(cmd);(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        with (ROOT/'build/gba/g26'/(name+'.log')).open('w') as log:
            subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(name,'passed',flush=True)
    for name in ('control','ui','low'):
        build=CONTROL if name=='control' else ROOT/f'build/gba/g26/probes/g26_{name}_transport'
        for language in (0,1):
            run(f'{name}-warm-{language}','benchmark_gba_spatial_sweep.py',build,'--samples',30,'--language',language,'--cases',*(CASES+['2:3039:90','3:1623:0','3:1940:270']),'--verify-output')
        if name!='control':
            run(name+'-ppu','benchmark_gba_hardware.py',build,'--reference-build',CONTROL,'--languages',0,1)

if __name__=='__main__':main()
