"""Archive the accepted N20 artifacts and keep earlier accepted pins resolvable."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/nds/n20'
def artifact(p):
    return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,
        sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def copy(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    assert artifact(source)['sha256']==artifact(destination)['sha256']
    return dict(source=artifact(source),archive=artifact(destination))
def main():
    for lang in ('ru','en'):
        target=ROOT/f'build/nds/n20/final-release-{lang}'
        for name in ('map','hud','audio','emulator'):
            assert json.loads((target/name/'result.json').read_text('utf8'))['passed']
        assert json.loads((target/'audio/title-gate.json').read_text('utf8'))['passed']
    assert json.loads((ROOT/'build/nds/n20/combat-touch-ru/map/combat-result.json').read_text('utf8'))['passed']
    assert json.loads((ROOT/'build/nds/n20/title-smoke-ru/audio/title-gate.json').read_text('utf8'))['passed']
    changes=json.loads((E/'supersession.json').read_text('utf8'))
    accepted=json.loads((ROOT/'build/nds/n19/final-release-ru/build-manifest.json').read_text('utf8'))
    pins={p['path']:p['sha256'] for p in accepted['sources']}
    for row in changes['changes']:
        assert artifact(ROOT/row['archive'])['sha256']==row['original_sha256']
        if row['path'] in pins:assert row['original_sha256']==pins[row['path']]
        row['current_sha256']=artifact(ROOT/row['path'])['sha256']
    (E/'supersession.json').write_text(json.dumps(changes,indent=2)+'\n','utf8')
    sources=[]
    for p in sorted((ROOT/'ports/nds').rglob('*')):
        if p.is_file() and (p.suffix in ('.c','.h','.inc','.py','.ps1') or p.name.startswith('Makefile')):
            sources.append(copy(p,E/'final-sources'/p.relative_to(ROOT)))
    exporter=ROOT/'tools/ghidra/ExportNdsMap.java'
    sources.append(copy(exporter,E/'final-sources'/exporter.relative_to(ROOT)))
    records=[]
    for lang in ('ru','en'):
        target=ROOT/f'build/nds/n20/final-release-{lang}'
        for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map','build-manifest.json','build.log'):
            records.append(copy(target/name,E/'release'/lang/name))
        for p in target.glob('*.log'):
            if p.name!='build.log':records.append(copy(p,E/'release'/lang/p.name))
        for folder in ('map','hud','audio','emulator'):
            for p in sorted((target/folder).rglob('*')):
                if p.is_file() and p.suffix in ('.json','.png','.log'):
                    records.append(copy(p,E/'release'/lang/folder/p.relative_to(target/folder)))
    generated=[copy(ROOT/'build/nds/n20'/name,E/'release'/name) for name in ('scene.dat','ui.dat')]
    for name in ('scene-combat-touch.dat','scene-title.dat'):
        generated.append(copy(ROOT/'build/nds/n20'/name,E/'release'/name))
    generated.append(copy(ROOT/'build/nds/n19/generated/ui_native.inc',E/'release/generated/ui_native.inc'))
    target=ROOT/'build/nds/n20/combat-touch-ru'
    for p in target.glob('*.log'):records.append(copy(p,E/'release/combat-touch-ru'/p.name))
    for folder in ('map','hud','audio','emulator'):
        for p in sorted((target/folder).rglob('*')):
            if p.is_file() and p.suffix in ('.json','.png','.log'):
                records.append(copy(p,E/'release/combat-touch-ru'/folder/p.relative_to(target/folder)))
    target=ROOT/'build/nds/n20/title-smoke-ru'
    for p in target.glob('*.log'):records.append(copy(p,E/'release/title-smoke-ru'/p.name))
    for folder in ('audio','emulator'):
        for p in sorted((target/folder).rglob('*')):
            if p.is_file() and p.suffix in ('.json','.png','.log'):
                records.append(copy(p,E/'release/title-smoke-ru'/folder/p.relative_to(target/folder)))
    report=dict(task='N20',passed=True,sources=sources,reports=records,generated=generated,
        supersession=artifact(E/'supersession.json'))
    (E/'final-archive.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,sources=len(sources),reports=len(records))))
if __name__=='__main__':main()
