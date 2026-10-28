"""Unshaded original image/animation mip tiles; native copy applies16 LIGHT rows."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib
import struct

SIZES=((8,16),(16,32),(32,64),(64,128))


def sector_gpu_tables(out,data):
    u16=lambda p:struct.unpack_from('<H',data,p)[0]
    u32=lambda p:struct.unpack_from('<I',data,p)[0]
    directory=u32(24);views=u32(28);bank=bytearray();cache={};records=[];offsets=[];summaries=[]
    def resource(rid,language):
        at=directory+(rid-1)*32+16+language*8
        return u32(at),u32(at+4)
    for view in range(6):
        at=views+view*80
        links=[struct.unpack_from('<4H',data,u32(at+72)+k*8) for k in range(u16(at+76))]
        light_id=next(rid for obj,slot,rid,_ in links if obj==65535 and slot==65524)
        light_at,light_size=resource(light_id,view%2);assert light_size==4096
        light=data[light_at:light_at+4096]
        pal_id=next(rid for obj,slot,rid,_ in links if obj==65535 and slot==65523)
        pal_at,_=resource(pal_id,view%2);pal=data[pal_at:pal_at+768]
        rgb=lambda i:tuple(c>>1 for c in pal[i*3:i*3+3])
        black=next(i for i in range(1,256) if rgb(i)==rgb(0));images={}
        for obj,slot,rid,_ in links:
            if obj==65535 or slot not in (65520,65521,0xe8,0xec,0x5e):continue
            start,size=resource(rid,view%2);w,h=struct.unpack_from('<2H',data,start)
            if w and h and size==18+w*h:images[start+18]=(w,h)
            elif slot in (65521,0xe8,0xec):
                n=u16(start);pos=start+8+10*n
                for _ in range(n):
                    w,h=struct.unpack_from('<2H',data,pos)
                    assert 0<w<=256 and 0<h<=512 and pos+18+w*h<=start+size
                    images[pos+18]=(w,h);pos+=18+w*h
                assert pos==start+size
        first=len(records)
        for pixels,(width,height) in sorted(images.items()):
            for masked in (0,8):
                records.append((pixels,len(offsets),width,height,masked,0))
                mip_offsets=[]
                for w,h in SIZES:
                        tile=bytes(data[pixels+(y*(height-1)//(h-1))*width+x*(width-1)//(w-1)]
                            for ty in range(0,h,8) for tx in range(0,w,8)
                            for y in range(ty,ty+8) for x in range(tx,tx+8))
                        if tile not in cache:cache[tile]=len(bank);bank.extend(tile)
                        mip_offsets.append(cache[tile])
                for _ in range(16):offsets.extend(mip_offsets)
        summaries.append(dict(view=view,first=first,count=len(records)-first,images=len(images),black_index=black))
    files={'sector-gpu-bank.bin':bank,'sector-gpu-offsets.bin':struct.pack('<'+'I'*len(offsets),*offsets)}
    assembly=[]
    for symbol,file in (('dok_sector_gpu_bank','sector-gpu-bank.bin'),('dok_sector_gpu_offsets','sector-gpu-offsets.bin')):
        (out/file).write_bytes(files[file]);assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'.global {symbol}',symbol+':',f'.incbin "{(out/file).resolve().as_posix()}"']
    assembly += ['.section .rodata.dok_sector_gpu_records,"a",%progbits','.balign 4','.global dok_sector_gpu_records','dok_sector_gpu_records:']
    for pixels,first,w,h,mask,reserved in records:
        assembly += [f'.word dok_gba_assets+{pixels},{first}',f'.short {w},{h},{mask},{reserved}']
    assembly += ['.section .rodata.dok_sector_gpu_views,"a",%progbits','.balign 4','.global dok_sector_gpu_views','dok_sector_gpu_views:']
    for row in summaries:assembly += [f'.word {row["first"]},{row["count"]}']
    (out/'sector-gpu-tables.S').write_text('\n'.join(assembly)+'\n',encoding='utf-8')
    return dict(views=summaries,sizes=SIZES,bank_bytes=len(bank),unique_mips=len(cache),light_policy='Runtime original16-row LIGHT, no UV resampling during source copy',
        bank_sha256=hashlib.sha256(bank).hexdigest(),offsets_sha256=hashlib.sha256(files['sector-gpu-offsets.bin']).hexdigest())
