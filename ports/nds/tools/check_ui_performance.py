"""N21P reproducible baseline/candidate builds; emulator execution is separate."""
import json,os,shutil,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n21p'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def build(label):
    target=OUT/label;target.mkdir(parents=True,exist_ok=True)
    (target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    for name,source in (('scene.dat','n20/scene.dat'),('scene-title.dat','n20/scene-title.dat'),('scene-combat-touch.dat','n20/scene-combat-touch.dat'),('ui.dat','n19/ui.dat'),('frontend.dat','n21/frontend.dat')):
        shutil.copyfile(ROOT/'build/nds'/source,OUT/name)
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    fixture=int('fixture' in label); frontend=int('frontend' in label or fixture); audit=int('audit' in label);fine=int('fine' in label)
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n21p OUTDIR=../../build/nds/n21p/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} GAMEPLAY_FRONTEND={frontend} FRONTEND_FIXTURES={fixture} SPRITE_AUDIT={audit} N21P_AUDIT={audit} N21P_GEOMETRY_PROFILE={fine} GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    try:log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    except Exception as error:
        (target/'build-error.log').write_text(str(error),'utf8');raise
    (target/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(k+' '+v in log for k,v in lock['packages'].items())
    sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py','.S','.ld','.lds') or p.name.startswith('Makefile'))]
    generated=[OUT/n for n in ('scene.dat','ui.dat','frontend.dat')]
    generated += [ROOT/'build/nds/n19/generated/ui_native.inc',ROOT/'build/nds/n21/generated/frontend_hash.inc',target/'generated/render_trig.inc']
    report=dict(task='N21P',label=label,frontend=bool(frontend),frontend_fixtures=bool(fixture),audit=bool(audit),geometry_profile=bool(fine),packages=lock['packages'],artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=sources,generated_inputs=[artifact(p) for p in generated],build_drivers=[artifact(Path(__file__)),artifact(ROOT/'ports/nds/toolchain_versions.json')],arm7_dependency=artifact(ROOT/'build/nds/n18/arm7/dok_audio_arm7.elf'))
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    for row in sources:
        source=ROOT/row['path'];destination=EVIDENCE/'sources'/label/row['path'];destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    print(json.dumps(report['artifacts']))
if __name__=='__main__':build(sys.argv[1])
