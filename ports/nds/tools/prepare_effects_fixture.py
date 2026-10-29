"""N13 immutable accumulated input packet and source placement inventory."""
from __future__ import annotations
import hashlib,json,struct
from pathlib import Path
from audit_geometry import NativeCellOracle
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n13';EVIDENCE=ROOT/'research/evidence/nds/n13'
def prepare():
    old=(ROOT/'build/nds/n12/scene.dat').read_bytes();n=struct.unpack_from('<I',old,12)[0]
    previous=json.loads((ROOT/'research/evidence/nds/n12/host-ru.json').read_text('utf8'))
    assert hashlib.sha256(old).hexdigest()==previous['fixture']['sha256'] and n==26
    records=[old[64+i*64:128+i*64] for i in range(n)]
    for level in (1,2,3):
        row=list(struct.unpack('<16I',records[(level-1)*2]));row[0]=len(records)+1;row[9]=60;row[11]=15;row[10]=0
        records.append(struct.pack('<16I',*row))
    for h in (0,90,180,270):
        records.append(struct.pack('<16I',len(records)+1,2,2160,(-300)&0xffffffff,2160,h,0,0,0,36,0,14,4,4,0,0))
    row=list(struct.unpack('<16I',records[25]));row[0]=len(records)+1;row[11]=17;records.append(struct.pack('<16I',*row))
    for level in (1,2,3):
        row=list(struct.unpack('<16I',records[(level-1)*2]));row[0]=len(records)+1;row[9]=68;row[11]=18;row[10]=0
        records.append(struct.pack('<16I',*row))
    body=b''.join(records)+old[64+n*64:]
    data=b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True);(OUT/'scene.dat').write_bytes(data)
    inventory=[]
    for level in (1,2,3):
        native=NativeCellOracle(level);objects={r['glyph']:r for r in native.base['objects']}
        maps=[bytes.fromhex(v) for v in native.world['maps']]
        scrolling=[i for i,v in enumerate(maps[2]) if objects[v]['class_id']==12 and objects[v]['sprite_flags']&0x3c00]
        inventory.append(dict(level=level,source='N05 canonical source model, verified native N07 per-cell branches',
            scrolling_cells=len(scrolling),by_glyph={str(g):maps[2].count(g) for g in (60,62,123,125)},
            definitions=[dict(glyph=o['glyph'],flags=o['sprite_flags'],image=native.source[o['sprite'][0]:sum(o['sprite'])].hex())
                for o in native.base['objects'] if o['class_id']==12]))
    report=dict(cases=len(records),presentations=sum(struct.unpack('<16I',r)[9] for r in records),
        packet_sha256=hashlib.sha256(data).hexdigest(),levels=inventory,
        controlled_kinds={'12':'all154 frames, one per presentation','13':'masked depth row0','14':'explicit water phase walk',
                          '15':'N11 native door phase view0..29..0, extent30','17':'after-LIGHT mask near=-2000','18':'VBlank source palette fade68 steps'})
    (EVIDENCE/'fixture.json').write_text(json.dumps(report,indent=2)+'\n','utf8');return report
if __name__=='__main__':print(json.dumps(prepare(),indent=2))
