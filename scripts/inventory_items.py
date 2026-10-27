"""G12: preserve six original inventories, compare effect data explicitly."""
import hashlib,json
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(task='G12',status='passed',levels=[],differences=[])
for language in ('ru','en'):
 for n in (1,2,3):
  path=Path(f'research/evidence/runtime/{language}/LEVEL{n}-runtime.json');m=json.loads(path.read_text());maps=[bytes.fromhex(x) for x in m['maps'][:3]];records=[]
  for i,o in enumerate(m['objects']):
   if o['class'] not in (3,10):continue
   fields={f['offset']:f['number'] for f in o['fields'] if 'number' in f};counts=[p.count(o['glyph']) for p in maps]
   assert counts[0]==counts[2]==0 # native pickup scans TMap
   assert fields[84] in (0,1,3,10,200) and (o['class']!=10 or fields[129] in (1,2))
   records.append(dict(object_index=i,glyph=o['glyph'],class_id=o['class'],category=fields[84],value=fields[86],weapon_slot=fields.get(129),map_uses=counts,
    flags=o['flags'],sprite_flags=o['sprite_flags'],width=o['width_parameter'],message_cp866_hex=next(f['hex'] for f in o['fields'] if f['offset']==88),
    sprite=o['sprite'],animation=o['animation'],sound=o['sound'],cells=[i for i,g in enumerate(maps[1]) if g==o['glyph']]))
  zones=[]
  for o in m['objects']:
   if o['class']!=12 or not maps[2].count(o['glyph']):continue
   fields={f['offset']:f['number'] for f in o['fields'] if 'number' in f}
   assert fields[102] in (100,102,1000)
   zones.append(dict(glyph=o['glyph'],category=fields[102],value=fields[104],cells=maps[2].count(o['glyph'])))
  report['levels'].append(dict(language=language,level=n,source=path.as_posix(),source_sha256=sha(path),items=records,used_class12_zones=zones))
for n in (1,2,3):
 ru,en=[next(l['items'] for l in report['levels'] if l['language']==lang and l['level']==n) for lang in ('ru','en')]
 for a,b in zip(ru,en):
  for key in ('glyph','class_id','category','value','weapon_slot','map_uses','flags','cells'):assert a[key]==b[key],(n,key,a,b)
  for key in ('width','message_cp866_hex','sprite_flags','sprite','animation','sound'):
   if a[key]!=b[key]:report['differences'].append(dict(level=n,glyph=a['glyph'],field=key,ru=a[key],en=b[key]))
report['item_placements_per_language']=sum(sum(i['map_uses']) for l in report['levels'] if l['language']=='ru' for i in l['items'])
Path('analysis/g12-item-inventory.json').write_text(json.dumps(report,indent=2)+'\n')
print('G12 six original inventories:',report['item_placements_per_language'],'TMap placements per language; effect values equal, messages/scales kept separate')
