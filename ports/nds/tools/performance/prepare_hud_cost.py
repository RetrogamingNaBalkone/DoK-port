"""Derive a short existing N19 HUD case for paired N23 redraw timing."""
import hashlib,json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact
source=ROOT/'build/nds/n23/scene.dat';data=source.read_bytes();count=struct.unpack_from('<I',data,12)[0]
assert hashlib.sha256(data[64:]).digest()==data[32:64]
selected=None
for index in range(count):
    record=data[64+index*64:128+index*64]
    row=struct.unpack('<16I',record)
    if row[0]==60:
        assert row[1]==1 and row[11]==28
        selected=bytearray(record);struct.pack_into('<I',selected,36,64)
assert selected is not None
body=bytes(selected)+data[64+count*64:]
header=bytearray(data[:64]);struct.pack_into('<I',header,12,1);struct.pack_into('<I',header,24,64+len(body));header[32:64]=hashlib.sha256(body).digest()
destination=ROOT/'build/nds/n23/scene-hud-cost.dat';destination.write_bytes(header+body)
report=dict(task='N23',passed=True,source=artifact(source),derived=artifact(destination),script=artifact(Path(__file__)),
    derivation='Keep existing case60/sourcekind28 and three original HUD banks; truncate only frames to64; update count/length/bodySHA.',
    scope='Controlled diagnostic timing and native pixel checks; no full N19 coverage or gameplay FPS claim.')
(ROOT/'research/evidence/nds/n23/hud-cost-fixture.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
print(json.dumps(report))
