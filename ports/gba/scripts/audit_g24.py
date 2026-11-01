"""Observe G24 span reuse under stationary battle and moving cameras; mutation oracle."""
import hashlib, json, os, struct, sys
from pathlib import Path
from check_g24 import CONTROL, TRIAL, OUT, ROOT
from research_gba_fps import GameProbe, freeze
from verify_sprites_gba import load_json
from verify_combat_gba import actor_pose
from verify_gba import VIDEO, sha
from verify_gba_static_cache import Workspace
from verify_items_gba import MAP_OFFSET

def main():
    global TRIAL,OUT
    if '--top' in sys.argv:TRIAL=ROOT/'build/gba/g24/top/g24_span_cache_top';OUT=OUT/'top'
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1';OUT.mkdir(parents=True,exist_ok=True)
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    rows=[];mutations={}
    for label,build in [('trial',TRIAL),('control',CONTROL)]:
        p=GameProbe(core,build)
        try:
            p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
            p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
            p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
            if label=='trial':
                from check_g22 import CASES
                for lang in (0,1):
                    p.language(lang)
                    for case in CASES:
                        lv,cell,heading=map(int,case.split(':'))
                        p.camera(lv%3,15120,-300,22800,90)
                        p.camera(lv-1,cell%64*480+240,-300,cell//64*480+240,heading)
                        for _ in range(8):p.present()
                        before=list(p.words('g_hw_span_cache_work',4))
                        for _ in range(12):p.present()
                        after=list(p.words('g_hw_span_cache_work',4))
                        rows.append(dict(level=lv,language=lang,cell=cell,heading=heading,mode='warm-heavy',work=[a-b for a,b in zip(after,before)]))
            for lang in (0,1):
                p.language(lang)
                for lv in (1,2,3):
                    model=load_json(f'research/evidence/runtime/ru/LEVEL{lv}-runtime.json');world=p.symbols['g_render_world']
                    h=model['objects'][model['player_index']-1]['actor']
                    p.camera(lv%3,15120,-300,22800,90);p.camera(lv-1,h['x'],h['y'],h['z'],h['heading'])
                    # Actual destruction flags disable the retained geometry, then topology removal.
                    wall=next(i for i,o in enumerate(model['objects']) if o['class']==2)
                    address=world+4+wall*36;flags=struct.unpack('<H',p.frontend.read(address,2))[0]
                    for mutation in ('initial','wall-destroyed','wall-restored','topology-removed'):
                        if mutation=='wall-destroyed':p.write(address,struct.pack('<H',flags|0x800))
                        if mutation=='wall-restored':p.write(address,struct.pack('<H',flags))
                        if mutation=='topology-removed':
                            tmap=p.frontend.read(world+MAP_OFFSET+4096,4096)
                            p.write(world+MAP_OFFSET+4096,tmap.replace(bytes([model['objects'][wall]['glyph']]),b' '))
                        for _ in range(8):p.present()
                        coverage=p.frontend.read(p.symbols['coverage_bits'],1920)
                        leases=p.frontend.read(p.symbols['g_render_frame']+Workspace.image_known.offset,256)+p.frontend.read(p.symbols['g_render_frame']+Workspace.surface_known.offset,768)
                        d=p.diag();p.frontend.run(2)
                        value=dict(rgb=sha_bytes(p.frontend.rgb),coverage=sha_bytes(coverage),leases=sha_bytes(leases),geometry=d['geometry_fnv1a'])
                        key=f'{lang}-{lv}-{mutation}'
                        if label=='trial':mutations[key]=value
                        else:assert mutations[key]==value,(key,mutations[key],value)
                    if label=='control':continue
                    for mode in (('battle','battle-shotgun','moving') if lv==3 else ('battle','moving')):
                        freeze(p,True);p.camera(lv%3,15120,-300,22800,90)
                        if mode.startswith('battle'):
                            x,z,heading={1:(h['x']//480,h['z']//480,h['heading']),2:(31,47,90),3:(23,25,0)}[lv]
                            if mode=='battle-shotgun':x,z,heading=20,30,270
                            p.camera(lv-1,x*480+240,-300,z*480+240,heading)
                            actor_pose(p,model,model['player_index']-1,x*480+240,z*480+240,heading)
                            player=p.player();player[0]=player[2]=32767;p.set_player(player);p.control(1,1,32767)
                        else:p.camera(lv-1,h['x'],h['y'],h['z'],h['heading'])
                        p.write(p.symbols['test_camera'],b'\0')
                        for _ in range(8):p.present()
                        warm_before=list(p.words('g_hw_span_cache_work',4))
                        for _ in range(12):p.present()
                        warm_after=list(p.words('g_hw_span_cache_work',4))
                        rows.append(dict(level=lv,language=lang,mode='warm-'+mode,work=[a-b for a,b in zip(warm_after,warm_before)]))
                        freeze(p,False)
                        previous=list(p.words('g_hw_span_cache_work',4));old=None
                        for segment in range(6):
                            buttons=(8,) if mode.startswith('battle') else (4,) if segment<2 else (8,7) if segment<4 else (4,8,7)
                            p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',0));p.frontend.run(100,buttons=buttons)
                            p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));draw=p.diag()['draws']
                            p.until(lambda d:d['draws']>draw and d['phase']==2,buttons=buttons)
                            now=list(p.words('g_hw_span_cache_work',4));cache=p.frontend.read(p.symbols['hw_span_cache'],640)
                            rows.append(dict(level=lv,language=lang,mode=mode,segment=segment,work=[(a-b)&0xffffffff for a,b in zip(now,previous)],stable_slots=sum(cache[k:k+160]==old[k:k+160] for k in range(0,640,160)) if old else None,foreground_sha256=sha_bytes(p.frontend.read(p.symbols['g_render_frame']+Workspace.frame.offset,16384))))
                            previous=now;old=cache
                        freeze(p,True)
        finally:p.close()
    result=dict(status='passed',rom_sha256=sha(TRIAL/'dok-diagnostic.gba'),rows=rows,mutation_pairs=mutations,scope='Cache counters over six 100-video-frame segments; held snapshots, not timing run. Destruction flag and TMAP removal/restoration synthetic states compare RGB/coverage/leases with production.')
    (OUT/'stability.json').write_text(json.dumps(result,indent=2)+'\n');print('stability and',len(mutations),'mutation pairs passed',flush=True)
def sha_bytes(b):return hashlib.sha256(b).hexdigest()
if __name__=='__main__':main()
