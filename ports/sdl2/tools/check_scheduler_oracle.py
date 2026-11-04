"""Compare SDL2 Events against executed original instructions, not a Python model."""
import json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def main():
    oracle=json.loads((ROOT/'research/evidence/source-port/sp05-scheduler/vectors.json').read_text())
    results=[]
    for profile,cases in oracle['samples'].items():
        for case in cases:
            lines=[]
            for cb,a in case.get('actions',{}).items():
                lines.append(f"A {cb} {a.get('advance',0)} {int(a.get('once',False))}")
                for op in a.get('operations',[]):
                    nums=[{'cancel':1,'interval':2,'schedule':3}[op[0]],*[n&0xffffffff for n in op[1:]]]
                    nums += [0]*(5-len(nums)); lines.append(f"B {cb} "+' '.join(map(str,nums)))
            for op in case['operations']:
                if op[0]=='now':lines.append(f'N {op[1]&0xffffffff}')
                elif op[0]=='dispatch':lines.append('D')
                elif op[0]=='clear':lines.append('K')
                elif op[0]=='schedule':lines.append('S '+' '.join(str(n&0xffffffff) for n in op[1:]))
            actual=subprocess.run([str(ROOT/'build/sdl2/windows/dok_scheduler_probe.exe')],input='\n'.join(lines)+'\n',text=True,capture_output=True,check=True).stdout.splitlines()
            expected=[f"C {r['owner']} {r['callback']} {r['clock']}" for r in case['callbacks']]
            expected.append(f"H {case['final']['owner_count']}")
            for owner in case['final']['owners']:
                expected.append(f"O {owner['id']} {owner['count']}")
                expected += ['E '+' '.join(str(e[k]) for k in ('deadline','interval','callback','parameter')) for e in owner['events']]
            if actual!=expected:raise AssertionError((profile,case['name'],actual,expected))
            results.append(dict(profile=profile,case=case['name'],passed=True))
    path=ROOT/'build/sdl2/checks/scheduler-oracle.json';path.write_text(json.dumps(dict(passed=True,cases=results),indent=2)+'\n')
    print(len(results),'original x86/C++ scheduler cases passed')
if __name__=='__main__':main()
