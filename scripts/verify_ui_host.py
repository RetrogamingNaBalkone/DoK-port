"""Bind executed G17 host tests and explicit cross compilation artifacts."""
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
    executable='build/native/dok-ui-tests.exe'
    stdout=subprocess.check_output([executable]).decode().strip()
    assert 'G17 UI:' in stdout and stdout.endswith('passed')
    artifacts=[executable]
    if args.cross_checks:
        artifacts+=['build/native/dok-ui-tests-linux','build/native/ui-arm7tdmi.o']
    report=dict(task='G17',status='passed',stdout=stdout,
                linux_execution=False,msvc_execution=False,cmake_execution=False,
                cross_compiled=args.cross_checks,
                sources={p:sha(p) for p in ('include/dok/ui.h','src/ui.c','src/ui_text.inc','src/ui_native_text.inc','tests/test_ui.c')},
                artifacts={p:sha(p) for p in artifacts})
    Path('analysis/g17-host-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(stdout)

if __name__=='__main__':
    main()
