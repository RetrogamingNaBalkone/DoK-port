"""Compare PERF06 visual compromises separately and together against PERF04."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trials',type=Path,default=Path('build/gba/perf06/fused'))
    ap.add_argument('--out',type=Path,default=Path('research/evidence/gba-perf06'))
    ap.add_argument('--stage',choices=['spatial','quality','live','checks','summary'],required=True)
    a=ap.parse_args();os.chdir(ROOT);a.out.mkdir(parents=True,exist_ok=True)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    builds={'base':Path('build/gba/perf04/final/hardware/perf04_full8'),
            **{n:a.trials/n for n in ['perf06_ceiling','perf06_fences','perf06_both']}}
    commands=[]
    def run(label,script,*args):
        argv=[sys.executable,'-X','utf8','ports/gba/scripts/'+script,*map(str,args)]
        commands.append(argv)
        with (a.out/(label+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(label,'passed',flush=True)
    if a.stage=='spatial':
        cells=[(50,41),(51,41),(55,41),(50,42),(14,48),(14,47),(14,49),(15,49)]
        cases=[f'2:{z*64+x}:{h}' for x,z in cells for h in range(0,360,45)]
        for name,build in builds.items():
            for lang in (0,1):
                run(f'{name}-{lang}','benchmark_gba_spatial_sweep.py','--build-dir',build,
                    '--report',a.out/f'{name}-{lang}.json','--language',lang,'--samples',30,'--verify-output','--cases',*cases)
    if a.stage=='quality':
        from benchmark_gba_hardware import capture
        core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
        cameras=[]
        for lang in (0,1):
            for x,z,h in [(50,41,270),(51,41,315),(54,41,270),(55,41,90),(14,48,135)]:
                # Short camera movement through the LOD threshold, with unchanged world.
                for step in range(5):
                    cameras.append(dict(name=f'perf06-{lang}-{x}-{z}-{h}-{step}',language=lang,level=1,
                        camera=[x*480+240+step*60,-300,z*480+240,h]))
        results={}
        for name,build in builds.items():
            with (a.out/(name+'-quality.log')).open('w',encoding='utf-8') as log,contextlib.redirect_stdout(log):
                result,images=capture(build,core,cameras,save_images=True)
            results[name]=result
            (a.out/(name+'-quality.json')).write_text(json.dumps(result,indent=2)+'\n')
            print(name,'quality',len(images),flush=True)
        for name,result in results.items():
            for base,row in zip(results['base']['rows'],result['rows'],strict=True):
                assert base['lease_sha256']==row['lease_sha256'],(name,row['name'],'leases')
                if name=='perf06_ceiling':assert base['coverage_sha256']==row['coverage_sha256']
    if a.stage=='live':
        import benchmark_battles_gba as battles
        core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
        battles.CASES=((2,50,41,270,'ammo-entrance'),(2,51,41,315,'ammo-diagonal'),(2,14,48,90,'copper'))
        for name,build in builds.items():
            for fire in (False,True):
                with (a.out/f'{name}-live-{int(fire)}.log').open('w',encoding='utf-8') as log,contextlib.redirect_stdout(log):
                    result=battles.measure(build,core,120,(0,1),fire)
                (a.out/f'{name}-live-{int(fire)}.json').write_text(json.dumps(result,indent=2)+'\n')
                print(name,'live',fire,flush=True)
    if a.stage=='checks':
        build=builds['perf06_both']
        for name in ['frontend','time','audio','flow','sprites']:
            run(name,'verify_'+name+'_gba.py','--build-dir',build,'--report',a.out/(name+'.json'))
        run('transitions','verify_gba_hardware_transitions.py','--build-dir',build,'--report',a.out/'transitions.json','--mode4-transitions')
        run('kernels','verify_perf06_arm.py','--build-dir',build,'--report',a.out/'arm.json')
    if a.stage=='summary':
        rows=[]
        for lang in (0,1):
            base=json.loads((a.out/f'base-{lang}.json').read_text())['rows']
            for name in list(builds)[1:]:
                candidate=json.loads((a.out/f'{name}-{lang}.json').read_text())['rows']
                for b,c in zip(base,candidate,strict=True):
                    assert (b['cell'],b['heading'])==(c['cell'],c['heading'])
                    if name=='perf06_ceiling':assert b['coverage_sha256']==c['coverage_sha256']
                    rows.append(dict(variant=name,language=lang,cell=b['cell'],heading=b['heading'],
                        cpu_change=100*(c['median']/b['median']-1),fps_before=b['presented_fps'],fps_after=c['presented_fps'],
                        image_changed=b['completed_ppu_sha256']!=c['completed_ppu_sha256']))
        result=dict(rows=rows,resolution=[120,128],threshold=1440,
            sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),Path('ports/gba/src/renderer_far_lod.inc'),Path('ports/gba/platform/perf06_flat.S'),Path('ports/gba/scripts/gba_perf06_assets.py')]})
        (a.out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    if commands:(a.out/('commands-'+a.stage+'.json')).write_text(json.dumps(commands,indent=2)+'\n')


if __name__=='__main__':main()
