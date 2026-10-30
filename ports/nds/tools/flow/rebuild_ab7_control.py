"""Recover the exact N22 ab7 ROM for a recorded real-input failure.

This isolated source/build tree is a justified regression control. Current
production sources/targets and all older accepted checkpoints stay untouched.
"""
import hashlib,json,os,shutil,subprocess,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact,run
EXPECTED='ab7a6040e4cc1a0c5aef469b5743a0a4a7578861e4684969b9de87720b50f693'
E=ROOT/'research/evidence/nds/n22/control-ab7'
TREE=ROOT/'build/nds/n22/control-ab7-source'
OUT=ROOT/'build/nds/n22/control-ab7-ru'

def copy(source,destination):
    assert destination.resolve().is_relative_to((ROOT/'build/nds/n22').resolve())
    assert not any(p.is_symlink() or p.is_junction() for p in (source,*source.parents,destination,*destination.parents) if p.exists())
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    assert source.read_bytes()==destination.read_bytes()

def main():
    E.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
    current=[p for directory in ('core','render','ui','platform','include') for p in (ROOT/'ports/nds'/directory).rglob('*') if p.is_file()]
    current += list((ROOT/'ports/nds').glob('Makefile*'))
    before=[artifact(p) for p in current]
    frozen=E/'recovered-source'
    recovery_manifest=E/'recovered-source.json'
    if recovery_manifest.exists():
        saved=json.loads(recovery_manifest.read_text('utf8'))
        for row in saved['files']:
            source=ROOT/row['archive']['path'];assert artifact(source)==row['archive']
            copy(source,TREE/row['relative'])
    else:
        for path in current:copy(path,TREE/path.relative_to(ROOT))
    baseline=ROOT/'research/evidence/nds/n22/retry-overlap-baseline'
    manifest=json.loads((baseline/'manifest.json').read_text('utf8'))
    overrides=[]
    for row in manifest['files']:
        if row['path'].endswith('test_retry_overlap.c'):continue
        source=baseline/row['path']
        assert hashlib.sha256(source.read_bytes()).hexdigest()==row['sha256']
        if not recovery_manifest.exists():copy(source,TREE/row['path'])
        overrides.append(dict(original=row,snapshot=artifact(source)))
    header=ROOT/'research/evidence/nds/n21p/final-sources/ports/nds/include/dok/collision.h'
    if not recovery_manifest.exists():
        git_header=subprocess.run(['git','show','HEAD:ports/nds/include/dok/collision.h'],cwd=ROOT,capture_output=True,check=True).stdout
        assert header.read_bytes()==git_header,'Archived pre-overlap collision header differs from repository baseline'
        copy(header,TREE/'ports/nds/include/dok/collision.h')
    overrides.append(dict(snapshot=artifact(header)))
    generated=[]
    for task in ('n19','n21'):
        source_root=TREE if recovery_manifest.exists() else ROOT
        for source in (source_root/f'build/nds/{task}/generated').glob('*.inc'):
            if source_root==ROOT:copy(source,TREE/source.relative_to(ROOT))
            generated.append(artifact(source))
    arm7=(TREE if recovery_manifest.exists() else ROOT)/'build/nds/n18/arm7/dok_audio_arm7.elf'
    if not recovery_manifest.exists():copy(arm7,TREE/arm7.relative_to(ROOT))
    trig=(E/'recovered-inputs/render_trig.inc') if recovery_manifest.exists() else ROOT/'build/nds/n07/generated/render_trig.inc'
    copy(trig,OUT/'generated/render_trig.inc');generated.append(artifact(trig))
    msys=lambda path:'/'+path.as_posix()[0].lower()+path.as_posix()[2:]
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+msys(TREE)+"'\nmake -C ports/nds -f Makefile.n22 OUTDIR="+msys(OUT)+" SPRITE_LANGUAGE=1 GAMEPLAY_FRONTEND=1 FRONTEND_FIXTURES=0 SPRITE_AUDIT=0 N21P_AUDIT=0 GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n"
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    try:log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    except Exception as error:
        (OUT/'build-error.log').write_text(str(error),'utf8');raise
    (OUT/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(k+' '+v in log for k,v in lock['packages'].items())
    rom=artifact(OUT/'dok_nds_gameplay.nds')
    assert rom['sha256']==EXPECTED,('Recovered ROM differs from the actual failure control',rom)
    assert before==[artifact(p) for p in current]
    if not recovery_manifest.exists():
        archived_trig=E/'recovered-inputs/render_trig.inc';archived_trig.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(trig,archived_trig)
        saved=[]
        for source in sorted(TREE.rglob('*')):
            if not source.is_file():continue
            relative=source.relative_to(TREE);destination=frozen/relative
            destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
            assert source.read_bytes()==destination.read_bytes()
            saved.append(dict(relative=relative.as_posix(),archive=artifact(destination)))
        recovery_manifest.write_text(json.dumps(dict(task='N22',passed=True,expected_rom_sha256=EXPECTED,files=saved),indent=2)+'\n','utf8')
    report=dict(task='N22',passed=True,purpose='Exact old production ROM recovery for L2 V14 recorded failure, not a candidate or acceptance',
        expected_rom_sha256=EXPECTED,rom=rom,elf=artifact(OUT/'dok_nds_gameplay.elf'),map=artifact(OUT/'dok_nds_gameplay.map'),
        driver=artifact(Path(__file__)),packages=lock['packages'],overrides=overrides,generated=generated,arm7_dependency=artifact(arm7),
        current_sources_before=before,current_sources_after=[artifact(p) for p in current],
        recovery_sources=[artifact(p) for p in sorted((TREE/'ports/nds').rglob('*')) if p.is_file()])
    (E/'rebuild.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,rom=rom,isolated_source=str(TREE))))

if __name__=='__main__':main()
