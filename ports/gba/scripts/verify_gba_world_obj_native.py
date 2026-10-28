"""Native OBJ/CPU/UI composition, coverage backing and original scene leases."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha,scene_source_y
from verify_gba_static_cache import Workspace
from verify_sprites_gba import load_json
from verify_renderer_gba import stack_usage


class ObjState(ct.Structure):
    _fields_=[('magic',ct.c_uint32),('count',ct.c_uint32),('matrices',ct.c_uint32),
        ('view',ct.c_uint32),('stride',ct.c_uint32),('scan_max',ct.c_uint32),
        ('oam',ct.c_uint16*512),('slots',ct.c_uint32*32),('uploaded',ct.c_uint32*32),
        ('used',ct.c_uint16*32),('scanline',ct.c_uint16*144),('matrix',ct.c_int16*120),
        ('first_y',ct.c_uint8*128),('last_y',ct.c_uint8*128)]


def run(build,core,out,trial):
    p=GameProbe(core,build);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for language in (0,1):
            p.language(language);p.camera(2,15120,-300,22800,90)
            for level in (1,2,3):
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level}-runtime.json');hero=model['objects'][model['player_index']-1]['actor']
                cell=hero['z']//480*64+hero['x']//480
                cases=[(cell,h) for h in (0,45,90,135)]+{1:[(520,225),(3640,225)],2:[(1592,135),(2871,225)],3:[(1544,315)]}[level]
                cases=[(c,h,False) for c,h in cases]+[(cases[-1][0],cases[-1][1],True)]
                for cell,heading,mode_cycle in cases:
                    if trial and 'g_world_obj_trace' in p.symbols:
                        p.write(p.symbols['g_world_obj_trace'],struct.pack('<16I',int(level==1 and cell==520 and heading==225),0,66,*([0]*13)))
                    p.camera(level-1,cell%64*480+240,-300,cell//64*480+240,heading)
                    if mode_cycle:
                        for state_id in (3,0):
                            request=p.words('g_ui_probe',2)[1]+1
                            p.write(p.symbols['g_ui_probe']+8,struct.pack('<4I',1,state_id,0,0))
                            p.write(p.symbols['g_ui_probe'],struct.pack('<I',request))
                            p.until(lambda d:p.words('g_ui_probe',2)[1]==request and d['phase']==2)
                            p.present();p.present()
                            if trial and state_id==3:assert p.words('g_world_obj_state',1)[0]==0,'Mode4 must invalidate OBJ scratch'
                    for _ in range(4):d=p.present()
                    assert d['guard_ok']==1 and d['phase']==2
                    address=p.symbols['g_render_frame'];w=Workspace.from_buffer_copy(p.frontend.read(address,ct.sizeof(Workspace)))
                    lease=bytes(w.image_known)+bytes(w.surface_known)
                    row=dict(level=level,language=language,cell=cell,heading=heading,mode4_cycle=mode_cycle,
                        geometry_sha256=hashlib.sha256(b''.join(bytes(f) for f in w.faces[:w.face_count])+
                            b''.join(bytes(f) for f in w.surfaces[:w.surface_count])).hexdigest(),
                        lease_sha256=hashlib.sha256(lease).hexdigest())
                    coverage=struct.unpack('<480I',p.frontend.read(p.symbols['coverage_bits'],1920))
                    state=None
                    if trial:
                        ptr=p.words('g_world_obj_state',1)[0];assert ptr%4==0
                        assert address+Workspace.covered.offset<=ptr and ptr+ct.sizeof(ObjState)<=address+Workspace.surfaces.offset
                        state=ObjState.from_buffer_copy(p.frontend.read(ptr,ct.sizeof(ObjState)))
                        assert state.magic==0x574f424a and state.count<=120 and state.matrices<=30
                        expected_first=[next(y for y in range(144) if scene_source_y(y,64)==sy) for sy in range(128)]
                        expected_last=[max(y for y in range(144) if scene_source_y(y,64)==sy) for sy in range(128)]
                        assert list(state.first_y)==expected_first and list(state.last_y)==expected_last,'Mode4 scratch lifetime'
                        assert sum(bool(v) for v in state.used)<=32
                        row['world_obj_work']=list(p.words('g_world_obj_work',12))
                    p.frontend.run(2)
                    video=p.frontend.read(0x06000000,98304);oam=p.frontend.read(0x07000000,1024)
                    palette=struct.unpack('<512H',p.frontend.read(0x05000000,1024))
                    actual=p.frontend.colors555();gpu=bytearray(38400);line_cost=[552+2*(8+(state.count if state else 0))]*144
                    if state:
                        dispcnt=struct.unpack('<H',p.frontend.read(0x04000000,2))[0]
                        assert dispcnt==0x3561,hex(dispcnt)
                        for slot in range(32):
                            if state.used[slot]:
                                assert state.slots[slot]==state.uploaded[slot]
                                assert video[0x14000+slot*512:0x14000+(slot+1)*512]==p.frontend.read(state.slots[slot],512)
                        for i in range(8,8+state.count):
                            a,b,c=struct.unpack_from('<3H',oam,i*8)
                            assert [a,b,c]==list(state.oam[i*4:i*4+3])
                            assert a&0xe100==0xa100 and b>>14==2 and c&0xc00==0x400
                            mi=(b>>9)&31;assert 2<=mi<32
                            pa,pb,pc,pd=(struct.unpack_from('<h',oam,mi*32+k)[0] for k in (6,14,22,30))
                            left=b&511;left=left if left<256 else left-512
                            top=a&255;top=top if top<160 else top-256
                            width,height=(32,64) if a&0x200 else (16,32)
                            tile=(c&1023)*32+0x10000
                            for y in range(max(0,top),min(144,top+height)):
                                line_cost[y]+=10+2*width
                                for x in range(max(0,left),min(240,left+width)):
                                    at=y*240+x
                                    if gpu[at]:continue
                                    dx=x-left-width//2;dy=y-top-height//2
                                    u=(pa*dx+pb*dy+16*128)>>8;v=(pc*dx+pd*dy+32*128)>>8
                                    if 0<=u<16 and 0<=v<32:
                                        gpu[at]=video[tile+(v//8)*128+(u//8)*64+(v&7)*8+(u&7)]
                        assert max(line_cost)<=954,(level,cell,heading,'scanline budget',max(line_cost))
                    checked=revealed=0
                    for y in range(160):
                        sy=scene_source_y(y,64) if y<144 else 0
                        for x in range(240):
                            at=y*240+x
                            ui_tile=struct.unpack_from('<H',video,0x7800+((y//8)*32+x//8)*2)[0]&1023
                            ui=video[ui_tile*64+(y&7)*8+(x&7)]
                            cpu=video[0x10000+(sy//8)*1024+(x//16)*64+(sy&7)*8+(x//2)%8] if y<144 else 0
                            color=palette[ui] if ui else palette[256+cpu] if cpu else palette[256+gpu[at]] if gpu[at] else None
                            if color is not None:
                                assert actual[at]==color,(level,cell,heading,x,y,'PPU composite',actual[at],color,ui,cpu,gpu[at])
                                checked+=1
                            if gpu[at] and not ui and not cpu:revealed+=1
                            if y<144:
                                bit=bool(coverage[(x//2)*4+sy//32]&(1<<(sy&31)))
                                if bit and not cpu and not gpu[at]:
                                    details=[]
                                    for ii in range(8,8+(state.count if state else 0)):
                                        aa,bb,cc=struct.unpack_from('<3H',oam,ii*8)
                                        details.append([ii,hex(aa),hex(bb),hex(cc)])
                                    raise AssertionError((level,cell,heading,x,y,'coverage without foreground',sy,
                                        list(state.first_y[sy:sy+1]) if state else None,
                                        list(state.last_y[sy:sy+1]) if state else None,details,
                                        list(p.words('g_world_obj_trace',16)) if trial and 'g_world_obj_trace' in p.symbols else None,
                                        list(state.last_y[55:70]) if state else None))
                    image=out/f'{"trial" if trial else "control"}-{language}-L{level}-{cell}-{heading}{"-mode4" if mode_cycle else ""}.png'
                    p.frontend.screenshot(image)
                    row.update(foreground_ppu_pixels_checked=checked,revealed_direct_obj_pixels=revealed,
                        max_scanline_bound=max(line_cost),objects=state.count if state else 0,
                        image=str(image),image_sha256=sha(image));rows.append(row)
        return dict(rom_sha256=sha(p.rom),rows=rows,stack=stack_usage(p.frontend))
    finally:p.close()


def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--build-dir',type=Path,required=True);a.add_argument('--reference-build',type=Path,required=True)
    a.add_argument('--out',type=Path,required=True);a=a.parse_args();os.chdir(ROOT);a.out.mkdir(parents=True,exist_ok=True)
    os.environ['DOK_PROBE_FAST_VIDEO']='1';core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before=run(a.reference_build,core,a.out,False);after=run(a.build_dir,core,a.out,True)
    assert len(before['rows'])==len(after['rows'])==40
    for b,t in zip(before['rows'],after['rows']):
        assert b['geometry_sha256']==t['geometry_sha256'],('geometry',t['level'],t['cell'],t['heading'])
        assert b['lease_sha256']==t['lease_sha256'],('leases',t['level'],t['cell'],t['heading'])
    assert sum(r['revealed_direct_obj_pixels'] for r in after['rows'])>0
    result=dict(task='G20A',status='passed',before=before,after=after,core_sha256=sha(core),script_sha256=sha(__file__),
        scope='40 original-map RU/EN poses including six pause/Mode4/resume cycles: exact projected geometry and leases, source/VRAM residency, legal HBlank access, scanline upper bound, coverage backed by foreground, independent actual PPU CPU/OBJ/UI composition. Background UV/art quality, full gameplay and physical GBA not accepted.')
    (a.out/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Passed40 native poses; revealed OBJ pixels',sum(r['revealed_direct_obj_pixels'] for r in after['rows']))


if __name__=='__main__':main()
