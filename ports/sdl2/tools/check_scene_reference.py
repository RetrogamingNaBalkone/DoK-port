"""Compare original indexed snapshots; native-history is diagnostic, never a golden rebaseline."""
import sys,struct,hashlib,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[3];out=root/'research/evidence/source-port/sp07-render';out.mkdir(parents=True,exist_ok=True)
r=[]
for lang,folder,res,snap in [('ru','RUS','PKR','ru-03'),('en','ENG','PKE','en-02')]:
 ref=(root/'research/evidence/reference'/snap/'level1-start.idx').read_bytes()[:51200]
 mem=(root/'research/evidence/reference'/snap/'level1-memory.bin').read_bytes()
 for mode in ['fixed','native']:
  dst=out/f'{lang}-{mode}.idx'; geo=out/f'{lang}-geometry.txt'
  cmd=[str(root/'build/sdl2/windows/dok_render_probe.exe'),str(root/f'originals/pkr/{folder}/{res}.RES'),str(dst),'LEVEL1','--geometry',str(geo)]
  if mode=='native':cmd+=['--native-light-history']
  subprocess.run(cmd,check=True,capture_output=True)
  frame=dst.read_bytes(); diffs=[dict(x=i%320,y=i//320,actual=a,expected=b) for i,(a,b) in enumerate(zip(frame,ref)) if a!=b]
  hits=[]
  for n,line in enumerate(geo.read_text().splitlines()):
   a,b=line.split('|');parts=a.split();meta=parts[:3]
   for side,vs in [('primary',parts[3:]),('secondary',b.split())]:
    if not vs:continue
    payload=b''.join(struct.pack('<iii',*map(int,v.split(','))) for v in vs)
    offset=mem.find(payload)
    hits.append(dict(polygon=n,side=side,axis=int(meta[0]),material=int(meta[1]),sort=int(meta[2]),linear_ram_offset=hex(offset) if offset>=0 else None,vertices=vs))
  item=dict(language=lang,mode=mode,matching=51200-len(diffs),sha256=hashlib.sha256(frame).hexdigest(),differences=diffs,geometry_matches=sum(x['linear_ram_offset'] is not None for x in hits),geometry_total=len(hits),geometry=hits)
  r.append(item);print(lang,mode,item['matching'],item['geometry_matches'],len(hits),diffs[:8])
assert r[1]['matching']==51200, 'PKR native-history reference regression'
(out/'report.json').write_text(json.dumps(r,indent=2))
