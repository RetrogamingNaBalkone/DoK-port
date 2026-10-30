"""C8 exact active-plane pruning and once-per-prepare camera dot products."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/scene-visibility'
EVIDENCE=ROOT/'research/evidence/nds/n21p'

def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def signed(n):return n if n<2**31 else n-2**32

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=EVIDENCE/'pre-scene/pins.json'
    row=json.loads(manifest.read_text('utf8'))['source_pins'][0]
    old=ROOT/row['archived'];current=ROOT/row['path']
    assert pin(old)['sha256']==row['sha256']
    old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    prefix=old_text[:old_text.index('static int visible(')]
    assert prefix==new_text[:new_text.index('typedef struct scene_camera_dot')]
    old_bytes=old.read_bytes();new_bytes=current.read_bytes()
    assert old_bytes[:old_bytes.index(b'static int visible(')]==new_bytes[:new_bytes.index(b'typedef struct scene_camera_dot')]
    release='dok_render_scene_result dok_render_scene_release('
    assert old_text[old_text.index(release):]==new_text[new_text.index(release):]
    prepare='dok_render_scene_result dok_render_scene_prepare('
    old_prepare=old_text[old_text.index(prepare):old_text.index(release)]
    new_prepare=new_text[new_text.index(prepare):new_text.index(release)]
    reverse=new_prepare.replace('    scene_camera_dot dot;\n','',1)
    reverse=reverse.replace('    dot.lateral=(int64_t)camera->x*camera->cos_q15+(int64_t)camera->z*camera->sin_q15;\n','',1)
    reverse=reverse.replace('    dot.depth=(int64_t)camera->z*camera->cos_q15-(int64_t)camera->x*camera->sin_q15;\n','',1)
    reverse=reverse.replace('visible(source,camera,&dot)','visible(source,camera)',1)
    assert reverse==old_prepare
    sources=['ports/nds/render/scene.c','ports/nds/render/geometry.c','ports/nds/include/dok/render_scene.h',
             'ports/nds/include/dok/render_packet.h','ports/nds/core/world.c','ports/nds/core/sha256.c',
             'ports/nds/tests/scene_visibility_equivalence.c','ports/nds/tests/scene_prepare_equivalence.c',
             'ports/nds/tools/map/check_scene_visibility.py']
    before=[pin(ROOT/p) for p in sources]
    env=os.environ.copy();env['ZIG_LOCAL_CACHE_DIR']=str(OUT/'zig-local-cache')
    env['ZIG_GLOBAL_CACHE_DIR']=str(OUT/'zig-global-cache')
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic',
           '-Iports/nds/include','-Ibuild/nds/n07/generated']
    names=['dok_render_trig','dok_render_scene_init','dok_render_merge','dok_render_bounds',
           'dok_render_scene_prepare','dok_render_scene_release']
    wrapped=OUT/'baseline_scene.c'
    wrapped.write_bytes(old.read_bytes()+b'\nint baseline_visible(const dok_geometry_quad *q,const dok_nds_render_camera *c){return visible(q,c);}\n')
    common=['ports/nds/render/geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c']
    commands=[flags+['-D'+n+'=baseline_'+n for n in names]+['-c',str(wrapped),'-o',str(OUT/'baseline.o')],
              flags+['ports/nds/tests/scene_visibility_equivalence.c']+common+
                    [str(OUT/'baseline.o'),'-o',str(OUT/'scene_visibility_equivalence.exe')],
              flags+['ports/nds/tests/scene_prepare_equivalence.c','ports/nds/render/scene.c']+common+
                    [str(OUT/'baseline.o'),'-o',str(OUT/'scene_prepare_equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    numeric=json.loads(subprocess.check_output([str(OUT/'scene_visibility_equivalence.exe')],cwd=ROOT,text=True))
    assert numeric['passed'];print('numeric',numeric,flush=True)
    n07=ROOT/'build/nds/n07/scene-manifest.json'
    poses={i:[] for i in (1,2,3)};n07_count=0
    for case in json.loads(n07.read_text('utf8'))['cases']:
        for ordinal in range(case['frames']):
            poses[case['level']].append((case['x']+case['dx']*ordinal,case['y'],
                case['z']+case['dz']*ordinal,(case['heading']+case['dheading']*ordinal)%360))
            n07_count+=1
    assert n07_count==936
    trig=ROOT/'build/nds/n07/generated/render_trig.inc'
    tables=re.findall(r'=\s*\{([^}]+)\}',trig.read_text('utf8'))
    sine,cosine=([int(n) for n in re.findall(r'-?\d+',table)] for table in tables)
    assert len(sine)==len(cosine)==361
    headings={(sine[h],cosine[h]):h for h in range(361)}
    runtime=ROOT/'build/nds/n21p/candidate5-ru/emulator/result.json'
    actual=json.loads(runtime.read_text('utf8'));assert actual['passed']
    runtime_count=0;unique_runtime=set()
    for record in actual['history_records']:
        pair=(signed(record['sin_q15']),signed(record['cos_q15']))
        if not record['frame_id'] or pair not in headings:continue
        pose=(signed(record['camera_x']),signed(record['camera_y']),signed(record['camera_z']),headings[pair])
        poses[record['level']].append(pose);runtime_count+=1;unique_runtime.add((record['level'],)+pose)
    rows=[];fixtures=[]
    for level in (1,2,3):
        inputs=OUT/f'poses-level{level}.txt'
        inputs.write_text(''.join(' '.join(map(str,p))+'\n' for p in poses[level]),'utf8')
        fixture=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        fixtures.extend(pin(p) for p in fixture)
        observed=json.loads(subprocess.check_output([str(OUT/'scene_prepare_equivalence.exe')]+list(map(str,fixture)),
            input=inputs.read_text('utf8'),cwd=ROOT,text=True))
        assert observed['passed'] and observed['scenes']==len(poses[level])
        observed.update(level=level,inputs=pin(inputs));rows.append(observed)
        print('scene level',level,observed,flush=True)
    assert before==[pin(ROOT/p) for p in sources]
    result=dict(task='N21P C8 exact visibility',passed=True,source_pins=before,source_pins_before_after_equal=True,
                archived_source=pin(old),archive_manifest=pin(manifest),numeric=numeric,
                numeric_domain='Full signed32 quad vertices and camera; sin/cos in -32768..32768. All predicates remain signed64.',
                proof='AND of outside-plane bits can only lose bits; predicates for cleared planes cannot affect the final intersection. Camera dot distributivity is exact with no overflow: depth/lateral magnitude <2^48, all weighted predicates magnitude <2^58.',
                unchanged_discovery_dependency_prefix_sha256=hashlib.sha256(prefix.encode('utf8')).hexdigest(),
                unchanged_functions=['wrap32','sar15','word','dok_render_trig','dok_render_scene_init','dok_render_merge','dok_render_bounds','dok_render_scene_release'],
                original936_frames=n07_count,observed_runtime_camera_frames=runtime_count,
                distinct_runtime_camera_poses=len(unique_runtime),scene_rows=rows,
                full_scenes=sum(r['scenes'] for r in rows),
                normalized_bytes_compared=sum(r['normalized_bytes_compared'] for r in rows),
                scope='Complete scene bytes and normalized pointer offsets on initial original LEVEL1-3 geometry at all936 original fixtures and actual observed runtime camera poses. This is not a replay of mutable live world state.',
                inputs=[pin(n07),pin(runtime),pin(trig)]+fixtures,commands=commands,
                binaries=[pin(OUT/p) for p in ('baseline_scene.c','baseline.o','scene_visibility_equivalence.exe','scene_prepare_equivalence.exe')])
    report=EVIDENCE/'scene-visibility-equivalence.json'
    report.write_text(json.dumps(result,indent=2)+'\n','utf8')
    affected=[]
    for name in ('discovery-equivalence.json','discovery-native.json','discovery-validation.json','discovery-math.json'):
        path=EVIDENCE/name;r=json.loads(path.read_text('utf8'))
        match=[p for p in r['source_pins'] if p['path']==row['path']]
        assert len(match)==1 and match[0]['sha256']==row['sha256']
        affected.append(dict(report=pin(path),original_compiled_path=row['path'],original_sha256=row['sha256']))
    bridge=dict(task='N21P C8 scene source-pin bridge',passed=True,original_compiled_path=row['path'],
                scope='Only ports/nds/render/scene.c. Original trig/gather bounds and complete dependency prefix remain byte-identical; only private visual culling and precomputed dot use changed.',
                archived_original=pin(old),current_equivalent_source=pin(current),equivalence_proof=pin(report),
                affected_reports=affected,native_replay=False,old_native_reports_modified=False)
    (EVIDENCE/'scene-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,numeric_quads=numeric['numeric_quads'],full_scenes=result['full_scenes'])))

if __name__=='__main__':main()
