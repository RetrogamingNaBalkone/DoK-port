"""Record real RES/package host AI replay and source integrity."""

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
  inventory=json.loads(Path(f'analysis/originals/inventory-{lang}.json').read_text(encoding='utf-8-sig'))
  assert sha(inventory['file'])==inventory['sha256']
  assert sha(inventory['resource']['file'])==inventory['resource']['sha256']
 run=subprocess.run(['build/gba/host/dok-ai-tests.exe'],capture_output=True,text=True,check=True);assert 'G14 host:' in run.stdout
 assert before=={p:sha(p) for p in samples}
 sources=['ports/gba/src/ai.c','ports/gba/src/ai_atan.inc','ports/gba/include/dok/ai.h','ports/gba/tests/test_ai.c','ports/gba/platform/ai_probe.c','ports/gba/platform/combat_probe.c','ports/gba/platform/renderer_probe.c','ports/gba/scripts/build-native.ps1']
 report=dict(task='G14',status='passed',stdout=run.stdout.strip(),sources={p:sha(p) for p in sources},original_sha256_before=before,original_sha256_after=before,package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in ['build/gba/host/dok-ai-tests.exe','build/gba/host/dok-ai-tests-linux','build/gba/host/ai-arm7tdmi.o']},limits='Windows host executed; Linux musl static and Thumb cross compiled, not executed. Original RU/EN RES rebuilt separately; release language balance deliberately shared.')
 Path('analysis/gba/g14-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
if __name__=='__main__':main()
