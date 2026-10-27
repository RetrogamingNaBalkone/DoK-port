"""Check every actual PPU video frame across held camera changes for tearing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha,png

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--mode4-transitions',action='store_true',help='Also retain every callback while opening and closing Pause.')
    args=parser.parse_args()
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,args.build_dir);frames=[];rows=[]
    def video(data,width,height,pitch):
        p.frontend.video(data,width,height,pitch)
        if data:frames.append(p.frontend.deferred_video)
    try:
        p.video_callback=VIDEO(video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        def ui_command(op,arg=0):
            previous=p.diag()['draws']
            request=p.words('g_ui_probe',2)[1]+1
            p.write(p.symbols['g_ui_probe']+8,struct.pack('<4I',op,arg,0,0));p.write(p.symbols['g_ui_probe'],struct.pack('<I',request))
            p.until(lambda d:p.words('g_ui_probe',2)[1]==request and d['phase']==2 and d['draws']>previous)
        def check(name,operation):
            # Video settling can leave the CPU halfway through the next draw.
            # Finish prior mutations before defining the old image, then hold
            # a completed draw before changing fields without an ack mailbox.
            p.present();p.present();p.frontend.run(2)
            old=frames[-1];frames.clear();before=list(p.words('g_hw_bg_diag',8))
            p.present();operation();p.present();p.present();p.frontend.run(2);new=frames[-1]
            assert len(old)==len(new)==240*160*(4 if p.frontend.pixel_format==1 else 2)
            mixed=[n for n,frame in enumerate(frames) if frame!=old and frame!=new]
            after=list(p.words('g_hw_bg_diag',8))
            row=dict(level=level+1,language=language,action=name,video_frames=len(frames),mixed_frames=mixed,
                     cold_loads=after[1]-before[1],overflows=after[4]-before[4],
                     old_sha256=hashlib.sha256(old).hexdigest(),new_sha256=hashlib.sha256(new).hexdigest())
            rows.append(row)
            if mixed:
                bpp=4 if p.frontend.pixel_format==1 else 2
                row['mixed_pixel_differences']=[dict(frame=n,
                    old=sum(frames[n][i:i+bpp]!=old[i:i+bpp] for i in range(0,len(old),bpp)),
                    new=sum(frames[n][i:i+bpp]!=new[i:i+bpp] for i in range(0,len(new),bpp))) for n in mixed]
                for label,raw in [('old',old),('new',new),('mixed',frames[mixed[0]])]:
                    (args.build_dir/f'transition-{language}-{level}-{name}-{label}.video').write_bytes(raw)
                p.frontend.decode_video(frames[mixed[0]])
                png(args.build_dir/f'transition-{language}-{level}-{name}.png',240,160,p.frontend.rgb)
        prior=json.loads((ROOT/'analysis/g20a-fps-audit.json').read_text())['results']['baseline']['rows']
        for language in (0,1):
            p.language(language)
            for level in range(3):
                pose=next(r['camera'] for r in prior if r['level']==level and 'median' in r['name'])
                p.camera(level,*pose);p.write(p.symbols['g_ui']+52,struct.pack('<I',0));p.write(p.symbols['g_ui']+80,struct.pack('<I',0))
                p.present();p.frontend.run(2)
                for angle in (0,45,90,135,180,225,270,315):
                    check(f'heading-{angle}',lambda:p.camera(level,*pose[:3],angle))
                for slot,count in ((1,9),(2,3)):
                    p.control(1,slot,1000);p.present();p.frontend.run(2)
                    for frame in range(count):
                        for shift in range(3):
                            def change_weapon():
                                p.write(p.symbols['g_combat']+10798,struct.pack('<HH',shift,frame));p.present()
                            check(f'weapon-{slot}-{frame}-{shift}',change_weapon)
                for length in (1,61,0):check(f'message-{length}',lambda:ui_command(4,length))
                if args.mode4_transitions:
                    check('pause-mode4',lambda:ui_command(1,3))
                    check('resume-hardware',lambda:ui_command(1,0))
        result=dict(rom_sha256=sha(p.rom),core_sha256=sha(core),script_sha256=sha(__file__),rows=rows,
                    status='passed' if all(not r['mixed_frames'] and not r['overflows'] for r in rows) else 'failed',
                    mode4_transitions=args.mode4_transitions,
                    scope=f'Every PPU callback for{len(rows)} held camera/weapon/message/Pause changes in six world/language views; not continuous gameplay or physical GBA.')
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(result['status'],len(rows),sum(r['video_frames'] for r in rows),'frames',sum(len(r['mixed_frames']) for r in rows),'mixed')
        assert result['status']=='passed'
    finally:p.close()

if __name__=='__main__':main()
