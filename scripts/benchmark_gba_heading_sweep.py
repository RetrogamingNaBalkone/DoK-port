"""Unheld presentation timing across fixed start headings, with continuous PCM."""
import argparse
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe, ROOT, freeze, collect
from verify_audio_gba import sounds
from verify_sprites_gba import load_json
from verify_gba import sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--heading-step',type=int,default=15)
    parser.add_argument('--samples',type=int,default=6)
    args=parser.parse_args();assert 1<=args.heading_step<=90 and args.samples>=6
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,args.build_dir);rows=[]
    try:
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        rate=max(sounds()[0],key=lambda r:r['rate'])
        for level in range(3):
            p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
            model=load_json(f'build/runtime/ru/LEVEL{level+1}-runtime.json')
            hero=model['objects'][model['player_index']-1]['actor']
            p.camera((level+1)%3,15120,-300,22800,90)
            p.camera(level,hero['x'],hero['y'],hero['z'],hero['heading'])
            p.command(6);p.command(2,rate['id'])
            for heading in range(0,360,args.heading_step):
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
                p.camera(level,hero['x'],hero['y'],hero['z'],heading)
                collect(p,2)
                r=collect(p,args.samples)
                assert r['audio']['underruns']==0 and r['sound_active_frames']==args.samples
                assert all(s['flow_state']==2 and s['ui_state']==0 for s in r['samples'])
                r.update(level=level+1,language=0,heading=heading,position=[hero[k] for k in ('x','y','z')])
                rows.append(r)
            print('LEVEL',level+1,'min FPS',min(r['presented_fps'] for r in rows if r['level']==level+1),flush=True)
        result=dict(rom_sha256=sha(p.rom),core_sha256=sha(core),script_sha256=sha(__file__),
            capture_hold_during_samples=False,simulation_frozen=True,heading_step=args.heading_step,rows=rows,
            scope='Frozen world at original player start; every selected heading measured separately, PCM on. This is a renderer floor and is not a live simulation or whole-level minimum.')
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    finally:p.close()


if __name__=='__main__':main()
