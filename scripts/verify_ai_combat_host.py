"""Record G15 host/source/ABI and original RU/EN coverage."""
import hashlib,json,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 samples=['originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES'];before={p:sha(p) for p in samples}
 for lang in ('ru','en'):
  inv=json.loads(Path(f'analysis/inventory-{lang}.json').read_text(encoding='utf-8-sig'));assert sha(inv['file'])==inv['sha256'];assert sha(inv['resource']['file'])==inv['resource']['sha256']
 run=subprocess.run(['build/native/dok-ai-combat-tests.exe'],capture_output=True,text=True,check=True);assert 'G15 host:' in run.stdout and len(run.stdout.splitlines())==29
 assert before=={p:sha(p) for p in samples}
 sources=['src/ai.c','src/combat.c','include/dok/combat.h','include/dok/ai.h','include/dok/runtime.h','src/runtime.c','src/renderer_sprites.inc','tests/test_ai_combat.c','gba/ai_probe.c','scripts/build-native.ps1','CMakeLists.txt']
 report=dict(task='G15',status='passed',stdout=run.stdout.strip(),groups=list(range(1,15)),languages=['ru','en'],actual_projectile_front_class8=True,sources={p:sha(p) for p in sources},original_sha256_before=before,original_sha256_after=before,package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in ['build/native/dok-ai-combat-tests.exe','build/native/dok-ai-combat-tests-linux','build/native/ai-arm7tdmi.o']},limits='Windows host executed. Linux static and Thumb cross compiled, not executed. Tests rebuild original RU/EN LEVELs from RES; shared release profile checked separately on ARM. Full DOS gameplay and physical GBA untested.')
 Path('analysis/g15-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
if __name__=='__main__':main()
