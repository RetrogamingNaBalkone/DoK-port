"""G23 coverage-aware foreground attribution on an unchanged production ROM.

Counts samples at visible_column/rectangle/surface entry, before those calls
modify coverage. Nested assembly samplers are deliberately not counted twice.
Depth is the renderer's integer interpolation, not Euclidean distance.
"""
import argparse
from collections import Counter, defaultdict
import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_raster_work import replay, ROOT, GameProbe, freeze, sha, R
from check_g22 import CASES
from verify_sprites_gba import load_json


def trunc(n, d):
    return (abs(n)//abs(d)) * (-1 if (n<0)!=(d<0) else 1)


def light(depth):
    d=(depth+32768)%65536-32768
    n=0 if d<1920 else (d-1920+32768)%65536-32768
    return min(15, trunc(n,120)&65535)


class Attribution:
    entries=('visible_column','dok_gba_tiled_rectangle')

    def __init__(self):
        self.face=None
        self.surface=None
        self.counts=defaultdict(Counter)
        self.boundaries=Counter()

    def __call__(self, cpu, name, symbols):
        reg=lambda i:cpu.reg_read(getattr(R,f'UC_ARM_REG_R{i}'))
        words=lambda a,n:struct.unpack('<'+'I'*n,cpu.mem_read(a,4*n))
        ints=lambda a,n:struct.unpack('<'+'i'*n,cpu.mem_read(a,4*n))
        if name in ('raster','raster_rect'):
            self.face=ints(reg(1) if name=='raster_rect' else reg(2),11)
            if name=='raster':
                d0,d1=self.face[6:8]
                self.boundaries['faces']+=1
                self.boundaries['faces_cross_depth3720']+=min(d0,d1)<3720<=max(d0,d1)
            return
        if name in ('surface_raster_hw','surface_raster_general'):
            # HW specialization drops unused runtime argument (checked ARM listing).
            self.surface=ints(reg(1) if name=='surface_raster_hw' else reg(2),13)
            ds=self.surface[8:12]
            self.boundaries['surfaces']+=1
            self.boundaries['surfaces_cross_depth3720']+=min(ds)<3720<=max(ds)
            return

        def add(kind,depth,opened,zero=0,requested=None):
            c=self.counts[kind]
            c['calls']+=1
            c['requested']+=opened if requested is None else requested
            c['open']+=opened
            c['open_zero']+=zero
            c['light'+str(light(depth))+'_open']+=opened
            c['depth3720_open']+=opened if depth>=3720 else 0
            c['depth3720_calls']+=depth>=3720
            c['light15_nonzero']+=opened-zero if light(depth)==15 else 0
            if self.face and kind!='surface':
                d0,d1=self.face[6:8]
                c['crossing_face_open']+=opened if min(d0,d1)<3720<=max(d0,d1) else 0
                c['crossing_face_far_open']+=opened if min(d0,d1)<3720<=max(d0,d1) and depth>=3720 else 0

        def column(kind,x,begin,count,source,v,step,masked):
            f=self.face
            assert f is not None
            depth=f[6] if f[0]==f[1] else f[6]+trunc((f[7]-f[6])*(x-f[0]),f[1]-f[0])
            bits=words(symbols['coverage_bits']+(x//2)*16,4)
            opened=zero=0
            for k in range(count):
                y=begin+k
                if bits[y//32]&(1<<(y&31)):continue
                opened+=1
                zero+=cpu.mem_read(source+(((v+k*step)&0xffffffff)>>16),1)[0]==0
            add(kind,depth,opened,zero,count)
            self.counts[kind]['writes']+=opened-zero if masked else opened

        if name=='visible_column':
            # Pinned optimized ARM ABI: compiler eliminated constant record/strip_first.
            source,v,step,mode=words(cpu.reg_read(R.UC_ARM_REG_SP),4)
            mode &= 0x7fffffff
            assert mode in (0,1,8), ('unexpected column mode',hex(mode))
            column('masked' if mode else 'opaque',reg(1),reg(2),reg(3)-reg(2)+1,source,v,step,bool(mode))
        elif name=='dok_gba_tiled_rectangle':
            s=words(reg(0),24)
            for x in range(s[5],s[6]+1,2):
                numerator=(s[9]+((x-s[5])//2)*((s[10]+2**31)%2**32-2**31))&0xffffffff
                u=min(numerator//s[12],s[7]-1)
                solid=cpu.mem_read(s[2]+u,1)[0]
                masked=bool(s[18] and not solid)
                column('rectangle_masked' if masked else 'rectangle_opaque',x,s[16],s[15],s[1]+u*s[8],s[13],s[14],masked)
        elif name=='dok_gba_tiled_surface':
            s=words(reg(0),15)
            f=self.surface
            assert f is not None
            ys=f[4:8];lo=min(range(4),key=lambda k:ys[k]);hi=max(range(4),key=lambda k:ys[k])
            d0,d1=abs(f[8+lo]),abs(f[8+hi])
            step=trunc((d1-d0)*65536,ys[hi]-ys[lo])
            depth=(d0*65536+(s[7]-ys[lo])*step)>>16
            count=(s[6]-s[5])//2+1
            opened=zero=0
            for k in range(count):
                if words(s[12]+k*16,1)[0]&s[13]:continue
                opened+=1
                u=((s[8]+k*2*s[10]+2**31)%2**32-2**31)>>16
                v=((s[9]+k*2*s[11]+2**31)%2**32-2**31)>>16
                u=max(0,min(s[3]-1,u));v=max(0,min(s[4]-1,v))
                zero+=cpu.mem_read(s[1]+v*s[3]+u,1)[0]==0
            add('surface',depth,opened,zero,requested=count)
            self.counts['surface']['writes']+=opened

    def result(self):
        return dict(classes={k:dict(v) for k,v in self.counts.items()},boundaries=dict(self.boundaries))


def snapshot(p,level,lang):
    attribution=Attribution()
    result=replay(p,level-1,lang,attribution)
    result.update(attribution.result())
    for band in range(16):
        assert result['classes'].get('surface',{}).get(f'light{band}_open',0)==result['counts'].get(f'ceiling_light{band}_open',0)
    assert sum(c['writes'] for c in result['classes'].values())==p.words('g_render_stats',11)[7], 'Sample attribution does not cover all writes'
    return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    ap.add_argument('--cases',nargs='+',default=CASES+['2:3039:90','3:1623:0','3:1940:270'])
    ap.add_argument('--languages',type=int,nargs='+',default=[0,1])
    ap.add_argument('--moving',action='store_true',help='Live route checkpoints; extra held completion at each 100-frame boundary, no FPS claim')
    ap.add_argument('--cold',action='store_true',help='Invalidate only geometry cache before the audited draw')
    ap.add_argument('--battles',action='store_true',help='Live firing PERF01-03 checkpoints')
    a=ap.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    assert sha(a.build_dir/'dok-diagnostic.gba')=='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247', 'Optimized private ABI is pinned to G22+UI02'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.until(lambda d:d['draws']>=1)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for lang in a.languages:
            p.language(lang)
            if a.moving or a.battles:
                setups=[(2,31,47,90),(3,23,25,0),(3,20,30,270)] if a.battles else [(l,0,0,0) for l in (1,2,3)]
                for level,x,z,heading in setups:
                    freeze(p,True)
                    model=load_json(f'research/evidence/runtime/ru/LEVEL{level}-runtime.json')
                    h=model['objects'][model['player_index']-1]['actor']
                    p.camera(level%3,15120,-300,22800,90)
                    if a.battles:
                        from verify_combat_gba import actor_pose
                        p.camera(level-1,x*480+240,-300,z*480+240,heading)
                        actor_pose(p,model,model['player_index']-1,x*480+240,z*480+240,heading)
                        player=p.player();player[0]=player[2]=32767;p.set_player(player)
                        p.control(1,1,32767)
                    else:p.camera(level-1,h['x'],h['y'],h['z'],h['heading'])
                    p.write(p.symbols['test_camera'],b'\0')
                    freeze(p,False)
                    for segment in range(3 if a.battles else 6):
                        buttons=(8,) if a.battles else (4,) if segment<2 else (8,7) if segment<4 else (4,8,7)
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',0))
                        p.frontend.run(100,buttons=buttons)
                        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
                        previous=p.diag()['draws']
                        d=p.until(lambda d:d['draws']>previous and d['phase']==2,buttons=buttons)
                        result=snapshot(p,level,lang)
                        result.update(level=level,language=lang,segment=segment,diag=d,cell=[x,z] if a.battles else None)
                        rows.append(result)
                        print('moving',lang,level,segment,flush=True)
                freeze(p,True)
                continue
            for case in a.cases:
                level,cell,heading=map(int,case.split(':'))
                p.camera(level%3,15120,-300,22800,90)
                p.camera(level-1,cell%64*480+240,-300,cell//64*480+240,heading)
                for _ in range(8):p.present()
                if a.cold:
                    p.write(p.symbols['hw_static_active'],struct.pack('<I',0));p.present()
                result=snapshot(p,level,lang)
                result.update(level=level,cell=cell,heading=heading,language=lang)
                rows.append(result)
                print(lang,case,{k:(v['open'],v.get('light15_open',0)) for k,v in result['classes'].items()},flush=True)
        result=dict(rom_sha256=sha(p.rom),elf_sha256=sha(a.build_dir/'dok-diagnostic.elf'),
                    core_sha256=sha(core),script_sha256=sha(__file__),rows=rows,moving=a.moving,battles=a.battles,cold_geometry=a.cold,
                    scope='Production ARM replay; frozen coverage-aware foreground counts, not cycle estimates. Hardware base planes excluded: they do not do CPU texture sampling.')
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    finally:p.close()


if __name__=='__main__':main()
