"""Current N23 combat/AI regression, with optional old-C differential proof."""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
sys.path.insert(0,str(ROOT/'ports/nds/tools/map'))
from check_combat import SOURCES
from check_interactions import FLAGS
from check_items import run, artifact
from compiler_cache import compiler_environment


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path)
    args=p.parse_args()
    out=ROOT/'build/nds/n23/monsters40-host';out.mkdir(parents=True,exist_ok=True)
    env=compiler_environment(ROOT)
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    report=dict(passed=False,variants={})
    tests={'test_combat':['combat_cases.c'],'test_ai_combat':['ai_combat_cases.c'],
           'test_ai':['ai_cases.c'],'test_collision':['collision_cases.c'],
           'test_collision_routes':['collision_routes.c'],'test_playtest34_hits':[]}
    variants=['production','generic-division']+(['baseline'] if args.baseline else [])
    for variant in variants:
        results={}
        for name,extra in tests.items():
            sources=[str(args.baseline.resolve()) if s=='ports/nds/core/combat.c' and variant=='baseline' else s for s in SOURCES]
            command=[zig,'cc',*FLAGS,'-DN23_COMBAT_SAR=1','-DN23_COLLISION_OBJECT=1',
                     '-DN23_COLLISION_CORNERS=1','-DN23_CLOCK_FAST32=1',
                     f'-DN23_NATIVE_DIV480={0 if variant=="generic-division" else 1}',
                     *sources,'ports/nds/core/ai.c',*[f'ports/nds/tests/{x}'for x in extra],
                     f'ports/nds/tests/{name}.c','-o',out/(variant+'-'+name+'.exe')]
            run(command,env=env)
            # The old C includes its neighboring combat_hits.inc.
            result=json.loads(run([out/(variant+'-'+name+'.exe')]+
                                  ([] if name=='test_collision' else [ROOT/'build/nds/assets/dok-assets.ndspak'])))
            assert not result.get('failures',0),result
            results[name]=result
        report['variants'][variant]=results
        assert results==report['variants']['production'],(variant,'deterministic results changed')
        print(variant,json.dumps(results),flush=True)
    report.update(passed=True,baseline=artifact(args.baseline.resolve()) if args.baseline else None,
                  source=artifact(ROOT/'ports/nds/core/combat.c'),header=artifact(ROOT/'ports/nds/include/dok/combat.h'))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')


if __name__=='__main__':main()
