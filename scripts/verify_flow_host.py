"""Bind executed G18 host fixtures and cross compile artifacts."""
import argparse,json,subprocess,hashlib
from pathlib import Path

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--cross-checks',action='store_true');a=p.parse_args()
 artifacts=['build/native/dok-flow-tests.exe','build/native/dok-ui-tests.exe'];outputs=[subprocess.check_output([x]).decode().strip() for x in artifacts]
 assert all(x.endswith('passed') for x in outputs)
 if a.cross_checks:artifacts+=['build/native/dok-flow-tests-linux','build/native/flow-arm7tdmi.o','build/native/dok-ui-tests-linux','build/native/ui-arm7tdmi.o']
 sources=['include/dok/flow.h','src/flow.c','tests/test_flow.c','include/dok/ui.h','src/ui.c','tests/test_ui.c']
 report=dict(task='G18',status='passed',stdout=outputs,cross_compiled=a.cross_checks,linux_execution=False,msvc_execution=False,cmake_execution=False,sources={x:sha(x) for x in sources},artifacts={x:sha(x) for x in artifacts})
 Path('analysis/g18-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print('\n'.join(outputs))
if __name__=='__main__':main()
