"""Record real RES/package host AI replay and source integrity."""
import hashlib,json,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 samples=['originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES'];before={p:sha(p) for p in samples}
 for lang in ('ru','en'):
  inventory=json.loads(Path(f'analysis/inventory-{lang}.json').read_text(encoding='utf-8-sig'))
  assert sha(inventory['file'])==inventory['sha256']
  assert sha(inventory['resource']['file'])==inventory['resource']['sha256']
 run=subprocess.run(['build/native/dok-ai-tests.exe'],capture_output=True,text=True,check=True);assert 'G14 host:' in run.stdout
 assert before=={p:sha(p) for p in samples}
 sources=['src/ai.c','src/ai_atan.inc','include/dok/ai.h','tests/test_ai.c','gba/ai_probe.c','gba/combat_probe.c','gba/renderer_probe.c','scripts/build-native.ps1']
 report=dict(task='G14',status='passed',stdout=run.stdout.strip(),sources={p:sha(p) for p in sources},original_sha256_before=before,original_sha256_after=before,package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in ['build/native/dok-ai-tests.exe','build/native/dok-ai-tests-linux','build/native/ai-arm7tdmi.o']},limits='Windows host executed; Linux musl static and Thumb cross compiled, not executed. Original RU/EN RES rebuilt separately; release language balance deliberately shared.')
 Path('analysis/g14-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
if __name__=='__main__':main()
