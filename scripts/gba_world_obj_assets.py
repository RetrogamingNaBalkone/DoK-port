"""Lossless 16x32 prelit OBJ strips from the existing GBA asset package."""
import hashlib
from pathlib import Path
import struct


def build_world_obj_tables(out, data):
    u16 = lambda at:struct.unpack_from('<H',data,at)[0]
    u32 = lambda at:struct.unpack_from('<I',data,at)[0]
    count,directory,views = u32(20),u32(24),u32(28)
    columns=(out/'columns.bin').read_bytes()
    prelit=(out/'prelit.bin').read_bytes()
    exact=(out/'exact-offsets.bin').read_bytes()
    records=bytearray();offsets=[];bank=bytearray();cache={};summaries=[]
    for view in range(6):
        rec=views+view*80
        links={tuple(struct.unpack_from('<3H',data,u32(rec+72)+i*8)[:2]):u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}
        opacity={}
        for (obj,slot),rid in links.items():
            if obj==65535 or slot!=65520:continue
            opacity.setdefault(rid,set()).add(u16(u32(rec+36)+obj*32+4)&8)
        def resource(rid):
            e=directory+(rid-1)*32+16+(view%2)*8
            return data[u32(e):u32(e)+u32(e+4)]
        palette=resource(links[65535,65523])
        colors=[tuple(c>>1 for c in palette[i*3:i*3+3]) for i in range(256)]
        black=next(i for i in range(1,256) if colors[i]==colors[0])
        images=patches=0
        for rid in range(count+1):
            column_offset=struct.unpack_from('<I',columns,(view*(count+1)+rid)*8)[0]
            if not column_offset:
                records.extend(bytes(12));continue
            image=resource(rid);w,h,flags=struct.unpack_from('<3H',image)
            modes={((flags|mode)&8) for mode in opacity.get(rid,{0})}
            if len(modes)!=1:
                records.extend(bytes(12));continue
            effective=next(iter(modes))
            assert len(image)==18+w*h and w<=256 and h<=512
            cols,rows=(w+15)//16,(h+31)//32
            records.extend(struct.pack('<I4H',len(offsets),w,h,cols,effective))
            images+=1
            for light in range(16):
                at=struct.unpack_from('<I',exact,((view*(count+1)+rid)*16+light)*4)[0]
                assert at
                for sy in range(0,h,32):
                    for sx in range(0,w,16):
                        patch=bytes((prelit[at+(sx+x)*h+sy+y] or (0 if effective else black))
                                    if sx+x<w and sy+y<h else 0
                                    for y in range(32) for x in range(16))
                        tiled=bytes(patch[(ty+y)*16+tx+x] for ty in range(0,32,8) for tx in range(0,16,8)
                                    for y in range(8) for x in range(8))
                        if tiled not in cache:
                            cache[tiled]=len(bank);bank.extend(tiled)
                        offsets.append(cache[tiled]|int(0 not in patch));patches+=1
            assert rows*cols*16>0
        summaries.append(dict(view=view,images=images,patch_references=patches,black_index=black))
    files={'world-obj-records.bin':records,'world-obj-patch-offsets.bin':struct.pack('<'+'I'*len(offsets),*offsets),
           'world-obj-bank.bin':bank}
    assembly=[]
    for symbol,name in (('dok_world_obj_records','world-obj-records.bin'),
                        ('dok_world_obj_patch_offsets','world-obj-patch-offsets.bin'),
                        ('dok_world_obj_bank','world-obj-bank.bin')):
        (out/name).write_bytes(files[name])
        assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'.global {symbol}',f'{symbol}:',
                     '.incbin "'+(out/name).resolve().as_posix()+'"']
    (out/'world-obj-tables.S').write_text('\n'.join(assembly)+'\n',encoding='utf-8')
    return dict(views=summaries,bank_bytes=len(bank),unique_patches=len(cache),
                sha256={name:hashlib.sha256(payload).hexdigest() for name,payload in files.items()})
