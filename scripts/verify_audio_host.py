"""Build/run portable voice tests and record source/original hashes."""
import hashlib,json,os,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe';env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(Path('build/zig-cache').resolve()))
 common=[str(zig),'cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','include']
 samples=['originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES'];before={p:sha(p) for p in samples}
 subprocess.run(common+['src/audio.c','tests/test_audio.c','-o','build/native/dok-audio-tests.exe'],env=env,check=True)
 stdout=subprocess.check_output(['build/native/dok-audio-tests.exe'],text=True)
 subprocess.run(common+['-target','x86_64-linux-musl','-static','src/audio.c','tests/test_audio.c','-o','build/native/dok-audio-tests-linux'],env=env,check=True)
 subprocess.run(common+['-target','thumb-freestanding-eabi','-mcpu=arm7tdmi','-ffreestanding','-c','src/audio.c','-o','build/native/audio-arm7tdmi.o'],env=env,check=True)
 assert before=={p:sha(p) for p in samples}
 sources=['include/dok/audio.h','src/audio.c','tests/test_audio.c','gba/audio_backend.c','gba/startup.S','gba/renderer_probe.c','gba/ai_probe.c','gba/combat_probe.c','gba/movement_probe.c','src/interactions.c','include/dok/interactions.h','scripts/verify_audio_host.py','scripts/build-native.ps1','scripts/build-gba.ps1','CMakeLists.txt']
 report=dict(task='G16',status='passed',stdout=stdout.strip(),sources={p:sha(p) for p in sources},original_sha256_before=before,original_sha256_after=before,package_sha256=sha('build/gba/assets/assets.bin'),artifacts={p:sha(p) for p in ['build/native/dok-audio-tests.exe','build/native/dok-audio-tests-linux','build/native/audio-arm7tdmi.o']},limits='Windows executed, Linux/ARM cross compiled only. Native object loop flags and backend latched loop separated. GBA/DOS audio quality and timing require independent capture; no hardware/listening claim.')
 Path('analysis/g16-host-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(stdout.strip())
if __name__=='__main__':main()
