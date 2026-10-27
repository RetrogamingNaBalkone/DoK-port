"""Derive affine-plane tiles from the verified G03 package; no EXE analysis."""
import struct
import re
from pathlib import Path


def hardware_background_tables(out,data,scene_rows=128):
    u16=lambda a:struct.unpack_from('<H',data,a)[0]
    u32=lambda a:struct.unpack_from('<I',data,a)[0]
    counts=[];objects=[];animated=[];mapping=bytearray([255]*(6*2*256))
    offsets=[0]*(6*8*18*3);bank=bytearray();cache={};summaries=[]
    for view in range(6):
        rec=u32(28)+view*80;defs=u32(rec+36);fields=u32(rec+40);maps=u32(rec+48);glyphs=u32(rec+32)
        links={tuple(struct.unpack_from('<3H',data,u32(rec+72)+i*8)[:2]):u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}
        def resource(rid):
            e=u32(24)+(rid-1)*32+16+(view%2)*8
            return data[u32(e):u32(e)+u32(e+4)]
        light=resource(links[65535,65524]);assert len(light)==4096
        materials=[];keys={};insets=set()
        for glyph in sorted(set(data[maps+8192:maps+12288])):
            obj=u16(glyphs+glyph*2)-1;at=defs+obj*32;cls=u16(at);flags=u16(at+4)
            inset=next(struct.unpack_from('<i',data,fields+i*8+4)[0] for i in range(u16(at+14),u16(at+14)+u16(at+16)) if u16(fields+i*8)==84)
            insets.add(inset)
            assert struct.unpack_from('<h',data,at+8)[0]==0 and inset in (0,10,400)
            for plane in (0,1):
                if plane and inset>10:continue
                rid=links[obj,65520 if not plane and cls in (12,13) else 86 if plane else 90]
                moving=not plane and cls in (12,13) and bool(flags&0x3fc0)
                key=(rid,plane,obj if moving else -1)
                if key not in keys:
                    keys[key]=len(materials);materials.append(dict(object=obj,rid=rid,plane=plane,moving=moving,flags=flags if moving else 0))
                mapping[(view*2+plane)*256+glyph]=keys[key]
        assert len(materials)<=7,(view,materials)
        counts.append(len(materials));objects.extend([m['object'] for m in materials]+[65535]*(8-len(materials)))
        animated.extend([int(m['moving']) for m in materials]+[0]*(8-len(materials)))
        for mindex,material in enumerate(materials):
            raw=resource(material['rid']);w,h=struct.unpack_from('<HH',raw);assert len(raw)==18+w*h
            for phase in range(18):
                sx=sy=0;wrap=False;flags=material['flags']
                if flags&0x440:sx=-(w//18)*phase;wrap=bool(flags&0x400)
                elif flags&0x880:sx=(w//18)*phase;wrap=bool(flags&0x800)
                elif flags&0x1100:sy=-(h//18)*phase;wrap=bool(flags&0x1000)
                elif flags&0x2200:sy=(h//18)*phase;wrap=bool(flags&0x2000)
                plane_pixels=[]
                for y in range(32):
                    for x in range(32):
                        tx=(x*2048*(w-1))>>16;fy=y*2048 if material['plane'] else 65535-y*2048
                        ty=(fy*(h-1))>>16;tx+=sx;ty+=sy
                        if wrap:tx%=w;ty%=h
                        plane_pixels.append(raw[18+ty*w+tx] if 0<=tx<w and 0<=ty<h else 0)
                for band,row in enumerate((0,7,15)):
                    tiled=bytes(light[row*256+plane_pixels[(ty*8+y)*32+tx*8+x]]
                        for ty in range(4) for tx in range(4) for y in range(8) for x in range(8))
                    if tiled not in cache:cache[tiled]=len(bank);bank.extend(tiled)
                    offsets[((view*8+mindex)*18+phase)*3+band]=cache[tiled]
        classes=bytes(u16(defs+(u16(glyphs+g*2)-1)*32) for g in data[maps+4096:maps+8192])
        min_walls=min(sum(classes[(z+j)*64+x+i]==2 for j in range(16) for i in range(16)) for z in range(49) for x in range(49))
        assert min_walls>0
        summaries.append(dict(view=view,materials=materials,insets=sorted(insets),minimum_walls_in_any16_cell_window=min_walls))
    files={'hw-bg-bank.bin':bytes(bank),'hw-bg-counts.bin':bytes(counts),
           'hw-bg-objects.bin':struct.pack('<48H',*objects),'hw-bg-animated.bin':bytes(animated),
           'hw-bg-maps.bin':bytes(mapping),'hw-bg-offsets.bin':struct.pack('<2592I',*offsets)}
    distances=[]
    for y in range(144):
        dy=y*10//9-79;height=-951 if dy<0 else 0
        d=(abs((height+300)*200)//abs(dy)) if dy else 0
        distances.append(d if 0<d-200<=3600 else 0)
    files['hw-bg-distances.bin']=struct.pack('<144H',*distances)
    trig=[int(v) for v in re.findall(r'-?\d+',(Path(__file__).resolve().parents[1]/'src/renderer_trig.inc').read_text().split('{',1)[1].split('}',1)[0])]
    assert len(trig)==722
    div=lambda n,d:(abs(n)//d)*(-1 if n<0 else 1)
    row_cnt=[];matrices=bytearray()
    for y,d in enumerate(distances):
        if not d:row_cnt.append(0);continue
        light=max(0,min(15,(d-200-1920)//120));band=0 if light<4 else 1 if light<11 else 2
        row_cnt.append(0xa082|((16+(y<72)*6+band*2)<<8)|(4 if band==2 else 0))
    for heading in range(360):
        sine,cosine=trig[heading],trig[361+heading]
        for d in distances:
            if not d:matrices.extend(bytes(12));continue
            px,pz=div(cosine*d,18000),div(sine*d,18000);depth=d-200
            matrices.extend(struct.pack('<hhii',div(px,16),div(pz,16),-div(sine*depth,1920)-div(px*477,64),div(cosine*depth,1920)-div(pz*477,64)))
    files['hw-bg-row-matrix.bin']=matrices
    files['hw-bg-row-cnt.bin']=struct.pack('<144H',*row_cnt)
    oam=bytearray()
    bands=scene_rows//8;step=144//bands
    for y in range(161):
        row=min(y//step,bands-1)
        if scene_rows==96 and y and y%step==0 and y<144:row-=1
        for col in range(4):
            oam.extend(struct.pack('<4H',0x6300|((row*step+step//2-8)&255) if y<144 else 0x200,0x4000+col*64,0x400+(row*4+col)*8,0))
    files['hw-obj-row-oam.bin']=oam
    for name,payload in files.items():(out/name).write_bytes(payload)
    return dict(texture_size=32,light_rows=[0,7,15],bank_bytes=len(bank),scene_rows=scene_rows,views=summaries)


def hardware_lowered_tables(out,data,background):
    """A second affine layer keeps inset400 at its actual y=-561 height."""
    u16=lambda a:struct.unpack_from('<H',data,a)[0]
    u32=lambda a:struct.unpack_from('<I',data,a)[0]
    mapping=bytearray([255]*(6*256));cells=[]
    for view in range(6):
        rec=u32(28)+view*80;defs=u32(rec+36);fields=u32(rec+40);maps=u32(rec+48);glyphs=u32(rec+32)
        links={tuple(struct.unpack_from('<3H',data,u32(rec+72)+i*8)[:2]):u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}
        lower=set()
        for glyph in sorted(set(data[maps+8192:maps+12288])):
            obj=u16(glyphs+glyph*2)-1;at=defs+obj*32
            inset=next(struct.unpack_from('<i',data,fields+i*8+4)[0] for i in range(u16(at+14),u16(at+14)+u16(at+16)) if u16(fields+i*8)==84)
            if inset<=10:continue
            assert inset==400
            rid=links[obj,86]
            material=next((n for n,m in enumerate(background['views'][view]['materials']) if m['plane']==1 and m['rid']==rid),None)
            assert material is not None,(view,glyph,rid)
            mapping[view*256+glyph]=material;lower.add(glyph)
        cells.append(sum(g in lower for g in data[maps+8192:maps+12288]))
    distances=[];controls=[]
    for y in range(144):
        dy=y*10//9-79
        d=52200//(-dy) if dy<0 else 0
        d=d if 0<d-200<=3600 else 0;distances.append(d)
        if not d:controls.append(0);continue
        row=max(0,min(15,(d-200-1920)//120));band=0 if row<4 else 1 if row<11 else 2
        controls.append(0xa081|((20,26,20)[band]<<8)|(4 if band==2 else 0))
    trig=[int(v) for v in re.findall(r'-?\d+',(Path(__file__).resolve().parents[1]/'src/renderer_trig.inc').read_text().split('{',1)[1].split('}',1)[0])]
    div=lambda n,d:(abs(n)//d)*(-1 if n<0 else 1)
    matrices=bytearray()
    for heading in range(360):
        sine,cosine=trig[heading],trig[361+heading]
        for d in distances:
            if not d:matrices.extend(bytes(12));continue
            px,pz=div(cosine*d,18000),div(sine*d,18000);depth=d-200
            matrices.extend(struct.pack('<hhii',div(px,16),div(pz,16),-div(sine*depth,1920)-div(px*477,64),div(cosine*depth,1920)-div(pz*477,64)))
    (out/'hw-low-maps.bin').write_bytes(mapping)
    (out/'hw-low-row-matrix.bin').write_bytes(matrices)
    (out/'hw-low-row-cnt.bin').write_bytes(struct.pack('<144H',*controls))
    base=list(struct.unpack('<144H',(out/'hw-bg-row-cnt.bin').read_bytes()))
    for n,cnt in enumerate(base):
        if cnt&4:base[n]=(cnt&~0x1f00)|((((cnt>>8)&31)-4)<<8)
    (out/'hw-bg-row-cnt.bin').write_bytes(struct.pack('<144H',*base))
    return dict(height=-561,inset=400,maps_screenblocks=[20,26,20],cells_by_view=cells,row_lut_bytes=len(matrices),
                reused_duplicate_far_maps=True)
