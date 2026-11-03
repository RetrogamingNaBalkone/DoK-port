"""Bounded G22 trials; frozen CPU and equal-duration live input are separate."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from research_gba_fps import GameProbe, freeze
from verify_gba import sha
from verify_sprites_gba import load_json
import struct
from verify_audio_gba import sounds

BUILDS={'control':'control/g20d_compact','counts':'trials/g22_counts',
        'edges':'trials/g22_edges','split':'split/g22_split','rows96':'rows-split/g22_rows96_split',
        'active':'active/g22_split_active','sparse_control':'sparse/g22_sparse_control','sparse':'sparse/g22_sparse'}
CASES=['1:520:225','1:3640:225','1:2242:90','2:1592:135','2:2871:225','3:1544:315']

def live(build,frames=600):
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    rows=[]
    for language in (0,1):
        for level in range(3):
            p=GameProbe(core,build)
            try:
                p.until(lambda d:d['draws']>=4)
                freeze(p,True)
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
                p.language(language)
                h=load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json')['objects']
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json')
                h=h[model['player_index']-1]['actor']
                p.camera((level+1)%3,15120,-300,22800,90)
                p.camera(level,h['x'],h['y'],h['z'],h['heading'])
                p.write(p.symbols['test_camera'],b'\0')
                p.command(6);p.command(2,max(sounds()[0],key=lambda r:r['rate'])['id'])
                freeze(p,False)
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',0))
                previous=p.diag();samples=[];checkpoints=[];last_frame=0;latencies=[];pending=None
                start_ticks=p.words('g_time_diag',3)[2]
                for frame in range(frames):
                    # Timestamped in emulated video time, never presentation count.
                    buttons=(4,) if frame<200 else (8,7) if frame<400 else (4,8,7)
                    if frame in (0,200,400):pending=(frame,sum(1<<{4:6,8:0,7:4}[b] for b in buttons))
                    p.frontend.run(1,buttons=buttons)
                    d=p.diag()
                    if pending and d['last_keys']==pending[1]:
                        latencies.append(dict(event_frame=pending[0],observed_after_frames=frame-pending[0]+1));pending=None
                    assert d['guard_ok']==1
                    if d['draws']!=previous['draws']:
                        samples.append(dict(frame=frame,cycles=d['last_cycles'],interval=frame-last_frame))
                        last_frame=frame;previous=d
                    if (frame+1)%100==0:
                        checkpoints.append(dict(frame=frame+1,player={k:d[k] for k in ('player_x','player_y','player_z')},
                            logic=list(p.words('g_hw_completed_logic',8))))
                cycles=sorted(s['cycles'] for s in samples[1:])
                rows.append(dict(level=level+1,language=language,emulated_frames=frames,
                    median=statistics.median(cycles),p95=cycles[int((len(cycles)-1)*.95)],maximum=max(cycles),
                    presented_fps=len(samples)*16777216/(frames*280896),
                    checkpoints=checkpoints,samples=samples,audio=p.audio()))
                rows[-1].update(game_ticks=(p.words('g_time_diag',3)[2]-start_ticks)&0xffffffff,
                    input_observation_latency=latencies,pending_input=pending)
                assert p.audio()['underruns']==0
            finally:p.close()
    return dict(rom_sha256=sha(build/'dok-diagnostic.gba'),core_sha256=sha(core),rows=rows,
        scope='Equal emulated duration and input timestamps. States recorded, not assumed equal; not a renderer-only causal comparison or physical input latency.')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ROOT/'build/gba/g22')
    ap.add_argument('--control-sha',default='f40a13f56b70cfec5e673c7e0b80c659a48f35e30e7bad2e236ef7967a59a359',help='Pin the source generation; UI02 changes the control ROM.')
    ap.add_argument('--variants',nargs='+',choices=BUILDS,default=list(BUILDS))
    ap.add_argument('--stage',choices=['build','heavy','ppu','live','spatial','cold','qualify','all'],default='all')
    a=ap.parse_args();os.chdir(ROOT);out=a.out.resolve();os.environ['DOK_PROBE_FAST_VIDEO']='1'
    out.mkdir(parents=True,exist_ok=True)
    commands=[]
    def run(label,script,*extra):
        argv=[sys.executable,'-X','utf8',str(ROOT/'ports/gba/scripts'/script),*map(str,extra)]
        commands.append(argv)
        (out/f'commands-{a.stage}.json').write_text(json.dumps(commands,indent=2)+'\n')
        with (out/(label+'.log')).open('w') as log:subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(label,'done',flush=True)
    if a.stage=='build':
        run('base-control','build_gba_frontend.py','--out',out/'base-control','--keep-base-objects')
        groups={}
        for name in a.variants:
            relative=Path(BUILDS[name]);groups.setdefault(relative.parent,[]).append(relative.name)
        for folder,names in groups.items():
            run('build-'+str(folder),'prototype_gba_bsp.py','--base',out/'base-control/base','--out',out/folder,'--variants',*names)
        return
    assert sha(out/BUILDS['control']/'dok-diagnostic.gba')==a.control_sha
    if a.stage=='qualify':
        run('verified','build_gba_frontend.py','--out',out/'verified','--variant','g22_split_active','--verify','--keep-base-objects')
        run('repeat','prototype_gba_bsp.py','--base',out/'verified/base','--out',out/'repeat','--variants','g22_split_active')
        from check_g21 import EXTRA
        for name,script,extra in [*EXTRA,('cache','verify_gba_static_cache.py',('reference',)),
                ('mode4','verify_gba_hardware_transitions.py',('--mode4-transitions',)),
                ('profile-game','profile_game_gba.py',()),('battles','benchmark_battles_gba.py',('--samples','120','--fire'))]:
            argv=[]
            for v in extra:argv.extend(['--reference-build',str(out/BUILDS['control'])] if v=='reference' else [v])
            run('active-'+name,script,'--build-dir',out/BUILDS['active'],'--report',out/f'active-{name}.json',*argv)
        run('active-detail-ppu','benchmark_gba_hardware.py','--build-dir',out/BUILDS['active'],
            '--reference-build',out/BUILDS['control'],'--report',out/'active-detail-ppu.json','--details','--languages',0,1)
        for name in ('control','active'):
            run(name+'-work','audit_raster_work.py','--build-dir',out/BUILDS[name],
                '--report',out/f'{name}-work.json','--cases',*CASES)
            run(name+'-heavy-repeat','benchmark_gba_spatial_sweep.py','--build-dir',out/BUILDS[name],
                '--report',out/f'{name}-heavy-repeat.json','--samples',30,'--cases',*CASES)
        from benchmark_gba_hardware import capture
        core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
        cameras=[]
        for lang in (0,1):
            for r in json.loads((out/f'control-spatial-{lang}.json').read_text())['rows']:
                cameras.append(dict(name=f"L{r['level']}-c{r['cell']}-h{r['heading']}-lang{lang}",
                    level=r['level']-1,language=lang,camera=[r['cell']%64*480+240,-300,r['cell']//64*480+240,r['heading']]))
        before,bi=capture(out/BUILDS['control'],core,cameras,save_images=False)
        after,ai=capture(out/BUILDS['active'],core,cameras,save_images=False)
        assert bi==ai
        for b,c in zip(before['rows'],after['rows']):
            for key in ('rgb_sha256','coverage_sha256','lease_sha256'):assert b[key]==c[key]
        (out/'active-spatial-ppu.json').write_text(json.dumps(dict(status='passed',frames=len(cameras),before=before,after=after),indent=2)+'\n')
        return
    for name in a.variants:
        build=out/BUILDS[name]
        if a.stage=='spatial':
            for lang in (0,1):
                run(f'{name}-spatial-{lang}','benchmark_gba_spatial_sweep.py','--build-dir',build,
                    '--report',out/f'{name}-spatial-{lang}.json','--language',lang)
            continue
        if a.stage=='cold':
            run(name+'-cold','benchmark_gba_spatial_sweep.py','--build-dir',build,'--report',out/f'{name}-cold.json',
                '--cold-geometry','--samples',12,'--cases',*CASES)
            continue
        for stage in ('heavy','ppu','live') if a.stage=='all' else (a.stage,):
            report=out/f'{name}-{stage}.json'
            if stage=='live':report.write_text(json.dumps(live(build),indent=2)+'\n');continue
            script='benchmark_gba_spatial_sweep.py' if stage=='heavy' else 'benchmark_gba_hardware.py'
            extra=['--samples','30','--cases',*CASES] if stage=='heavy' else ['--reference-build',str(out/BUILDS['control']),'--languages','0','1']
            argv=[sys.executable,'-X','utf8',str(ROOT/'ports/gba/scripts'/script),'--build-dir',str(build),'--report',str(report),*extra]
            with (out/f'{name}-{stage}.log').open('w') as log:subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,check=True)
            print(name,stage,'done',flush=True)

if __name__=='__main__':main()
