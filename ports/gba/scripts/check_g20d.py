"""Reproduce the bounded G20/G20D exact-kernel comparison (no FPS gate).

Shared Zig/mGBA/Python only. Baseline is the accepted BUG01-fixed frontend.
Outputs are private experiments; this script does not replace UI01 or publish.
"""
import argparse
from contextlib import redirect_stdout
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from verify_gba import sha

PIN='a584bb1bcd9defaf636a8a9853ff1d72f376c18c82ea24912ff70c06c061f12c'
CASES=['1:520:225','1:3640:225','1:2242:90','2:1592:135','2:2871:225','3:1544:315']
TRIALS=['hw_v128_vblank_only','g20d_zero','g20d_pair','g20d_pair_only',
        'g20d_masked_cache','g20d_masked_open','g20d_masked_zero','g20d_masked_unroll',
        'g20d_active','g20d_active_hot','g20d_active_thumb','g20d_exact',
        'g20d_pair_open','g20d_compact']
PROFILES=['hw_v128_vblank_only_kernel_profile','g20d_pair_kernel_profile','g20d_compact_kernel_profile']
MEASURE=['hw_v128_vblank_only','g20d_pair','g20d_masked_unroll','g20d_compact']


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=Path('build/gba/g20d/qualification'))
    ap.add_argument('--stage',choices=('build','measure','checks','all'),default='all')
    args=ap.parse_args();os.chdir(ROOT)
    args.out.mkdir(parents=True,exist_ok=True)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    from build_cache import compiler_environment
    os.environ.update(compiler_environment(ROOT))
    py=[sys.executable,'-X','utf8'];commands=[]
    def run(name,script,*extra):
        argv=[*py,'ports/gba/scripts/'+script,*map(str,extra)]
        commands.append(dict(name=name,argv=argv))
        with (args.out/(name+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run(argv,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(name,'passed',flush=True)
    def save(name,value):
        (args.out/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    def read(name):return json.loads((args.out/name).read_text(encoding='utf-8-sig'))
    builds=args.out/'hardware'
    selected=builds/'g20d_compact'
    if args.stage in ('all','build'):
        run('clean-control','build_gba_frontend.py','--out',args.out/'clean-control','--keep-base-objects')
        assert sha(args.out/'clean-control/hardware/hw_v128_vblank_only/dok-diagnostic.gba')==PIN
        run('trials','prototype_gba_bsp.py','--base',args.out/'clean-control/base',
            '--out',builds,'--variants',*TRIALS,*PROFILES)
        assert sha(builds/'hw_v128_vblank_only/dok-diagnostic.gba')==PIN
    if args.stage in ('all','measure'):
        for variant in TRIALS:
            run(variant+'-heavy','benchmark_gba_spatial_sweep.py','--build-dir',builds/variant,
                '--report',args.out/(variant+'-heavy.json'),'--samples',30,'--cases',*CASES)
        for variant in PROFILES:
            run(variant,'benchmark_gba_spatial_sweep.py','--build-dir',builds/variant,
                '--report',args.out/(variant+'.json'),'--samples',12,'--profile','--cases',*CASES)
        for variant in MEASURE:
            for language in (0,1):
                name=variant+'-spatial-'+str(language)
                run(name,'benchmark_gba_spatial_sweep.py','--build-dir',builds/variant,
                    '--report',args.out/(name+'.json'),'--samples',6,'--language',language)
            run(variant+'-work','audit_raster_work.py','--build-dir',builds/variant,
                '--report',args.out/(variant+'-work.json'),'--cases',*CASES,'1:520:90','1:3640:90')
            run(variant+'-arm','verify_performance_arm.py','--build-dir',builds/variant,
                '--report',args.out/(variant+'-arm.json'))
        # Actual PPU captures use the normal callback, independent of fast-video
        # spatial timing. Never use its empty RGB buffer as an image oracle.
        for variant in TRIALS[1:]:
            extra=['--details'] if variant in ('g20d_pair','g20d_compact') else []
            run(variant+'-ppu','benchmark_gba_hardware.py','--build-dir',builds/variant,
                '--reference-build',builds/'hw_v128_vblank_only','--languages',0,1,
                '--report',args.out/(variant+'-ppu.json'),*extra)
            assert all(r['different_rgb_pixels']==0 and r['coverage_equal'] for r in read(variant+'-ppu.json')['after']['rows'])
        from research_gba_fps import live_measure
        from benchmark_gba_hardware import capture
        core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
        live={}
        for variant in MEASURE:
            live[variant]={}
            for route,buttons in (('turn-fire',(8,7)),('walk-turn-fire',(4,8,7))):
                live[variant][route]=live_measure(builds/variant,core,follow_camera=True,samples=120,buttons=buttons)
                print('live',variant,route,'passed',flush=True)
        save('live.json',live)
        cameras=[]
        for language in (0,1):
            for r in read('hw_v128_vblank_only-spatial-'+str(language)+'.json')['rows']:
                cameras.append(dict(name=f"L{r['level']}-c{r['cell']}-h{r['heading']}-lang{language}",
                    level=r['level']-1,language=language,
                    camera=[r['cell']%64*480+240,-300,r['cell']//64*480+240,r['heading']]))
        with (args.out/'spatial-ppu.log').open('w',encoding='utf-8') as log, redirect_stdout(log):
            before,images=capture(builds/'hw_v128_vblank_only',core,cameras)
            after,changed=capture(selected,core,cameras)
        assert all(images[r['name']]==changed[r['name']] for r in after['rows'])
        assert [r['coverage_sha256'] for r in before['rows']]==[r['coverage_sha256'] for r in after['rows']]
        save('spatial-ppu.json',dict(status='passed',frames=len(cameras),before=before,after=after))
    if args.stage in ('all','checks'):
        run('frontend-verified','build_gba_frontend.py','--out',args.out/'verified','--variant','g20d_compact','--verify','--keep-base-objects')
        assert sha(args.out/'verified/hardware/g20d_compact/dok-diagnostic.gba')==sha(selected/'dok-diagnostic.gba')
        from check_g21 import EXTRA
        for name,script,extra in EXTRA:
            argv=['--build-dir',selected,'--report',args.out/(name+'.json')]
            for value in extra:
                argv.extend(['--reference-build',str(selected)] if value=='reference' else [value])
            run(name,script,*argv)
        run('repeat','prototype_gba_bsp.py','--base',args.out/'verified/base','--out',args.out/'repeat','--variants','g20d_compact')
        assert sha(args.out/'repeat/g20d_compact/dok-diagnostic.gba')==sha(selected/'dok-diagnostic.gba')
    save('commands-'+args.stage+'.json',commands)


if __name__=='__main__':main()
