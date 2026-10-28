"""Bind executed G17 host tests and explicit cross compilation artifacts."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cross-checks',action='store_true')
    args=parser.parse_args()
    executable='build/gba/host/dok-ui-tests.exe'
    stdout=subprocess.check_output([executable]).decode().strip()
    assert 'G17 UI:' in stdout and stdout.endswith('passed')
    artifacts=[executable]
    if args.cross_checks:
        artifacts+=['build/gba/host/dok-ui-tests-linux','build/gba/host/ui-arm7tdmi.o']
    report=dict(task='G17',status='passed',stdout=stdout,
                linux_execution=False,msvc_execution=False,cmake_execution=False,
                cross_compiled=args.cross_checks,
                sources={p:sha(p) for p in ('ports/gba/include/dok/ui.h','ports/gba/src/ui.c','ports/gba/src/ui_text.inc','ports/gba/src/ui_native_text.inc','ports/gba/tests/test_ui.c')},
                artifacts={p:sha(p) for p in artifacts})
    Path('analysis/gba/g17-host-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(stdout)

if __name__=='__main__':
    main()
