"""Build/run portable voice tests and record source/original hashes."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,os,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe';env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(Path('build/gba/zig-cache').resolve()))
 common=[str(zig),'cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','ports/gba/include']
 samples=['originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES'];before={p:sha(p) for p in samples}
 subprocess.run(common+['ports/gba/src/audio.c','ports/gba/tests/test_audio.c','-o','build/gba/host/dok-audio-tests.exe'],env=env,check=True)
 stdout=subprocess.check_output(['build/gba/host/dok-audio-tests.exe'],text=True)
 subprocess.run(common+['-target','x86_64-linux-musl','-static','ports/gba/src/audio.c','ports/gba/tests/test_audio.c','-o','build/gba/host/dok-audio-tests-linux'],env=env,check=True)
 subprocess.run(common+['-target','thumb-freestanding-eabi','-mcpu=arm7tdmi','-ffreestanding','-c','ports/gba/src/audio.c','-o','build/gba/host/audio-arm7tdmi.o'],env=env,check=True)
 assert before=={p:sha(p) for p in samples}
 sources=['ports/gba/include/dok/audio.h','ports/gba/src/audio.c','ports/gba/tests/test_audio.c','ports/gba/platform/audio_backend.c','ports/gba/platform/startup.S','ports/gba/platform/renderer_probe.c','ports/gba/platform/ai_probe.c','ports/gba/platform/combat_probe.c','ports/gba/platform/movement_probe.c','ports/gba/src/interactions.c','ports/gba/include/dok/interactions.h','ports/gba/scripts/verify_audio_host.py','ports/gba/scripts/build-native.ps1','ports/gba/scripts/build-gba.ps1','CMakeLists.txt']
 report=dict(task='G16',status='passed',stdout=stdout.strip(),sources={p:sha(p) for p in sources},original_sha256_before=before,original_sha256_after=before,package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in ['build/gba/host/dok-audio-tests.exe','build/gba/host/dok-audio-tests-linux','build/gba/host/audio-arm7tdmi.o']},limits='Windows executed, Linux/ARM cross compiled only. Native object loop flags and backend latched loop separated. GBA/DOS audio quality and timing require independent capture; no hardware/listening claim.')
 Path('analysis/gba/g16-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(stdout.strip())
if __name__=='__main__':main()
