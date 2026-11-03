"""Render original LEVEL1 doors; report comparison without claiming a full frame."""
import binascii
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib
from check_assets import read_archive

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/sdl2/checks'


def png(path,pixels,width,height,palette):
    def chunk(name,data):
        return struct.pack('>I',len(data))+name+data+struct.pack('>I',binascii.crc32(name+data)&0xffffffff)
    scanlines=b''.join(b'\0'+pixels[y*width:(y+1)*width] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,3,0,0,0))+
                     chunk(b'PLTE',bytes(v*255//63 for v in palette))+chunk(b'IDAT',zlib.compress(scanlines))+chunk(b'IEND',b''))


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    report={'scope':'Two door surfaces only; no full-world pixel-equivalence assertion','profiles':{}}
    for language,folder,filename,snapshot in [('ru','RUS','PKR.RES','ru-03'),('en','ENG','PKE.RES','en-02')]:
        source=ROOT/'originals/pkr'/folder/filename
        original=source.read_bytes()
        subprocess.run([str(ROOT/'build/sdl2/windows/dok_render_probe.exe'),str(source),str(OUT/f'doors-{language}.idx')],check=True)
        pixels=(OUT/f'doors-{language}.idx').read_bytes()
        reference=(ROOT/'research/evidence/reference'/snapshot/'level1-start.idx').read_bytes()
        if len(pixels)!=320*160 or len(reference)!=320*200:raise RuntimeError('Wrong frame size')
        palette=dict(read_archive(original))[b'GAMEP1']
        png(OUT/f'doors-{language}.png',pixels,320,160,palette)
        png(OUT/f'reference-{language}.png',reference,320,200,palette)
        drawn=[i for i,pixel in enumerate(pixels) if pixel]
        if not drawn:raise RuntimeError('No rendered door texels')
        # These fixed visible regions are inside the measured door records.
        # The top five rows are covered by a wall in both DOS frames; in ENG
        # the right edge is also covered. Do not compare transparent background.
        visible=[i for i in drawn if i//320>=64 and (language=='ru' or i%320<=137)]
        if not visible or any(pixels[i]!=reference[i] for i in visible):
            raise RuntimeError('Visible original door texels differ: '+language)
        report['profiles'][language]={'nonzero_pixels':len(drawn),
            'matching_reference_at_nonzero_pixels':sum(pixels[i]==reference[i] for i in drawn),
            'verified_visible_pixels':len(visible),
            'render_sha256':hashlib.sha256(pixels).hexdigest(),
            'source_sha256':hashlib.sha256(original).hexdigest()}
        if source.read_bytes()!=original:raise RuntimeError('Original changed')
    (OUT/'render-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
