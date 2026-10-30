"""Independent RAW RES/N04 proof for language-specific POPAL artwork.

Only asset IDs are generated. No original image pixels are emitted to source.
Original world IDs/bindings need not change when presentation language changes.
"""
from __future__ import annotations
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from audit_assets import originals,decode_image,asset_id,sha,pot
from extract_frontend import EVIDENCE,OUT,pin,write

PACKAGE=ROOT/'build/nds/assets/dok-assets.ndspak'
PACKAGE_SHA='2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511'
EXE_LABEL={1:'ru',2:'en'}

def package_entries():
    data=PACKAGE.read_bytes()
    if sha(data)!=PACKAGE_SHA:raise ValueError('Unaccepted N04 package')
    count=struct.unpack_from('<I',data,16)[0]
    entries={}
    for i in range(count):
        raw=data[160+i*128:288+i*128]
        key,source,offset,size=struct.unpack_from('<4I',raw)
        rep,lang,kind,index,subindex,w,h,sw,sh=struct.unpack_from('<HBB6H',raw,16)
        payload=data[offset:offset+size]
        if len(payload)!=size or sha(payload)!=raw[40:72].hex():raise ValueError('N04 payload hash/bounds')
        entries[key]=dict(id=key,source_id=source,file_offset=offset,bytes=size,representation=rep,
            language=lang,source_kind=kind,source_index=index,subindex=subindex,width=w,height=h,
            storage_width=sw,storage_height=sh,sha256=sha(payload),payload=payload)
    return entries

def frames(entry):
    data=entry['data']
    count,unknown,pointer=struct.unpack_from('<HHI',data)
    if count!=6:raise ValueError('POPAL original frame count')
    cursor=8+10*count
    rows=[]
    for frame in range(count):
        interval,a,b,far=struct.unpack_from('<HHHI',data,8+10*frame)
        start=cursor
        w,h,flags,pixels,cursor=decode_image(data,cursor)
        rows.append(dict(frame=frame,interval=interval,a=a,b=b,far=far,width=w,height=h,flags=flags,
                         resource_offset=start,header=data[start:start+18],pixels=pixels))
    if cursor!=len(data):raise ValueError('POPAL trailing bytes')
    return rows,dict(count=count,unknown=unknown,pointer=pointer,table=data[:8+10*count])

