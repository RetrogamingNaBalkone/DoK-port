"""Native Mode2/BG2/BG3/OBJ composition, source mips and sector clip contract."""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha
from verify_gba_static_cache import Workspace
from verify_gba_sector import State,div,ppu_check
from verify_sprites_gba import load_json
from verify_renderer_gba import stack_usage


U8,U16,U32,I16=ct.c_uint8,ct.c_uint16,ct.c_uint32,ct.c_int16
class Patch(ct.Structure):
    _fields_=[('source',U32),('face',U16),('sx',U16),('sy',U16),
        ('width',U8),('height',U8),('source_width',U8),('shade',U8),('black',U8),('reserved',U8)]
class GPU(ct.Structure):
    _fields_=[('valid',U32),('failed',U32),('count',U32),('matrices',U32),('bytes',U32),('view',U32),
        ('oam',(U16*4)*128),('matrix',(I16*2)*32),('scan',U16*144),('texture',U8*16384),
        ('ui_cost',U16*144),('patch',Patch*64),('top',U8*120),('bottom',U8*120),('deny_first',U8*120),('deny_last',U8*120)]


def cut(s,hist,x,z,cy):
    for rank in reversed(range(s.counts[x])):
        near,far=struct.unpack_from('<HH',hist,(x*64+rank)*4)
        if near&32767>z:continue
        result=s.history_top[x][rank]
        if z<far:
            yy=(((-561-cy)*(6553600//(z+200)))>>15)+79
            result=min(128,max(0,yy*4//5+1))
            if rank:result=max(result,s.history_top[x][rank-1])
        return result
    return 0


def verify(p,w,s,g,model,camera,pcs,pressure=False):
    video=p.frontend.read(0x06000000,98304);oam=p.frontend.read(0x07000000,1024)
    palette=struct.unpack('<512H',p.frontend.read(0x05000000,1024));actual=p.frontend.colors555()
    assert struct.unpack('<H',p.frontend.read(0x04000000,2))[0]==0x1c42
    assert p.frontend.read(0x0600c000,16384)==bytes(w.frame)[:16384],'CPU BG tile upload'
    assert struct.unpack('<4h',p.frontend.read(0x04000020,8))==(128,0,0,228)
    front=p.words('hw_bg_front',1)[0];base=p.symbols['hw_bg_frames']+front*2900
    rows=[struct.unpack_from('<4h2i',p.frontend.read(base,2576),y*16) for y in range(161)]
    cnt=struct.unpack('<161H',p.frontend.read(base+2576,322));gpu=bytearray(38400);gpu_owner=bytearray(38400);source_checked=clip_checked=0
    history=p.frontend.read(p.words('sector_history_rows',1)[0],120*64*4)
    if not g.failed:
        assert g.count<=64 and g.matrices<=32 and g.bytes<=16384
        assert video[0x10000:0x10000+g.bytes]==bytes(g.texture)[:g.bytes],'GPU texture upload'
        for rank in reversed(range(g.count)):
            a,b,c=struct.unpack_from('<3H',oam,(rank+64)*8)
            assert [a,b,c]==list(g.oam[rank][:3])
            shape,size=a>>14,b>>14
            dims={(0,0):(8,8),(0,3):(64,64),(2,0):(8,16),(2,1):(8,32),(2,2):(16,32),(2,3):(32,64)}
            width,height=dims[shape,size];dbl=bool(a&512);cw,ch=width*(2 if dbl else 1),height*(2 if dbl else 1)
            left=b&511;top=a&255
            if left>=256:left-=512
            if top>=160:top-=256
            mi=(b>>9)&31;pa,pb,pc,pd=(struct.unpack_from('<h',oam,mi*32+k)[0] for k in (6,14,22,30))
            assert [pa,pd]==list(g.matrix[mi]) and pb==0 and pc==pcs[mi]
            patch=g.patch[rank];assert patch.face<w.face_count and patch.shade<16
            f=w.faces[patch.face];im=w.images[f.s[0]] if f.s[0]<256 else w.instance_images[f.s[0]-256]
            original=p.frontend.read(im.p[0],im.s[0]*im.s[1]);srcw=patch.source_width;srch=srcw*2
            pad_x=(width-patch.width)//2;pad_y=(height-patch.height)//2
            tile_at=(c&1023)*32
            for ty in range(height):
                for tx in range(width):
                    off=(ty>>3)*width*8+(tx>>3)*64+(ty&7)*8+(tx&7)
                    color=video[0x10000+tile_at+off]
                    if not(pad_x<=tx<pad_x+patch.width and pad_y<=ty<pad_y+patch.height):
                        assert color==0,('nonzero source padding',rank,tx,ty);continue
                    if not color:continue
                    u=(patch.sx+tx-pad_x)*(im.s[0]-1)//(srcw-1);v=(patch.sy+ty-pad_y)*(im.s[1]-1)//(srch-1)
                    expected=w.lights[patch.shade*256+original[v*im.s[0]+u]] or patch.black
                    assert color==expected,('original mip/LIGHT source',rank,tx,ty,color,expected)
                    source_checked+=1
            n=[ct.c_int32(v).value for v in f.n];x0,x1,t0,t1,b0,b1,z0,z1=n[:8]
            if x0>x1:x0,x1,t0,t1,b0,b1,z0,z1=x1,x0,t1,t0,b1,b0,z1,z0
            q0=max(1,6553600//(z0+200)>>8);q1=max(1,6553600//(z1+200)>>8)
            for yy in range(max(0,top),min(144,top+ch)):
                for xx in range(max(0,left),min(240,left+cw)):
                    u=(pa*(xx-left-cw//2)>>8)+width//2
                    v=((pc*(xx-left-cw//2)+pd*(yy-top-ch//2))>>8)+height//2
                    if not 0<=v<height:continue
                    if not 0<=u<width:continue
                    color=video[0x10000+tile_at+(v>>3)*width*8+(u>>3)*64+(v&7)*8+(u&7)]
                    if not color:continue
                    logical=xx&~1;native_y=yy*228>>8
                    assert x0<=logical<=x1,('GPU horizontal face extent',rank,xx)
                    a0=q0*(x1-logical);a1=q1*(logical-x0);z=div(z0*a0+z1*a1,a0+a1)
                    cutoff=cut(s,history,xx//2,z,camera[1])
                    assert native_y>=cutoff,('GPU above sector clip',rank,xx,yy,native_y,cutoff)
                    assert not(z>=s.wall_z[xx//2] and s.wall_first[xx//2]<=native_y<=s.wall_last[xx//2]),('GPU through wall',rank,xx,yy)
                    clip_checked+=1;gpu[yy*240+xx]=color;gpu_owner[yy*240+xx]=rank+64
    else:assert g.count==g.bytes==g.matrices==0
    ui=p.frontend.mode2_ui_indices();payload=bytearray();base_checked=0
    ui_cost=[0]*144
    for i in range(64):
        a,b,c=struct.unpack_from('<3H',oam,i*8)
        if a&512:continue
        ow=8 if a>>14==0 else 16 if b>>14==0 else 32
        top=a&255
        if top>=160:top-=256
        for y in range(max(0,top),min(144,top+8)):ui_cost[y]+=ow
    assert all(ui_cost[y]<=g.ui_cost[y] for y in range(144)),'Native UI exceeded admission reservation'
    mapping=p.frontend.read(p.symbols['dok_hw_bg_maps']+g.view*512,512)
    phases=p.frontend.read(p.symbols['hw_bg_phases'],8)
    atlas={};offsets=p.words('dok_hw_bg_offsets',(g.view+1)*432)
    for mat in range(p.frontend.read(p.symbols['dok_hw_bg_counts']+g.view,1)[0]):
        for band in range(3):
            off=offsets[g.view*432+mat*54+phases[mat]*3+band]
            atlas[mat,band]=p.frontend.read(p.symbols['dok_hw_bg_bank']+off,1024)
    defs={o['glyph']:o for o in model['objects']};tmap=bytes.fromhex(model['maps'][1]);fc=bytes.fromhex(model['maps'][2])
    origin_x=ct.c_int32(p.words('hw_bg_x',1)[0]).value;origin_z=ct.c_int32(p.words('hw_bg_z',1)[0]).value
    stopped_after=[127]*160
    if pressure:
        active=[]
        for idx in range(128):
            a,b,c=struct.unpack_from('<3H',oam,idx*8)
            affine=bool(a&256)
            if not affine and a&512:continue
            shape,size=a>>14,b>>14
            dimensions={0:((8,8),(16,16),(32,32),(64,64)),1:((16,8),(32,8),(32,16),(64,32)),2:((8,16),(8,32),(16,32),(32,64))}
            ow,oh=dimensions[shape][size]
            if affine and a&512:ow*=2;oh*=2
            ox=b&511;oy=a&255
            if ox>=256:ox-=512
            if oy>=160 and oy+oh<228:continue
            if (b&511)>=240 and (b&511)+ow<512:continue
            if oy+oh>256:oy-=256
            cost=(8+ow*2+(ox if ox<0 else 0)) if affine else ow-2+(ox//2 if ox<0 else 0)
            active.append((idx,oy,oh,cost))
        for y in range(160):
            remaining=1210;last=0;stop=-1
            for idx,oy,oh,cost in active:
                remaining-=2*(idx-last);last=idx
                if remaining<=0:break
                stop=idx
                if oy<=y<oy+oh:remaining-=cost
            stopped_after[y]=stop
    ppu_mismatches=capacity_mismatches=0
    for y in range(160):
        pa,pb,pc,pd,rx,ry=rows[y];control=cnt[y];screen=(control>>8)&31
        for x in range(240):
            u=((rx+x*pa)>>8)&511;v=((ry+x*pc)>>8)&511
            tile=video[screen*2048+(v>>3)*64+(u>>3)]
            bg=video[((control>>2)&3)*16384+tile*64+(v&7)*8+(u&7)]
            plane=(screen-12)//6;band=((screen-12)%6)//2
            wx=origin_x+(((u>>5)-(origin_x&15))&15);wz=origin_z+(((v>>5)-(origin_z&15))&15);cell=wz*64+wx
            mat=mapping[plane*256+fc[cell]];expected=0
            if defs[tmap[cell]]['class']!=2 and mat!=255:
                a=atlas[mat,band];expected=a[((v&31)>>3)*256+((u&31)>>3)*64+(v&7)*8+(u&7)]
            assert bg==expected,('BG3 original material/relocated map',x,y,bg,expected,screen,wx,wz)
            base_checked+=1
            sy=y*228>>8;sx=x//2;fg=0
            if sy<128:
                tile=video[0x5800+(sy>>3)*16+(sx>>3)]
                fg=video[0xc000+tile*64+(sy&7)*8+(sx&7)]
            i=y*240+x;color=palette[256+ui[i]] if ui[i] else palette[256+gpu[i]] if gpu[i] else palette[fg or bg]
            if actual[i]!=color:
                ppu_mismatches+=1
                if gpu_owner[i]>stopped_after[y]:capacity_mismatches+=1
                assert pressure,('actual Mode2/BG2/BG3/OBJ PPU',x,y,actual[i],color)
            if ui[i]:payload+=struct.pack('<IH',i,palette[256+ui[i]])
    mask=bytearray(bool(c) for c in gpu)
    if g.failed:
        native=struct.unpack('<480I',bytes(g.texture)[:1920])
        for y in range(144):
            sy=y*228>>8
            for x in range(240):mask[y*240+x]=bool(native[x//2*4+(sy>>5)]&(1<<(sy&31)))
    packed=bytes(sum(mask[i*8+b]<<b for b in range(8)) for i in range(4800))
    return dict(source_texels=source_checked,gpu_clip_pixels=clip_checked,base_pixels=base_checked,effective_mask=packed.hex(),
        ppu_pixels=38400,ppu_pressure_mismatches=ppu_mismatches,ppu_capacity_tail_mismatches=capacity_mismatches,
        gpu_pieces=g.count,gpu_fallback=bool(g.failed),overlay_sha256=hashlib.sha256(payload).hexdigest())


def run(build,core,out,trial,pressure=False):
    p=GameProbe(core,build);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        if trial:p.write(p.symbols['g_sector_gpu_trace'],struct.pack('<I',1))
        if pressure and trial:p.write(p.symbols['g_sector_gpu_pressure_probe'],struct.pack('<I',1))
        for language in ((0,) if pressure else (0,1)):
            p.language(language)
            for level in (1,2,3):
                p.camera(level%3,15120,-300,22800,90);model=load_json(f'research/evidence/runtime/ru/LEVEL{level}-runtime.json')
                hero=model['objects'][model['player_index']-1]['actor'];cell=hero['z']//480*64+hero['x']//480
                cases=[(cell,h,False) for h in (0,45,90,135)]
                heavy={1:[(520,225),(3640,225)],2:[(1592,135),(2871,225)],3:[(1544,315)]}[level]
                cases += [(c,h,False) for c,h in heavy]+[(cell,135,True)]
                cases=[(c,h,cycle,None) for c,h,cycle in cases]
                cases += [(cell,90,False,f) for f in ((1,0,0),(1,8,2),(2,2,0))]
                if pressure:cases=[(c,h,False,None) for c,h in heavy]
                for cell,heading,cycle,fixture in cases:
                    camera=(cell%64*480+240,-300,cell//64*480+240,heading);p.camera(level-1,*camera)
                    if fixture:
                        slot,frame,shift=fixture;p.control(1,slot,1000)
                        p.write(p.symbols['g_combat']+10798,struct.pack('<HH',shift,frame))
                    if cycle:
                        for state in (3,0):
                            request=p.words('g_ui_probe',2)[1]+1;p.write(p.symbols['g_ui_probe']+8,struct.pack('<4I',1,state,0,0))
                            p.write(p.symbols['g_ui_probe'],struct.pack('<I',request));p.until(lambda d:p.words('g_ui_probe',2)[1]==request and d['phase']==2)
                            p.present();p.present()
                    for _ in range(4):d=p.present()
                    assert d['guard_ok']==1 and d['phase']==2
                    address=p.symbols['g_render_frame'];w=Workspace.from_buffer_copy(p.frontend.read(address,ct.sizeof(Workspace)))
                    faces=[bytes(f) for f in w.faces[:w.face_count] if f.s[0]>=256 or model['objects'][f.s[0]]['class']!=2]
                    row=dict(level=level,language=language,cell=cell,heading=heading,mode4_cycle=cycle,
                        weapon_fixture=fixture,
                        dynamic_sha256=hashlib.sha256(b''.join(faces)).hexdigest(),
                        lease_sha256=hashlib.sha256(bytes(w.image_known)+bytes(w.surface_known)).hexdigest())
                    if trial:
                        sp=p.words('sector',1)[0];gp=p.words('g_sector_gpu',1)[0]
                        shear='g_sector_gpu_shear_work' in p.symbols
                        assert sp%4==gp%4==0 and sp+ct.sizeof(State)<=gp and gp+ct.sizeof(GPU)+(64 if shear else 0)<=address+Workspace.surfaces.offset
                        s=State.from_buffer_copy(p.frontend.read(sp,ct.sizeof(State)));g=GPU.from_buffer_copy(p.frontend.read(gp,ct.sizeof(GPU)))
                        pcs=struct.unpack('<32h',p.frontend.read(gp+ct.sizeof(GPU),64)) if shear else [0]*32
                        if shear:
                            work=list(p.words('g_sector_gpu_shear_work',8));assert work[2]<=2 and work[3]<=2
                            row.update(shear_work=work,gpu_work=list(p.words('g_sector_gpu_work',12)),nonzero_pc_matrices=sum(v!=0 for v in pcs[:g.matrices]))
                        search=2*(64+g.count) if shear and not g.failed else 128 if shear else 256
                        assert g.valid and (pressure or all(g.scan[y]+g.ui_cost[y]+search<=1210 for y in range(144)))
                    p.frontend.run(2)
                    if trial:row.update(verify(p,w,s,g,model,camera,pcs,pressure))
                    else:row['overlay_sha256']=ppu_check(p,False)
                    suffix='none' if fixture is None else '-'.join(map(str,fixture))
                    image=out/f'gpu-qa-{build.name}-{language}-{level}-{cell}-{heading}-{int(cycle)}-{suffix}.png';p.frontend.screenshot(image)
                    row.update(image=str(image),image_sha256=sha(image));rows.append(row)
        return dict(rom_sha256=sha(p.rom),rows=rows,stack=stack_usage(p.frontend))
    finally:p.close()


def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--build-dir',type=Path,required=True)
    a.add_argument('--reference-build',type=Path,required=True);a.add_argument('--report',type=Path,required=True)
    a.add_argument('--cpu-build',type=Path)
    a.add_argument('--pressure',action='store_true',help='Non-FPS diagnostic: ignore scan admission, quantify actual PPU loss for capped command set')
    a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before=run(a.reference_build,core,a.report.parent,False,a.pressure);after=run(a.build_dir,core,a.report.parent,True,a.pressure)
    for b,t in zip(before['rows'],after['rows']):
        for key in ('dynamic_sha256','lease_sha256','overlay_sha256'):assert b[key]==t[key],(key,t)
    cpu=run(a.cpu_build,core,a.report.parent,True,a.pressure) if a.cpu_build else None
    if cpu:
        for reference,trial in zip(cpu['rows'],after['rows']):
            left=bytes.fromhex(reference['effective_mask']);right=bytes.fromhex(trial['effective_mask'])
            trial['missing_physical_mask_pixels']=sum((x&~y).bit_count() for x,y in zip(left,right))
            trial['extra_physical_mask_pixels']=sum((y&~x).bit_count() for x,y in zip(left,right))
            trial['cpu_dynamic_physical_pixels']=sum(x.bit_count() for x in left)
            missing=extra=0
            def bit(raw,x,y):return 0<=x<240 and 0<=y<144 and bool(raw[(y*240+x)>>3]&(1<<((y*240+x)&7)))
            for y in range(144):
                for x in range(240):
                    expected=bit(left,x,y);actual=bit(right,x,y)
                    if expected and not actual and all(bit(left,x+dx,y+dy) for dy in range(-2,3) for dx in range(-2,3)):missing+=1
                    if actual and not expected and not any(bit(left,x+dx,y+dy) for dy in range(-2,3) for dx in range(-2,3)):extra+=1
            trial['missing_interior_radius2_pixels']=missing;trial['extra_beyond_radius2_pixels']=extra
            del reference['effective_mask'];del trial['effective_mask']
    a.report.write_text(json.dumps(dict(before=before,after=after,cpu=cpu,pressure_probe=a.pressure,script_sha256=sha(__file__),core_sha256=sha(core),
        scope=('Diagnostic pressure probe: scan rejection bypassed and hard-cap commands skipped. Counts actual PPU mismatch against the uploaded command set; capacity-tail classification follows the documented mGBA OAM traversal/cycle model. Not FPS or complete scene/quality acceptance. ' if a.pressure else '')+
        'Original dynamic geometry/leases/native HUD, actual Mode2 BG2/BG3/OBJ composition, original BG3 materials after VRAM relocation, nonzero original mip/LIGHT source texels and static sector clip contract for GPU pixels. Source padding/VRAM/OAM/matrices/guards. Normal runs additionally check scan budget and Mode4 cycles. Not an independent proof of static histories or complete dynamic visibility; no gameplay/physical GBA acceptance.'),indent=2)+'\n',encoding='utf-8')
    print('Passed',len(after['rows']),'poses; GPU frames',sum(bool(r['gpu_pieces']) for r in after['rows']))


if __name__=='__main__':main()
