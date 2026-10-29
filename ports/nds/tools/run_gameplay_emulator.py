"""N13 read-only ARM9/GPU/texture-stream observer. Uses real libretro inputs.

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

def effect_pixels(source,sw,sh,w,h,key,light):
    phase=((key>>33)&31)-1;mask=(key>>38)&31;code=(key>>43)&15
    if not mask and not code:return source
    axis=(code-1)//4 if code else 0
    direction=-1 if code in (1,2,5,6) else 1
    cyclic=code in (2,4,6,8)
    shift=((h if axis else w)//18)*max(phase,0) if code else 0
    out=bytearray(sw*sh)
    for y in range(h):
        for x in range(w):
            xx=x+(direction*shift if not axis else 0);yy=y+(direction*shift if axis else 0)
            if cyclic:xx%=w;yy%=h
            if 0<=xx<w and 0<=yy<h:
                n=source[yy*sw+xx]
                if not mask or light[(mask-1)*256+n]:out[y*sw+x]=n
    return bytes(out)
def catalog():
    data=(ROOT/'build/nds/assets/dok-assets.ndspak').read_bytes();assert hashlib.sha256(data).hexdigest()==render.PACKAGE_SHA
    entries={}
    for i in range(struct.unpack_from('<I',data,16)[0]):
        row=data[160+i*128:288+i*128];rid,_,off,size=struct.unpack_from('<4I',row)
        entries[rid]=dict(raw=row,pixels=data[off:off+size],width=struct.unpack_from('<H',row,24)[0],height=struct.unpack_from('<H',row,26)[0])
    return data,entries
def check_packet(frontend,probe,sprite,entries):
    ea,_=transport.elf_symbol(EFFECT_ELF,'g_nds_effect_probe')
    ef=struct.unpack('<32I',render.ram(frontend,ea,128))
    assert ef[0]==0x13504b44 and not ef[21]
    light=render.ram(frontend,ef[4],4096);rgb6=render.ram(frontend,ef[5],768)
    assert rgb6 in [e['pixels'] for e in entries.values() if len(e['pixels'])==768]
    assert light in [e['pixels'] for e in entries.values() if len(e['pixels'])==4096]
    expected_palette=[sum(((rgb6[n*3+k]*ef[8]//100)>>1)<<(k*5) for k in range(3)) for n in light]
    assert struct.unpack('<4096H',render.ram(frontend,ef[6],8192))==tuple(expected_palette)
    raw=render.ram(frontend,probe['render_frame_address'],32)
    x,y,z,s,c,pointer,count,generation=struct.unpack('<5i3I',raw)
    cells=[];total=0
    for i in range(count):
        ox,oy,oz,qp,n=struct.unpack('<3i2I',render.ram(frontend,pointer+i*20,20));quads=[]
        for j in range(n):
            q=render.ram(frontend,qp+j*92,92)
            slot,flags,sid,color,reserved=struct.unpack_from('<HHIHH',q,80)
            quads.append(dict(vertices=[list(struct.unpack_from('<5i',q,k*20)) for k in range(4)],texture_slot=slot,source_surface_id=sid,color=color,row=reserved))
        cells.append(dict(origin=[ox,oy,oz],quads=quads));total+=n
    frame=dict(cells=cells,quads=total)
    poses=[]
    for i in range(sprite['sprite_count']):
        p=struct.unpack('<3i2h4H2I',render.ram(frontend,sprite['list_address']+i*32,32));poses.append(p)
    demand=[]
    for i in range(sprite['demand_count']):
        data=render.ram(frontend,sprite['demand_address']+i*sprite['demand_bytes'],sprite['demand_bytes'])
        key,rid,pixels,size,palette,sw,sh,w,h,transparent=struct.unpack_from('<Q4I4HI',data)
        e=entries[rid];assert size==sw*sh
        tiled=(key>>48)&3
        assert (w,h)==((192,96) if tiled==1 else (96,192) if tiled==2 else (e['width'],e['height']))
        actual=render.ram(frontend,pixels,size)
        if tiled:
            expected=bytearray(size)
            for yy in range(h):
                for xx in range(w):expected[yy*sw+xx]=e['pixels'][(yy%96)*128+xx%96]
            expected=bytes(expected)
        else:expected=effect_pixels(e['pixels'],sw,sh,w,h,key,light)
        assert actual==expected,f'Actual ARM9 effect texture differs {rid} key {key:016x}'
        mask=(key>>38)&31
        zeroed=sum(v!=0 and light[(mask-1)*256+v]==0 for v in e['pixels']) if mask else 0
        demand.append(dict(key=key,id=rid,bytes=size,transparent=transparent,zeroed_by_light=zeroed,pixels_sha256=hashlib.sha256(actual).hexdigest()))
    assert sum(r['bytes'] for r in demand)<=393216
    # The scene frame remains a complete lease; append must never overwrite base
    # quads or silently drop a gathered object. Match stable provenance exactly.
    all_quads=[q for cell in frame['cells'] for q in cell['quads']]
    expected={p[-1] for p in poses};actual={q['source_surface_id'] for q in all_quads if q['source_surface_id']&0x80000000}
    assert expected==actual and len(actual)==len(poses)
    for p in poses:
        qs=[q for q in all_quads if q['source_surface_id']==p[-1]]
        q=qs[0]
        if q['texture_slot']!=65535:
            d=demand[q['texture_slot']]
            assert d['id']==((p[-2]&~0x3fc00)|(5<<10))
        assert max(v[3] for q in qs for v in q['vertices'])==(p[7]-1)*65536
        assert max(v[4] for q in qs for v in q['vertices'])==(p[8]-1)*65536
    assert sprite['commit_start_line']>=192 or sprite['commit_start_line']<48
    assert sprite['commit_end_line']>=192 or sprite['commit_end_line']<48
    return dict(sprites=len(poses),quads=frame['quads'],texture_bytes=sum(r['bytes'] for r in demand),
                effects=list(ef),demand=demand,exact_texture_bytes=sum(r['bytes'] for r in demand),frame=frame)

def check_occlusion(frontend,probe,frame,entries,demand,packet,case_count):
    projection=struct.unpack('<16i',render.ram(frontend,probe['projection_address'],64));polygons=[]
    ea,_=transport.elf_symbol(EFFECT_ELF,'g_nds_effect_probe');ef=struct.unpack('<32I',render.ram(frontend,ea,128))
    palette=struct.unpack('<4096H',render.ram(frontend,ef[6],8192));light=render.ram(frontend,ef[4],4096)
    variants={}
    for d in demand:
        e=entries[d['id']];sw,sh=struct.unpack_from('<2H',e['raw'],28)
        variants[d['key']]=effect_pixels(e['pixels'],sw,sh,e['width'],e['height'],d['key'],light)
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
                pixels=variants[demand[q['texture_slot']]['key']]
                indices={pixels[vv*sw+uu] for vv in range(max(0,v-1),min(h,v+2)) for uu in range(max(0,u-1),min(w,u+2))}
                colors={palette[q['row']*256+n] if n else None for n in indices}
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

def read_flat_frame(frontend, probe: dict) -> dict:
    if (probe["render_frame_bytes"],probe["cell_record_bytes"],probe["quad_record_bytes"]) != (32,20,92):
        raise ValueError("N13 ARM9 frame/cell/quad ABI changed")
    raw = render.ram(frontend,probe["render_frame_address"],32)
    x,y,z,s,c,pointer,count,generation = struct.unpack("<5i3I",raw)
    if generation != probe["render_generation"] or not 1 <= count <= 2048:
        raise AssertionError("N13 observed frame generation or cell count")
    cells = []
    total = 0
    for index in range(count):
        ox,oy,oz,quads,quad_count = struct.unpack("<3i2I",render.ram(frontend,pointer+20*index,20))
        if not 1 <= quad_count <= 2048:
            raise AssertionError("N13 cell quad count outside geometry contract")
        values = []
        for number in range(quad_count):
            data = render.ram(frontend,quads+92*number,92)
            vertices = [struct.unpack_from("<5i",data,20*i) for i in range(4)]
            slot,flags,surface,color,reserved = struct.unpack_from("<HHIHH",data,80)
            if slot != 65535 or flags or reserved or not 0 < color <= 32767:
                raise AssertionError("N13 flat diagnostic contains texture/transparent/invalid material")
            expected_color = (((surface*2654435761)&0xFFFFFFFF)>>17) or 1
            if color != expected_color:
                raise AssertionError("N13 flat color does not identify its source surface")
            values.append(dict(vertices=vertices,source_surface_id=surface,color=color))
        total += quad_count
        if total > 2048:
            raise AssertionError("N13 flat diagnostic unbounded surface stream")
        cells.append(dict(origin=(ox,oy,oz),quads=values))
    return dict(camera=(x,y,z),sine=s,cosine=c,generation=generation,cells=cells,quads=total)

render.read_flat_frame=read_flat_frame

def read_flat_frame_unchecked(frontend,probe):
    raw=render.ram(frontend,probe['render_frame_address'],32)
    x,y,z,s,c,cp,count,gen=struct.unpack('<5i3I',raw)
    out=[]
    for i in range(min(count,2048)):
        ox,oy,oz,qp,n=struct.unpack('<3i2I',render.ram(frontend,cp+i*20,20))
        out.append(dict(origin=[ox,oy,oz],quads=[struct.unpack('<23I',render.ram(frontend,qp+j*92,92)) for j in range(min(n,2048))]))
    return out

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,default=CORE);parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--packet',type=Path,default=ROOT/'build/nds/n07/scene.dat')
    parser.add_argument('--max-frontend-frames',type=int,default=150000)
    parser.add_argument('--output-subdir',choices=('emulator','water-raster'),default='emulator')
    args=parser.parse_args();target=args.target.absolute();directory=target/args.output_subdir
    assert target.is_relative_to(ROOT/'build/nds') and not any(p.is_symlink() or p.is_junction() for p in [target,*target.parents] if p.exists())
    directory.mkdir(parents=True,exist_ok=True)
    report=dict(task='N17P',passed=False,scope='DS-mode emulator only; physical DS/DS Lite remain N24',cases=[],history_records=[])
    frontend=None
    try:
        assert transport.sha(args.core)==render.CORE_SHA
        package,entries=catalog();packet,cases,fixture=render.read_fixture(args.packet)
        rom=target/'dok_nds_gameplay.nds';elf=target/'dok_nds_gameplay.elf'
        global EFFECT_ELF
        EFFECT_ELF=elf
        report.update(core_sha256=transport.sha(args.core),rom_sha256=transport.sha(rom),elf_sha256=transport.sha(elf),fixture=fixture)
        frontend=transport.Frontend(args.core,directory,'happy')
        image=frontend.save/'melonDS DS/dldi_sd_card.bin';image.with_name(image.name+'.idx').unlink(missing_ok=True)
        fat.write_image(image,[('assets.ndp',b'ASSETS  NDP',package),('scene.dat',b'SCENE   DAT',packet)])
        frontend.load(rom);transport.flush_native_stdio();before=transport.sha(image)
        header=ROOT/'ports/nds/platform/render_probe/render_probe.h'
        fields=render.scalar_fields(header,'NdsRenderProbe');hf=render.scalar_fields(header,'NdsRenderHistoryRecord')
        sf=render.scalar_fields(ROOT/'ports/nds/platform/sprites_probe/sprites_probe.h','NdsSpriteProbe')
        address,_=transport.elf_symbol(elf,'g_nds_render_probe');sa,_=transport.elf_symbol(elf,'g_nds_sprite_probe')
        ordinal=0;next_case=0;stable=0;last=None;joypad=0;trace_ordinal=0
        report['gameplay_frames']=[];report['weapon_captures']=[];weapon_seen=set();game_seen=set();ga,_=transport.elf_symbol(elf,'g_nds_gameplay_probe')
        report['effect_traces']=[];report['effect_frames']=[];effect_frame_seen=set()
        ea,_=transport.elf_symbol(elf,'g_nds_effect_probe')
        for _ in range(args.max_frontend_frames):
            frontend.joypad=joypad;joypad=0
            if frontend.frame_time and frontend.frame_time.callback:frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run();frontend.frames+=1
            assert not frontend.callback_error and not frontend.shutdown
            probe=render.observe(frontend,address,fields,0x37524b44)
            if not probe:continue
            report['last_observation']=probe
            gp=struct.unpack('<64I',render.ram(frontend,ga,256))
            if gp[0]==0x17504b44 and gp[3] not in game_seen:
                assert gp[10]==gp[11]==gp[12]==0,gp
                game_seen.add(gp[3]);report['gameplay_frames'].append(list(gp))
                if len(gp)>48 and gp[42] and gp[46]==0 and gp[47] in (2,3,5,6,7) and gp[2] not in weapon_seen:
                    weapon_seen.add(gp[2]);report['weapon_captures'].append(dict(case_id=gp[2],frame_id=gp[3],view=list(gp[42:49]),images=frontend.capture(f'weapon-{gp[2]:02}')))
            if probe['status']==1 and 0<probe['case_id']<=len(cases) and 22<=cases[probe['case_id']-1]['source_kind']<=27:
                phase=gp[9]%120;kind=cases[probe['case_id']-1]['source_kind']
                joypad=(1<<11)
                if kind in (22,23,25):joypad|=((1<<4) if phase<60 else (1<<7))
                if kind in (23,24) and phase%24==0:joypad|=1<<2
                if kind==27 and phase%40<3:joypad|=1<<9

            ef=struct.unpack('<32I',render.ram(frontend,ea,128))
            if ef[0]==0x13504b44 and ef[24]==52:
                assert 0<=ef[25]-trace_ordinal<=512
                for number in range(trace_ordinal+1,ef[25]+1):
                    row=struct.unpack('<13I',render.ram(frontend,ef[23]+((number-1)%512)*52,52))
                    if row[1]&1 or row[2]!=number:break
                    assert row[0]==0x13454b44
                    if row[7]==13:
                        assert row[11]==0 and row[9] in range(1,19) and row[10]==row[9]-1
                        if row[12]:assert row[9:11]==(18,17)
                    report['effect_traces'].append(list(row));trace_ordinal=number
                if ef[28]==probe['presented_frame_id'] and ef[28] not in effect_frame_seen:
                    report['effect_frames'].append(list(ef));effect_frame_seen.add(ef[28])

            if probe['status']==3 or probe['error_bits']:
                report['last_sprite_observation']=render.observe(frontend,sa,sf,0x12504b44)
                ea,_=transport.elf_symbol(elf,'g_nds_effect_probe')
                report['last_effect_observation']=struct.unpack('<32I',render.ram(frontend,ea,128))
                if probe['render_frame_address']:
                    render.write_json(directory/'failed-frame.json',read_flat_frame_unchecked(frontend,probe))
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
            if cases[next_case]['source_kind'] in (13,17):
                result['masked_occlusion']=check_occlusion(frontend,probe,frame,entries,result['demand'],packet,len(cases))
                if cases[next_case]['source_kind']==17:assert sum(d['zeroed_by_light'] for d in result['demand'])>0
            if cases[next_case]['source_kind']==18:
                assert result['effects'][8]==1
                width,height,rgb=frontend.rgb();assert (width,height)==(256,384) and not any(rgb[:256*192*3])
                result['whole_top_fade_black']=True
            result.update(id=probe['case_id'],level=probe['level'],sprite=sprite,
                          images=frontend.capture(f"case-{probe['case_id']:02}"))
            report['cases'].append(result)
            (directory/'progress.json').write_text(json.dumps(dict(cases=len(report['cases']),case_id=probe['case_id'],frame_id=probe['frame_id'],frontend_frames=frontend.frames)), 'utf8')
            print(f"case {probe['case_id']}: {result['sprites']} sprites, {result['quads']} quads, {result['texture_bytes']} texture bytes",flush=True)
            next_case+=1;joypad=1<<8;stable=0
        else:raise AssertionError('No completed N13 run within observation bound')
        report['memory']=render.verify_memory(frontend,probe,elf)
        try:
            pa,_=transport.elf_symbol(elf,'g_nds_effect_profile')
        except (AssertionError,KeyError,ValueError):pa=0
        if pa:
            report['preparation_peaks']=[list(struct.unpack('<8I',render.ram(frontend,pa+i*32,32))) for i in range(64) if struct.unpack('<I',render.ram(frontend,pa+i*32,4))[0]]
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
        normal=[c for c in cases if c['source_kind'] not in (12,13,14,17,18,25,26,28)]
        stress=[c for c in cases if c['source_kind'] in (12,13,14,17,18,25,26,28)]
        normal_ids={c['case_id'] for c in normal};stress_ids={c['case_id'] for c in stress}
        if normal:report['history']=summarize([r for r in report['history_records'] if r['case_id'] in normal_ids],probe,normal,quad_strips=True)
        for label,kinds in (('live_routes',{22,23,24,27}),('all_awake',{25}),('controlled_death',{26})):
            group=[c for c in cases if c['source_kind'] in kinds];ids={c['case_id'] for c in group}
            if group:report[label]=summarize([r for r in report['history_records'] if r['case_id'] in ids],probe,group,quad_strips=True)
        if stress:
            report['all_frame_state_stress']=summarize([r for r in report['history_records'] if r['case_id'] in stress_ids],probe,stress,quad_strips=True)
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
