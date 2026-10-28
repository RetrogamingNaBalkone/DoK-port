"""Record G15 host/source/ABI and original RU/EN coverage."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 samples=['originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES'];before={p:sha(p) for p in samples}
 for lang in ('ru','en'):
  inv=json.loads(Path(f'analysis/originals/inventory-{lang}.json').read_text(encoding='utf-8-sig'));assert sha(inv['file'])==inv['sha256'];assert sha(inv['resource']['file'])==inv['resource']['sha256']
 run=subprocess.run(['build/gba/host/dok-ai-combat-tests.exe'],capture_output=True,text=True,check=True);assert 'G15 host:' in run.stdout and len(run.stdout.splitlines())==29
 assert before=={p:sha(p) for p in samples}
 sources=['ports/gba/src/ai.c','ports/gba/src/combat.c','ports/gba/include/dok/combat.h','ports/gba/include/dok/ai.h','ports/gba/include/dok/runtime.h','ports/gba/src/runtime.c','ports/gba/src/renderer_sprites.inc','ports/gba/tests/test_ai_combat.c','ports/gba/platform/ai_probe.c','ports/gba/scripts/build-native.ps1','CMakeLists.txt']
 report=dict(task='G15',status='passed',stdout=run.stdout.strip(),groups=list(range(1,15)),languages=['ru','en'],actual_projectile_front_class8=True,sources={p:sha(p) for p in sources},original_sha256_before=before,original_sha256_after=before,package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in ['build/gba/host/dok-ai-combat-tests.exe','build/gba/host/dok-ai-combat-tests-linux','build/gba/host/ai-arm7tdmi.o']},limits='Windows host executed. Linux static and Thumb cross compiled, not executed. Tests rebuild original RU/EN LEVELs from RES; shared release profile checked separately on ARM. Full DOS gameplay and physical GBA untested.')
 Path('analysis/gba/g15-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
if __name__=='__main__':main()
