"""Build N22 with the pinned shared SDK; do not overwrite accepted checkpoints."""
import json,os,re,shutil,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n22'

def build(label):
    assert re.fullmatch(r'(?:frontend|audit|candidate-[a-z0-9-]+)-(?:ru|en)',label)
    target=OUT/label;target.mkdir(parents=True,exist_ok=True)
    if label.startswith('candidate-') and (target/'build-manifest.json').exists():
        raise ValueError('Candidate target is immutable; choose a new label')
    (target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    for name,source in (('ui.dat','n19/ui.dat'),('frontend.dat','n21/frontend.dat')):
        shutil.copyfile(ROOT/'build/nds'/source,OUT/name)
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    audit=int(label.startswith('audit'))
    largest=int(label.startswith('candidate-') and 'largest' in label)
    # Production/audit use the accepted partial planner. Candidate labels retain
    # explicit experimental selection so old controls remain reproducible.
    partial=int(not label.startswith('candidate-') or 'partial' in label)
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n22 OUTDIR=../../build/nds/n22/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} GAMEPLAY_FRONTEND=1 FRONTEND_FIXTURES=0 SPRITE_AUDIT={audit} N21P_AUDIT={audit} N22_STREAM_LARGEST_MISSES={largest} N22_STREAM_PARTIAL_DEFRAG={partial} GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    try:log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    except Exception as error:
        (target/'build-error.log').write_text(str(error),'utf8');raise
    (target/'build.log').write_text(log,'utf8')
    (target/'build-error.log').unlink(missing_ok=True)
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(k+' '+v in log for k,v in lock['packages'].items())
    sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py','.S','.ld','.lds') or p.name.startswith('Makefile'))]
    generated=[OUT/n for n in ('ui.dat','frontend.dat')]
    generated += [ROOT/'build/nds/n19/generated/ui_native.inc',ROOT/'build/nds/n21/generated/frontend_hash.inc',target/'generated/render_trig.inc']
    report=dict(task='N22',label=label,frontend=True,frontend_fixtures=False,audit=bool(audit),stream_largest_misses=bool(largest),stream_partial_defrag=bool(partial),packages=lock['packages'],
        artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=sources,
        generated_inputs=[artifact(p) for p in generated],build_drivers=[artifact(Path(__file__)),artifact(ROOT/'ports/nds/toolchain_versions.json'),
            *[artifact(ROOT/'ports/nds/tools'/name) for name in ('check_items.py','check_interactions.py','prepare_interactions_fixture.py')]],
        arm7_dependency=artifact(ROOT/'build/nds/n18/arm7/dok_audio_arm7.elf'))
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    if label.startswith('candidate-'):
        evidence=ROOT/'research/evidence/nds/n22/candidates'/label
        for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map','build-manifest.json','build.log'):
            destination=evidence/name;destination.parent.mkdir(parents=True,exist_ok=True)
            assert not destination.exists();shutil.copyfile(target/name,destination)
        for row in report['sources']:
            if Path(row['path']).suffix not in ('.c','.h','.inc','.S','.ld','.lds') and not Path(row['path']).name.startswith('Makefile'):continue
            source=ROOT/row['path'];assert artifact(source)==row
            destination=evidence/'sources'/row['path'];destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
        for row in report['generated_inputs']+report['build_drivers']+[report['arm7_dependency']]:
            source=ROOT/row['path'];assert artifact(source)==row
            destination=evidence/'inputs'/row['path'];destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    print(json.dumps(report['artifacts']))
if __name__=='__main__':build(sys.argv[1])
