"""Validate every bound immutable ROM animation and index its original frames.

Only offsets are derived. Pixel bytes, frame order and timing stay in the
unchanged G03 package; the ordinary decoder remains the fallback for other data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from probe_render_layout import frames


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    data = a.assets.read_bytes()
    assert data[:8] == b'DOKGBA1\0'
    u16 = lambda off: struct.unpack_from('<H', data, off)[0]
    u32 = lambda off: struct.unpack_from('<I', data, off)[0]
    resources, views = u32(24), u32(28)
    selected = {}
    for view in range(6):
        base = views+view*80
        definitions, fields, bindings = u32(base+36), u32(base+40), u32(base+72)
        slots = set()
        for obj in range(u16(base+16)):
            definition = definitions+obj*32
            slots.add((obj, 0xfff1))
            for i in range(u16(definition+14), u16(definition+14)+u16(definition+16)):
                if data[fields+i*8+2] == 4:
                    slots.add((obj, u16(fields+i*8)))
        for i in range(u16(base+76)):
            row = bindings+i*8
            if (u16(row), u16(row+2)) not in slots:
                continue
            rid = u16(row+4)
            record = resources+(rid-1)*32+16+(view%2)*8
            offset, size = u32(record), u32(record+4)
            if offset not in selected:
                decoded = frames(data[offset:offset+size])
                selected[offset] = (size, [metadata['image_offset'] for metadata, _ in decoded])
    assert selected
    words = [len(selected)]
    offsets = []
    for offset, (size, indices) in sorted(selected.items()):
        words.extend((offset, size, len(indices), 1+4*len(selected)+len(offsets)))
        offsets.extend(indices)
    words.extend(offsets)
    digest = hashlib.sha256(data).hexdigest()
    source = ['.section .rodata.animation_index,"a",%progbits', f'.ascii "{digest}"',
              '.balign 4', '.global dok_animation_index', 'dok_animation_index:']
    source.extend('.word '+','.join(map(str, words[i:i+12])) for i in range(0, len(words), 12))
    a.output.write_text('\n'.join(source)+'\n', encoding='ascii')
    a.output.with_suffix('.json').write_text(json.dumps(dict(
        package_sha256=digest, script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        resources=len(selected), frames=len(offsets), index_bytes=len(words)*4,
        source_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),
        lossless=True, layout='LE32 count, sorted [package_offset,size,frame_count,offset_table_word_index], frame image offsets'),
        indent=2)+'\n', encoding='utf-8')
    print(f'Animation index: {len(selected)} immutable resources, {len(offsets)} original frames, {len(words)*4} bytes')


if __name__ == '__main__':
    main()
