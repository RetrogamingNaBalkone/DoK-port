"""Exact scene equivalence: cell-plane admission vs frozen room50 renderer."""
import json
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT/'ports/nds/tools'))
from check_items import artifact, run
from map.compiler_cache import compiler_environment


def main():
    out = ROOT/'build/nds/n23/angles51-host'
    out.mkdir(parents=True, exist_ok=True)
    baseline = ROOT/'research/evidence/nds/n23/angles51/baseline_scene.c'
    flags = [Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe', 'cc',
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-Iports/nds/include', '-Iports/nds/render', '-Ibuild/nds/n07/generated',
        '-DN23_STATIC_GEOMETRY=1', '-DN23_FUSED_CULL=1']
    env = compiler_environment(ROOT)
    exports = ('dok_render_trig', 'dok_render_scene_init', 'dok_render_merge',
        'dok_render_bounds', 'dok_render_scene_prepare', 'dok_render_scene_release')
    run(flags+['-D'+n+'=baseline_'+n for n in exports]+
        ['-c', baseline, '-o', out/'baseline.o'], env=env)
    harness = (ROOT/'ports/nds/tests/scene_prepare_equivalence.c').read_text('utf8')
    harness = '#include "static_geometry.h"\nstatic _Alignas(32) unsigned char backing[32768];\n'+harness
    needle = '    baseline_dok_render_scene_init(&old_scene);'
    assert harness.count(needle) == 1
    harness = harness.replace('if(argc!=3)return 1;', 'if(argc!=3&&argc!=4)return 1;')
    harness = harness.replace(needle,
        '    if(argc==4)for(uint32_t n=0;n<256u;++n)for(uint32_t k=0;k<4u;++k)bindings[n].images[k].width*=2u;\n'
        '    uint32_t used;\n    if(!dok_static_geometry_load(&world,bindings,backing,sizeof(backing),&used))return 19;\n'
        '    if(argc==4){uint32_t fallback=0,count;int64_t o[3]={0};\n'
        '        for(uint32_t z=0;z<64u;++z)for(uint32_t x=0;x<64u;++x)fallback+=dok_static_geometry_cull(&world,bindings,x,z,o,o,o,&count)==31u;\n'
        '        if(!fallback)return 20;}\n'+needle)
    generated = out/'equivalence.c'
    generated.write_text(harness, 'utf8')
    sources = ['ports/nds/render/'+n+'.c' for n in ('scene','geometry','static_geometry')]
    sources += ['ports/nds/core/world.c', 'ports/nds/core/sha256.c']
    run(flags+[generated, *sources, out/'baseline.o', '-o', out/'equivalence.exe'], env=env)
    rng = random.Random(51)
    poses = [(x*480+240,-300,z*480+240,h) for x in range(0,64,8)
             for z in range(0,64,8) for h in range(0,360,15)]
    poses += [(rng.randrange(-1000,31720),rng.randrange(-1000,200),
               rng.randrange(-1000,31720),rng.randrange(361)) for _ in range(1000)]
    poses += [(x*480+dx,-300,z*480+dz,h) for x,z in ((57,51),(58,52),(46,54))
              for dx,dz in ((0,0),(239,241),(479,479)) for h in range(361)]
    text = ''.join('%d %d %d %d\n'%p for p in poses)
    results = []
    for level in (1,2,3):
        inputs = [ROOT/f'build/nds/n07/geometry-level{level}.cp866',
                  ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        result = json.loads(run([out/'equivalence.exe', *inputs], input=text, env=env))
        assert result['passed'], result
        overhang = json.loads(run([out/'equivalence.exe', *inputs, 'overhang'], input=text, env=env))
        assert overhang['passed'], overhang
        results.append(dict(level=level, inputs=[artifact(p) for p in inputs], result=result, overhang=overhang))
        print(json.dumps(results[-1]), flush=True)
    report = dict(passed=True, results=results, baseline=artifact(baseline),
                  sources=[artifact(ROOT/p) for p in sources], generated=artifact(generated))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')


if __name__ == '__main__':
    main()