def main():
    archives=originals()
    package_before=pin(PACKAGE)
    source_before=[pin(archives[lang]['path']) for lang in (1,2)]
    packages=package_entries()
    differences=[];art=[]
    for ru,en in zip(archives[1]['entries'],archives[2]['entries']):
        if (ru['index'],ru['name'],ru['kind'])!=(en['index'],en['name'],en['kind']):raise ValueError('RES catalog pair')
        equal=ru['data']==en['data']
        if not equal:differences.append(dict(index=ru['index'],name=ru['name'],kind=ru['kind']))
        if ru['kind'] in ('image','animation','font','palette','light'):
            art.append(dict(index=ru['index'],name=ru['name'],kind=ru['kind'],equal=equal,
                ru_sha256=sha(ru['data']),en_sha256=sha(en['data'])))
    if len(differences)!=12 or [r['name'] for r in art if not r['equal']]!=['POPAL']:
        raise ValueError('Unexpected new language-specific art')
    source=[archives[lang]['entries'][145] for lang in (1,2)]
    if any(e['name']!='POPAL' or e['kind']!='animation' for e in source):raise ValueError('POPAL resource binding')
    pair=[frames(e) for e in source]
    if pair[0][1]!=pair[1][1]:raise ValueError('POPAL original animation table differs')
    frame_rows=[];gpu_last=[]
    for frame in range(6):
        ru,en=pair[0][0][frame],pair[1][0][frame]
        if {k:v for k,v in ru.items() if k!='pixels'}!={k:v for k,v in en.items() if k!='pixels'}:
            raise ValueError('POPAL frame geometry/metadata differs')
        width,height=ru['width'],ru['height']
        if frame==5 and (width,height)!=(158,70):raise ValueError('POPAL last frame geometry')
        delta=[dict(x=i%width,y=i//width,ru=a,en=b,raw_pixel_offset=i)
               for i,(a,b) in enumerate(zip(ru['pixels'],en['pixels'])) if a!=b]
        expected=[] if frame<5 else [dict(x=x,y=69,ru=value,en=0,raw_pixel_offset=69*158+x)
                                      for x,value in ((155,147),(156,121),(157,41))]
        if delta!=expected:raise ValueError('POPAL expected pixel difference')
        proofs=[]
        for lang,row in ((1,ru),(2,en)):
            gid=asset_id(lang,145,5,frame)
            flat=asset_id(lang,145,4,frame)
            gpu=packages[gid];cpu=packages[flat]
            if (gpu['subindex'],gpu['representation'],gpu['source_index'])!=(frame,5,145):raise ValueError('GPU asset ID indexing')
            sw,sh=pot(width),pot(height)
            if (gpu['width'],gpu['height'],gpu['storage_width'],gpu['storage_height'],gpu['bytes'])!=(width,height,sw,sh,sw*sh):
                raise ValueError('N04 GPU storage geometry')
            expected_gpu=bytearray(sw*sh)
            for y in range(height):expected_gpu[y*sw:y*sw+width]=row['pixels'][y*width:(y+1)*width]
            if gpu['payload']!=expected_gpu or cpu['payload']!=row['pixels']:
                raise ValueError('RAW RES/N04 GPU or flat pixel mismatch')
            meta=packages[asset_id(lang,145,3)]
            target,interval,a,b,flags,w,h=struct.unpack_from('<I6H',meta['payload'],8+16*frame)
            if (target,interval,a,b,flags,w,h)!=(flat,row['interval'],row['a'],row['b'],row['flags'],width,height):
                raise ValueError('N04 animation mapping differs from RAW source')
            proofs.append({k:v for k,v in gpu.items() if k!='payload'})
            if frame==5:
                gpu_last.append(gid)
                write(OUT/f'visual-popal-{EXE_LABEL[lang]}.bin',gpu['payload'])
        frame_rows.append(dict(frame=frame,source_resource_offset=ru['resource_offset'],width=width,height=height,
            source_headers_equal=True,animation_metadata_equal=True,ru_pixels_sha256=sha(ru['pixels']),
            en_pixels_sha256=sha(en['pixels']),differences=delta,n04_gpu=proofs))
    source_after=[pin(archives[lang]['path']) for lang in (1,2)]
    package_after=pin(PACKAGE)
    if source_before!=source_after or package_before!=package_after:raise ValueError('Source data changed during proof')
    dest=OUT/'generated/frontend_visual.inc'
    include=('/* Generated POPAL presentation IDs only; original pixels remain in N04 package. */\n'
             f'#define DOK_FRONTEND_VISUAL_POPAL_RU 0x{gpu_last[0]:08x}u\n'
             f'#define DOK_FRONTEND_VISUAL_POPAL_EN 0x{gpu_last[1]:08x}u\n'
             'static const unsigned int dok_frontend_visual_frame_ids[2] = {'+
             ','.join(f'0x{key:08x}u' for key in gpu_last)+'};\n')
    write(dest,include.encode('ascii'))
    result=dict(task='N21',passed=True,script=pin(Path(__file__)),source_before=source_before,source_after=source_after,
        independent_resource_decoder=pin(ROOT/'ports/nds/tools/audit_assets.py'),
        package_before=package_before,package_after=package_after,all_resource_pairs=146,differing_resource_pairs=differences,
        game_art_pairs=len(art),identical_game_art_pairs=sum(r['equal'] for r in art),game_art=art,
        popal_resource=dict(index=145,source_offsets=[e['offset'] for e in source],source_bytes=[e['size'] for e in source],
            source_sha256=[sha(e['data']) for e in source],frame_count=6,animation_table_equal=True),
        frames=frame_rows,alternate_gpu_ids=gpu_last,generated=pin(dest),
        oracle_files=[pin(OUT/f'visual-popal-{label}.bin') for label in ('ru','en')],
        expected_gpu_difference_offsets=[69*256+x for x in (155,156,157)],
        boundaries=['Source art-only proof; no claim that frontend runtime selected the correct frame',
        'Only presentation frame selection may change; original world/bank IDs remain immutable',
        'N04 frame subindices are zero-based; last POPAL frame is5'])
    write(EVIDENCE/'visual-source.json',(json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
    print(f'N21 visual source PASS: {len(art)} art pairs, only POPAL frame5 differs in3 pixels; GPU IDs '+','.join(hex(k) for k in gpu_last))

if __name__=='__main__':main()
