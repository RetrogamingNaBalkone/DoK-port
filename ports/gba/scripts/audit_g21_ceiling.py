"""Trace ceiling LIGHT discontinuities in GBA geometry and original x86 raster.

Original maps/poses and actual PPU captures; frozen simulation, camera route
fixtures. Native raster execution intercepts image_crop, LIGHT binding and
span dispatch only. It does not execute a whole DOS frame.
"""
import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'ports/gba/scripts'))
from profile_game_gba import GameProbe, freeze
from verify_gba import VIDEO, sha
from verify_gba_static_cache import Workspace
from verify_surfaces import Original, R
from unicorn import UC_HOOK_CODE


def signed(n):
    return ct.c_int32(n).value


def trunc(n, d):
    return (abs(n)//abs(d)) * (-1 if (n < 0) != (d < 0) else 1)


def affine_rows(vertices):
    lo = min(range(4), key=lambda k: vertices[k][1])
    hi = max(range(4), key=lambda k: vertices[k][1])
    y0, y1 = vertices[lo][1], vertices[hi][1]
    if y0 == y1:
        return {}
    d0, d1 = abs(vertices[lo][2]), abs(vertices[hi][2])
    step = trunc((d1-d0)*65536, y1-y0)
    rows = {}
    for y in range(max(0,y0),min(143,y1)+1):
        depth = (d0*65536+(y-y0)*step)>>16
        rows[y] = max(0,min(15,(depth-1920)//120))
    return rows


class NativeRaster:
    def __init__(self, language):
        self.o = Original(language)
        self.rows = {}
        self.ru = language == 'ru'
        self.o.c.hook_add(UC_HOOK_CODE, self.hook)

    def ret(self):
        c = self.o.c
        sp = c.reg_read(R.UC_X86_REG_SP)
        ip, cs = struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        c.reg_write(R.UC_X86_REG_SP,sp+4)
        c.reg_write(R.UC_X86_REG_CS,cs)
        c.reg_write(R.UC_X86_REG_IP,ip)

    def hook(self,c,address,size,unused):
        cs = c.reg_read(R.UC_X86_REG_CS)
        if cs in (0x5000,0x6000,0x7000):
            return
        sp = c.reg_read(R.UC_X86_REG_SP)
        if cs == (0x1088 if self.ru else 0x1028):
            # cdecl16far: far LIGHT receiver then signed row at SP+8.
            row = struct.unpack('<h',c.mem_read(0x30000+sp+8,2))[0]
            y = struct.unpack('<h',c.mem_read(0x30000+(0xc1d8 if self.ru else 0xc288),2))[0]
            self.rows[y] = max(0,min(15,row))
        elif cs == (0x10f8 if self.ru else 0x1060):
            c.reg_write(R.UC_X86_REG_AX,0)
            c.reg_write(R.UC_X86_REG_DX,0x4000)
        elif cs != 0x1010:
            raise AssertionError(('unrecognized native boundary',hex(cs),hex(address)))
        self.ret()

    def run(self,vertices):
        c=self.o.c
        self.rows={}
        # The original global maximum-vertex slot is not initialized when
        # vertex0 is the maximum. Seed its intended value for isolated calls.
        c.mem_write(0x30000+(0xc146 if self.ru else 0xc1f6),bytes(2))
        c.mem_write(0x41000,b''.join(struct.pack('<iii',*v) for v in vertices))
        c.mem_write(0x42000,struct.pack('<5HII',32,32,0,0,0,1024,0x40000000))
        delta=0 if self.ru else 0xa8
        c.mem_write(0x30000+0xdfa+delta,struct.pack('<h',1920))
        c.mem_write(0x30000+0xe00+delta,struct.pack('<4h',0,239,0,143))
        self.o.setup(0x6000)
        c.mem_write(0x3f000,struct.pack('<11H',0xfff0,0x7000,0,0x4000,
                                      0x1000,0x4000,0x2000,0x4000,0,0,4))
        c.emu_start(0x60000+(0xb12 if self.ru else 0xb18),0x7fff0,count=200000)
        assert c.reg_read(R.UC_X86_REG_CS)==0x7000
        return self.rows.copy()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    args.out.mkdir(parents=True,exist_ok=True)
    # Verify exports against immutable samples before and after execution.
    pins={}
    for language,exe in (('ru','RUS/PKR.EXE'),('en','ENG/PKE.EXE')):
        path=Path('originals/pkr')/exe
        manifest=json.loads((Path('research/evidence/surfaces')/language/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        pins[str(path)]=sha(path)
        assert pins[str(path)]==manifest['sha256_before']==manifest['sha256_after']
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,args.build_dir)
    trig=list(map(int,re.findall(r'-?\d+',(ROOT/'ports/gba/src/renderer_trig.inc').read_text().split('{',1)[1])))
    rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2)
        freeze(p,True)
        for language in (0,1):
            p.language(language)
            native=NativeRaster('ru' if language==0 else 'en')
            for level in range(3):
                model=json.loads((ROOT/f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json').read_text())
                hero=model['objects'][model['player_index']-1]['actor']
                if level==0:
                    # Wide original lowered-ceiling room, away from start walls.
                    hero=dict(x=57*480+240,z=52*480+240)
                for heading in (0,1,2,5,10,20,30,44,45,46,60,70,80,85,88,89,90,91,92,95,100,110,135,225,315):
                    for offset in (-120,-60,0,60,120):
                        x,z=hero['x']+offset,hero['z']
                        p.camera(level,x,-300,z,heading)
                        p.present()
                        w=Workspace.from_buffer_copy(p.frontend.read(p.symbols['g_render_frame'],ct.sizeof(Workspace)))
                        surfaces=[]
                        bands={}
                        original_bands={}
                        for f in w.surfaces[:w.surface_count]:
                            if f.s[1]!=1:
                                continue
                            vertices=[[signed(f.n[k]),signed(f.n[4+k]),signed(f.n[8+k])] for k in range(4)]
                            if not all(-4000<=v[1]<=4000 for v in vertices):
                                continue
                            affine=affine_rows(vertices)
                            actual=native.run(vertices)
                            for y,value in actual.items():
                                assert affine[y]==value,(language,vertices,y,value,affine[y])
                            definition=model['objects'][f.s[0]]
                            inset=next(field['number'] for field in definition['fields'] if field['offset']==0x54)
                            camera=dict(x=x,y=-300,z=z,sine=trig[heading],cosine=trig[361+heading])
                            _,native_vertices,_=native.o.quad(camera,list(f.s[2:4]),definition['height_parameter'],inset)
                            original_light=native.run(native_vertices)
                            for y,value in original_light.items():
                                original_bands.setdefault(y,set()).add(value)
                            surfaces.append(dict(cell=list(f.s[2:4]),object=f.s[0],vertices=vertices,light_rows=actual,
                                                 original_vertices=native_vertices,original_light_rows=original_light))
                            for y,value in actual.items():
                                bands.setdefault(y,set()).add(value)
                        seams={y:sorted(values) for y,values in bands.items() if len(values)>1}
                        name=f'{language}-L{level+1}-h{heading}-x{offset}'
                        p.frontend.run(2)
                        # Save the whole sequence, including frames without seams.
                        p.frontend.decode_video(p.frontend.last_video_raw)
                        p.frontend.screenshot(args.out/(name+'.png'))
                        rows.append(dict(name=name,level=level+1,language=language,camera=[x,-300,z,heading],
                                         surfaces=surfaces,rows_with_different_light=seams,
                                         original_rows_with_different_light={y:sorted(v) for y,v in original_bands.items() if len(v)>1},
                                         ppu_sha256=sha(args.out/(name+'.png'))))
    finally:
        p.close()
    for path,value in pins.items():
        assert sha(path)==value
    result=dict(status='passed',rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),
                script_sha256=sha(__file__),core_sha256=sha(core),samples=pins,
                frames=len(rows),frames_with_potential_seams=sum(bool(r['rows_with_different_light']) for r in rows),
                original_frames_with_potential_seams=sum(bool(r['original_rows_with_different_light']) for r in rows),
                native_boundaries='Original constructor projection executes x86; its array-construction helper is stubbed. '
                    'Raster image_crop and LIGHT binding/span dispatch are stubbed. Max-vertex global is seeded0 '
                    'for isolated calls. GBA geometry uses scaled coordinates, original geometry uses native coordinates. '
                    'Potential seams may be occluded by nearer walls; counts alone are not visible-defect counts.',
                scope=__doc__,rows=rows)
    (args.out/'ceiling.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Ceiling trace:',result['frames'],'frames;',result['frames_with_potential_seams'],'potential seams')


if __name__=='__main__':
    main()
