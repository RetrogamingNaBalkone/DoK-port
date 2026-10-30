"""C13 freshly prepared field54 metadata; host counts and equivalence."""
from __future__ import annotations
import hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/field54-prepared'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def pin(p):
    p=Path(p);return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
OUT.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env['ZIG_LOCAL_CACHE_DIR']=str(OUT/'zig-local-cache');env['ZIG_GLOBAL_CACHE_DIR']=str(OUT/'zig-global-cache')
flags=[str(Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-DN21P_PROFILE=0','-DN21P_GEOMETRY_PROFILE=0','-Iports/nds/include','-Iports/nds/render','-Ibuild/nds/n07/generated']
commands=[]
def build(args):
    command=flags+list(map(str,args));subprocess.run(command,cwd=ROOT,env=env,check=True);commands.append(command)
def counts(after=False):
    for name,src in [('geometry','render/geometry.c'),('scene','render/scene.c'),('discovery','ui/map_discovery.c')]:
        build(['-Ddok_level_field_at=counted_field_at','-c','ports/nds/'+src,'-o',OUT/(name+'.o')])
    build(['-Ddok_geometry_build_cell=baseline_dok_geometry_build_cell','-Ddok_geometry_build_cell_inplace=baseline_dok_geometry_build_cell_inplace','-Ddok_geometry_build_cell_prepared=baseline_dok_geometry_build_cell_prepared','-c','ports/nds/render/geometry.c','-o',OUT/'baseline.o'])
    build(['ports/nds/tests/field54_call_counts.c','ports/nds/render/sprites.c','ports/nds/core/world.c','ports/nds/core/sha256.c']+[OUT/(n+'.o') for n in ['geometry','scene','discovery','baseline']]+['-o',OUT/'counts.exe'])
    rows=[json.loads(s) for s in subprocess.check_output([str(OUT/'counts.exe'),'build/nds/n07/geometry-level2.cp866','build/nds/n07/geometry-bindings2.bin'],cwd=ROOT,text=True).splitlines()]
    result=dict(passed=True,rows=rows,world_lookup=pin(ROOT/'ports/nds/core/world.c'),counter=pin(ROOT/'ports/nds/tests/field54_call_counts.c'),object_slots=256)
    (EVIDENCE/('field54-call-counts-after.json' if after else 'field54-call-counts.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result))
def archive():
    paths=['ports/nds/render/geometry.c','ports/nds/render/geometry_private.h','ports/nds/render/scene.c','ports/nds/ui/map_discovery.c','ports/nds/include/dok/map_discovery.h','ports/nds/include/dok/render_scene.h','ports/nds/render/door_render.c','ports/nds/render/geometry_cache.c','ports/nds/core/world.c']
    paths += [str(p.relative_to(ROOT)).replace('\\','/') for p in EVIDENCE.glob('*.json') if p.name.startswith(('geometry-inplace-','discovery-','field54-call-'))]
    rows=[]
    for name in paths:
        src=ROOT/name;dst=EVIDENCE/'pre-field54'/name;dst.parent.mkdir(parents=True,exist_ok=True)
        if dst.exists():assert dst.read_bytes()==src.read_bytes()
        else:dst.write_bytes(src.read_bytes())
        rows.append(dict(**pin(src),archived=dst.relative_to(ROOT).as_posix()))
    (EVIDENCE/'pre-field54/pins.json').write_text(json.dumps(dict(source_pins=rows),indent=2)+'\n',encoding='utf8')
def equivalence():
    archive=EVIDENCE/'pre-field54/pins.json';rows=json.loads(archive.read_text('utf8'))['source_pins']
    old=lambda name:ROOT/next(r['archived'] for r in rows if r['path']==name)
    for r in rows:assert pin(ROOT/r['archived'])['sha256']==r['sha256']
    sources=['ports/nds/render/geometry.c','ports/nds/render/geometry_private.h','ports/nds/render/scene.c','ports/nds/ui/map_discovery.c','ports/nds/include/dok/map_discovery.h','ports/nds/include/dok/geometry_prepared.h','ports/nds/render/door_render.c','ports/nds/core/world.c','ports/nds/core/sha256.c','ports/nds/include/dok/geometry.h','ports/nds/include/dok/world.h','ports/nds/include/dok/render_scene.h','ports/nds/render/geometry_cache.c','ports/nds/render/sprites.c','ports/nds/tests/field54_prepared_edges.c','ports/nds/tests/geometry_clear_equivalence.c','ports/nds/tests/geometry_inplace_equivalence.c','ports/nds/tests/scene_prepare_equivalence.c','ports/nds/tests/door_lazy_equivalence.c','ports/nds/tests/map_discovery_equivalence.c','ports/nds/tests/geometry_inplace_cache.c','ports/nds/tools/map/check_field54_prepared.py']
    before=[pin(ROOT/p) for p in sources]
    core=['ports/nds/core/world.c','ports/nds/core/sha256.c']
    geometry=['-Ddok_geometry_build_cell=baseline_dok_geometry_build_cell','-Ddok_geometry_build_cell_inplace=baseline_dok_geometry_build_cell_inplace']
    build(geometry+['-c',old('ports/nds/render/geometry.c'),'-o',OUT/'old-geometry.o'])
    exports=('dok_render_trig','dok_render_scene_init','dok_render_merge','dok_render_bounds','dok_render_scene_prepare','dok_render_scene_release')
    build(['-D'+n+'=baseline_'+n for n in exports]+geometry+['-c',old('ports/nds/render/scene.c'),'-o',OUT/'old-scene.o'])
    build(['-Ddok_door_geometry_build=baseline_dok_door_geometry_build','-Ddok_door_scene_prepare=baseline_dok_door_scene_prepare','-Ddok_render_scene_prepare=baseline_dok_render_scene_prepare','-Ddok_render_scene_release=baseline_dok_render_scene_release']+geometry+['-c',old('ports/nds/render/door_render.c'),'-o',OUT/'old-door.o'])
    original=(ROOT/'ports/nds/tests/geometry_inplace_equivalence.c').read_text('utf8')
    original=original.replace('b=dok_geometry_build_cell_inplace(&world,bindings,x,z,&inplace);','dok_geometry_field54 prepared[DOK_LEVEL_MAX_OBJECTS];\n    dok_geometry_prepare_fields(world.definition,prepared);\n    b=dok_geometry_build_cell_prepared(&world,bindings,x,z,&inplace,prepared);')
    original=original.replace('#include "geometry_clear_equivalence.c"','#include "'+(ROOT/'ports/nds/tests/geometry_clear_equivalence.c').as_posix()+'"').replace('#include "../render/geometry_private.h"','#include "geometry_private.h"')
    (OUT/'prepared-poison.c').write_text(original,encoding='utf8')
    build([OUT/'prepared-poison.c','ports/nds/render/geometry.c']+core+[OUT/'old-geometry.o','-o',OUT/'poison.exe'])
    build(['ports/nds/tests/field54_prepared_edges.c','ports/nds/render/geometry.c']+core+[OUT/'old-geometry.o','-o',OUT/'edges.exe'])
    build(['ports/nds/tests/scene_prepare_equivalence.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+[OUT/'old-geometry.o',OUT/'old-scene.o','-o',OUT/'scene.exe'])
    build(['ports/nds/tests/door_lazy_equivalence.c','ports/nds/render/door_render.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+[OUT/'old-geometry.o',OUT/'old-scene.o',OUT/'old-door.o','-o',OUT/'door.exe'])
    exports=('dok_map_discovery_bounds','dok_map_record_geometry','dok_map_record_sprite','dok_map_discovery_records','dok_map_discovery_update')
    objs=[]
    for prefix in ['baseline_','candidate4_','candidate5_']:
        obj=OUT/(prefix+'map.o');build(['-D'+n+'='+prefix+n for n in exports]+['-c',old('ports/nds/ui/map_discovery.c'),'-o',obj]);objs.append(obj)
    discovery_test=(ROOT/'ports/nds/tests/map_discovery_equivalence.c').read_text('utf8').replace('bytes[35268]','bytes[46532]').replace(':35268',':46532')
    (OUT/'prepared-discovery.c').write_text(discovery_test,encoding='utf8')
    build([OUT/'prepared-discovery.c','ports/nds/ui/map_discovery.c','ports/nds/render/sprites.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+objs+['-o',OUT/'discovery.exe'])
    mutated=discovery_test.replace('static dok_level_definition definition,candidate_definition;','static dok_level_definition definition,candidate_definition,normal_definition;')
    mutated=mutated.replace('    for(n=0;n<512u;++n) {','    normal_definition=definition;\n    for(n=0;n<512u;++n) {')
    mutation="""        definition=normal_definition;
        for(i=0;i<definition.object_count;++i) {
            dok_level_object *o=&definition.objects[i];uint32_t mode=n%12u;
            if(mode==0u)o->glyph=definition.objects[0].glyph;
            if(mode==1u)definition.glyph_index[o->glyph]=(uint16_t)(1u+(i+1u)%definition.object_count);
            if(mode>=2u){o->field_count=2;o->fields[0].native_offset=0x54;
                o->fields[0].kind=DOK_LEVEL_WORD;o->fields[0].value=(int32_t)((i*997u+mode*32767u)&65535u);
                o->fields[1]=o->fields[0];o->fields[1].value=1234;
                if(mode==3u)o->fields[0].kind=DOK_LEVEL_LONG;
                if(mode==4u)o->fields[0].native_offset=0x56;
                if(mode==5u)o->fields[0].native_offset=o->fields[1].native_offset=0x56;
                if(mode==6u)o->field_count=DOK_LEVEL_MAX_FIELDS+1u;
                if(mode==7u)o->field_count=0;
                if(mode==8u)o->fields[0].value=INT32_MIN;
                if(mode==9u)o->fields[0].value=INT32_MAX;
                if(mode==10u)o->fields[0].value=32768;
                if(mode==11u)o->fields[0].value=-1;}
        }
"""
    mutated=mutated.replace('        clock_t start;', '        clock_t start;\n'+mutation)
    (OUT/'prepared-discovery-mutated.c').write_text(mutated,encoding='utf8')
    build([OUT/'prepared-discovery-mutated.c','ports/nds/ui/map_discovery.c','ports/nds/render/sprites.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+objs+['-o',OUT/'discovery-mutated.exe'])
    build(['ports/nds/tests/map_discovery_dump.c','ports/nds/ui/map_discovery.c','ports/nds/render/sprites.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+['-o',OUT/'map_discovery_dump.exe'])
    build(['-Ddok_geometry_build_cell=dok_geometry_build_cell_uncached','-c','ports/nds/render/geometry.c','-o',OUT/'uncached.o'])
    build(['-DGAMEPLAY_GEOMETRY_CACHE=1','ports/nds/tests/geometry_inplace_cache.c','ports/nds/render/geometry_cache.c']+core+[OUT/'uncached.o',OUT/'old-geometry.o','-o',OUT/'cache.exe'])
    c8=json.loads((EVIDENCE/'scene-visibility-equivalence.json').read_text('utf8'));results=[]
    for row in c8['scene_rows']:
        level=row['level'];fixtures=[f'build/nds/n07/geometry-level{level}.cp866',f'build/nds/n07/geometry-bindings{level}.bin'];poses=ROOT/row['inputs']['path'];assert pin(poses)==row['inputs']
        result={'level':level}
        for name in ['poison','edges','scene','door','discovery','discovery-mutated','cache']:
            output=subprocess.check_output([str(OUT/(name+'.exe'))]+fixtures,input=poses.read_text('utf8') if name in ['scene','door'] else None,cwd=ROOT,text=True)
            result[name]=json.loads(output.splitlines()[-1]);assert result[name]['passed']
        results.append(result);print('field54 level',level,json.dumps(result),flush=True)
    assert before==[pin(ROOT/r['path']) for r in before]
    snapshots=[]
    for name in sources:
        dst=EVIDENCE/'c13-field54'/name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes((ROOT/name).read_bytes());snapshots.append(pin(dst))
    result=dict(passed=True,source_pins=before,archive=pin(archive),snapshots=snapshots,rows=results,commands=commands,generated_inputs=[pin(OUT/n) for n in ['prepared-poison.c','prepared-discovery.c','prepared-discovery-mutated.c']],fixtures=[pin(ROOT/f'build/nds/n07/geometry-{kind}{level}.{ext}') for level in [1,2,3] for kind,ext in [('level','cp866'),('bindings','bin')]],invalid_global_no_field_read_cases=30,workspace_common_bytes_compared=46532,workspace_bytes=47556,scene_prepared_bytes=1024,scope='Fresh metadata per call, actual object index, no retained world state; scene poses replay on parsed initial worlds, not mutable live-state replay.')
    (EVIDENCE/'field54-prepared-equivalence.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
if __name__=='__main__':
    import sys
    if '--counts-before' in sys.argv:counts();archive()
    elif '--counts-after' in sys.argv:counts(True)
    else:equivalence()
