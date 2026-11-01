"""G25: heavy-first paired CPU, presentation and quality evidence."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from check_g22 import ROOT, CASES, live
from verify_gba import sha

PIN='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
BUILDS={
    'control': ROOT/'build/gba/g25/control/hardware/g22_split_active',
    'rows96': ROOT/'build/gba/g25/rows96/g25_rows96',
    'lod': ROOT/'build/gba/g25/lod-iwram/g25_ceiling_lod',
}
OUT=ROOT/'research/evidence/gba-g25'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stage',choices=('warm','cold','spatial','tail','battles','live','ppu','qualify'),required=True)
    ap.add_argument('--variants',nargs='+',choices=BUILDS,default=list(BUILDS))
    ap.add_argument('--checks',nargs='+',help='Restrict qualification to named checks after a harness correction.')
    a=ap.parse_args();os.chdir(ROOT);OUT.mkdir(parents=True,exist_ok=True)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    assert sha(BUILDS['control']/'dok-diagnostic.gba')==PIN
    command_file=OUT/f'commands-{a.stage}.json'
    commands=json.loads(command_file.read_text()) if command_file.exists() else []
    def run(name,script,*args):
        cmd=[sys.executable,'-X','utf8',str(ROOT/'ports/gba/scripts'/script),*map(str,args)]
        commands.append(cmd)
        command_file.write_text(json.dumps(commands,indent=2)+'\n')
        with (ROOT/'build/gba/g25'/f'{name}.log').open('w') as f:
            subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
        print(name,'passed',flush=True)
    cases=CASES+['2:3039:90','3:1623:0','3:1940:270']
    if a.stage=='qualify':
        from check_g21 import EXTRA
        build=BUILDS['rows96']
        for name,script,extra in [*EXTRA,
                ('cache','verify_gba_static_cache.py',('reference',)),
                ('profile-game','profile_game_gba.py',())]:
            if a.checks and name not in a.checks:continue
            argv=[]
            # Static-cache RGB is compared within the new sampling layout.
            # Collision traces retain the production control as reference.
            reference=build if name=='cache' else BUILDS['control']
            for v in extra:
                argv.extend(['--reference-build',str(reference)] if v=='reference' else [v])
            run('qualify-'+name,script,'--build-dir',build,'--report',OUT/f'qualify-{name}.json',*argv)
        return
    for name in a.variants:
        build=BUILDS[name]
        if a.stage in ('warm','cold','spatial','tail'):
            for lang in (0,1):
                if a.stage=='tail' and lang:continue
                extra=[] if a.stage=='spatial' else ['--samples',30,'--cases',*cases]
                if a.stage=='tail':
                    extra=['--samples',30,'--cases','2:1592:45','2:1907:225','2:1907:315','2:2552:45','2:3640:135','1:520:315']
                if a.stage=='cold':extra+=['--cold-geometry']
                run(f'{name}-{a.stage}-{lang}','benchmark_gba_spatial_sweep.py',
                    '--build-dir',build,'--report',OUT/(f'{name}-tail.json' if a.stage=='tail' else f'{name}-{a.stage}-{lang}.json'),
                    '--language',lang,*extra)
        elif a.stage=='battles':
            run(name+'-battles','benchmark_battles_gba.py','--build-dir',build,
                '--report',OUT/f'{name}-battles.json','--samples',120,'--fire')
        elif a.stage=='live':
            (OUT/f'{name}-live.json').write_text(json.dumps(live(build),indent=2)+'\n')
            print(name,'live passed',flush=True)
        elif a.stage=='ppu' and name!='control':
            run(name+'-ppu','benchmark_gba_hardware.py','--build-dir',build,
                '--reference-build',BUILDS['control'],'--languages',0,1,
                '--report',OUT/f'{name}-ppu.json')

if __name__=='__main__':main()
