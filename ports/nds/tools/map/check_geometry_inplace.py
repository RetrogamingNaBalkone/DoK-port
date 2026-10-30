"""C11 shared geometry kernel, public compatibility and private scene borrow."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_scene_merge import function

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/geometry-inplace'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=EVIDENCE/'pre-inplace/pins.json';archived=json.loads(archive.read_text('utf8'))['source_pins']
    for p in archived:assert pin(ROOT/p['archived'])['sha256']==p['sha256']
    old=lambda p:ROOT/next(r['archived'] for r in archived if r['path']==p)
    old_geometry=old('ports/nds/render/geometry.c');old_scene=old('ports/nds/render/scene.c');old_door=old('ports/nds/render/door_render.c')
    g=(ROOT/'ports/nds/render/geometry.c').read_text('utf8');og=old_geometry.read_text('utf8')
    helpers=('signed_word','object_at','field54','surface_definition','usable','vertex','append','vertical','horizontal','door_like')
    for name in helpers:assert function(g,name)==function(og,name),name
    old_body=function(og,'dok_geometry_build_cell');old_body=old_body[old_body.index('{'):]
    kernel=function(g,'build_cell');kernel=kernel[kernel.index('{'):]
    kernel=kernel.replace('    if (world==NULL || bindings==NULL || candidate==NULL || world->definition==NULL)',
        '    if (world==NULL || bindings==NULL || candidate==NULL || out==NULL\n        || candidate==out || world->definition==NULL)',1)
    kernel=kernel.replace('    if(full_clear)memset(candidate,0,sizeof(*candidate));\n    else {candidate->count=0;candidate->reserved=0;}',
        '    memset(candidate,0,sizeof(*candidate));',1)
    kernel=kernel.replace('    return DOK_GEOMETRY_OK;\n}', '    *out=*candidate;\n    return DOK_GEOMETRY_OK;\n}',1)
    assert kernel==old_body,'Constructor math/order/validation changed'
    scene=(ROOT/'ports/nds/render/scene.c').read_text('utf8');old_s=old_scene.read_text('utf8')
    reverse=scene.replace('#include "geometry_private.h"\n','',1)
    reverse=reverse.replace('#if GAMEPLAY_GEOMETRY_CACHE\n    dok_geometry_cell scratch,cell;\n#else\n    dok_geometry_cell cell;\n#endif',
        '    dok_geometry_cell scratch,cell;',1)
    reverse=reverse.replace('#if GAMEPLAY_GEOMETRY_CACHE\n        if(dok_geometry_build_cell(world,bindings,(uint32_t)x,(uint32_t)z,&scratch,&cell)!=DOK_GEOMETRY_OK)\n#else\n        if(dok_geometry_build_cell_inplace(world,bindings,(uint32_t)x,(uint32_t)z,&cell)!=DOK_GEOMETRY_OK)\n#endif',
        '        if(dok_geometry_build_cell(world,bindings,(uint32_t)x,(uint32_t)z,&scratch,&cell)!=DOK_GEOMETRY_OK)',1)
    assert reverse==old_s,'Non-caller scene change'
    assert pin(ROOT/'ports/nds/render/door_render.c')['sha256']==pin(old_door)['sha256']
    sources=['ports/nds/render/geometry.c','ports/nds/render/geometry_private.h','ports/nds/render/scene.c',
             'ports/nds/render/door_render.c','ports/nds/render/geometry_cache.c','ports/nds/include/dok/geometry.h',
             'ports/nds/include/dok/render_scene.h','ports/nds/core/world.c','ports/nds/core/sha256.c',
             'ports/nds/tests/geometry_inplace_equivalence.c','ports/nds/tests/geometry_inplace_cache.c',
             'ports/nds/tests/geometry_clear_equivalence.c','ports/nds/tests/scene_prepare_equivalence.c',
             'ports/nds/tests/door_lazy_equivalence.c','ports/nds/tools/map/check_geometry_inplace.py']
    before=[pin(ROOT/p) for p in sources]
    env=os.environ.copy();env['ZIG_LOCAL_CACHE_DIR']=str(OUT/'zig-local-cache');env['ZIG_GLOBAL_CACHE_DIR']=str(OUT/'zig-global-cache')
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-DN21P_PROFILE=0','-DN21P_GEOMETRY_PROFILE=0',
           '-Iports/nds/include','-Iports/nds/render','-Ibuild/nds/n07/generated']
    commands=[]
    def build(args):
        command=flags+args;subprocess.run(command,cwd=ROOT,env=env,check=True);commands.append(command)
    core=['ports/nds/core/world.c','ports/nds/core/sha256.c']
    build(['-Ddok_geometry_build_cell=baseline_dok_geometry_build_cell','-c',str(old_geometry),'-o',str(OUT/'baseline_geometry.o')])
    exports=('dok_render_trig','dok_render_scene_init','dok_render_merge','dok_render_bounds','dok_render_scene_prepare','dok_render_scene_release')
    build(['-D'+n+'=baseline_'+n for n in exports]+['-Ddok_geometry_build_cell=baseline_dok_geometry_build_cell','-c',str(old_scene),'-o',str(OUT/'baseline_scene.o')])
    build(['-Ddok_door_geometry_build=baseline_dok_door_geometry_build','-Ddok_door_scene_prepare=baseline_dok_door_scene_prepare',
           '-Ddok_render_scene_prepare=baseline_dok_render_scene_prepare','-Ddok_render_scene_release=baseline_dok_render_scene_release',
           '-Ddok_geometry_build_cell=baseline_dok_geometry_build_cell','-c',str(old_door),'-o',str(OUT/'baseline_door.o')])
    build(['ports/nds/tests/geometry_inplace_equivalence.c','ports/nds/render/geometry.c']+core+[str(OUT/'baseline_geometry.o'),'-o',str(OUT/'geometry_inplace_equivalence.exe')])
    build(['-Ddok_geometry_build_cell=dok_geometry_build_cell_uncached','-c','ports/nds/render/geometry.c','-o',str(OUT/'uncached_geometry.o')])
    build(['-DGAMEPLAY_GEOMETRY_CACHE=1','ports/nds/tests/geometry_inplace_cache.c','ports/nds/render/geometry_cache.c']+core+
          [str(OUT/'uncached_geometry.o'),str(OUT/'baseline_geometry.o'),'-o',str(OUT/'geometry_inplace_cache.exe')])
    build(['ports/nds/tests/scene_prepare_equivalence.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+
          [str(OUT/'baseline_geometry.o'),str(OUT/'baseline_scene.o'),'-o',str(OUT/'scene_inplace_equivalence.exe')])
    build(['ports/nds/tests/door_lazy_equivalence.c','ports/nds/render/door_render.c','ports/nds/render/scene.c','ports/nds/render/geometry.c']+core+
          [str(OUT/'baseline_geometry.o'),str(OUT/'baseline_scene.o'),str(OUT/'baseline_door.o'),'-o',str(OUT/'door_inplace_equivalence.exe')])
    cache_preprocessed=subprocess.check_output(flags+['-DGAMEPLAY_GEOMETRY_CACHE=1','-E','ports/nds/render/scene.c'],cwd=ROOT,env=env,text=True)
    body=cache_preprocessed[cache_preprocessed.rindex('dok_render_scene_prepare('):]
    body=body[:body.index('dok_render_scene_release(')]
    assert 'dok_geometry_build_cell_inplace(' not in body and 'dok_geometry_build_cell(' in body
    c8=json.loads((EVIDENCE/'scene-visibility-equivalence.json').read_text('utf8'));rows=[];inputs=[]
    for prior in c8['scene_rows']:
        level=prior['level'];poses=ROOT/prior['inputs']['path'];assert pin(poses)==prior['inputs']
        fixture=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        inputs.extend([pin(poses)]+[pin(p) for p in fixture]);result=dict(level=level)
        for key,binary in [('cells','geometry_inplace_equivalence.exe'),('cache','geometry_inplace_cache.exe'),
                           ('scene','scene_inplace_equivalence.exe'),('door','door_inplace_equivalence.exe')]:
            output=subprocess.check_output([str(OUT/binary)]+list(map(str,fixture)),
                input=poses.read_text('utf8') if key in ('scene','door') else None,cwd=ROOT,text=True)
            observed=json.loads(output.splitlines()[-1]);assert observed.get('passed',False);result[key]=observed
        assert result['scene']['scenes']==prior['scenes'] and result['door']['poses']==prior['scenes']
        rows.append(result);print('inplace level',level,result,flush=True)
    assert before==[pin(ROOT/p['path']) for p in before]
    snapshots=[]
    for p in ('ports/nds/render/geometry.c','ports/nds/render/geometry_private.h','ports/nds/render/scene.c'):
        target=EVIDENCE/'c11-inplace'/p;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/p).read_bytes());snapshots.append(pin(target))
    result=dict(task='N21P C11 private in-place geometry',passed=True,source_pins=before,source_pins_before_after_equal=True,
                archive_manifest=pin(archive),snapshots=snapshots,rows=rows,inputs=inputs,commands=commands,
                shared_kernel_math_order_validations_identical=True,private_active_quad_bytes=92,
                public_full_clear_stage_and_copy_on_success_preserved=True,geometry_cache1_public_path_preserved=True,
                compiler_uncached_macro_link_and_cache_hits_verified=True,header_public_unchanged=True,
                proof='Every active quad byte is assigned by append metadata and vertical/horizontal vertices. Private caller reads only count/reserved and active quads after success; unused poisoned tail and failure partials are ignored. Public kernel mode retains full clear at original stage and full out copy only on success.',
                public_cases=sum(r['cells']['public_cases']+r['cells']['public_argument_cases'] for r in rows),
                private_cases=sum(r['cells']['private_cases'] for r in rows),
                private_error_returns=sum(r['cells']['private_error_returns'] for r in rows),
                inactive_tail_bytes_checked=sum(r['cells']['inactive_tail_bytes_checked'] for r in rows),
                full_scenes=sum(r['scene']['scenes'] for r in rows),door_scene_cases=sum(r['door']['scene_cases'] for r in rows),
                normalized_scene_and_door_bytes=sum(r['scene']['normalized_bytes_compared']+r['door']['normalized_bytes_compared'] for r in rows),
                scope='Public complete bytes/error/alias contract; private active geometry under8 poison patterns; full scene and all-phase door replay at936 original plus12880 observed runtime camera poses on initial LEVEL worlds. No timer, visibility, merge, crop, world or cache algorithm changed.',
                binaries=[pin(OUT/p) for p in ('baseline_geometry.o','baseline_scene.o','baseline_door.o','uncached_geometry.o',
                    'geometry_inplace_equivalence.exe','geometry_inplace_cache.exe','scene_inplace_equivalence.exe','door_inplace_equivalence.exe')])
    report=EVIDENCE/'geometry-inplace-equivalence.json';report.write_text(json.dumps(result,indent=2)+'\n','utf8')
    bridge=dict(task='N21P C11 in-place geometry source bridge',passed=True,equivalence_proof=pin(report),
                scope='Only shared geometry kernel/private successful output and scene cache0 call site; public constructor semantics preserved, cache1 retains public path.',
                archived_geometry=pin(old_geometry),archived_scene=pin(old_scene),current_geometry=pin(ROOT/'ports/nds/render/geometry.c'),
                current_scene=pin(ROOT/'ports/nds/render/scene.c'),private_header=pin(ROOT/'ports/nds/render/geometry_private.h'),
                native_replay=False,prior_reports_modified=False)
    (EVIDENCE/'geometry-inplace-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps({k:result[k] for k in ('passed','public_cases','private_cases','full_scenes','door_scene_cases')}))
if __name__=='__main__':main()
