"""Check every GPU water phase against original RES pixels and actual PPU RGB."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import json
import os
from pathlib import Path
import struct

from research_gba_fps import GameProbe, ROOT, freeze
from verify_gba import VIDEO, sha, scene_source_y
from verify_sprites_gba import load_json, original_animations


def tiles(raw, flags, phase, light, light_row):
    width,height=struct.unpack_from('<HH',raw);assert len(raw)==18+width*height
    dx=width//18*phase;dy=height//18*phase
    sx=sy=0;wrap=False
    if flags&0x440:sx=-dx;wrap=bool(flags&0x400)
    elif flags&0x880:sx=dx;wrap=bool(flags&0x800)
    elif flags&0x1100:sy=-dy;wrap=bool(flags&0x1000)
    elif flags&0x2200:sy=dy;wrap=bool(flags&0x2000)
    result=bytearray(1024)
    for y in range(32):
        for x in range(32):
            u=x*(width-1)//32+sx
            v=((65535-y*2048)*(height-1))//65536+sy
            if wrap:u%=width;v%=height
            color=raw[18+v*width+u] if 0<=u<width and 0<=v<height else 0
            result[(y//8*4+x//8)*64+(y%8)*8+x%8]=light[light_row*256+color]
    return bytes(result)


def color555(video,fmt,at):
    if fmt==1:
        value=struct.unpack_from('<I',video,at*4)[0]
        return ((value>>19)&31)|(((value>>11)&31)<<5)|(((value>>3)&31)<<10)
    value=struct.unpack_from('<H',video,at*2)[0]
    return ((value>>(11 if fmt==2 else 10))&31)|(((value>>(6 if fmt==2 else 5))&31)<<5)|((value&31)<<10)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    manifest=load_json(args.build_dir.parent/'prototype-build.json')
    views=manifest['tables']['hardware_bg']['views'];p=GameProbe(core,args.build_dir);checks=[];sources=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for language in (0,1):
            original,source=original_animations(language);sources.append(source);p.language(language)
            for level in range(3):
                # Release geometry/definitions use the shared RUS baseline;
                # localization changes the original RES payload only.
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json')
                materials=views[level*2+language]['materials'];count=len(materials)
                light=original[model['light']];maps=bytes.fromhex(model['maps'][2])
                for mat,material in enumerate(materials):
                    if not material['moving']:continue
                    obj=material['object'];definition=model['objects'][obj]
                    assert definition['class'] in (12,13) and material['flags']==definition['sprite_flags']
                    texture=original[definition['sprite']]
                    tm=bytes.fromhex(model['maps'][1]);glyphs=model['glyph_index']
                    open_cell=lambda i:model['objects'][glyphs[tm[i]]-1]['class']!=2
                    candidates=[i for i,glyph in enumerate(maps) if glyph==definition['glyph'] and i//64<60 and open_cell(i)]
                    cell=max(candidates,key=lambda i:sum(maps[i+k*64]==definition['glyph'] and open_cell(i+k*64) for k in range(1,5)))
                    p.camera(level,cell%64*480+240,-300,cell//64*480+240,0)
                    for phase in range(18):
                        p.present() # Mailbox writes must precede the next draw.
                        words=list(p.object_words(obj));words[1]=phase;words[4]=phase;words[6]|=8
                        p.write(p.symbols['g_render_world']+4+obj*36,struct.pack('<8H',*words))
                        p.present();p.frontend.run(2)
                        vram=p.frontend.read(0x06000000,0x14800)
                        expected=[tiles(texture,definition['sprite_flags'],phase,light,row) for row in (0,7,15)]
                        for band in range(3):
                            offset=(16384 if band==2 else 0)+64+(mat+(count if band==1 else 0))*1024
                            assert vram[offset:offset+1024]==expected[band],(language,level,obj,phase,band)
                        # Independently decode affine BG tile addresses. Only
                        # unobstructed water pixels are compared with PPU RGB.
                        front=p.words('hw_bg_front',1)[0]
                        dual='hw_bg_frames' not in p.symbols
                        if dual:
                            from verify_gba_static_cache import Workspace
                            flags=json.loads((args.build_dir.parent/'prototype-build.json').read_text(encoding='utf-8'))['variants'][args.build_dir.name]['prototype_defines']
                            reserved=11592+(5152 if 'HW_OAM_RAM' in flags else 0)
                            base=(p.symbols['g_render_frame']+Workspace.covered.offset+34560-reserved)&~3
                            active=p.frontend.read(base+front*5796,5796);row_stride,cnt_base,cnt_stride=32,5152,4
                        else:
                            active=p.frontend.read(p.symbols['hw_bg_frames']+front*2900,2900);row_stride,cnt_base,cnt_stride=16,2576,2
                        palette=struct.unpack('<256H',p.frontend.read(0x05000000,512))
                        video=p.frontend.last_video_raw;pixels=0
                        scene_objects=p.words('g_hw_obj_diag',6)[5]
                        mode=struct.unpack('<H',p.frontend.read(0x04000000,2))[0]&7
                        obj_ui=p.frontend.mode2_ui_indices() if mode==2 else None
                        for y in range(80,144):
                            pa,pb,pc,pd,rx,ry=struct.unpack_from('<4h2i',active,y*row_stride)
                            cnt=struct.unpack_from('<H',active,cnt_base+y*cnt_stride)[0]
                            for x in range(0,240,2):
                                ui=struct.unpack_from('<H',vram,0x7800+(y//8*32+x//8)*2)[0]&1023
                                if obj_ui[y*240+x] if obj_ui is not None else vram[ui*64+(y%8)*8+x%8]:continue
                                sy=scene_source_y(y,scene_objects)
                                if vram[0x10000+(sy//8)*1024+(x//16)*64+(sy%8)*8+(x//2)%8]:continue
                                ix=((rx+pa*x)>>8)&511;iy=((ry+pc*x)>>8)&511
                                tile=vram[((cnt>>8)&31)*2048+(iy//8)*64+ix//8]
                                charbase=(cnt>>2)&3
                                band=2 if charbase==1 else (1 if ((cnt>>8)&31)==18 else 0)
                                base=1+(mat+(count if band==1 else 0))*16
                                if not base<=tile<base+16:continue
                                color=expected[band][(tile-base)*64+(iy%8)*8+ix%8]
                                assert color555(video,p.frontend.pixel_format,y*240+x)==palette[color],(language,level,obj,phase,x,y)
                                pixels+=1
                        assert pixels>=8,('No visible water oracle pixels',language,level,obj,phase,pixels)
                        checks.append(dict(language=language,level=level+1,object=obj,phase=phase,ppu_pixels=pixels,three_light_bands_exact=True))
        for source in sources:assert sha(source['path'])==source['sha256']
        result=dict(status='passed',rom_sha256=sha(p.rom),core_sha256=sha(core),script_sha256=sha(__file__),
            checks=checks,originals=sources,phase_cases=len(checks),ppu_pixels=sum(c['ppu_pixels'] for c in checks),
            scope='Every moving GPU material,18 crop/wrap phases, all3 LIGHT bands in VRAM; unobstructed actual PPU water pixels, RU/EN. Not native affine geometry equivalence.')
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print('passed',len(checks),'phases')
    finally:p.close()


if __name__=='__main__':main()
