"""Freeze explicit N22 acceptance inputs without copying FAT/cache trees."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[4]
E=ROOT/'research/evidence/nds/n22'
COMPILED_SUFFIXES={'.c','.h','.inc','.S','.ld','.lds'}
EVIDENCE_SUFFIXES=COMPILED_SUFFIXES|{'.json','.jsonl','.png','.wav','.py','.md',
                                  '.bin','.nds','.elf','.map','.log','.txt','.su',
                                  '.asm','.java','.ps1','.tsv'}


def evidence_file(path):
    return path.suffix in EVIDENCE_SUFFIXES or path.name.startswith('Makefile')

def pin(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def same_content(path,row):
    actual=pin(path)
    assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256']),('Frozen input mismatch',path,row)

def candidate_inputs(relative):
    target=ROOT/relative
    assert target.resolve().is_relative_to((ROOT/'build/nds/n22').resolve())
    frozen=E/'candidates'/target.name
    manifest=json.loads((frozen/'build-manifest.json').read_text('utf8'))
    assert manifest['task']=='N22' and manifest['label']==target.name and not manifest['frontend_fixtures']
    assert manifest['stream_partial_defrag'] is True
    pairs=[]
    for row in manifest['sources']:
        path=Path(row['path'])
        if path.suffix in COMPILED_SUFFIXES or path.name.startswith('Makefile'):
            source=frozen/'sources'/path;same_content(source,row)
            pairs.append((row,source))
    for row in manifest['generated_inputs']+manifest['build_drivers']:
        source=frozen/'inputs'/row['path'];same_content(source,row)
        pairs.append((row,source))
    # check_flow imports these modules even though their host-test entry points
    # do not execute. Early candidate freezers pinned but did not copy them.
    copied={row['path'] for row,_ in pairs}
    for name in ('check_items.py','check_interactions.py','prepare_interactions_fixture.py'):
        relative='ports/nds/tools/'+name
        if relative in copied:continue
        row=next(row for row in manifest['sources'] if row['path']==relative)
        source=ROOT/relative;same_content(source,row);pairs.append((row,source))
    # ARM7 is a separately accepted dependency, pinned by its original manifest.
    # Initial candidate freezers did not duplicate it; require exact content.
    row=manifest['arm7_dependency'];source=frozen/'inputs'/row['path']
    if not source.exists():source=ROOT/row['path']
    same_content(source,row)
    pairs.append((row,source))
    for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map','build-manifest.json','build.log'):
        assert (target/name).read_bytes()==(frozen/name).read_bytes(),('Current candidate differs',target/name)
        if name.endswith(('.nds','.elf','.map')):
            assert pin(target/name) in manifest['artifacts']
    return target,frozen,manifest,pairs

def freeze(source,category):
    assert source.resolve().is_relative_to(ROOT.resolve())
    destination=E/'final-archive'/category/source.relative_to(ROOT)
    assert destination.resolve().is_relative_to(E.resolve())
    assert not any(p.is_symlink() or p.is_junction() for p in (source,*source.parents,destination,*destination.parents) if p.exists())
    before=pin(source)
    destination.parent.mkdir(parents=True,exist_ok=True)
    if destination.exists():
        assert destination.read_bytes()==source.read_bytes(),('Refuse to overwrite frozen input',destination)
    else:shutil.copyfile(source,destination)
    after=pin(destination)
    assert before['sha256']==after['sha256'] and before['bytes']==after['bytes']
    return dict(source=before,archive=after)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--acceptance',type=Path,default=E/'root-acceptance.json')
    p.add_argument('--verify-inputs-only',nargs='+',metavar='TARGET',help='Read-only verification of immutable candidate compiled inputs')
    args=p.parse_args()
    if args.verify_inputs_only:
        rows=[]
        for relative in args.verify_inputs_only:
            target,_,_,pairs=candidate_inputs(relative)
            rows.append(dict(target=target.relative_to(ROOT).as_posix(),compiled_inputs=len(pairs)))
        print(json.dumps(dict(passed=True,candidates=rows)));return
    args.acceptance=args.acceptance.resolve()
    acceptance=json.loads(args.acceptance.read_text('utf8'))
    assert acceptance['task']=='N22' and acceptance['passed'] is True
    assert acceptance['ordinary_playthrough_completed'] is True
    # This complement records final tools/fixtures for reproduction. It is NOT
    # the source provenance of a candidate built before those tools changed.
    final_tooling_sources=[]
    for source in sorted((ROOT/'ports/nds').rglob('*')):
        if source.is_file() and (source.suffix in ('.c','.h','.inc','.py','.ps1','.java','.S','.ld','.lds','.json') or source.name.startswith('Makefile')):
            final_tooling_sources.append(freeze(source,'final-tooling'))
    builds=[];compiled_inputs=[]
    for relative in acceptance['targets']:
        target,frozen,manifest,pairs=candidate_inputs(relative)
        for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map','build-manifest.json','build.log'):
            source=target/name
            if name.endswith(('.nds','.elf','.map')):
                assert pin(source) in manifest['artifacts'],('Artifact manifest mismatch',source)
            builds.append(freeze(source,'builds'))
        for row,source in pairs:
            compiled_inputs.append(dict(candidate=target.name,original_pin=row,
                frozen=freeze(source,'compiled-inputs/'+target.name)))
    reports=[]
    for relative in acceptance['reports']:
        source=ROOT/relative;report=json.loads(source.read_text('utf8'))
        assert report['passed'] is True,relative
        reports.append(freeze(source,'reports'))
    for relative in acceptance.get('evidence_artifacts',[]):
        source=ROOT/relative
        assert evidence_file(source),source
        reports.append(freeze(source,'evidence'))
    result=dict(task='N22',passed=True,acceptance=freeze(args.acceptance,'acceptance'),
                compiled_inputs=compiled_inputs,final_tooling_sources=final_tooling_sources,
                provenance='Candidate compiled inputs are authoritative; final-tooling is a separately labelled later complement.',
                builds=builds,reports=reports)
    (E/'final-archive.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,compiled_inputs=len(compiled_inputs),final_tooling_sources=len(final_tooling_sources),builds=len(builds),reports=len(reports))))

if __name__=='__main__':main()
