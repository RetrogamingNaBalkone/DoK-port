"""Resize original UI icons offline into immutable, standalone ROM images."""
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
    args.output.write_text('\n'.join(assembly)+'\n', encoding='ascii')
    report = dict(package_sha256=digest, bytes=len(bank), icons=manifest,
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  source_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  filter='nearest neighbour, index0 transparent', runtime_resize=False)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'UI icons: {len(manifest)} pre-sized ROM images, {len(bank)} bytes; no runtime resize')


if __name__ == '__main__':
    main()
