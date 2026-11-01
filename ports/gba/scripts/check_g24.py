"""G24 limited span-cache native A/B; no production selection."""
from pathlib import Path
import json, os, subprocess, sys
from check_g22 import ROOT, CASES, live
from verify_gba import sha

CONTROL=ROOT/'build/gba/g24/control/hardware/g22_split_active'
TRIAL=ROOT/'build/gba/g24/trial/g24_span_cache'
OUT=ROOT/'research/evidence/gba-g24'

def main():
    global TRIAL,OUT
    top='--top' in sys.argv
    if top:TRIAL=ROOT/'build/gba/g24/top/g24_span_cache_top';OUT=OUT/'top'
    os.chdir(ROOT);OUT.mkdir(parents=True,exist_ok=True)
    assert sha(CONTROL/'dok-diagnostic.gba')=='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
    commands=json.loads((OUT/'commands.json').read_text()) if (OUT/'commands.json').exists() else []
    def run(name,script,build=TRIAL,*args):
        cmd=[sys.executable,'-X','utf8',str(ROOT/'ports/gba/scripts'/script),'--build-dir',str(build),'--report',str(OUT/(name+'.json')),*map(str,args)]
        commands.append(cmd);(OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        with (ROOT/'build/gba/g24'/(name+'.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
        print(name,'passed',flush=True)
    if '--timing-only' not in sys.argv:
        run('ppu','benchmark_gba_hardware.py',TRIAL,'--reference-build',CONTROL,'--details','--languages',0,1)
        run('cache-mutations','verify_gba_static_cache.py',TRIAL,'--reference-build',CONTROL)
        run('water','verify_gba_hardware_water.py')
        run('transitions','verify_gba_hardware_transitions.py',TRIAL,'--mode4-transitions')
    cases=CASES+['2:3039:90','3:1623:0','3:1940:270']
    for name,build in ([('trial',TRIAL)] if top else [('control',CONTROL),('trial',TRIAL)]):
        for lang in (0,1):
            for mode,extra in [('warm',()),('cold',('--cold-geometry',))]:
                run(f'{name}-{mode}-{lang}','benchmark_gba_spatial_sweep.py',build,'--samples',30,'--language',lang,'--cases',*cases,*extra)
        if '--timing-only' in sys.argv:continue
        run(name+'-battles','benchmark_battles_gba.py',build,'--samples',120,'--fire')
        (OUT/(name+'-live.json')).write_text(json.dumps(live(build),indent=2)+'\n')
        print(name,'live passed',flush=True)
if __name__=='__main__':main()
