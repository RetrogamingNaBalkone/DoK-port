"""Build isolated N22 ARM9 overlap fixture using the shared pinned SDK."""
import json
import os
from pathlib import Path
import shutil
import subprocess
from check_host import ROOT,pin

def main():
    out=ROOT/'build/nds/n22/overlap-arm9';out.mkdir(parents=True,exist_ok=True)
    (out/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',out/'generated/render_trig.inc')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    bash=Path.home()/'.local/msys64/usr/bin/bash.exe'
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n22overlap\n"
    run=subprocess.run([str(bash),'--login','-s'],input=command,env=env,cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
    (out/'build.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text(encoding='utf-8-sig'))
    assert all(k+' '+v in run.stdout for k,v in lock['packages'].items())
    # Dependencies explicitly compiled by this standalone target, plus every
    # project header. SDK versions and ARM7 are pinned independently.
    sources=[]
    for folder in ('core','render'):
        names=('gameflow events contracts input controls motion camera time collision interactions interactions_move assets memory sha256 world balance items combat ai'.split()
               if folder=='core' else 'scene geometry materials door_render sprites sprite_assets weapon_overlay'.split())
        sources += [ROOT/f'ports/nds/{folder}/{name}.c' for name in names]
    sources += [ROOT/'ports/nds/platform/gameplay_probe/gameplay.c',ROOT/'ports/nds/platform/assets/file_reader.c',
                ROOT/'ports/nds/tools/flow/test_retry_overlap.c',ROOT/'ports/nds/tools/flow/overlap_arm9_main.c',
                ROOT/'ports/nds/Makefile.n22overlap',Path(__file__).resolve(),out/'generated/render_trig.inc',
                ROOT/'ports/nds/toolchain_versions.json']
    sources += sorted((ROOT/'ports/nds/include').rglob('*.h'))
    sources += sorted((ROOT/'ports/nds/platform/gameplay_probe').glob('*.h'))
    sources += [ROOT/'ports/nds/platform/assets/file_reader.h']
    evidence=ROOT/'research/evidence/nds/n22/overlap-arm9';evidence.mkdir(parents=True,exist_ok=True)
    snapshots=[]
    for source in sources:
        relative=source.relative_to(ROOT);destination=evidence/'inputs'/relative
        destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
        original=pin(source);archived=pin(destination);assert original['sha256']==archived['sha256']
        snapshots.append(dict(current=original,snapshot=archived))
    manifest=dict(task='N22',scope='Separate ARM9 overlap integration fixture; no normal-playthrough claim',
                  command=command,packages=lock['packages'],arm7=pin(ROOT/'build/nds/n18/arm7/dok_audio_arm7.elf'),
                  package=pin(ROOT/'build/nds/assets/dok-assets.ndspak'),inputs=snapshots,
                  artifacts=[pin(out/f'dok_nds_overlap.{suffix}') for suffix in ('nds','elf','map')],build_log=pin(out/'build.log'))
    (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (evidence/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest['artifacts']))

if __name__=='__main__':main()
