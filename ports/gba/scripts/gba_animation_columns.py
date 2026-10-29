"""Lossless column-major copies of original animation frames, keyed by package offset."""

import hashlib
import struct
from probe_render_layout import frames


def build_animation_columns(out, data):
    u16 = lambda at: struct.unpack_from('<H', data, at)[0]
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    selected = {}
    for view in range(6):
        base = u32(28) + view*80
        definitions, fields, bindings = u32(base+36), u32(base+40), u32(base+72)
        slots = set()
        for obj in range(u16(base+16)):
            definition = definitions + obj*32
            slots.add((obj, 0xfff1))
            for i in range(u16(definition+14), u16(definition+14)+u16(definition+16)):
                if data[fields+i*8+2] == 4:
                    slots.add((obj, u16(fields+i*8)))
        for i in range(u16(base+76)):
            row = bindings + i*8
            if (u16(row),u16(row+2)) not in slots:
                continue
            record = u32(24) + (u16(row+4)-1)*32 + 16 + (view%2)*8
            offset,size = u32(record),u32(record+4)
            selected[offset] = size
    bank = bytearray(4); index = []; cache = {}; total = 0
    for offset,size in sorted(selected.items()):
        for metadata,pixels in frames(data[offset:offset+size]):
            w,h = metadata['width'],metadata['height']
            assert 0 < w <= 256 and 0 < h <= 512
            column = bytes(pixels[y*w+x] for x in range(w) for y in range(h))
            key = (w,h,column)
            if key not in cache:
                bank.extend(bytes(-len(bank)%4))
                bank.extend(bytes(int(0 not in column[x*h:(x+1)*h]) for x in range(w)))
                bank.extend(bytes(256-w))
                cache[key] = len(bank)
                bank.extend(column)
            assert bytes(column[x*h+y] for y in range(h) for x in range(w)) == pixels
            index.append((offset+metadata['image_offset']+18,w|(h<<16),cache[key]))
            total += len(pixels)
    index.sort()
    assert len({row[0] for row in index}) == len(index)
    (out/'animation-columns.bin').write_bytes(bank)
    words = [len(index),*(value for row in index for value in row)]
    (out/'animation-columns-index.bin').write_bytes(struct.pack('<'+'I'*len(words),*words))
    assembly = []
    for symbol,filename in (('dok_animation_columns','animation-columns.bin'),
                            ('dok_animation_columns_index','animation-columns-index.bin')):
        assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'.global {symbol}',
                     f'{symbol}:',f'.incbin "{(out/filename).resolve().as_posix()}"']
    (out/'animation-columns.S').write_text('\n'.join(assembly)+'\n',encoding='ascii')
    return dict(resources=len(selected),frames=len(index),source_texels=total,
                unique_images=len(cache),bank_bytes=len(bank),index_bytes=len(words)*4,
                bank_sha256=hashlib.sha256(bank).hexdigest(),lossless=True)
