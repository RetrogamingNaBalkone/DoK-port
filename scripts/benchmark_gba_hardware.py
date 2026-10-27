"""Compare actual PPU video for an isolated Mode1/OBJ G20A prototype."""
import argparse
import json
import os
from pathlib import Path
import statistics
import struct

from research_gba_fps import GameProbe, ROOT, freeze, live_measure
from verify_gba import sha, VIDEO, scene_source_y


def capture(build,core,cameras,force_low_mode=0):
    p=GameProbe(core,build);rows=[];images={}
    try:
        p.video_callback=VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        if force_low_mode:
            assert 'g_hw_low_force' in p.symbols,'This ROM has no mode-force test mailbox'
            p.write(p.symbols['g_hw_low_force'],struct.pack('<I',force_low_mode))
        # A slow new renderer can advance more AI ticks before the boot draw
        # barrier. Recreate the initial world after freeze for paired images.
        if cameras:p.camera((cameras[0]['level']+1)%3,15120,-300,22800,90)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        def ui_command(op,arg=0):
            request=p.words('g_ui_probe',2)[1]+1
            p.write(p.symbols['g_ui_probe']+8,struct.pack('<4I',op,arg,0,0))
            p.write(p.symbols['g_ui_probe'],struct.pack('<I',request))
            p.until(lambda d:p.words('g_ui_probe',2)[1]==request and d['phase']==2)
        for case in cameras:
            p.language(case['language']);p.camera(case['level'],*case['camera'])
            if 'fixture' in case:
                fixture=case['fixture'];player=p.player()
                player[0]=fixture.get('health',100)&65535;player[1]=fixture.get('energy',0)&65535
                player[10]=fixture.get('keys',0);player[18:27]=[0]*9;p.set_player(player)
                if fixture.get('weapon'):
                    p.control(1,fixture['weapon'],1000)
                    p.write(p.symbols['g_combat']+10798,struct.pack('<HH',fixture.get('shift',0),fixture.get('frame',0)))
                if 'message_length' in fixture:ui_command(4,fixture['message_length'])
                for _ in range(fixture.get('message_page',0)):
                    ui_command(5,(p.words('g_ui',13)[12]+1)&0xffffffff)
                if fixture.get('pause_cycle'):
                    ui_command(1,3);p.present();ui_command(1,0)
            values=[]
            for _ in range(4):values.append(p.present()['last_cycles'])
            completed_coverage=None
            if 'coverage_bits' in p.symbols:
                size=120*(4 if p.words('g_hw_obj_diag',6)[5] in (64,48,36) else 5)*4
                completed_coverage=__import__('hashlib').sha256(p.frontend.read(p.symbols['coverage_bits'],size)).hexdigest()
            p.frontend.run(2)
            output=build/(case['name']+'-ppu.png');p.frontend.screenshot(output)
            rgb=p.frontend.rgb;images[case['name']]=rgb
            assert len(rgb)==240*160*3,'Missing actual PPU video'
            if 'g_hw_bg_diag' in p.symbols:
                video=p.frontend.read(0x06000000,0x14800);mask=bytearray(38400);overlay=bytearray(38400)
                mode=struct.unpack('<H',p.frontend.read(0x04000000,2))[0]&7
                obj_ui=p.frontend.mode2_ui_indices() if mode==2 else None
                scene_objects=p.words('g_hw_obj_diag',6)[5]
                for y in range(160):
                    for x in range(240):
                        tile=struct.unpack_from('<H',video,0x7800+((y//8)*32+x//8)*2)[0]&1023
                        ui=obj_ui[y*240+x] if obj_ui is not None else video[tile*64+(y%8)*8+x%8]
                        source_y=scene_source_y(y,scene_objects) if y<144 else 0
                        fg=video[0x10000+(source_y//8)*1024+(x//16)*64+(source_y%8)*8+(x//2)%8] if y<144 else 0
                        mask[y*240+x]=bool(ui or fg)
                        overlay[y*240+x]=bool(ui)
                images[case['name']+'#foreground']=bytes(mask)
                images[case['name']+'#overlay']=bytes(overlay)
            row=dict(name=case['name'],camera=case['camera'],cycles=statistics.median(values),
                     rgb_sha256=__import__('hashlib').sha256(rgb).hexdigest(),dispcnt=struct.unpack('<H',p.frontend.read(0x04000000,2))[0])
            if 'g_hw_obj_diag' in p.symbols:row['hardware_work']=list(p.words('g_hw_obj_diag',6))
            if 'g_hw_bg_diag' in p.symbols:row['hardware_bg_work']=list(p.words('g_hw_bg_diag',8))
            if 'g_hw_bg_prepare_cycles' in p.symbols:row['hardware_bg_prepare_cycles']=p.words('g_hw_bg_prepare_cycles',1)[0]
            if 'fixture' in case:row['fixture']=case['fixture']
            if completed_coverage is not None:row['coverage_sha256']=completed_coverage
            rows.append(row);print(build.name,row,flush=True)
        return dict(rom_sha256=sha(p.rom),rows=rows),images
    finally:p.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--reference-build',type=Path,default=Path('research/experiments/g20a-sides/sides_exact'))
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--live',action='store_true')
    parser.add_argument('--languages',type=int,nargs='+',choices=(0,1),default=[0])
    parser.add_argument('--details',action='store_true',help='Add every weapon frame/raise phase, keys and HUD widths')
    parser.add_argument('--follow-camera',action='store_true',help='Live fixtures release the diagnostic pose and follow the real player')
    parser.add_argument('--force-low-mode',type=int,choices=(0,1,2),default=0,help='Force only the trial backend:0 auto,1 CPU ceiling,2 BG3 ceiling')
    a=parser.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    prior=json.loads((ROOT/'analysis/g20a-fps-audit.json').read_text(encoding='utf-8'))['results']['baseline']['rows']
    cameras=[]
    for language in a.languages:
        for r in prior:
            case={key:r[key] for key in ('name','camera','level','language')};case['language']=language
            if language:case['name']+='-en'
            cameras.append(case)
    if a.details:
        for language in a.languages:
            for level in range(3):
                pose=next(r for r in prior if r['level']==level and 'median' in r['name'])['camera']
                fixtures=[dict(keys=k) for k in range(8)]
                fixtures += [dict(health=h,energy=e) for h,e in ((1,1),(99,99),(999,999),(9999,9999),(32767,32767),(-32768,-32768))]
                fixtures += [dict(weapon=slot,frame=frame,shift=shift) for slot,count in ((1,9),(2,3)) for frame in range(count) for shift in range(3)]
                fixtures += [dict(message_length=n) for n in (1,9,30,60,61,120,239,240,0)]
                fixtures += [dict(message_length=240,message_page=page) for page in (1,2,3)]
                fixtures += [dict(pause_cycle=True)]
                for n,fixture in enumerate(fixtures):
                    cameras.append(dict(name=f'L{level+1}-detail-{language}-{n:02}',camera=pose,level=level,language=language,fixture=fixture))
    before,bimages=capture(a.reference_build,core,cameras);after,aimages=capture(a.build_dir,core,cameras,a.force_low_mode)
    before_rows={r['name']:r for r in before['rows']}
    for row in after['rows']:
        b=bimages[row['name']];v=aimages[row['name']]
        row['different_rgb_pixels']=sum(b[i:i+3]!=v[i:i+3] for i in range(0,len(b),3))
        row['max_channel_difference']=max(abs(x-y) for x,y in zip(b,v))
        if 'coverage_sha256' in row:row['coverage_equal']=row['coverage_sha256']==before_rows[row['name']]['coverage_sha256']
        if row['name']+'#foreground' in aimages:
            mask=aimages[row['name']+'#foreground']
            row['different_foreground_rgb_pixels']=sum(mask[i//3] and b[i:i+3]!=v[i:i+3] for i in range(0,len(b),3))
            row['foreground_rgb_pixels']=sum(mask)
            overlay=aimages[row['name']+'#overlay']
            row['different_overlay_rgb_pixels']=sum(overlay[i//3] and b[i:i+3]!=v[i:i+3] for i in range(0,len(b),3))
            row['different_foreground_examples']=[dict(x=(i//3)%240,y=(i//3)//240,before=list(b[i:i+3]),after=list(v[i:i+3]))
                for i in range(0,len(b),3) if mask[i//3] and b[i:i+3]!=v[i:i+3]][:12]
        row['different_pixel_examples']=[dict(x=(i//3)%240,y=(i//3)//240,before=list(b[i:i+3]),after=list(v[i:i+3]))
            for i in range(0,len(b),3) if b[i:i+3]!=v[i:i+3]][:24]
    result=dict(task='G20A',status='hardware-bringup-measurement',core_sha256=sha(core),script_sha256=sha(__file__),
                before=before,after=after,targets_met=False,
                languages=a.languages,
                capture_forced_low_mode=a.force_low_mode,
                frozen_initial_world_reset=True,
                scope='Actual PPU video, selected frozen cameras. Not native-index oracle, full regressions or hardware acceptance.')
    if a.live:result['live']=live_measure(a.build_dir,core,follow_camera=a.follow_camera)
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('RGB differences:',[(r['name'],r['different_rgb_pixels'],r['max_channel_difference']) for r in after['rows']])


if __name__=='__main__':main()
