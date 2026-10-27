"""Audit final LIGHT bands and AI wake radii from the current ROM package."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True)
    a=parser.parse_args();path=ROOT/'build/gba/assets/assets.bin';data=path.read_bytes()
    u16=lambda n:struct.unpack_from('<H',data,n)[0]
    u32=lambda n:struct.unpack_from('<I',data,n)[0]
    views=[]
    for view in range(6):
        rec=u32(28)+80*view;defs,fields=u32(rec+36),u32(rec+40)
        links={struct.unpack_from('<HH',data,u32(rec+72)+i*8):u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}
        def resource(rid):
            at=u32(24)+(rid-1)*32+16+(view%2)*8
            return data[u32(at):u32(at)+u32(at+4)]
        palette=resource(links[65535,0xfff3]);light=resource(links[65535,0xfff4])
        used=set();images=0
        for (obj,slot),rid in links.items():
            if obj==65535 or slot not in (0xfff0,0x56,0x5a,0x5e):continue
            raw=resource(rid)
            if len(raw)<18:continue
            w,h=struct.unpack_from('<HH',raw)
            if len(raw)!=18+w*h:continue
            used.update(raw[18:]);images+=1
        rows=[]
        for row in range(16):
            colors=[tuple(palette[light[row*256+i]*3+c]>>1 for c in range(3)) for i in range(256)]
            rows.append(dict(row=row,start_depth=0 if row==0 else 1920+row*120,
                all_palette_nonblack=sum(c!=(0,0,0) for c in colors),
                static_image_colors_nonblack=sum(colors[i]!=(0,0,0) for i in used),
                maximum_gba5_component=max(max(c) for c in colors),
                maximum_static_image_gba5_component=max(max(colors[i]) for i in used)))
        radii=Counter()
        for obj in range(u16(rec+16)):
            d=defs+obj*32
            if u16(d)!=9:continue
            for f in range(u16(d+14),u16(d+14)+u16(d+16)):
                if u16(fields+f*8)==0xba:
                    radii[struct.unpack_from('<i',data,fields+f*8+4)[0]]+=1
        views.append(dict(level=view//2+1,language=view%2,rows=rows,static_image_bindings=images,
            static_image_source_colors=len(used),wake_radii=[dict(distance=d,definitions=n) for d,n in sorted(radii.items())]))
    result=dict(package_sha256=hashlib.sha256(data).hexdigest(),script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),views=views,
        palette_conversion='VGA6 components >>1, matching gba/renderer_probe.c to native GBA5.',
        scope='Asset/implementation audit only, no visibility threshold, human perception result, FPS improvement or AI rebalance claimed.',
        interpretation='Final LIGHT15 is not identically black, including bound static art. Perceptually negligible dark detail may still be removed in a separate draw-distance quality prototype.',
        ai='0xBA uses max(abs(dx),abs(dz))<distance to initially wake considered monsters. Existing owners are not stopped at that radius; combat wake bypasses this test. No gameplay fields changed.')
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    for v in views[::2]:print(v['level'],v['rows'][-1],v['wake_radii'])


if __name__=='__main__':main()
