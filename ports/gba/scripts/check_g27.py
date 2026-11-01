"""Pinned G27 sampler captures and exact band-atlas packing (offline only).

No host time is called GBA cycles. Postcoverage packing is an optimistic packing
oracle, not a renderer: its sampled pixels already cost the production raster.
"""
import argparse, ctypes as ct, hashlib, json, os, struct, sys, zlib
from collections import defaultdict
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from audit_raster_work import replay, R, ROOT
from research_gba_fps import GameProbe, freeze
from verify_gba import sha, scene_source_y
from verify_gba_static_cache import Workspace
from verify_sprites_gba import load_json
from check_g22 import CASES

PIN='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
OUT=ROOT/'research/evidence/gba-g27'
BUILD=ROOT/'build/gba/g27/control/hardware/g22_split_active'

class Capture:
    entries=('visible_column','dok_gba_tiled_rectangle')
    def __init__(self,definitions):
        self.definitions=definitions;self.faces=[];self.current=None;self.columns=[]
    def __call__(self,cpu,name,symbols):
        reg=lambda n:cpu.reg_read(getattr(R,f'UC_ARM_REG_R{n}'))
        words=lambda a,n:struct.unpack('<'+'I'*n,cpu.mem_read(a,n*4))
        if name in ('raster','raster_rect'):
            # raster_rect is a nested fast path of raster, not another face.
            # Keep the original projected bbox for admission before cropping.
            if name=='raster_rect':
                assert self.current is not None
                return
            at=reg(1) if name=='raster_rect' else reg(2)
            vals=struct.unpack('<11i2H',cpu.mem_read(at,48));obj=vals[11]
            cls=self.definitions[obj]['class'] if obj<len(self.definitions) else -1
            self.current=len(self.faces)
            self.faces.append(dict(values=vals,object_class=cls,columns=[]))
            return
        if name not in self.entries:return
        def column(x,y,count,source,v,step,masked):
            bits=words(symbols['coverage_bits']+x//2*16,4)
            colors=[];indices=[];open_count=0
            for k in range(count):
                yy=y+k;idx=((v+k*step)&0xffffffff)>>16
                color=cpu.mem_read(source+idx,1)[0]
                opened=not(bits[yy//32]&(1<<(yy&31)))
                open_count+=opened
                # Opaque prelit columns already substitute nontransparent black.
                if opened and (color or not masked):
                    if not color:color=words(symbols['g_hw_tile_black'],1)[0]
                    assert color
                    colors.append(color);indices.append(idx)
                else:colors.append(0);indices.append(None)
            rec=dict(face=self.current,x=x//2,y=y,colors=colors,indices=indices,
                     source=source,v=v,step=step,masked=masked,open=open_count)
            self.faces[self.current]['columns'].append(len(self.columns));self.columns.append(rec)
        if name=='visible_column':
            source,v,step,mode=words(cpu.reg_read(R.UC_ARM_REG_SP),4)
            column(reg(1),reg(2),reg(3)-reg(2)+1,source,v,step,bool(mode&0x7fffffff))
        else:
            s=words(reg(0),24)
            for x in range(s[5],s[6]+1,2):
                num=(s[9]+(x-s[5])//2*ct.c_int32(s[10]).value)&0xffffffff
                u=min(num//s[12],s[7]-1)
                column(x,s[16],s[15],s[1]+u*s[8],s[13],s[14],bool(s[18] and not cpu.mem_read(s[2]+u,1)[0]))

def capture():
    assert sha(BUILD/'dok-diagnostic.gba')==PIN
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    os.environ['DOK_PROBE_FAST_VIDEO']='1';p=GameProbe(core,BUILD);rows=[]
    try:
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for lang in (0,1):
            p.language(lang)
            cases=[]
            for spec in CASES+['2:3039:90','3:1623:0','3:1940:270']:
                lv,cell,heading=map(int,spec.split(':'))
                cases.append((lv,cell%64*480+240,-300,cell//64*480+240,heading,'heavy-'+spec))
            for lv in (1,2,3):
                defs=load_json(f'research/evidence/runtime/ru/LEVEL{lv}-runtime.json')['objects']
                actor=next(o['actor'] for o in defs if o.get('actor') and o['class']==9)
                for distance in (80,160,320):
                    for heading,dx,dz in ((0,0,-distance),(90,distance,0)):
                        cases.append((lv,actor['x']+dx,-300,actor['z']+dz,heading,f'near-{lv}-{distance}-{heading}'))
            for lv,x,y,z,heading,name in cases:
                p.camera(lv%3,15120,-300,22800,90);p.camera(lv-1,x,y,z,heading)
                for _ in range(6):p.present()
                defs=load_json(f'research/evidence/runtime/ru/LEVEL{lv}-runtime.json')['objects']
                obs=Capture(defs);rr=replay(p,lv-1,lang,obs)
                frame=p.frontend.read(p.symbols['g_render_frame']+Workspace.frame.offset,16384)
                # Verify every recorded nonzero contribution against final raster.
                writes=0
                for c in obs.columns:
                    for k,color in enumerate(c['colors']):
                        if not color:continue
                        xx,yy=c['x'],c['y']+k;off=yy//8*1024+xx//8*64+yy%8*8+xx%8
                        assert frame[off]==color,(name,xx,yy,color,frame[off]);writes+=1
                row=dict(name=name,level=lv,language=lang,camera=[x,y,z,heading],
                    faces=obs.faces,columns=obs.columns,verified_writes=writes,
                    production_cycles=p.diag()['last_cycles'],coverage_sha256=rr['coverage_sha256'],
                    foreground_sha256=hashlib.sha256(frame).hexdigest(),stats_exact=rr['stats_exact'])
                rows.append(row);print('captured',lang,name,len(obs.columns),writes,flush=True)
    finally:p.close()
    old=ROOT/'research/experiments/g20a-sector-shear-final-v4/hw_v128_sector_gpu_shear/dok-diagnostic.gba'
    oldsha=sha(old) if old.exists() else None
    if oldsha:assert oldsha=='484099f868157067871d6241b61dfe1afcfd97951f9d0944045c06a6bf9deaae'
    manifest=dict(rom_sha256=PIN,elf_sha256=sha(BUILD/'dok-diagnostic.elf'),core_sha256=sha(core),
        prototype_manifest_sha256=sha(BUILD.parent/'prototype-build.json'),old_shear_sha256=oldsha,
        old_shear_build_report_sha256=sha(old.parent/'build-report.json'),
        old_shear_scope='Historical ROM2775. ROM removed by retention cleanup; source and build report retained. Not reused as a G22 capture.',rows=rows)
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'captures.json.zlib').write_bytes(zlib.compress(json.dumps(manifest,separators=(',',':')).encode(),9))
    return manifest

def affine_fit(values):
    """Optimistic arbitrary-phase 8.8 fit; OAM integer origin not yet constrained.
    A failed fit is a definite rejection of this band; a pass is not PPU proof.
    """
    points=[(i,v) for i,v in enumerate(values) if v is not None]
    if len(points)<2:return True
    low,high=1,32767
    for i,(x,a) in enumerate(points):
        for y,b in points[i+1:]:
            d=y-x
            low=max(low,(256*(b-a)-255+d-1)//d)
            high=min(high,(256*(b-a)+255)//d)
    if low>high:return False
    for pd in range(low,high+1):
        if max(v*256-pd*y for y,v in points)<=min((v+1)*256-1-pd*y for y,v in points):return True
    return False

def pack(layers,merge=False,details=False):
    # 8x8 /16x8 /32x8 legal source tiles, PA128, PD227, doubled OBJ.
    # Dynamic final colors are already sampled; zero is transparent.
    if merge:
        merged=bytearray(120*128)
        for layer in layers:
            for i,v in enumerate(layer):
                if v:assert not merged[i];merged[i]=v
        layers=[merged]
    commands=[];atlas={};raw_bytes=0;reconstructed=[bytearray(120*128) for _ in layers]
    for owner,layer in enumerate(layers):
        for band in range(16):
            tx=0
            while tx<15:
                if not any(layer[(band*8+y)*120+tx*8+x] for y in range(8) for x in range(8)):
                    tx+=1;continue
                run=1
                while run<4 and tx+run<15 and any(layer[(band*8+y)*120+(tx+run)*8+x] for y in range(8) for x in range(8)):run+=1
                width=32 if run>=3 else 16 if run==2 else 8
                # Last screen tile may use padding into logical x120..127.
                payload=bytes(layer[(band*8+y)*120+tx*8+x] if tx*8+x<120 else 0 for y in range(8) for x in range(width))
                raw_bytes+=len(payload);atlas.setdefault((width,payload),len(payload))
                commands.append(dict(owner=owner,x=tx*16,top=band*9-4,width=width*2,height=16,band=band,payload=payload))
                for y in range(8):
                    for x in range(min(width,120-tx*8)):
                        value=payload[y*width+x];idx=(band*8+y)*120+tx*8+x
                        assert reconstructed[owner][idx] in (0,value);reconstructed[owner][idx]=value
                tx+=width//8
    assert all(a==b for a,b in zip(layers,reconstructed)),'Exact atlas reconstruction'
    # Pixel oracle through actual PA/PD transform, including band boundaries.
    expected=bytearray(240*144);actual=bytearray(240*144)
    for y in range(144):
        sy=scene_source_y(y,64)
        for x in range(240):
            colors=[v[sy*120+x//2] for v in layers if v[sy*120+x//2]]
            assert len(colors)<=1
            if colors:expected[y*240+x]=colors[0]
    for cmd in commands:
        owner,band=cmd['owner'],cmd['band'];w=cmd['width']//2
        for y in range(max(0,cmd['top']),min(144,cmd['top']+16)):
            sy=((y-cmd['top']-8)*227+1024)>>8
            if not 0<=sy<8:continue
            for x in range(cmd['x'],min(240,cmd['x']+cmd['width'])):
                sx=(x-cmd['x'])//2
                color=layers[owner][(band*8+sy)*120+cmd['x']//2+sx]
                if color:assert actual[y*240+x] in (0,color);actual[y*240+x]=color
    assert actual==expected,'Affine mapping/alpha silhouette'
    count=len(commands);scan=[2*(64+count) for _ in range(144)]
    for y in range(144):scan[y]+=136 if y<16 else 48 if y>=128 else 0
    for cmd in commands:
        for y in range(max(0,cmd['top']),min(144,cmd['top']+16)):scan[y]+=cmd['width']*2+8
    reasons=[]
    if count>64:reasons.append('OAM')
    if sum(atlas.values())>16384:reasons.append('atlas')
    if max(scan)>1210:reasons.append('scanline')
    result=dict(objects=count,unique_atlas_bytes=sum(atlas.values()),raw_atlas_bytes=raw_bytes,
                matrices=1,peak_scan_cycles_model=max(scan),failed=reasons,capacity=not reasons,
                exact_pixels=sum(bool(x) for x in expected),pixel_sha256=hashlib.sha256(expected).hexdigest())
    if details:result.update(commands=commands,expected=expected)
    return result

def analyze(captures):
    assert captures['rom_sha256']==PIN,'Capture source pin differs'
    rows=[]
    for row in captures['rows']:
        layers=[];dynamic_columns=0;source_fit=source_bad=source_conflict=0;open_samples=0;visible_samples=0
        early_tiles=0;face_visits=0
        for face in row['faces']:
            # class2 walls remain CPU in the sector-fed candidate. Include every
            # other original face; no visibility pixel is dropped to fit caps.
            if face['object_class']==2:continue
            f=face['values'];face_visits+=1
            lo=max(0,min(f[0],f[1])//2);hi=min(119,max(f[0],f[1])//2)
            top=max(0,min(f[2],f[3]));bottom=min(127,max(f[4],f[5]))
            if hi>=lo and bottom>=top:early_tiles+=((hi//8-lo//8+1+3)//4)*(bottom//8-top//8+1)
            layer=bytearray(120*128)
            for ci in face['columns']:
                c=row['columns'][ci];dynamic_columns+=1;open_samples+=c['open']
                for k,v in enumerate(c['colors']):
                    layer[(c['y']+k)*120+c['x']]=v;visible_samples+=bool(v)
                # Can a copied source column, not a pre-sampled result, reproduce
                # a nine-line presentation band with one affine vertical mapping?
                for band in range(16):
                    values=[];masked={};conflict=False
                    for y in range(band*9,band*9+9):
                        sy=scene_source_y(y,64);k=sy-c['y']
                        if not 0<=k<len(c['colors']):values.append(None);continue
                        idx=((c['v']+k*c['step'])&0xffffffff)>>16
                        active=bool(c['colors'][k]);values.append(idx if active else None)
                        if idx in masked and masked[idx]!=active:conflict=True
                        masked[idx]=active
                    if any(x is not None for x in values):
                        source_fit+=1;source_bad+=not affine_fit(values);source_conflict+=conflict
            if any(layer):layers.append(layer)
        separate=pack(layers);merged=pack(layers,True)
        early_accept=early_tiles<=64
        rows.append(dict(name=row['name'],level=row['level'],language=row['language'],
            production_cycles=row['production_cycles'],visible_dynamic_faces=len(layers),face_visits=face_visits,
            dynamic_columns=dynamic_columns,open_samples=open_samples,visible_samples=visible_samples,
            source_bands=source_fit,source_affine_rejects=source_bad,source_alpha_conflicts=source_conflict,
            separate=separate,merged=merged,early_estimated_objects=early_tiles,early_accept=early_accept,
            early_false_accept=early_accept and not separate['capacity'],early_false_reject=not early_accept and separate['capacity']))
        print('packed',row['language'],row['name'],separate['objects'],separate['peak_scan_cycles_model'],merged['objects'],source_bad,flush=True)
    result=dict(task='G27',status='passed',rom_sha256=PIN,capture_sha256=sha(OUT/'captures.json.zlib'),
        rows=rows,limitations=['Offline oracle consumes production coverage and samples; not GPU speedup.',
        'Source affine fit uses optimistic arbitrary phase: success is not proof of OAM-origin feasibility.',
        'Scanline cycle model reserves 64 UI slots, 136 top/48 bottom pixels; weapon not included, so capacity is optimistic.',
        'Early bbox admission is measured against the concrete separate-band packer, not a universal optimum.'])
    (OUT/'packing.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--replay-only',action='store_true');a=ap.parse_args();os.chdir(ROOT)
    data=json.loads(zlib.decompress((OUT/'captures.json.zlib').read_bytes())) if a.replay_only else capture()
    analyze(data)
if __name__=='__main__':main()
