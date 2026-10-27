"""Independent nearest-owner, static ray geometry, pixels and visibility leases."""
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
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha
from verify_gba_static_cache import Workspace
from verify_sprites_gba import load_json
from verify_renderer_gba import stack_usage


def div(n,d):return (abs(n)//abs(d))*(-1 if (n<0)!=(d<0) else 1)


def dynamic_events(p,w,model,x):
    result=[];bank=p.symbols['dok_prelit_bank'];offsets=p.words('prelit_view',1)[0]
    for raw in w.faces[:w.face_count]:
        values=[(n+2147483648)%4294967296-2147483648 for n in raw.n]
        x0,x1,t0,t1,b0,b1,z0,z1,u0,u1,_=values;image=raw.s[0]
        im=w.images[image] if image<256 else w.instance_images[image-256]
        if x0>x1:x0,x1,t0,t1,b0,b1,z0,z1,u0,u1=x1,x0,t1,t0,b1,b0,z1,z0,u1,u0
        if not x0<=x<=x1:continue
        span=x1-x0;i=x-x0;top=t0+div((t1-t0)*i,span);bottom=b0+div((b1-b0)*i,span)
        if bottom<top or bottom<0 or top>=128:continue
        first,last=max(0,top),min(127,bottom)
        q0=max(1,6553600//(z0+200)>>8);q1=max(1,6553600//(z1+200)>>8)
        den=q0*(span-i)+q1*i
        u=(u0*q0*(span-i)+u1*q1*i)//den
        pixels,columns,_=im.p;width,height,flags,rid=im.s
        u=max(0,min(width-1,u));crop=0
        if image<len(model['objects']) and model['objects'][image]['class']==7:
            obj=p.symbols['g_render_world']+4+image*36
            frame=struct.unpack('<h',p.frontend.read(obj+2,2))[0]
            extent=max(1,struct.unpack('<h',p.frontend.read(obj+34,2))[0])
            if flags&0x40:
                shift=width//extent*frame
                if u<shift:continue
                u-=shift
            elif flags&0x100:crop=height//extent*frame
        start=max(0,min(height-1,(raw.s[1]+32768)%65536-32768));step=((height-1-start)<<16)//(bottom-top) if bottom>top else 0
        v=(start<<16)+(first-top)*step
        if crop:
            threshold=crop<<16
            if v<threshold:
                if not step:continue
                skip=(threshold-v+step-1)//step;first+=skip;v+=skip*step
                if first>last:continue
            v-=threshold
        z=(z0*q0*(span-i)+z1*q1*i)//den;row=min(15,max(0,(z-1920)//120))
        off=struct.unpack('<I',p.frontend.read(offsets+(rid*16+row)*4,4))[0] if rid else 0
        if columns and off:source,lut,stride=bank+off+u*height,0,1
        elif columns:source,lut,stride=columns+u*height,p.symbols['g_render_frame']+Workspace.lights.offset+row*256,1
        else:source,lut,stride=pixels+u,p.symbols['g_render_frame']+Workspace.lights.offset+row*256,width
        result.append((source,lut,v,step,z+1,stride,first,last,int(bool(flags&8)),0))
    return result


def ray_geometry(scene,camera,x,envelope,trig):
    cx,cy,cz,heading=camera;sine,cosine=trig[heading],trig[361+heading]
    lateral=x*4//3-159
    ox,oz=cx*65536+cosine*lateral*2,cz*65536+sine*lateral*2
    dx,dz=-sine+div(cosine*lateral,200),cosine+div(sine*lateral,200)
    wall=None
    # Intersect the original compiled wall segments directly, without grid DDA.
    for a in scene['source_pieces']:
        if a['kind']!='wall':continue
        cell=a['cell'];xx,zz=cell%64,cell//64
        if not(envelope[0]<=xx<=envelope[1] and envelope[2]<=zz<=envelope[3]):continue
        axis=a['axis'];direction=dx if axis==0 else dz
        if not direction:continue
        side=a['side']
        if not((side==0 and dz>0) or (side==1 and dx>0) or (side==2 and dz<0) or (side==3 and dx<0)):continue
        t=(a['fixed']*65536-(ox if axis==0 else oz))/direction
        if t<0:continue
        other=(oz+dz*t if axis==0 else ox+dx*t)/65536
        if a['lo']-0.00001<=other<=a['hi']+0.00001 and (wall is None or t<wall):wall=t
    low=set()
    for p in scene['patches']:
        if p['material'][1]==400:
            low.update(z*64+xx for z in range(p['z'],p['z']+p['length']) for xx in range(p['x'],p['x']+p['width']))
    ceilings={};border=set()
    for y in range(128):
        dy=y*5//4-79
        if dy>=0:continue
        z=div((-561-cy)*200,dy)-200
        if not 0<=z<65534:continue
        wx,wz=ox+dx*z*2,oz+dz*z*2
        xx,zz=wx//(480*65536),wz//(480*65536)
        if not(envelope[0]<=xx<=envelope[1] and envelope[2]<=zz<=envelope[3]):continue
        # DDA truncates each480-cell step; bound its accumulated time error by
        # the number of crossed cells on the dominant axis, not an arbitrary FPS tolerance.
        error=math.ceil(max(abs(dx*z*2),abs(dz*z*2))/(480*65536))+2
        fx=(wx//65536)%480;fz=(wz//65536)%480
        if min(fx,480-fx,fz,480-fz)<=error:border.add(y)
        if envelope[0]<=xx<=envelope[1] and envelope[2]<=zz<=envelope[3] and zz*64+xx in low:ceilings[y]=z+1
    return wall,(ox,oz,dx,dz),ceilings,border


def ceiling_pixel(camera,trig,x,y,width,height,pixels,lights):
    cx,cy,cz,heading=camera;sine,cosine=trig[heading],trig[361+heading]
    dy=y*5//4-79;z=div((-561-cy)*200,dy)-200;distance=z+200
    lateral=-distance*52101;step=div(distance*65536,75)
    wx=div(cx*65536,480)+div((lateral*cosine)>>15,480)-div(sine*z*2,480)
    wz=div(cz*65536,480)+div((lateral*sine)>>15,480)+div(cosine*z*2,480)
    wx+=x//2*div((step*cosine)>>15,480);wz+=x//2*div((step*sine)>>15,480)
    u=((wx&65535)*(width-1))>>16;v=((wz&65535)*(height-1))>>16
    row=min(15,max(0,(z-1920)//120));return lights[row*256+pixels[v*width+u]]


def run(build,core,scenes,trig,trial):
    p=GameProbe(core,build);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for language in (0,1):
            p.language(language)
            for level in (1,2,3):
                # Recreate initial actors after freezing. Different renderer speeds
                # otherwise allow different startup AI movement before freeze.
                p.camera(level%3,15120,-300,22800,90)
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level}-runtime.json');hero=model['objects'][model['player_index']-1]['actor']
                cell=hero['z']//480*64+hero['x']//480
                cases=[(cell,h) for h in (0,45,90,135)]
                cases+={1:[(520,225),(3640,225)],2:[(1592,135),(2871,225)],3:[(1544,315),(1560,90)]}[level]
                for cell,heading in cases:
                    camera=(cell%64*480+240,-300,cell//64*480+240,heading)
                    p.camera(level-1,*camera)
                    for _ in range(4):d=p.present()
                    frame_address=p.symbols['g_render_frame']
                    lease=p.frontend.read(frame_address+Workspace.image_known.offset,256)+p.frontend.read(frame_address+Workspace.surface_known.offset,768)
                    row=dict(level=level,language=language,cell=cell,heading=heading,lease_sha256=hashlib.sha256(lease).hexdigest(),columns=[])
                    w=Workspace.from_buffer_copy(p.frontend.read(frame_address,ct.sizeof(Workspace)))
                    dynamic=[bytes(f) for f in w.faces[:w.face_count] if f.s[0]>=256 or model['objects'][f.s[0]]['class']!=2]
                    row['dynamic_geometry_sha256']=hashlib.sha256(b''.join(dynamic)).hexdigest()
                    if trial:
                        for x in (0,118,238):
                            p.write(p.symbols['g_column_probe'],struct.pack('<I',x+1));d=p.present()
                            assert d['phase']==2 and d['guard_ok']==1
                            meta=list(p.words('g_column_probe',4));work=list(p.words('g_column_work',8))
                            assert meta[1]>0 and meta[3]==x and work[0]==1 and work[1]==120 and work[7]==0
                            base=p.words('column_ceiling_mask',1)[0];assert base%4==0
                            raw=p.frontend.read(base+16384,640+meta[2]*24)
                            depths=struct.unpack_from('<128H',raw);owners=struct.unpack_from('<128H',raw,256)
                            events=[struct.unpack_from('<4I2H4B',raw,640+i*24) for i in range(meta[2])]
                            # Validate every dynamic candidate against the original
                            # projected face, not merely those present in the result.
                            from collections import Counter
                            expected_events=dynamic_events(p,w,model,x)
                            actual_events=Counter(e for e in events if e[9]==0);expected_counter=Counter(expected_events)
                            assert actual_events==expected_counter,(level,cell,heading,x,'missing/altered dynamic event',list(actual_events-expected_counter),list(expected_counter-actual_events))
                            candidate=[[] for _ in range(128)];colors={}
                            for i,e in enumerate(events):
                                pixels,lut,v,step,z,stride,first,last,masked,kind=e
                                assert 0<=first<=last<128 and z>0 and 0<stride<=256
                                size=((v+(last-first)*step)>>16)*stride+1;assert size<=256*512
                                source=p.frontend.read(pixels,size);table=p.frontend.read(lut,256) if lut else None
                                for y in range(first,last+1):
                                    color=source[((v+(y-first)*step)>>16)*stride];color=table[color] if table else color
                                    colors[i,y]=color
                                    if color or not masked:candidate[y].append((z,i))
                            envelope=[(n+2147483648)%4294967296-2147483648 for n in p.words('g_hw_bg_envelope',4)]
                            wall,ray,ceilings,border=ray_geometry(scenes[level*2-2+language],camera,x,envelope,trig)
                            static_walls=[e[4]-1 for e in events if e[9]==1]
                            if wall is None:assert not static_walls,(level,cell,heading,x,'unexpected wall')
                            else:
                                assert len(static_walls)==1,(level,cell,heading,x,'missing/duplicate wall',static_walls,wall/2)
                                bound=math.ceil(max(abs(ray[2]*wall),abs(ray[3]*wall))/(480*65536))+2
                                assert abs(static_walls[0]-wall/2)<=bound,(level,cell,heading,x,'wall distance',static_walls,wall/2,bound)
                            black=p.words('g_hw_tile_black',1)[0]
                            frame=p.frontend.read(frame_address+Workspace.frame.offset,16384)
                            ceiling_object=p.words('column_ceiling_object',1)[0]
                            if ceiling_object!=65535:
                                im=w.surface_images[ceiling_object*3+1];cw,ch=im.s[:2]
                                ceiling_pixels=p.frontend.read(im.p,cw*ch)
                            bits=struct.unpack('<4I',p.frontend.read(p.symbols['coverage_bits']+x//2*16,16))
                            checked=0
                            for y in range(128):
                                if y in border:continue
                                if y in ceilings:candidate[y].append((ceilings[y],65534))
                                nearest=min((z for z,i in candidate[y]),default=65535)
                                valid={i for z,i in candidate[y] if z==nearest} if nearest!=65535 else {65535}
                                assert depths[y]==nearest and owners[y] in valid,(level,cell,heading,x,y,'nearest owner',depths[y],owners[y],nearest,valid)
                                assert bool(bits[y>>5]&(1<<(y&31)))==(owners[y]!=65535)
                                at=(y>>3)*1024+(x>>4)*64+(y&7)*8+((x>>1)&7)
                                if owners[y]==65534:expected=ceiling_pixel(camera,trig,x,y,cw,ch,ceiling_pixels,bytes(w.lights)) or black
                                else:expected=0 if owners[y]==65535 else colors[owners[y],y] or black
                                assert frame[at]==expected,(level,cell,heading,x,y,'pixel',frame[at],expected)
                                checked+=1
                            assert checked>=110,(level,cell,heading,x,'too many excluded boundaries',checked)
                            row['columns'].append(dict(x=x,events=len(events),checked_rows=checked,shared_boundary_rows=len(border),wall_depth=static_walls))
                        p.write(p.symbols['g_column_probe'],struct.pack('<I',0))
                    image=build/f'column-qa-{language}-L{level}-{cell}-{heading}.png';p.frontend.run(2);p.frontend.screenshot(image)
                    row.update(image=str(image),image_sha256=sha(image));rows.append(row)
        return dict(rom_sha256=sha(p.rom),rows=rows,stack=stack_usage(p.frontend))
    finally:p.close()


def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--build-dir',type=Path,required=True);a.add_argument('--reference-build',type=Path,required=True);a.add_argument('--report',type=Path,required=True)
    a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    data=(ROOT/'build/gba/assets/assets.bin').read_bytes();scenes=[compile_view(data,i) for i in range(6)]
    trig=[int(n) for n in re.findall(r'-?\d+',(ROOT/'src/renderer_trig.inc').read_text().split('{',1)[1].split('}',1)[0])]
    before=run(a.reference_build,core,scenes,trig,False);after=run(a.build_dir,core,scenes,trig,True)
    for b,t in zip(before['rows'],after['rows']):
        assert b['lease_sha256']==t['lease_sha256'],('visibility lease',t['level'],t['cell'],t['heading'])
        assert b['dynamic_geometry_sha256']==t['dynamic_geometry_sha256'],('dynamic geometry',t['level'],t['cell'],t['heading'])
    result=dict(task='G20A',status='passed',before=before,after=after,core_sha256=sha(core),script_sha256=sha(__file__),
        cold_reset_after_freeze=True,
        scope='Native all-dynamic-candidate/geometry checks, final nearest owner/alpha/vertical and ceiling pixels, direct segment-ray wall intersections, world-cell lowered ceiling oracle, visibility leases, guards. Shared grid boundary rows explicitly excluded. Not full gameplay or identical old affine pixels.')
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print('Passed',len(after['rows']),'poses,',sum(len(r['columns']) for r in after['rows']),'native column snapshots.')


if __name__=='__main__':main()
