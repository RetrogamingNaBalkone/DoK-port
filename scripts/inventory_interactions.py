"""Record actually used G09 types in six original LEVEL runtime reconstructions."""
import hashlib,json
from pathlib import Path
report={'task':'G09','status':'passed','address_system':'runtime-oriented map cells; native scalar offsets are evidence tags','levels':[]}
for language in ('ru','en'):
 for level in range(1,4):
  path=Path(f'build/runtime/{language}/LEVEL{level}-runtime.json');model=json.loads(path.read_text());maps=[bytes.fromhex(p) for p in model['maps'][:3]]
  objects=[];by_glyph={o['glyph']:o for o in model['objects']}
  for o in model['objects']:
   if o['class'] not in (6,7,12,13):continue
   fields={str(f['offset']):f['number'] for f in o['fields'] if 'number' in f}
   counts=[m.count(o['glyph']) for m in maps]
   record=dict(glyph=o['glyph'],class_id=o['class'],flags=o['flags'],sprite_flags=o['sprite_flags'],fields=fields,map_uses=counts)
   if o['class']==6:assert not fields
   if o['class']==7:assert o['sprite_flags'] in (0x48,0x108)
   if o['class']==12 and counts[2]:assert fields['102'] in (100,102,1000)
   if o['class']==13:
    target=by_glyph[fields['102']];assert target['class']==7 and fields['103']==0x800
    record['sensor_cells']=[i for i,g in enumerate(maps[2]) if g==o['glyph']]
    assert fields['102']==(ord('h') if level==2 else ord('k'))
   objects.append(record)
  report['levels'].append(dict(language=language,level=level,source=path.as_posix(),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),objects=objects))
Path('analysis/g09-original-level-inventory.json').write_text(json.dumps(report,indent=2)+'\n')
print('G09 six original LEVEL inventories: class6 no fields, door crop modes, sensor targets/events, effect categories verified')
