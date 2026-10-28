"""Resize original UI icons offline into immutable, standalone ROM images."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package = args.assets.read_bytes()
    assert package[:8] == b'DOKGBA1\0'
    count, table = struct.unpack_from('<II', package, 20)
    records = {package[table+i*32:table+i*32+16].split(b'\0', 1)[0]: table+i*32
               for i in range(count)}
    icons = [('ПЖИЗНЬ', (25, 17), (12, 8)), ('ПМАГИЯ', (17, 18), (8, 8)),
             ('ПКЛЮЧМ', (26, 30), (10, 12)), ('ПКЛЮЧЖ', (26, 30), (10, 12)),
             ('ПКЛЮЧЗ', (26, 30), (10, 12))]
    bank, manifest = bytearray(), []
    for language in (0, 1):
        start = len(bank)
        for name, original_size, target_size in icons:
            record = records[name.encode('cp866')]
            offset, size = struct.unpack_from('<II', package, record+16+language*8)
            source = package[offset:offset+size]
            sw, sh = struct.unpack_from('<HH', source)
            assert (sw, sh) == original_size and len(source) == 18+sw*sh
            assert struct.unpack_from('<I', source, 10)[0] == sw*sh
            width, height = target_size
            pixels = bytes(source[18+(y*sh//height)*sw+x*sw//width]
                           for y in range(height) for x in range(width))
            header = struct.pack('<5HIHH', width, height, 8, 1, 0, len(pixels), 0, 0)
            manifest.append(dict(language=language, name=name, offset=len(bank),
                                 source_sha256=hashlib.sha256(source).hexdigest(),
                                 width=width, height=height,
                                 pixels_sha256=hashlib.sha256(pixels).hexdigest()))
            bank.extend(header+pixels)
        assert len(bank)-start == 610
    digest = hashlib.sha256(package).hexdigest()
    assembly = ['.section .rodata.ui_icons,"a",%progbits', f'.ascii "{digest}"',
                '.balign 4', '.global dok_ui_icons', 'dok_ui_icons:']
    assembly += ['.byte '+','.join(map(str, bank[i:i+24])) for i in range(0, len(bank), 24)]
    # Static GUI screens are prepared offline. ARM7 must never resize or
    # modulo-tile38400 pixels on each input poll. Original payloads stay exact.
    frontend=[]
    for language in (0,1):
        def resource(name):
            at,size=struct.unpack_from('<II',package,records[name.encode('cp866')]+16+language*8)
            return package[at:at+size]
        splash=resource('ЗАСТАВКА_СПР');background=resource('FON')
        assert struct.unpack_from('<HH',splash)==(320,200)
        assert struct.unpack_from('<HH',background)==(46,46)
        pixels=bytearray(splash[18+(y+40)*320+x] if y>=149 and x<81 else splash[18+(y*200//160)*320+x*320//240]
                         for y in range(160) for x in range(240))
        # Source1995 contains shaded outlines, unlike the adjacent single-ink
        # NewCom. Keep its original light196 glyph core at1:1, clear the halo.
        for y in range(149,160):
            for x in range(58,81):
                if pixels[y*240+x]!=196:pixels[y*240+x]=0
        pixels=bytes(pixels)
        tiled=bytes(background[18+(y%46)*46+x%46] for y in range(160) for x in range(240))
        frontend.extend((pixels,tiled))
    assembly+=['.balign 4','.global dok_ui_frontend_pixels','dok_ui_frontend_pixels:']
    unique={}
    for pixels in frontend:
        key=hashlib.sha256(pixels).hexdigest()
        unique.setdefault(key,(f'.Lui_frontend_{len(unique)}',pixels))
        assembly+=['.word '+unique[key][0]]
    for symbol,pixels in unique.values():
        assembly+=['.balign 4',symbol+':']
        assembly+=['.byte '+','.join(map(str,pixels[i:i+24])) for i in range(0,len(pixels),24)]
    args.output.write_text('\n'.join(assembly)+'\n', encoding='ascii')
    report = dict(package_sha256=digest, bytes=len(bank), icons=manifest,
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  source_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  filter='nearest neighbour, index0 transparent', runtime_resize=False,
                  frontend_unique_bytes=sum(len(v[1]) for v in unique.values()),copyright_source_rectangle=[0,189,81,11],copyright_destination=[0,149],year_single_ink=196)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'UI icons: {len(manifest)} pre-sized ROM images, {len(bank)} bytes; no runtime resize')


if __name__ == '__main__':
    main()
