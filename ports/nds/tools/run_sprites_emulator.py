"""N12 read-only ARM9/GPU/texture-stream observer. Uses real libretro inputs.

Frame poses, texture contents and sizes are checked independently from probe
pass flags. Emulator virtual intervals are never labelled physical DS FPS.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import traceback
import math
import run_diag_emulator as transport
import run_renderer_emulator as render
import make_assets_fixture as fat
from render_metrics import summarize

ROOT=Path(__file__).resolve().parents[3]
CORE=Path.home()/'AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
def catalog():
    data=(ROOT/'build/nds/assets/dok-assets.ndspak').read_bytes();assert hashlib.sha256(data).hexdigest()==render.PACKAGE_SHA
    entries={}
    for i in range(struct.unpack_from('<I',data,16)[0]):
        row=data[160+i*128:288+i*128];rid,_,off,size=struct.unpack_from('<4I',row)
        entries[rid]=dict(raw=row,pixels=data[off:off+size],width=struct.unpack_from('<H',row,24)[0],height=struct.unpack_from('<H',row,26)[0])
    return data,entries
def check_packet(frontend,probe,sprite,entries):
    raw=render.ram(frontend,probe['render_frame_address'],32)
    x,y,z,s,c,pointer,count,generation=struct.unpack('<5i3I',raw)
    cells=[];total=0
    for i in range(count):
        ox,oy,oz,qp,n=struct.unpack('<3i2I',render.ram(frontend,pointer+i*20,20));quads=[]
        for j in range(n):
            q=render.ram(frontend,qp+j*92,92)
            slot,flags,sid,color,reserved=struct.unpack_from('<HHIHH',q,80)
            quads.append(dict(vertices=[list(struct.unpack_from('<5i',q,k*20)) for k in range(4)],texture_slot=slot,source_surface_id=sid,color=color))
        cells.append(dict(origin=[ox,oy,oz],quads=quads));total+=n
    frame=dict(cells=cells,quads=total)
    poses=[]
    for i in range(sprite['sprite_count']):
        p=struct.unpack('<3i2h4H2I',render.ram(frontend,sprite['list_address']+i*32,32));poses.append(p)
    demand=[]
    for i in range(sprite['demand_count']):
        data=render.ram(frontend,sprite['demand_address']+i*sprite['demand_bytes'],sprite['demand_bytes'])
        key,rid,pixels,size,palette,sw,sh,w,h,transparent=struct.unpack_from('<Q4I4HI',data)
        e=entries[rid];assert size==sw*sh and (w,h)==(e['width'],e['height'])
        actual=render.ram(frontend,pixels,size)
        assert actual==e['pixels'],f'Actual ARM9 texture differs {rid}'
        demand.append(dict(key=key,id=rid,bytes=size,transparent=transparent))
    assert sum(r['bytes'] for r in demand)<=393216
    # The scene frame remains a complete lease; append must never overwrite base
    # quads or silently drop a gathered object. Match stable provenance exactly.
    all_quads=[q for cell in frame['cells'] for q in cell['quads']]
    expected={p[-1] for p in poses};actual={q['source_surface_id'] for q in all_quads if q['source_surface_id']&0x80000000}
    assert expected==actual and len(actual)==len(poses)
    for p in poses:
        q=next(q for q in all_quads if q['source_surface_id']==p[-1])
        if q['texture_slot']!=65535:
            d=demand[q['texture_slot']]
            assert d['id']==((p[-2]&~0x3fc00)|(5<<10))
        assert max(v[3] for v in q['vertices'])==(p[7]-1)*65536
        assert max(v[4] for v in q['vertices'])==(p[8]-1)*65536
    assert sprite['commit_start_line']>=192 or sprite['commit_start_line']<48
    assert sprite['commit_end_line']>=192 or sprite['commit_end_line']<48
    return dict(sprites=len(poses),quads=frame['quads'],texture_bytes=sum(r['bytes'] for r in demand),
                demand=demand,exact_texture_bytes=sum(r['bytes'] for r in demand),frame=frame)

def check_occlusion(frontend,probe,frame,entries,demand,packet,case_count):
    projection=struct.unpack('<16i',render.ram(frontend,probe['projection_address'],64));polygons=[]
    palette=struct.unpack_from('<256H',packet,64+case_count*64)
    for cell in frame['cells']:
        matrix=render.cell_clip_matrix(projection,(0,-300,0),cell['origin'],0,32768)
        for q in cell['quads']:
            points=[]
            for v in q['vertices']:
                vec=[n*8 for n in v[:3]]+[4096]
                clip=[sum(matrix[k*4+r]*vec[k] for k in range(4))//4096 for r in range(4)]
                points.append([128*(clip[0]/clip[3]+1)-.5,77*(1-clip[1]/clip[3])-.5])
            polygons.append(dict(q=q,left=points[0][0],right=points[1][0],top=points[0][1],bottom=points[2][1],depth=q['vertices'][0][2]+cell['origin'][2]))
    polygons.sort(key=lambda p:-p['depth'])
    _,_,rgb=frontend.rgb();compared=0;holes=0;front=0;far=0;hidden=0;errors=[]
    for y in range(3,151):
        for x in range(3,253):
            color=0;winner=0;safe=True;has_hole=False;far_masked=False
            for p in polygons:
                if p['left']-2<x<p['right']+2 and p['top']-2<y<p['bottom']+2 and min(abs(x-p['left']),abs(x-p['right']),abs(y-p['top']),abs(y-p['bottom']))<2:
                    safe=False;break
                if not(p['left']<x<p['right'] and p['top']<y<p['bottom']):continue
                if min(x-p['left'],p['right']-x,y-p['top'],p['bottom']-y)<2:safe=False;break
                q=p['q'];sid=q['source_surface_id']
                if q['texture_slot']==65535:
                    color=q['color'];winner=sid;continue
                e=entries[demand[q['texture_slot']]['id']];w,h=e['width'],e['height'];sw=struct.unpack_from('<H',e['raw'],28)[0]
                u=math.floor((x-p['left'])/(p['right']-p['left'])*(w-1));v=math.floor((y-p['top'])/(p['bottom']-p['top'])*(h-1))
                indices={e['pixels'][vv*sw+uu] for vv in range(max(0,v-1),min(h,v+2)) for uu in range(max(0,u-1),min(w,u+2))}
                colors={palette[n] if n else None for n in indices}
                if len(colors)!=1:safe=False;break
                value=next(iter(colors))
                if value is None:has_hole=True;continue
                color=value;winner=sid
                if sid==0xc1000000 and x>=127:far_masked=True
            if not safe or not winner:continue
            actual=(rgb[(y*256+x)*3]>>3)|((rgb[(y*256+x)*3+1]>>3)<<5)|((rgb[(y*256+x)*3+2]>>3)<<10)
            compared+=1;holes+=has_hole;front+=winner==0xc1000001;far+=winner==0xc1000000
            hidden+=winner==2 and has_hole
            if actual!=color and len(errors)<12:errors.append(dict(x=x,y=y,expected=color,actual=actual,winner=winner))
    assert compared>1000 and holes>50 and front>10 and far>10 and hidden>10,(compared,holes,front,far,hidden)
    assert not errors,errors
    return dict(passed=True,compared_pixels=compared,transparent_holes=holes,foreground_pixels=front,far_sprite_pixels=far,wall_occlusion_pixels=hidden)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,default=CORE);parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--packet',type=Path,default=ROOT/'build/nds/n07/scene.dat')
    parser.add_argument('--max-frontend-frames',type=int,default=50000)
    args=parser.parse_args();target=args.target.absolute();directory=target/'emulator'
    assert target.is_relative_to(ROOT/'build/nds') and not any(p.is_symlink() or p.is_junction() for p in [target,*target.parents] if p.exists())
    directory.mkdir(parents=True,exist_ok=True)
    report=dict(task='N12',passed=False,scope='DS-mode emulator only; physical DS/DS Lite remain N24',cases=[],history_records=[])
    frontend=None
    try:
        assert transport.sha(args.core)==render.CORE_SHA
        package,entries=catalog();packet,cases,fixture=render.read_fixture(args.packet)
        rom=target/'dok_nds_sprites.nds';elf=target/'dok_nds_sprites.elf'
        report.update(core_sha256=transport.sha(args.core),rom_sha256=transport.sha(rom),elf_sha256=transport.sha(elf),fixture=fixture)
        frontend=transport.Frontend(args.core,directory,'happy')
        image=frontend.save/'melonDS DS/dldi_sd_card.bin'
        fat.write_image(image,[('assets.ndp',b'ASSETS  NDP',package),('scene.dat',b'SCENE   DAT',packet)])
        frontend.load(rom);transport.flush_native_stdio();before=transport.sha(image)
        header=ROOT/'ports/nds/platform/render_probe/render_probe.h'
        fields=render.scalar_fields(header,'NdsRenderProbe');hf=render.scalar_fields(header,'NdsRenderHistoryRecord')
        sf=render.scalar_fields(ROOT/'ports/nds/platform/sprites_probe/sprites_probe.h','NdsSpriteProbe')
        address,_=transport.elf_symbol(elf,'g_nds_render_probe');sa,_=transport.elf_symbol(elf,'g_nds_sprite_probe')
        ordinal=0;next_case=0;stable=0;last=None;joypad=0
        for _ in range(args.max_frontend_frames):
            frontend.joypad=joypad;joypad=0
            if frontend.frame_time and frontend.frame_time.callback:frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run();frontend.frames+=1
            assert not frontend.callback_error and not frontend.shutdown
            probe=render.observe(frontend,address,fields,0x37524b44)
            if not probe:continue
            report['last_observation']=probe
            if probe['status']==3 or probe['error_bits']:
                report['last_sprite_observation']=render.observe(frontend,sa,sf,0x12504b44)
                raise AssertionError('ARM9 renderer failed; see last observations')
            assert 0<=probe['history_total']-ordinal<=128
            for number in range(ordinal+1,probe['history_total']+1):
                row=render.history_record(frontend,probe,number,hf)
                if row is None:break
                report['history_records'].append(row);ordinal=number
            if probe['status']==2:
                assert next_case==len(cases);break
            if probe['status']!=4 or next_case>=len(cases):stable=0;continue
            if probe['case_id']!=cases[next_case]['case_id']:continue
            key=(probe['case_id'],probe['presented_frame_id']);stable=stable+1 if last==key else 1;last=key
            if stable<8:continue
            sprite=render.observe(frontend,sa,sf,0x12504b44)
            if not sprite:continue
            report['last_sprite_observation']=sprite
            result=check_packet(frontend,probe,sprite,entries)
            frame=result.pop('frame')
            if cases[next_case]['flags']&1:
                render.OUT=target
                result['depth_coverage']=render.flat_coverage(frontend,probe)
            if cases[next_case]['source_kind']==13:
                result['masked_occlusion']=check_occlusion(frontend,probe,frame,entries,result['demand'],packet,len(cases))
            result.update(id=probe['case_id'],level=probe['level'],sprite=sprite,
                          images=frontend.capture(f"case-{probe['case_id']:02}"))
            report['cases'].append(result)
            print(f"case {probe['case_id']}: {result['sprites']} sprites, {result['quads']} quads, {result['texture_bytes']} texture bytes",flush=True)
            next_case+=1;joypad=1<<8;stable=0
        else:raise AssertionError('No completed N12 run within observation bound')
        report['memory']=render.verify_memory(frontend,probe,elf)
        sprite=render.observe(frontend,sa,sf,0x12504b44)
        expected=[]
        for c in cases:
            if c['source_kind']!=12:continue
            ids=sorted(rid for rid,e in entries.items() if (rid>>30)==sprite['language'] and ((rid>>10)&255)==3)
            for rid in ids:
                meta=entries[rid]['pixels'];n=struct.unpack_from('<H',meta)[0]
                expected += [(c['case_id'],(struct.unpack_from('<I',meta,8+16*i)[0]&~0x3fc00)|(5<<10)) for i in range(n)]
        assert sprite['trace_count']==len(expected)<=sprite['trace_capacity']
        report['frame_texture_traces']=[]
        for i,(case_id,image_id) in enumerate(expected):
            row=struct.unpack('<14I',render.ram(frontend,sprite['trace_address']+i*sprite['trace_bytes'],56))
            digest=struct.pack('<8I',*row[6:]).hex();entry=entries[image_id]
            assert (row[0],row[2],row[3],row[4],row[5],digest)==(image_id,case_id,entry['width'],entry['height'],len(entry['pixels']),hashlib.sha256(entry['pixels']).hexdigest())
            report['frame_texture_traces'].append(dict(case_id=case_id,image_id=image_id,frame_id=row[1],sha256=digest))
        normal=[c for c in cases if c['source_kind'] not in (12,13)]
        stress=[c for c in cases if c['source_kind'] in (12,13)]
        normal_ids={c['case_id'] for c in normal};stress_ids={c['case_id'] for c in stress}
        if normal:report['history']=summarize([r for r in report['history_records'] if r['case_id'] in normal_ids],probe,normal)
        if stress:
            report['all_frame_state_stress']=summarize([r for r in report['history_records'] if r['case_id'] in stress_ids],probe,stress)
            report['all_frame_state_stress']['scope']='Explicit walk through every original frame, one frame per presentation; not animation timing or a normal gameplay FPS route'
        transport.flush_native_stdio();assert transport.sha(image)==before
        report['fat_unchanged']=True;report['passed']=True
    except Exception:report['error']=traceback.format_exc()
    finally:
        if frontend:frontend.close()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k in ('passed','error','history')},indent=2))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
