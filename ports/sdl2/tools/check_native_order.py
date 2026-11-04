"""Compare the native sorted descriptor list with portable Shellsort at RU LEVEL2/135."""
from pathlib import Path
import json,struct,zlib
ROOT=Path(__file__).resolve().parents[3];p=ROOT/'research/evidence/source-port/sp07-render/ru-level2-vga-08';r=json.loads((p/'report.json').read_text());m=zlib.decompress((p/'heading-135-memory.z').read_bytes());base=int(r['data_base'],16);ldt=int(r['surface_address']['ldt_linear'],16)
def far(at):
 off,sel=struct.unpack_from('<HH',m,at);d=m[ldt+(sel&~7):ldt+(sel&~7)+8];return int.from_bytes(d[2:4],'little')+(d[4]<<16)+(d[7]<<24)+off
ps=[]
for i,line in enumerate((p/'geometry-135.txt').read_text().splitlines()):
 h=line.split('|')[0].split();raw=b''.join(struct.pack('<iii',*map(int,v.split(','))) for v in h[3:]);ps.append(dict(id=i,key=int(h[2]),at=m.find(raw)))
count=struct.unpack_from('<H',m,base+0xaa38)[0];arr=far(base+0xaa3a);native=[far(arr+4*i) for i in range(count)];port=list(ps);gap=len(port)//2
while gap:
 for i in range(gap,len(port)):
  j=i-gap
  while j>=0 and port[j]['key']<port[j+gap]['key']:port[j],port[j+gap]=port[j+gap],port[j];j-=gap
 gap//=2
rows=[dict(native_descriptor_linear=hex(at),portable_polygon=x['id'],sort_key=x['key'],vertex_offset=x['at']-at,visible=bool(m[at+6]&1)) for at,x in zip(native,port)]
assert count==len(port)==112 and all(x['vertex_offset'] in [15,19,63] for x in rows)
(p/'native-order.json').write_text(json.dumps(dict(native_count=count,portable_count=len(port),rows=rows),indent=2));print('Native and portable sorted descriptors:112/112 PASS')
