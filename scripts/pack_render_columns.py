"""Build lossless column-major, LIGHT-row0 mapped static textures from G03.

Original image dimensions and world geometry stay unchanged. Deduplicated
derived bytes live only under build/. Used by the early GBA renderer, not G03.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
data = a.assets.read_bytes()
u16 = lambda at: struct.unpack_from('<H', data, at)[0]
u32 = lambda at: struct.unpack_from('<I', data, at)[0]
assert data[:8] == b'DOKGBA1\0'
# Header offsets are the explicit G03 format, see rom_assets.c.
resource_count, resource_offset, view_offset = u32(20), u32(24), u32(28)
bank = bytearray(6*(resource_count+1)*8)
cache = {}
strips = {}
images = 0
def initial_v(height,offset,cell_height):
    n=height*offset
    quotient=(abs(n)//cell_height)*(-1 if n<0 else 1)
    word=(quotient+32768)%65536-32768
    return max(0,min(height-1,word))
for view in range(6):
    record = view_offset+view*80
    defs, bindings, count = u32(record+36), u32(record+72), u16(record+76)
    links = {(u16(bindings+i*8), u16(bindings+i*8+2)): u16(bindings+i*8+4)
             for i in range(count)}

    def resource(rid):
        entry = resource_offset+(rid-1)*32+16+(view%2)*8
        at, size = u32(entry), u32(entry+4)
        return data[at:at+size]

    light = resource(links[0xffff, 0xfff4])[:256]
    # Values actually reachable through this level's FCMap; native field0x54
    # is the wall's top offset. Preserve the runtime's v0 calculation.
    maps, glyphs, fields = u32(record+48), u32(record+32), u32(record+40)
    floor_objects={u16(glyphs+2*g)-1 for g in data[maps+8192:maps+12288]}
    top_offsets=set()
    for floor in floor_objects:
        d=defs+floor*32
        for j in range(u16(d+14),u16(d+14)+u16(d+16)):
            if u16(fields+j*8)==0x54:
                top_offsets.add(struct.unpack_from('<i',data,fields+j*8+4)[0])
    for obj in range(u16(record+16)):
        # Definition class_id is word0 in the asserted 32-byte layout.
        if u16(defs+obj*32) not in (2, 4, 7):
            continue
        rid = links[obj, 0xfff0]
        image = resource(rid)
        w, h = struct.unpack_from('<HH', image)
        assert len(image) == 18+w*h and w <= 256 and h <= 512
        columns = bytes(light[image[18+y*w+x]] for x in range(w) for y in range(h))
        starts=tuple(sorted({initial_v(h,offset,u16(record+8)) for offset in top_offsets})) if u16(defs+obj*32)==2 else (0,)
        key=(columns,h,starts)
        if key not in cache:
            bank.extend(b'\0'*(-len(bank)%4))
            column_offset = len(bank)
            bank.extend(columns)
            # Match the runtime Q16 step, inclusive bottom sample, and clipping.
            # World dimensions and the original source payload stay unchanged.
            table = len(bank) if starts else 0
            if starts:
                bank.extend(bytes(h*4))
            for start in starts:
                span_table=len(bank)
                struct.pack_into('<I',bank,table+start*4,span_table)
                bank.extend(bytes(129*4))
                for span in range(1,129):
                    struct.pack_into('<I',bank,span_table+span*4,len(bank))
                    column_table=len(bank)
                    bank.extend(bytes(w*4))
                    step=((h-1-start)<<16)//span
                    samples=[start+((y*step)>>16) for y in range(span+1)]
                    for x in range(w):
                        colors=bytes(columns[x*h+y] for y in samples)
                        if colors not in strips:
                            runs=[]
                            y=0
                            while y<len(colors):
                                if not colors[y]:y+=1;continue
                                first=y
                                while y+1<len(colors) and colors[y+1]:y+=1
                                runs.extend((first,y));y+=1
                            payload=(4+len(runs)+3)&~3
                            assert payload<=255
                            strips[colors]=len(bank)
                            bank.extend(bytes((len(runs)//2,payload,0,0)))
                            bank.extend(bytes(runs));bank.extend(bytes(payload-4-len(runs)))
                            bank.extend(colors);bank.extend(bytes(-len(bank)%4))
                        struct.pack_into('<I',bank,column_table+x*4,strips[colors])
            cache[key] = (column_offset,table)
        # Class2 appears first and prepares v0=0 plus its FCMap starts, so a
        # shared wall/center-plane resource retains all needed starts.
        entry=(view*(resource_count+1)+rid)*8
        if starts or not struct.unpack_from('<I',bank,entry)[0]:
            struct.pack_into('<II', bank, entry, *cache[key])
        images += 1
a.output.write_bytes(bank)
a.output.with_suffix('.json').write_text(json.dumps(dict(
    package_sha256=hashlib.sha256(data).hexdigest(), bytes=len(bank),
    bank_sha256=hashlib.sha256(bank).hexdigest(), resource_count=resource_count,
    image_bindings=images, unique_images=len(cache), lossless=True,
    layout='6 views, resource_count+1 pairs of LE32 column/v0-table offsets; columns x*height+y; v0 table height LE32 bank offsets to span tables129 LE32 bank offsets to width LE32 record offsets; record u8 run_count,payload_offset,0,0 then inclusive u8 run pairs, align4, span+1 colors, align4; LIGHT row0',
    unique_strips=len(strips),
    scale_max_span=128, scale_v0='class2 reachable FCMap top offsets; class4/7 zero', scale_rounding='exact Q16 floor step, inclusive span+1 samples'
), indent=2)+'\n', encoding='utf-8')
