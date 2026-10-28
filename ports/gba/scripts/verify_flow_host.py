"""Bind executed G18 host fixtures and cross compile artifacts."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse,json,subprocess,hashlib
from pathlib import Path

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--cross-checks',action='store_true');a=p.parse_args()
 artifacts=['build/gba/host/dok-flow-tests.exe','build/gba/host/dok-ui-tests.exe'];outputs=[subprocess.check_output([x]).decode().strip() for x in artifacts]
 assert all(x.endswith('passed') for x in outputs)
 if a.cross_checks:artifacts+=['build/gba/host/dok-flow-tests-linux','build/gba/host/flow-arm7tdmi.o','build/gba/host/dok-ui-tests-linux','build/gba/host/ui-arm7tdmi.o']
 sources=['ports/gba/include/dok/flow.h','ports/gba/src/flow.c','ports/gba/tests/test_flow.c','ports/gba/include/dok/ui.h','ports/gba/src/ui.c','ports/gba/tests/test_ui.c']
 report=dict(task='G18',status='passed',stdout=outputs,cross_compiled=a.cross_checks,linux_execution=False,msvc_execution=False,cmake_execution=False,sources={x:sha(x) for x in sources},artifacts={x:sha(x) for x in artifacts})
 Path('analysis/gba/g18-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print('\n'.join(outputs))
if __name__=='__main__':main()
