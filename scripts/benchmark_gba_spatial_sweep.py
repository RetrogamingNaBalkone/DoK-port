"""Warm stationary renderer timing in spatially distributed original-map cells."""
import argparse
import json
import os
from pathlib import Path
import struct
import statistics
import hashlib
from research_gba_fps import GameProbe,ROOT,freeze,collect
from verify_audio_gba import sounds
from verify_sprites_gba import load_json
from verify_gba import sha,VIDEO


def positions(model):
    tmap=bytes.fromhex(model['maps'][1]);defs={o['glyph']:o for o in model['objects']}
    candidates=[i for i,g in enumerate(tmap) if defs[g]['flags']&16 and defs[g]['class'] not in (2,4,7) and 0<i%64<63 and 0<i//64<63]
    result={}
    for z in range(4):
        for x in range(4):
            cells=[i for i in candidates if i%64//16==x and i//64//16==z]
            if cells:
                c=min(cells,key=lambda i:(i%64-(x*16+8))**2+(i//64-(z*16+8))**2)
                result[c]=f'sector-{x}-{z}'
    hero=model['objects'][model['player_index']-1]['actor'];result[hero['z']//480*64+hero['x']//480]='start'
    for glyph in (60,62,123,125):
        cells=[i for i in candidates if bytes.fromhex(model['maps'][2])[i]==glyph]
        if cells:result[cells[len(cells)//2]]=f'water-{glyph}'
    return sorted(result.items())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--samples',type=int,default=6)
    parser.add_argument('--heading-step',type=int,default=45)
    parser.add_argument('--cases',nargs='+',help='Restrict to level:cell:heading triples for profiling.')
    parser.add_argument('--profile',action='store_true',help='Enable instrumented face/surface timers; timing includes their overhead.')
    parser.add_argument('--language',type=int,choices=(0,1),default=0)
    parser.add_argument('--verify-output',action='store_true',help='After timing, hold a completed draw and hash actual PPU/coverage/stats.')
    parser.add_argument('--force-low-mode',type=int,choices=(0,1,2),default=0,help='Feedback prototype only:0 auto,1 CPU ceiling,2 BG3 ceiling')
    a=parser.parse_args();assert a.samples>=6 and a.heading_step in (45,90)
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        p.language(a.language)
        if a.force_low_mode:
            assert 'g_hw_low_force' in p.symbols,'This ROM has no mode-force test mailbox'
            p.write(p.symbols['g_hw_low_force'],struct.pack('<I',a.force_low_mode))
        if a.profile:p.write(p.symbols['g_render_work'],struct.pack('<I',1))
        rate=max(sounds()[0],key=lambda r:r['rate'])
        for level in range(3):
            model=load_json(f'build/runtime/ru/LEVEL{level+1}-runtime.json')
            p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
            p.camera((level+1)%3,15120,-300,22800,90)
            p.camera(level,model['objects'][model['player_index']-1]['actor']['x'],-300,model['objects'][model['player_index']-1]['actor']['z'],0)
            selected=positions(model) if not a.cases else [(c,'selected') for c in sorted({int(t.split(':')[1]) for t in a.cases if int(t.split(':')[0])==level+1})]
            for cell,label in selected:
                for heading in range(0,360,a.heading_step):
                    if a.cases and f'{level+1}:{cell}:{heading}' not in a.cases:continue
                    p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
                    p.camera(level,cell%64*480+240,-300,cell//64*480+240,heading)
                    p.command(6);p.command(2,rate['id'])
                    collect(p,6)
                    r=collect(p,a.samples)
                    assert r['audio']['underruns']==0 and r['sound_active_frames']==a.samples
                    assert all(s['flow_state']==2 and s['ui_state']==0 for s in r['samples'])
                    r.update(level=level+1,language=a.language,cell=cell,label=label,heading=heading)
                    r['stage_medians']={key:statistics.median(s['stages'][key] for s in r['samples']) for key in r['samples'][0]['stages']}
                    r['renderer_stats']=list(p.words('g_render_stats',11))
                    r['ppu_sha256']=hashlib.sha256(p.frontend.last_video_raw).hexdigest()
                    if a.verify_output:
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
                        completed=p.present();assert completed['phase']==2 and completed['guard_ok']==1
                        r['completed_renderer_stats']=list(p.words('g_render_stats',11))
                        r['coverage_sha256']=hashlib.sha256(p.frontend.read(p.symbols['coverage_bits'],120*4*4)).hexdigest()
                        p.frontend.run(2)
                        r['completed_ppu_sha256']=hashlib.sha256(p.frontend.rgb).hexdigest()
                        r['foreground_sha256']=hashlib.sha256(p.frontend.read(0x06010000,128*128)).hexdigest()
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',0))
                    if a.profile:
                        # collect() observes completed published stage timings,
                        # while live per-primitive counters can already belong
                        # to the following draw. Snapshot one extra held draw
                        # after all timed samples; never hold during FPS timing.
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
                        completed=p.present();assert completed['phase']==2
                        r['raster_work']=list(p.words('g_render_work',5))
                        r['plane_work']=list(p.words('g_hw_plane_work',6))
                        assert r['raster_work'][3]==completed['face_count']
                        r['profile_snapshot_draw']=completed['draws']
                        r['profile_timer_cycles_per_tick']=64
                        if 'g_hw_face_class_work' in p.symbols:r['face_class_work']=list(p.words('g_hw_face_class_work',4))
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',0))
                    if 'g_hw_static_work' in p.symbols:r['static_cache_work']=list(p.words('g_hw_static_work',4))
                    if 'g_hw_low_choice' in p.symbols:r['low_choice']=list(p.words('g_hw_low_choice',4))
                    if 'g_world_obj_work' in p.symbols:
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
                        r['world_obj_work']=list(p.words('g_world_obj_work',12))
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',0))
                    del r['samples'];rows.append(r);p.pcm.clear()
            current=[r for r in rows if r['level']==level+1]
            if current:print('LEVEL',level+1,'cases',len(current),'minimum FPS',min(r['presented_fps'] for r in current),flush=True)
        result=dict(rom_sha256=sha(p.rom),core_sha256=sha(core),script_sha256=sha(__file__),rows=rows,
            capture_hold_during_samples=False,simulation_frozen=True,warmup_presentations=6,heading_step=a.heading_step,instrumented=a.profile,
            forced_low_mode=a.force_low_mode,
            language=a.language,held_output_verification_after_timing=a.verify_output,
            scope='Steady renderer with PCM, original maps, distributed walkable sector/start/water cells. No live AI cost or whole-level/playthrough minimum claimed.')
        a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    finally:p.close()


if __name__=='__main__':main()
