"""N23 UI revision; retain N19 inputs and use original weapon panel art."""
import hashlib
import json
import struct
from pathlib import Path
from extract_ui import ROOT, originals, scaled

OUT = ROOT / 'build/nds/n23/playtest-ui'


def prepare():
    OUT.mkdir(parents=True, exist_ok=True)
    old = (ROOT / 'build/nds/n19/ui.dat').read_bytes()
    blob = bytearray(old)
    rows = []
    for language, source in originals().items():
        resources = {bytes.fromhex(e['name']).decode('cp866'): e for e in source['entries']}
        start = 32 + (language - 1) * 8192
        for i in range(2):
            name = 'ПОРУЖИЕ' + str(i + 1)
            sw,sh=struct.unpack_from('<HH',resources[name]['payload'])
            factor=min(20/sw,10/sh)
            width,height=max(1,round(sw*factor)),max(1,round(sh*factor))
            image=scaled(resources[name],width,height)
            pixels=bytearray(200)
            for y in range(height):
                at=((10-height)//2+y)*20+(20-width)//2
                pixels[at:at+width]=image[y*width:(y+1)*width]
            at = start + 7778 + i * 200
            blob[at:at + 200] = pixels
            rows.append(dict(language=language, name=name, resource_index=resources[name]['index'],
                             bank_offset=at-start, sha256=hashlib.sha256(pixels).hexdigest()))
        assert hashlib.sha256(source['path'].read_bytes()).hexdigest() == source['sha256']
    header = (ROOT / 'build/nds/n19/generated/ui_native.inc').read_text('ascii')
    for language in range(2):
        at = 32 + language * 8192
        old_hash = hashlib.sha256(old[at:at+8192]).digest()
        new_hash = hashlib.sha256(blob[at:at+8192]).digest()
        encode = lambda b: '{' + ','.join(f'0x{x:02x}' for x in b) + '}'
        header = header.replace(encode(old_hash), encode(new_hash))
    (OUT / 'ui.dat').write_bytes(blob)
    (OUT / 'ui_native.inc').write_text(header, 'ascii')
    (OUT / 'sources.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), 'utf8')
    return OUT


if __name__ == '__main__':
    print(prepare())
