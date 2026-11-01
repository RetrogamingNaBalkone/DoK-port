"""Check full-size original lettering, QR quiet zone and independently decode PPU."""
import json
from pathlib import Path
import shutil
import struct
import subprocess

from vendor.nayuki.qrcodegen import QrCode, QrSegment
from verify_ui_gba import glyph_mask, palette_extremes

URL = 'https://t.me/RetroGamingNaBalkone'


def verify_author(probe, font, language, foreground):
    raw, palette = probe.frame()
    dark, light = palette_extremes(palette)
    qr = QrCode.encode_segments([QrSegment.make_bytes(URL.encode('ascii'))],
        QrCode.Ecc.MEDIUM, minversion=3, maxversion=3, boostecl=False)
    for y in range(111):
        for x in range(111):
            mx, my = x//3-4, y//3-4
            expected = dark if qr.get_module(mx,my) else light
            assert raw[(24+y)*240+8+x] == expected, ('QR/quiet zone',x,y)
    lines = [('GBA-порт', 'Ретрогейминг', 'на балконе'),
             ('GBA port by', 'Retrogaming', 'Na Balkone')][language]
    for y, line in zip((40,72,88),lines):
        glyph_mask(raw,font,128,y,line.encode('cp866'),foreground)
    link = URL.removeprefix('https://').encode('ascii')
    glyph_mask(raw,font,(240-len(link)*8)//2,136,link,foreground)
    # Decode actual libretro PPU callback, not the ROM mask or a reconstructed QR.
    rgba = bytearray()
    fmt = probe.frontend.pixel_format
    for (v,) in struct.iter_unpack('<I' if fmt==1 else '<H',probe.frontend.last_video_raw):
        if fmt==1:
            r,g,b = (v>>16)&255,(v>>8)&255,v&255
        else:
            bits = 6 if fmt==2 else 5
            r = ((v>>(5+bits))&31)*255//31
            g = ((v>>5)&((1<<bits)-1))*255//((1<<bits)-1)
            b = (v&31)*255//31
        rgba.extend((r,g,b,255))
    path = probe.build/'ui-check'/f'channel-{language}.rgba'
    path.parent.mkdir(exist_ok=True)
    path.write_bytes(rgba)
    node = shutil.which('node')
    assert node, 'Node.js is required for independent QR decoding'
    decoded = json.loads(subprocess.check_output(
        [node,str(Path(__file__).with_name('decode_channel_qr.cjs')),str(path)],text=True))
    assert decoded == dict(data=URL,version=3), decoded
    return dict(url=URL,version=3,ecc='M',scale=3,quiet_modules=4,
                decoded_actual_ppu=decoded,decoder='jsQR 1.4.0',
                original_font_pixels=True,all_qr_and_quiet_pixels=True)
