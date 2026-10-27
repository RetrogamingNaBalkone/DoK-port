"""Compare cached and uncached geometry, leases and actual PPU after mutations."""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha
from verify_sprites_gba import load_json
from verify_combat_gba import actor_pose
from verify_items_gba import MAP_OFFSET

U8,U16,U32=ct.c_uint8,ct.c_uint16,ct.c_uint32
class Image(ct.Structure):_fields_=[('p',U32*3),('s',U16*4)]
class ImageView(ct.Structure):_fields_=[('p',U32),('s',U16*5),('n',U32*2)]
class Face(ct.Structure):_fields_=[('n',U32*11),('s',U16*2)]
class Surface(ct.Structure):_fields_=[('n',U32*13),('s',U16*8)]
class Sprite(ct.Structure):_fields_=[('n',U32*3),('s',U16*2)]
class Workspace(ct.Structure):
    _fields_=[('guard',U32),('frame',U32*9600),('tail',U32),('faces',Face*256),('images',Image*256),
        ('image_known',U8*256),('face_count',U16),('covered_count',U8*240),('covered',U8*(240*72*2)),
        ('surfaces',Surface*512),('surface_images',ImageView*768),('surface_known',U8*768),
        ('surface_order',U16*512),('surface_count',U16),('lights',U8*4096),('sprites',Sprite*256),
        ('sprite_count',U16),('instance_images',Image*256),('sprite_resource',U16*256),('sprite_frame',U16*256)]


def exercise(build,core):
    p=GameProbe(core,build);rows=[];images={}
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        assert ct.sizeof(Workspace)==p.diag()['workspace_bytes']==160260
        def settle(name,prior=None,needs_hit=True):
            for _ in range(8):d=p.present()
            work=list(p.words('g_hw_static_work',4)) if 'g_hw_static_work' in p.symbols else None
            mode=p.words('hw_static_mode',1)[0] if work is not None else None
            if work is not None and needs_hit:assert mode==2 and work[0]>0,(name,mode,work)
            if prior is not None and work is not None:assert work[1]==prior[1],('Unneeded recapture',name,prior,work)
            leases=p.frontend.read(p.symbols['g_render_frame']+Workspace.image_known.offset,256)
            leases+=p.frontend.read(p.symbols['g_render_frame']+Workspace.surface_known.offset,768)
            row=dict(name=name,geometry_fnv1a=d['geometry_fnv1a'],face_count=d['face_count'],
                lease_sha256=hashlib.sha256(leases).hexdigest(),cache_mode=mode,cache_work=work)
            p.frontend.run(2);p.frontend.decode_video(p.frontend.last_video_raw)
            images[name]=p.frontend.rgb;row['rgb_sha256']=hashlib.sha256(p.frontend.rgb).hexdigest();rows.append(row)
            p.present() # Synchronize mailbox writes after video settling.
            return work
        for language in (0,1):
            p.language(language)
            for level in range(3):
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json');hero=model['objects'][model['player_index']-1]['actor']
                p.camera((level+1)%3,15120,-300,22800,90)
                p.camera(level,hero['x'],hero['y'],hero['z'],90)
                prefix=f'{language}-{level+1}-'
                settle(prefix+'native')
                # A small clear area makes the controlled moving sprite visible
                # while leaving the rest of the original static world present.
                world=p.symbols['g_render_world'];tmap=bytearray(p.frontend.read(world+MAP_OFFSET+4096,4096))
                cx,cz=hero['x']//480,hero['z']//480
                for z in range(max(0,cz-2),min(64,cz+3)):
                    for x in range(max(0,cx-3),min(64,cx+2)):tmap[z*64+x]=32
                p.write(world+MAP_OFFSET+4096,bytes(tmap));settle(prefix+'tmap-edit')
                monster=next(i for i,o in enumerate(model['objects']) if o['class']==9 and o['actor_index']!=65535)
                glyph=model['objects'][monster]['glyph'];mmap=p.frontend.read(world+MAP_OFFSET,4096).replace(bytes([glyph]),b' ')
                p.write(world+MAP_OFFSET,mmap)
                actor_pose(p,model,monster,hero['x']-640,hero['z']-200)
                work=settle(prefix+'actor-left')
                p.write(world+MAP_OFFSET,p.frontend.read(world+MAP_OFFSET,4096).replace(bytes([glyph]),b' '))
                actor_pose(p,model,monster,hero['x']-640,hero['z']+200)
                settle(prefix+'actor-right',prior=work)
                assert images[prefix+'actor-left']!=images[prefix+'actor-right'],'Moving actor produced no PPU change'
                # Original placed door, viewed from an adjacent traversable cell.
                tmap=bytes.fromhex(model['maps'][1]);defs={o['glyph']:o for o in model['objects']}
                spot=None
                for cell,g in enumerate(tmap):
                    if defs[g]['class']!=7:continue
                    for delta,heading in ((-64,0),(64,180),(-1,270),(1,90)):
                        neighbor=cell+delta
                        if 0<=neighbor<4096 and defs[tmap[neighbor]]['flags']&16:
                            spot=(cell,neighbor,heading,model['glyph_index'][g]-1);break
                    if spot:break
                assert spot is not None
                cell,neighbor,heading,door=spot
                p.write(world+MAP_OFFSET+4096,tmap)
                p.camera(level,neighbor%64*480+240,-300,neighbor//64*480+240,heading)
                object_address=world+4+door*36
                for frame,label in ((0,'closed'),(15,'half'),(29,'open')):
                    p.write(object_address+2,struct.pack('<H',frame));p.write(object_address+34,struct.pack('<h',30))
                    work=settle(prefix+'door-'+label,prior=work if label!='closed' else None)
                assert images[prefix+'door-closed']!=images[prefix+'door-open'],'Door geometry remained frozen'
                p.camera(level,hero['x'],hero['y'],hero['z'],195);settle(prefix+'camera-change')
        return dict(rom_sha256=sha(p.rom),rows=rows),images
    finally:p.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--reference-build',type=Path,default=Path('research/experiments/g20a-hw-projection/hw_v128_projection'))
    parser.add_argument('--report',type=Path,required=True);a=parser.parse_args()
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before,bi=exercise(a.reference_build,core);after,ai=exercise(a.build_dir,core)
    assert len(before['rows'])==len(after['rows'])==48
    for old,new in zip(before['rows'],after['rows']):
        assert old['name']==new['name']
        for key in ('geometry_fnv1a','face_count','lease_sha256','rgb_sha256'):assert old[key]==new[key],(new['name'],key,old[key],new[key])
    result=dict(status='passed',rom_sha256=after['rom_sha256'],core_sha256=sha(core),script_sha256=sha(__file__),
        before=before,after=after,cases=48,ppu_pixels=48*38400,
        scope='Three worlds/RU/EN; cold/warm geometry, exact leases, TMAP edits, MMAP actor movement without invalidation, dynamic door crop and camera invalidation. Actual PPU RGB, not cached bitmap.')
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print('passed',len(after['rows']),'cache mutation cases')


if __name__=='__main__':main()
