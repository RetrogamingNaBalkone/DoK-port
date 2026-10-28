"""Check derived strips/runs against G03 images and exact bytes linked in ROM."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
from gba_rom import elf_info

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=Path,default=Path('build/gba/g06a-fast'))
p.add_argument('--assets',type=Path,default=Path('build/gba/assets/assets.bin'))
p.add_argument('--report',type=Path,default=Path('analysis/gba/g06a-column-verification.json'))
a=p.parse_args()
data=a.assets.read_bytes();bank=(a.build_dir/'render-columns.bin').read_bytes()
meta=json.loads((a.build_dir/'render-columns.json').read_text())
digest=lambda b:hashlib.sha256(b).hexdigest()
assert digest(data)==meta['package_sha256']
assert digest(bank)==meta['bank_sha256'] and len(bank)==meta['bytes']
_,symbols,_=elf_info(a.build_dir/'dok-diagnostic.elf')
rom=(a.build_dir/'dok-diagnostic.gba').read_bytes()
for name,payload in (('dok_render_bank',bank),('dok_gba_assets',data)):
    first=symbols[name]-0x08000000;last=symbols[name+'_end']-0x08000000
    assert rom[first:last]==payload,('linked payload',name)
assert len(rom)<=32*1024*1024
u16=lambda b,at:struct.unpack_from('<H',b,at)[0]
u32=lambda b,at:struct.unpack_from('<I',b,at)[0]
ro,vo=u32(data,24),u32(data,28)
seen=set();cases=bindings=0;records=set()
for view in range(6):
    v=vo+view*80;defs,bind=u32(data,v+36),u32(data,v+72)
    links={(u16(data,bind+i*8),u16(data,bind+i*8+2)):u16(data,bind+i*8+4)
           for i in range(u16(data,v+76))}
    def resource(rid):
        r=ro+(rid-1)*32+16+(view%2)*8;off,size=u32(data,r),u32(data,r+4)
        return data[off:off+size]
    light=resource(links[0xffff,0xfff4])[:256]
    assert light==bytes(range(256)), 'G10 source-index columns require identity LIGHT row0'
    for obj in range(u16(data,v+16)):
        if u16(data,defs+obj*32) not in (2,4,7):continue
        rid=links[obj,0xfff0];im=resource(rid);w,h=u16(im,0),u16(im,2)
        entry=(view*(meta['resource_count']+1)+rid)*8
        columns,table=u32(bank,entry),u32(bank,entry+4);bindings+=1
        masked=bool((u16(data,defs+obj*32+4)|u16(im,4))&8)
        key=(columns,table,digest(im),light,masked)
        if key in seen:continue
        seen.add(key)
        expected=bytes(light[im[18+y*w+x]] for x in range(w) for y in range(h))
        assert bank[columns:columns+w*h]==expected
        assert bank[columns-256:columns-256+w]==bytes(int(0 not in expected[x*h:(x+1)*h]) for x in range(w))
        assert bank[columns-256+w:columns]==bytes(256-w)
        assert columns%4==table%4==0 and table
        for start in range(h):
            spans=u32(bank,table+start*4)
            if not spans:continue
            assert spans%4==0
            assert u32(bank,spans)==meta['scale_max_span']
            for span in range(1,meta['scale_max_span']+1):
                cols=u32(bank,spans+span*4);assert cols%4==0
                step=((h-1-start)*65536)//span
                for x in range(w):
                    record=u32(bank,cols+x*4);assert record%4==0
                    n,offset,reserved=struct.unpack_from('<BBH',bank,record)
                    assert reserved in (0,1) and offset%4==0 and offset>=4+n*2
                    assert not (masked and reserved),('masked binding needs opacity metadata',view,obj)
                    pixels=bank[record+offset:record+offset+span+1]
                    want=bytes(expected[x*h+start+(y*step)//65536] for y in range(span+1))
                    assert pixels==want,('samples',view,obj,start,span,x)
                    if record not in records:
                        # Independently group the actual mapped pixels by
                        # opacity; masking is applied after LIGHT mapping.
                        runs=[]
                        for opaque,group in itertools.groupby(enumerate(pixels),lambda item:bool(item[1])):
                            rows=[i for i,_ in group]
                            if opaque:runs.extend((rows[0],rows[-1]))
                        if reserved:
                            assert n==0 and offset==4
                        else:
                            assert bank[record+4:record+4+n*2]==bytes(runs)
                        records.add(record)
                    cases+=1
result=dict(status='passed',rom_sha256=digest(rom),bank_sha256=digest(bank),
            bank_bytes=len(bank),image_bindings=bindings,checked_image_views=len(seen),
            checked_strips=cases,unique_records=len(records),linked_payloads_equal=True,
            exact_q16_samples=True,mapped_opacity_runs=True,scale_max_span=meta['scale_max_span'])
a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
