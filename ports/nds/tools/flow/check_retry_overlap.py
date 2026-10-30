"""Fresh focused N22 retry -> real AI dispatch reproduction, not playthrough."""
from __future__ import annotations
import json
import argparse
from pathlib import Path
import subprocess
from check_host import ROOT,pin,compiler_environment

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expect-fixed',action='store_true')
    args=parser.parse_args()
    # Borrow the explicit host compiler recipe only, never its acceptance or
    # executable. Every source is freshly compiled, and every current input is
    # pinned into this separate focused result.
    recipe=json.loads((ROOT/'research/evidence/nds/n22/host/check.json').read_text(encoding='utf-8'))
    command=recipe['command'][:]
    command[command.index('ports/nds/tools/flow/test_flow_host.c')]='ports/nds/tools/flow/test_retry_overlap.c'
    output=ROOT/'build/nds/n22/host/retry-overlap.exe';command[-1]=str(output)
    result=subprocess.run(command,cwd=ROOT,env=compiler_environment(ROOT),capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    package=ROOT/'build/nds/assets/dok-assets.ndspak'
    result=subprocess.run([str(output),str(package)],cwd=ROOT,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    observed=json.loads(result.stdout)
    if args.expect_fixed:
        assert observed['retry_count']==1 and observed['ai_callbacks']>=1,observed
        assert observed['shared_actors']==3 and observed['corpse_variants']==2 and observed['repeated_retry_count']==2,observed
        assert not any(observed[k] for k in ('callback_status','game_fault','ai_fault')),observed
    report=dict(task='N22',scope='Focused controlled host retry followed by actual scheduled AI callback; not normal playthrough',
                command=command,observed=observed,binary=pin(output),package=pin(package),
                expect_fixed=args.expect_fixed,inputs=[pin(ROOT/p['path']) for p in recipe['inputs'] if not p['path'].endswith('test_flow_host.c')]+
                       [pin(__file__),pin(ROOT/'ports/nds/tools/flow/test_retry_overlap.c')])
    path=ROOT/'research/evidence/nds/n22/host/retry-overlap.json'
    path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(observed))

if __name__=='__main__':main()
