"""Record execution of the real-package host combat suite and cross artifacts."""
import argparse,hashlib,json,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--cross-checks',action='store_true');args=parser.parse_args()
 executable='build/native/dok-combat-tests.exe'
 run=subprocess.run([executable,'build/gba/assets/assets.bin'],capture_output=True,text=True,check=True)
 assert 'G13 host:' in run.stdout and 'passed' in run.stdout
 readiness=subprocess.check_output([executable,'build/gba/assets/assets.bin','readiness'],text=True).splitlines()
 assert len(readiness)==10
 artifacts=[executable]
 if args.cross_checks:artifacts.extend(('build/native/dok-combat-tests-linux','build/native/combat-arm7tdmi.o'))
 report=dict(task='G13',status='passed',stdout=run.stdout.strip(),returncode=run.returncode,readiness=readiness,
  package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in artifacts},
  script_sha256=sha(__file__),sources={p:sha(p) for p in ('tests/test_combat.c','src/combat.c','include/dok/combat.h','src/collision.c','include/dok/collision.h','scripts/build-native.ps1')},
  executed='Windows x86_64 host suite with real balanced RU LEVEL1 ROM descriptors',
  cross_compilation='Linux musl static and ARM7TDMI Thumb; cross binaries not executed' if args.cross_checks else 'not requested in this host recording',
  cases=['empty and below-cost charge for both weapons','readiness states0..9','first weapon show and pending hide/show',
   'full refusal preserves charge','first NULL/reclaim scan and reuse','flight/impact cleanup','sleep wake and active health/damage/death',
   'same-class immunity','ring newest-first and removal skip','deferred owner creation forbids current-slot reuse','dynamic occupancy blocks collision and clears on remove'])
 Path('analysis/g13-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
if __name__=='__main__':main()
