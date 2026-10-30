"""Prepare and build N21 production or explicit frontend-fixture binaries."""
import json,os,shutil,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n21'
def build(label):
    target=OUT/label;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    shutil.copyfile(ROOT/'build/nds/n19/ui.dat',OUT/'ui.dat')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    fixture=int(label.startswith('fixture'))
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n21 OUTDIR=../../build/nds/n21/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} FRONTEND_FIXTURES={fixture} SPRITE_AUDIT=0 GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    try:log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    except Exception as error:
        (target/'build-error.log').write_text(str(error),'utf8');raise
    (target/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'));assert all(k+' '+v in log for k,v in lock['packages'].items())
    report=dict(task='N21',label=label,frontend_fixtures=bool(fixture),packages=lock['packages'],
        artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],
        sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.S','.ld','.lds') or p.name.startswith('Makefile'))],
        build_drivers=[artifact(Path(__file__)),artifact(ROOT/'ports/nds/toolchain_versions.json'),
            artifact(ROOT/'ports/nds/tools/frontend/extract_frontend.py'),artifact(ROOT/'ports/nds/tools/frontend/export_labels.py')],
        generated_inputs=[artifact(OUT/'frontend.dat'),artifact(OUT/'generated/frontend_hash.inc'),artifact(OUT/'ui.dat'),
            artifact(ROOT/'build/nds/n19/generated/ui_native.inc'),artifact(target/'generated/render_trig.inc')],
        arm7_dependency=artifact(ROOT/'build/nds/n18/arm7/dok_audio_arm7.elf'))
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['artifacts']))
if __name__=='__main__':build(sys.argv[1])
