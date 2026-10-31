"""Generate the immutable RU simulation frame bank (both UI overlays included)."""
import hashlib
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[4]


def prepare(target):
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    directory=ROOT/'build/nds/n23/packed-frames'
    directory.mkdir(parents=True,exist_ok=True)
    exe=directory/'prepare.exe'
    core=[f'ports/nds/core/{n}.c' for n in ('contracts','assets','sha256','world','balance','audio_codec')]
    render=[f'ports/nds/render/{n}.c' for n in ('scene','geometry','materials','door_render','sprites','sprite_assets','texture_cache','frame_store')]
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    subprocess.run([str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-DN23_COMPACT_SOURCE=1',
                    f'-DFRAME_LANGUAGE={2 if target.name.endswith("en") else 1}',
                    '-I','ports/nds/include','-I','build/nds/n07/generated',*core,*render,
                    'ports/nds/ui/frontend_visual.c','ports/nds/platform/assets/file_reader.c',
                    'ports/nds/tools/performance/prepare_frames.c','-o',str(exe)],cwd=ROOT,env=env,check=True)
    bank=target/'nitrofs/dok/frames.dat'
    bank.parent.mkdir(parents=True,exist_ok=True)
    audio=bank.with_name('sounds.dat')
    subprocess.run([str(exe),str(ROOT/'build/nds/assets/dok-assets.ndspak'),str(bank),str(audio)],cwd=ROOT,check=True)
    digest=hashlib.sha256(bank.read_bytes()).digest()
    (target/'generated/frames_hash.inc').write_text('static const unsigned char frames_sha[32]={'+','.join(str(v) for v in digest)+'};\n','ascii')
    digest=hashlib.sha256(audio.read_bytes()).digest()
    (target/'generated/sounds_hash.inc').write_text('static const unsigned char sounds_sha[32]={'+','.join(str(v) for v in digest)+'};\n','ascii')
    return bank
