"""Measure the screen-space scope of a first direct affine-OBJ rectangle path."""
import argparse
from collections import Counter
import ctypes as ct
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe, ROOT, freeze
from verify_gba import VIDEO, sha
from verify_gba_static_cache import Workspace
from verify_sprites_gba import load_json


def signed(n):
    return (n+0x80000000)%0x100000000-0x80000000


def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--build-dir',type=Path,required=True)
    a.add_argument('--report',type=Path,required=True)
    a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        cases=[(1,520,225),(1,3640,225),(2,1592,135),(2,2871,225),(3,1544,315)]
        for level in (1,2,3):
            model=load_json(f'build/runtime/ru/LEVEL{level}-runtime.json')
            hero=model['objects'][model['player_index']-1]['actor']
            cell=hero['z']//480*64+hero['x']//480
            cases.extend((level,cell,h) for h in (0,45,90,135))
        for level,cell,heading in cases:
            model=load_json(f'build/runtime/ru/LEVEL{level}-runtime.json')
            p.camera(level-1,cell%64*480+240,-300,cell//64*480+240,heading)
            for _ in range(8):p.present()
            w=Workspace.from_buffer_copy(p.frontend.read(p.symbols['g_render_frame'],ct.sizeof(Workspace)))
            counts=Counter();areas=Counter();sizes=Counter();examples=[]
            for f in w.faces[:w.face_count]:
                x0,x1,t0,t1,b0,b1,d0,d1,u0,u1,key=map(signed,f.n)
                if x0>x1:x0,x1,t0,t1,b0,b1,u0,u1=x1,x0,t1,t0,b1,b0,u1,u0
                width=max(0,min(x1,239)-max(x0,0)+1)
                height=max(0,min(max(b0,b1),127)-max(min(t0,t1),0)+1)
                if not width or not height:continue
                counts['visible_bbox']+=1;areas['visible_bbox']+=width*height//2
                if d0!=d1 or t0!=t1 or b0!=b1:continue
                idx=f.s[0];im=w.images[idx] if idx<256 else w.instance_images[idx-256]
                iw,ih,flags,rid=im.s;v0=f.s[1] if f.s[1]<32768 else f.s[1]-65536
                cls=model['objects'][idx]['class'] if idx<len(model['objects']) else -1
                counts['rectangle']+=1;areas['rectangle']+=width*height//2
                sizes[f'{iw}x{ih}']+=1
                reason='eligible'
                if cls==7:reason='door'
                elif not im.p[1] or not rid:reason='no_prelit'
                elif flags&8:reason='masked'
                elif iw>64 or ih>64:reason='large_source'
                elif x1-x0+1>128 or (b0-t0+1)*9//8>128:reason='large_projection'
                elif v0 or u0<0 or u1<0:reason='cropped'
                counts[reason]+=1;areas[reason]+=width*height//2
                examples.append(dict(object=idx,resource=rid,source=[iw,ih],flags=flags,reason=reason,
                    bbox=[x0,t0,x1,b0],uv=[u0,u1,v0]))
            rows.append(dict(level=level,cell=cell,heading=heading,counts=dict(counts),
                bbox_logical_area_upper_bounds=dict(areas),rectangle_source_sizes=dict(sizes),rectangles=examples))
        a.report.write_text(json.dumps(dict(rom_sha256=sha(p.rom),core_sha256=sha(core),rows=rows,
            scope='Bounding-box upper bounds only, with overlap/occlusion not subtracted; no predicted FPS.'),indent=2)+'\n',encoding='utf-8')
        print([(r['level'],r['cell'],r['heading'],r['counts'].get('eligible',0),r['bbox_logical_area_upper_bounds'].get('eligible',0)) for r in rows])
    finally:p.close()


if __name__=='__main__':main()
