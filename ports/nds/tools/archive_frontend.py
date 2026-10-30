"""Freeze accepted N21 sources, generated inputs and explicitly listed reports."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/nds/n21'

def artifact(path):
    path=Path(path)
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def copy(source,destination):
    assert destination.resolve().is_relative_to(E.resolve())
    assert not any(p.is_symlink() or p.is_junction() for p in (destination,*destination.parents) if p.exists())
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,destination)
    a,b=artifact(source),artifact(destination)
    assert a['bytes']==b['bytes'] and a['sha256']==b['sha256']
    return dict(source=a,archive=b)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--acceptance',type=Path,default=E/'root-acceptance.json')
    args=parser.parse_args()
    acceptance=json.loads(args.acceptance.read_text('utf8'))
    assert acceptance['passed'] is True
    sources=[]
    for p in sorted((ROOT/'ports/nds').rglob('*')):
        if p.is_file() and (p.suffix in ('.c','.h','.inc','.py','.ps1','.java','.S','.ld','.lds','.json') or p.name.startswith('Makefile')):
            sources.append(copy(p,E/'final-sources'/p.relative_to(ROOT)))
    builds=[]
    for relative in acceptance['targets']:
        target=ROOT/relative
        assert target.resolve().is_relative_to((ROOT/'build/nds/n21').resolve())
        for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map','build-manifest.json','build.log'):
            builds.append(copy(target/name,E/'release'/target.name/name))
    generated=[]
    for relative in ('build/nds/n21/frontend.dat','build/nds/n21/ui.dat',
                     'build/nds/n21/generated/frontend_hash.inc','build/nds/n21/generated/frontend_visual.inc',
                     'build/nds/n21/visual-popal-ru.bin','build/nds/n21/visual-popal-en.bin',
                     'build/nds/n19/generated/ui_native.inc','build/nds/n07/generated/render_trig.inc'):
        p=ROOT/relative
        generated.append(copy(p,E/'release/generated'/p.relative_to(ROOT/'build/nds')))
    reports=[]
    for relative in acceptance['reports']:
        path=ROOT/relative
        report=json.loads(path.read_text('utf8'))
        assert report['passed'] is True,relative
        # Runtime directories contain FAT images and core caches that are not
        # evidence artifacts. Keep the explicit JSON, captures and WAV only.
        for p in sorted(path.parent.rglob('*')):
            if p.is_file() and p.suffix in ('.json','.png','.wav'):
                reports.append(copy(p,E/'release/reports'/p.relative_to(ROOT/'build/nds/n21')))
    preserved=E/'superseded/main.c'
    old=E/'superseded/manifest.json'
    previous=json.loads(old.read_text('utf8'))
    assert artifact(preserved)['sha256']==previous['sha256']
    supersession=dict(task='N21',reason=previous['reason'],changes=[dict(
        path='ports/nds/platform/gameplay_probe/main.c',archive=artifact(preserved),
        original_sha256=previous['sha256'],current_sha256=artifact(ROOT/'ports/nds/platform/gameplay_probe/main.c')['sha256'])])
    (E/'supersession.json').write_text(json.dumps(supersession,indent=2)+'\n','utf8')
    out=dict(task='N21',passed=True,acceptance=artifact(args.acceptance),
             sources=sources,builds=builds,generated=generated,reports=reports,
             supersession=artifact(E/'supersession.json'))
    (E/'final-archive.json').write_text(json.dumps(out,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,sources=len(sources),builds=len(builds),reports=len(reports))))

if __name__=='__main__':main()
