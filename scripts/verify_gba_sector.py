"""Native sector state, independent original-segment rays and plane pixels."""
import argparse
import ctypes as ct
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
from compile_gba_render_scene import compile_view
from research_gba_fps import GameProbe, ROOT, freeze
from verify_gba import VIDEO, sha
from verify_gba_static_cache import Workspace
from verify_gba_columns import ray_geometry, ceiling_pixel, dynamic_events
from verify_sprites_gba import load_json
from verify_renderer_gba import stack_usage
from verify_gba_columns import div


def rim_edges(scene,camera,ray,envelope):
    """Original opaque intersections, used only to identify raster edge rows."""
    result=[];ox,oz,dx,dz=ray
    for a in scene['source_pieces']:
        if a['height']<=10:continue
        cx,cz=a['cell']%64,a['cell']//64
        if not(envelope[0]<=cx<=envelope[1] and envelope[2]<=cz<=envelope[3]):continue
        axis=a['axis'];direction=dx if axis==0 else dz
        if not direction:continue
        t=(a['fixed']*65536-(ox if axis==0 else oz))/direction
        if t<0:continue
        other=(oz+dz*t if axis==0 else ox+dx*t)/65536
        if not a['lo']-1<=other<=a['hi']+1:continue
        z=t/2
        for height in (a['y'],a['y']-a['height']):
            result.append((math.floor((height-camera[1])*200/(z+200)+79)*4//5,z))
    return result


def boundary_wall(scene,ray,envelope,actual):
    """Only a <=2 world-unit segment endpoint ambiguity may explain an edge."""
    ox,oz,dx,dz=ray
    for a in scene['source_pieces']:
        if a['kind']!='wall':continue
        cx,cz=a['cell']%64,a['cell']//64
        if not(envelope[0]<=cx<=envelope[1] and envelope[2]<=cz<=envelope[3]):continue
        direction=dx if a['axis']==0 else dz
        if not direction:continue
        t=(a['fixed']*65536-(ox if a['axis']==0 else oz))/direction
        if t<0 or abs(actual-t/2)>8:continue
        other=(oz+dz*t if a['axis']==0 else ox+dx*t)/65536
        if min(abs(other-a['lo']),abs(other-a['hi']))<=2:return True
    return False


def bitmap_planes(p,w,camera,trig,env,model,view,coverage):
    """World rays select original map glyphs, not compiled material-cell bins."""
    cx,cy,cz,heading=camera;sine,cosine=trig[heading],trig[361+heading]
    maps=bytes.fromhex(model['maps'][1]);fc=bytes.fromhex(model['maps'][2])
    defs={o['glyph']:o for o in model['objects']}
    mapping=(p.rom.parent.parent/'hw-bg-maps.bin').read_bytes()[view*512:(view+1)*512]
    animated=p.frontend.read(p.symbols['dok_hw_bg_animated']+view*8,8)
    objects=struct.unpack('<8H',p.frontend.read(p.symbols['dok_hw_bg_objects']+view*16,16))
    count=p.frontend.read(p.symbols['dok_hw_bg_counts']+view,1)[0];textures={}
    for mat in range(count):
        phase=0
        if animated[mat]:
            raw=p.frontend.read(p.symbols['g_render_world']+4+objects[mat]*36,16)
            frame=struct.unpack_from('<H',raw,2)[0];known=struct.unpack_from('<H',raw,12)[0]
            phase=struct.unpack_from('<H',raw,8)[0]%18 if known&8 else frame%18
        for band in range(3):
            off=p.words('dok_hw_bg_offsets',view*432+mat*54+phase*3+band+1)[-1]
            textures[mat,band]=p.frontend.read(p.symbols['dok_hw_bg_bank']+off,1024)
    black=p.words('g_hw_tile_black',1)[0];frame=bytes(w.frame);checked=0
    for y in range(128):
        dy=y*5//4-79;z=div(((-951 if dy<0 else 0)-cy)*200,dy)-200 if dy else -1
        valid=0<z<=3600
        if valid:
            distance=z+200;lateral=-distance*52101;step=div(distance*65536,75)
            wx=div(cx*65536,480)+div((lateral*cosine)>>15,480)-div(sine*z*2,480)
            wz=div(cz*65536,480)+div((lateral*sine)>>15,480)+div(cosine*z*2,480)
            dx=div((step*cosine)>>15,480);dz=div((step*sine)>>15,480)
            light=min(15,max(0,(z-1920)//120));band=0 if light<4 else 1 if light<11 else 2
        for x in range(120):
            if coverage[x*4+(y>>5)]&(1<<(y&31)):continue
            expected=0
            if valid:
                xx=(wx+x*dx)>>16;zz=(wz+x*dz)>>16;expected=black
                if env[0]<=xx<=env[1] and env[2]<=zz<=env[3]:
                    cell=zz*64+xx;mat=mapping[(dy<0)*256+fc[cell]]
                    if defs[maps[cell]]['class']!=2 and mat!=255:
                        u=((wx+x*dx)&65535)>>11;v=((wz+x*dz)&65535)>>11
                        expected=textures[mat,band][(v>>3)*256+(u>>3)*64+(v&7)*8+(u&7)] or black
            assert frame[y*128+x]==expected,('software base plane',camera,x,y,frame[y*128+x],expected)
            checked+=1
    return checked


def ppu_check(p,bitmap):
    video=p.frontend.read(0x06000000,98304)
    palette=struct.unpack('<512H',p.frontend.read(0x05000000,1024))
    overlay=p.frontend.mode2_ui_indices() if bitmap else bytearray(38400)
    if not bitmap:
        for y in range(160):
            for x in range(240):
                tile=struct.unpack_from('<H',video,0x7800+(y//8*32+x//8)*2)[0]&1023
                overlay[y*240+x]=video[tile*64+(y&7)*8+(x&7)]
    payload=bytearray()
    if bitmap:
        actual=p.frontend.colors555();dispcnt=struct.unpack('<H',p.frontend.read(0x04000000,2))[0]
        assert dispcnt in (0x1444,0x1454),hex(dispcnt)
        assert struct.unpack('<4h',p.frontend.read(0x04000020,8))==(128,0,0,228)
        page=0xa000 if dispcnt&16 else 0
    for i,color in enumerate(overlay):
        if color:payload+=struct.pack('<IH',i,palette[256+color] if bitmap else palette[color])
        if bitmap:
            x,y=i%240,i//240
            expected=palette[256+color] if color else palette[video[page+((y*228)>>8)*240+x//2]]
            assert actual[i]==expected,('Mode4/OBJ PPU composition',x,y,actual[i],expected)
    return hashlib.sha256(payload).hexdigest()


def sector_events(p,w,model,x):
    """Original affine LIGHT interpolation with perspective UV/depth."""
    one=Workspace.from_buffer_copy(bytes(w));one.face_count=1;result=[]
    offsets=p.words('prelit_view',1)[0];bank=p.symbols['dok_prelit_bank']
    for f in w.faces[:w.face_count]:
        one.faces[0]=f;events=dynamic_events(p,one,model,x)
        if not events:continue
        e=list(events[0]);x0,x1=f.n[0],f.n[1];z0,z1=f.n[6],f.n[7]
        x0,x1,z0,z1=(ct.c_int32(n).value for n in (x0,x1,z0,z1))
        if x0>x1:x0,x1,z0,z1=x1,x0,z1,z0
        z=z0+div((z1-z0)*(x-x0),x1-x0);row=min(15,max(0,(z-1920)//120))
        prior=min(15,max(0,(e[4]-1-1920)//120))
        im=w.images[f.s[0]] if f.s[0]<256 else w.instance_images[f.s[0]-256]
        if im.p[1] and im.s[3]:
            a=struct.unpack('<I',p.frontend.read(offsets+(im.s[3]*16+prior)*4,4))[0]
            b=struct.unpack('<I',p.frontend.read(offsets+(im.s[3]*16+row)*4,4))[0]
            if a and b:e[0]+=b-a
        elif e[1]:e[1]=p.symbols['g_render_frame']+Workspace.lights.offset+row*256
        result.append(e)
    return result


class Ray(ct.Structure):
    _fields_=[(k,ct.c_int32) for k in ('ox','oz','dx','dz','ix','iz')]


class State(ct.Structure):
    _fields_=[('ray',Ray*120),('roof',(ct.c_uint32*4)*120),('closed',ct.c_uint32*4),
        ('wall_z',ct.c_uint16*120),('ceiling_depth',ct.c_uint16*128),
        ('top',ct.c_uint8*120),('counts',ct.c_uint8*120),
        ('wall_first',ct.c_uint8*120),('wall_last',ct.c_uint8*120),
        ('history_top',(ct.c_uint8*64)*120),('dynamic',ct.c_int32*6)]


def run(build,core,out,scenes,trig,trial):
    p=GameProbe(core,build);rows=[];fallbacks=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for language in (0,1):
            p.language(language)
            for level in (1,2,3):
                p.camera(level%3,15120,-300,22800,90)
                model=load_json(f'build/runtime/ru/LEVEL{level}-runtime.json')
                hero=model['objects'][model['player_index']-1]['actor'];cell=hero['z']//480*64+hero['x']//480
                cases=[(cell,h,False) for h in (0,45,90,135)]
                heavy={1:[(520,225),(3640,225)],2:[(1592,135),(2871,225)],3:[(1544,315)]}[level]
                cases += [(c,h,False) for c,h in heavy]+[(heavy[-1][0],heavy[-1][1],True)]
                for cell,heading,cycle in cases:
                    camera=(cell%64*480+240,-300,cell//64*480+240,heading)
                    p.camera(level-1,*camera)
                    if cycle:
                        for state in (3,0):
                            request=p.words('g_ui_probe',2)[1]+1
                            p.write(p.symbols['g_ui_probe']+8,struct.pack('<4I',1,state,0,0))
                            p.write(p.symbols['g_ui_probe'],struct.pack('<I',request))
                            p.until(lambda d:p.words('g_ui_probe',2)[1]==request and d['phase']==2)
                            p.present();p.present()
                    for _ in range(4):d=p.present()
                    assert d['guard_ok']==1 and d['phase']==2
                    address=p.symbols['g_render_frame'];w=Workspace.from_buffer_copy(p.frontend.read(address,ct.sizeof(Workspace)))
                    dynamic=[bytes(f) for f in w.faces[:w.face_count] if f.s[0]>=256 or model['objects'][f.s[0]]['class']!=2]
                    row=dict(level=level,language=language,cell=cell,heading=heading,mode4_cycle=cycle,
                        dynamic_sha256=hashlib.sha256(b''.join(dynamic)).hexdigest(),
                        lease_sha256=hashlib.sha256(bytes(w.image_known)+bytes(w.surface_known)).hexdigest())
                    if trial:
                        work=list(p.words('g_sector_work',12));assert work[0]==1 and work[11]==0,work
                        ptr=p.words('sector',1)[0];assert ptr%4==0
                        assert address+Workspace.covered.offset<=ptr and ptr+ct.sizeof(State)<=address+Workspace.surfaces.offset
                        s=State.from_buffer_copy(p.frontend.read(ptr,ct.sizeof(State)))
                        histories=p.frontend.read(p.words('sector_history_rows',1)[0],120*64*4)
                        frame=bytes(w.frame);black=p.words('g_hw_tile_black',1)[0]
                        coverage=struct.unpack('<480I',p.frontend.read(p.symbols['coverage_bits'],1920))
                        env=[ct.c_int32(n).value for n in p.words('g_hw_bg_envelope',4)]
                        roof_object=p.words('sector_ceiling_object',1)[0]
                        im=w.surface_images[roof_object*3+1] if roof_object!=65535 else None
                        pixels=p.frontend.read(im.p,im.s[0]*im.s[1]) if im else None
                        bitmap='dok_gba_bitmap_column' in p.symbols
                        checked=roof_checked=plane_pixels=dynamic_pixels=rim_boundary_rows=wall_boundary_columns=0
                        grid_boundary_rows=adjacent_roof_rows=wall_depth_edge_rows=0;errors=[]
                        order=struct.unpack('<'+'H'*w.face_count,p.frontend.read(p.symbols['face_order'],w.face_count*2))
                        ordered=Workspace.from_buffer_copy(bytes(w))
                        for rank,index in enumerate(order):ordered.faces[rank]=w.faces[index]
                        for x in range(120):
                            n=s.counts[x];assert n<=64 and s.top[x]<=128
                            tops=list(s.history_top[x][:n]);assert tops==sorted(set(tops)),(row,x,tops)
                            hh=[struct.unpack_from('<HH',histories,(x*64+i)*4) for i in range(n)]
                            assert all((a&32767)<=b for a,b in hh)
                            assert [a&32767 for a,b in hh]==sorted(a&32767 for a,b in hh),(row,x,hh)
                            scene=scenes[(level-1)*2+language]
                            wall,ray,ceilings,border=ray_geometry(scene,camera,x*2,env,trig)
                            rims=rim_edges(scene,camera,ray,env)
                            actual=s.wall_z[x]
                            if wall is None:
                                assert actual==65535,(row,x,'unexpected wall',actual)
                            else:
                                bound=math.ceil(max(abs(ray[2]*wall),abs(ray[3]*wall))/(480*65536))+3
                                if abs(actual-wall/2)>bound:
                                    assert boundary_wall(scene,ray,env,actual),(row,x,'wall depth',actual,wall/2,bound)
                                    wall_boundary_columns+=1
                            assert bool(s.closed[x>>5]&(1<<(x&31)))==(actual!=65535)
                            checked+=1
                            # The geometric oracle intersects original map cells,
                            # independently of the new BSP leaves/clip histories.
                            events=dynamic_events(p,w,model,x*2)
                            # Independent source sampling/alpha/overwrite oracle.
                            # Static clipping follows the separately ray-checked
                            # clip contract; it is not an independent history proof.
                            expected_dynamic={}
                            for e in sector_events(p,ordered,model,x*2):
                                source,lut,v,step,depth,stride,first,last,masked,_=e;z=depth-1;cutoff=0
                                for rank in reversed(range(n)):
                                    near,far=hh[rank]
                                    if near&32767>z:continue
                                    cutoff=tops[rank]
                                    if z<far:
                                        factor=6553600//(z+200);yy=(((-561-camera[1])*factor)>>15)+79
                                        cutoff=min(128,max(0,(yy*4//5)+1))
                                        if rank:cutoff=max(cutoff,tops[rank-1])
                                    break
                                raw=p.frontend.read(source,((v+(last-first)*step)>>16)*stride+1)
                                table=p.frontend.read(lut,256) if lut else None
                                for y in range(max(first,cutoff),last+1):
                                    if z>=actual and s.wall_first[x]<=y<=s.wall_last[x]:continue
                                    color=raw[((v+(y-first)*step)>>16)*stride]
                                    if table:color=table[color]
                                    if color or not masked:expected_dynamic[y]=color or black
                            for y,color in expected_dynamic.items():
                                at=y*128+x if bitmap else (y>>3)*1024+(x>>3)*64+(y&7)*8+(x&7)
                                assert frame[at]==color,(row,x,y,'dynamic source/alpha/order',frame[at],color)
                                dynamic_pixels+=1
                            for y in range(128):
                                bit=1<<(y&31);covered=bool(coverage[x*4+(y>>5)]&bit)
                                at=y*128+x if bitmap else (y>>3)*1024+(x>>3)*64+(y&7)*8+(x&7)
                                assert (not covered or bool(frame[at])) if bitmap else bool(frame[at])==covered,(row,x,y,'coverage backing')
                                if y in border:grid_boundary_rows+=1;continue
                                expected=y in ceilings and (wall is None or ceilings[y]<wall/2-2)
                                roof=bool(s.roof[x][y>>5]&bit)
                                # Quantized projected edge vs inverse-plane rows
                                # can differ by one row; exclude only those edges.
                                if roof!=expected:
                                    adjacent=any((yy in ceilings)!=(y in ceilings) for yy in (y-1,y+1))
                                    if adjacent:adjacent_roof_rows+=1;continue
                                    if wall is not None and y in ceilings and abs(ceilings[y]-wall/2)<30:
                                        wall_depth_edge_rows+=1;continue
                                    if y in ceilings and any(abs(y-edge)<=1 for edge,rz in rims):
                                        rim_boundary_rows+=1;continue
                                    errors.append(dict(x=x,y=y,roof=int(roof),expected=int(expected),depth=ceilings.get(y),
                                        wall=actual,history=hh,tops=tops,nearby=[bool(s.roof[x][yy>>5]&(1<<(yy&31))) for yy in range(max(0,y-2),min(128,y+3))]))
                                roof_checked+=1
                                if roof and im and not any(e[6]<=y<=e[7] for e in events):
                                    expected_pixel=ceiling_pixel(camera,trig,x*2,y,im.s[0],im.s[1],pixels,bytes(w.lights)) or black
                                    assert frame[at]==expected_pixel,(row,x,y,'shared ARM plane pixel',frame[at],expected_pixel)
                                    plane_pixels+=1
                        row.update(work=work,wall_columns_checked=checked,roof_rows_checked=roof_checked,
                            plane_pixels_checked=plane_pixels,opaque_edge_rows_excluded=rim_boundary_rows,
                            wall_endpoint_columns_excluded=wall_boundary_columns,
                            dynamic_pixels_checked=dynamic_pixels,
                            world_grid_rows_excluded=grid_boundary_rows,adjacent_roof_rows_excluded=adjacent_roof_rows,
                            wall_depth_edge_rows_excluded=wall_depth_edge_rows,
                            roof_disagreements=errors[:20],roof_disagreement_count=len(errors))
                        assert not errors,(row,'roof geometry')
                        if bitmap:row['software_base_pixels_checked']=bitmap_planes(p,w,camera,trig,env,model,(level-1)*2+language,coverage)
                    p.frontend.run(2);image=out/f'sector-{int(trial)}-{language}-{level}-{cell}-{heading}-{int(cycle)}.png'
                    row['overlay_sha256']=ppu_check(p,trial and 'dok_gba_bitmap_column' in p.symbols)
                    p.frontend.screenshot(image);row.update(image=str(image),image_sha256=sha(image));rows.append(row)
                from verify_items_gba import MAP_OFFSET
                for offset,label in ((4096,'TMap-edit'),(8192,'FCMap-edit')):
                    target=p.symbols['g_render_world']+MAP_OFFSET+offset
                    original=p.frontend.read(target,4096);edited=bytearray(original)
                    edited[0]=next(g for g in original if g!=edited[0]);p.write(target,bytes(edited))
                    for _ in range(4):d=p.present()
                    assert d['guard_ok']==1 and d['phase']==2
                    if trial:assert p.words('g_sector_work',1)[0]==0,('mutated map needs fallback',label)
                    address=p.symbols['g_render_frame'];w=Workspace.from_buffer_copy(p.frontend.read(address,ct.sizeof(Workspace)))
                    fallbacks.append(dict(level=level,language=language,fixture=label,
                        geometry_sha256=hashlib.sha256(b''.join(bytes(f) for f in w.faces[:w.face_count])+
                            b''.join(bytes(f) for f in w.surfaces[:w.surface_count])).hexdigest(),
                        face_sha256=hashlib.sha256(b''.join(bytes(f) for f in w.faces[:w.face_count])).hexdigest(),
                        lease_sha256=hashlib.sha256(bytes(w.image_known)+bytes(w.surface_known)).hexdigest()))
                    p.write(target,original)
                    for _ in range(4):d=p.present()
                    if trial:assert p.words('g_sector_work',1)[0]==1,('native restore',label)
        return dict(rom_sha256=sha(p.rom),rows=rows,fallbacks=fallbacks,stack=stack_usage(p.frontend))
    finally:p.close()


def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--build-dir',type=Path,required=True);a.add_argument('--reference-build',type=Path,required=True)
    a.add_argument('--report',type=Path,required=True);a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    data=(ROOT/'build/gba/assets/assets.bin').read_bytes();scenes=[compile_view(data,i) for i in range(6)]
    trig=[int(n) for n in re.findall(r'-?\d+',(ROOT/'src/renderer_trig.inc').read_text().split('{',1)[1].split('}',1)[0])]
    before=run(a.reference_build,core,a.report.parent,scenes,trig,False)
    after=run(a.build_dir,core,a.report.parent,scenes,trig,True)
    for b,t in zip(before['rows'],after['rows']):
        assert b['dynamic_sha256']==t['dynamic_sha256'],('dynamic geometry',t)
        assert b['lease_sha256']==t['lease_sha256'],('visibility leases',t)
        assert b['overlay_sha256']==t['overlay_sha256'],('native HUD/weapon overlay',t)
    bitmap='bitmap' in a.build_dir.name
    for b,t in zip(before['fallbacks'],after['fallbacks']):
        assert b['face_sha256']==t['face_sha256'],('fallback faces',b,t)
        assert b['lease_sha256']==t['lease_sha256'],('fallback leases',b,t)
        if not bitmap:assert b['geometry_sha256']==t['geometry_sha256'],('fallback surfaces',b,t)
    a.report.write_text(json.dumps(dict(before=before,after=after,core_sha256=sha(core),script_sha256=sha(__file__),
        cold_reset_after_freeze=True,scope='Original dynamic geometry/leases; BSP-independent wall rays and world-cell roof masks with separately counted world-grid, segment endpoint, adjacent roof row, wall-depth edge and opaque-edge exclusions; shared ARM plane pixels; dynamic source/alpha/order pixels against the ray-checked static clip contract; coverage backing; Mode4 cycles; original map-edit fallback geometry/leases. Bitmap also checks software base pixels and full actual PPU composition. No independent proof of dynamic clip histories, full gameplay, or physical GBA acceptance.'),indent=2)+'\n',encoding='utf-8')
    print('Passed',len(after['rows']),'poses.')


if __name__=='__main__':main()
