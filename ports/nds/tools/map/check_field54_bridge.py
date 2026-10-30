"""Scoped C13 source identity bridge; accepted historical reports stay immutable."""
import hashlib,json
from pathlib import Path
from check_field54_prepared import ROOT,EVIDENCE,pin
from check_scene_merge import function
manifest=json.loads((EVIDENCE/'pre-field54/pins.json').read_text('utf8'))['source_pins']
def old(name):return (ROOT/next(r['archived'] for r in manifest if r['path']==name)).read_text('utf8')
def digest(s):return hashlib.sha256(s.encode()).hexdigest()
g=(ROOT/'ports/nds/render/geometry.c').read_text('utf8');og=old('ports/nds/render/geometry.c')
names=('signed_word','object_at','usable','vertex','append','vertical','horizontal','door_like')
identities={}
for name in names:
    assert function(g,name)==function(og,name);identities['geometry.'+name]=digest(function(g,name))
body=function(g,'build_cell');baseline=function(og,'build_cell')
body=body.replace(',const dok_geometry_field54 *prepared)',')')
for obj in ['fcmap','nf[sideindex]']:
    body=body.replace('surface_definition('+obj+',level,prepared)','surface_definition('+obj+')')
    body=body.replace('field54('+obj+',level,prepared)','field54('+obj+')')
assert body==baseline,'Geometry kernel changed beyond metadata reads'
for name in ['dok_geometry_build_cell','dok_geometry_build_cell_inplace']:
    assert function(g,name).replace(',1u,NULL)',',1u)').replace(',0u,NULL)',',0u)')==function(og,name)
s=(ROOT/'ports/nds/render/scene.c').read_text('utf8');os=old('ports/nds/render/scene.c')
reverse=s.replace('    dok_geometry_field54 prepared[DOK_LEVEL_MAX_OBJECTS];\n','',1)
reverse=reverse.replace('#if !GAMEPLAY_GEOMETRY_CACHE\n    dok_geometry_prepare_fields(world->definition,prepared);\n#endif\n','',1)
reverse=reverse.replace('dok_geometry_build_cell_prepared(world,bindings,(uint32_t)x,(uint32_t)z,&cell,prepared)','dok_geometry_build_cell_inplace(world,bindings,(uint32_t)x,(uint32_t)z,&cell)')
assert reverse==os,'Scene changed beyond prepared caller'
for name in ['dok_render_trig','dok_render_bounds','visible','dok_render_merge']:
    assert function(s,name)==function(os,name);identities['scene.'+name]=digest(function(s,name))
m=(ROOT/'ports/nds/ui/map_discovery.c').read_text('utf8');om=old('ports/nds/ui/map_discovery.c')
reverse=m
for name in ['map_field54','map_surface']:reverse=reverse.replace(function(m,name),function(om,name))
reverse=reverse.replace('uint32_t *count,\n    const dok_geometry_field54 *prepared)','uint32_t *count)')
for obj in ['f','nf[i]']:
    reverse=reverse.replace('map_surface('+obj+',d,prepared)','map_surface('+obj+')')
    reverse=reverse.replace('map_field54('+obj+',d,prepared)','map_field54('+obj+')')
reverse=reverse.replace('    dok_geometry_prepare_fields(world->definition,workspace->field54);\n','',1)
reverse=reverse.replace('shapes,&count,workspace->field54)','shapes,&count)')
assert reverse==om,'Discovery changed beyond metadata reads'
for name in ['rotate','sar','dok_map_discovery_bounds','record_geometry','dok_map_record_sprite','dok_map_discovery_records']:
    assert function(m,name)==function(om,name);identities['discovery.'+name]=digest(function(m,name))
for r in manifest:assert pin(ROOT/r['archived'])['sha256']==r['sha256']
host=json.loads((EVIDENCE/'field54-prepared-equivalence.json').read_text('utf8'))
for r in host['source_pins']:assert pin(ROOT/r['path'])==r
native=json.loads((EVIDENCE/'field54-prepared-native.json').read_text('utf8'))
assert native['passed'] and not native['complete_source_gate'] and native['combined_producer']['cases']==54
for r in native['source_pins']:assert all(pin(ROOT/r['path'])[k]==v for k,v in r.items())
assert all(pin(ROOT/native['executable']['path'])[k]==v for k,v in native['executable'].items())
stack=[]
for variant,expected in [('baseline-ru',2608),('candidate11-ru',1464),('candidate13-ru',2488)]:
    p=ROOT/f'build/nds/n21p/{variant}/obj/scene.su';line=next(s for s in p.read_text().splitlines() if 'dok_render_scene_prepare' in s)
    assert int(line.split('\t')[1])==expected;stack.append(dict(**pin(p),prepare_frame=expected))
result=dict(passed=True,scope='C13 per-call field54 metadata only; no historical report integrity bypass',
    public_kernel_validation_math_order_identical_after_metadata_call_substitution=True,
    scene_whole_source_identical_except_transient_table_and_private_caller=True,
    discovery_whole_source_identical_except_transient_table_reads=True,
    unchanged_helpers_sha256=identities,host=pin(EVIDENCE/'field54-prepared-equivalence.json'),
    native=pin(EVIDENCE/'field54-prepared-native.json'),
    native_scope='54 fresh full static producer original constructor/projection/sort/column cases; 6078 records. Not a rerun of 121776 constructor-only vectors.',
    historical_native=pin(EVIDENCE/'pre-field54/research/evidence/nds/n21p/discovery-native.json'),
    source_pins=host['source_pins'],stack=stack,workspace=47556,scratch_limit=131072,
    actual_slot_identity='object_at/map_object validates glyph_index 1..object_count before objects[index-1]; subtraction by definition.objects recovers that actual index, independent of object.glyph',
    word_semantics='First matching offset54, including nonWORD value, narrowed modulo65536 then explicit signed conversion; count>MAX or missing => value0/is_word0. Surface still checks live class and separately is_word.',
    preparation='Fresh after argument/bounds checks and global kernel validation guard; no retained state or global tables; cache1 retains old public path.')
(EVIDENCE/'field54-source-supersession.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
print(json.dumps(dict(passed=True,stack=stack,native_cases=54)))
