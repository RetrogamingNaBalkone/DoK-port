"""Archive passed ordinary N22 controls without claiming N22 acceptance.

Copies a byte-identical report, its executed Python snapshots and explicit
screenshots. Never copies FAT images, caches, console logs or original payload.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
ARCHIVE=ROOT/'research/evidence/nds/n22/ordinary-controls'


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def source_path(value):
    path=Path(value)
    if not path.is_absolute():path=ROOT/path
    resolved=path.resolve()
    assert resolved.is_relative_to(ROOT/'build/nds/n22') or resolved.is_relative_to(ROOT/'research/evidence/nds/n22')
    assert path.absolute()==resolved,'Source aliases are not permitted'
    assert path.is_file() and not path.is_symlink()
    return resolved


def exact_copy(source,destination):
    resolved=destination.resolve()
    assert resolved.is_relative_to(ARCHIVE.resolve()) and resolved!=ARCHIVE.resolve()
    for parent in (destination,*destination.parents):
        if parent==ROOT:break
        assert not parent.is_symlink() and not parent.is_junction()
    destination.parent.mkdir(parents=True,exist_ok=True)
    raw=source.read_bytes()
    if destination.exists():assert destination.read_bytes()==raw,('Immutable archive differs',destination)
    else:destination.write_bytes(raw)
    return dict(source=pin(source),archive=pin(destination))


def archive(report_path):
    report_path=source_path(report_path)
    r=json.loads(report_path.read_text('utf8'))
    assert r['passed'] and r['completed_playthrough'] and not r.get('bootstrap_only')
    assert r['finish_probe'][3]==10 and r['finish_probe'][31]==0
    assert r['finish_flow'][26]==3 and r['finish_flow'][3]==0
    assert r['level_select_after_finish'][3]==3
    assert r['disk']['passed'] and r['play_audio']['nonzero_frames']>0
    level=r['rows'][0]['level'];language=r['finish_probe'][4]
    assert level in (1,2,3) and language in (1,2)
    out=ARCHIVE/f"{r['rom']['sha256'][:12]}-{'ru' if language==1 else 'en'}-level{level}"
    mappings=[exact_copy(report_path,out/'report.json')]
    snapshots=[entry['snapshot'] for entry in r['source_snapshots']]
    if r.get('archived_policy_launcher_snapshot'):snapshots.append(r['archived_policy_launcher_snapshot'])
    seen={report_path}
    for entry in snapshots:
        path=source_path(entry['path']);assert pin(path)==entry
        if path not in seen:mappings.append(exact_copy(path,out/'sources'/path.name));seen.add(path)
    for group in r.get('screens',[]):
        for entry in group:
            path=source_path(entry['path'])
            assert path.suffix.lower()=='.png' and pin(path)['sha256']==entry['sha256']
            if path not in seen:mappings.append(exact_copy(path,out/'screens'/path.name));seen.add(path)
    manifest=dict(task='N22',ordinary_control_passed=True,N22_accepted=False,
        purpose='Passed ordinary input gameplay control; separate final N22 acceptance and hardware validation remain required',
        level=level,language=language,rom=r['rom'],elf=r['elf'],source_report=pin(report_path),
        archive_tool=pin(Path(__file__)),mappings=mappings)
    encoded=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf8')
    target=out/'manifest.json'
    if target.exists():assert target.read_bytes()==encoded,('Immutable manifest differs',target)
    else:target.write_bytes(encoded)
    print(json.dumps(dict(manifest=pin(target),files=len(mappings),N22_accepted=False)))


def verify(manifest_path):
    path=manifest_path.resolve();assert path.is_relative_to(ARCHIVE.resolve())
    manifest=json.loads(path.read_text('utf8'))
    assert manifest['ordinary_control_passed'] and manifest['N22_accepted'] is False
    for row in manifest['mappings']:
        copy=ROOT/row['archive']['path'];assert pin(copy)==row['archive']
        assert row['source']['bytes']==row['archive']['bytes'] and row['source']['sha256']==row['archive']['sha256']
    print(f"Archive mapping PASS: {len(manifest['mappings'])} files; N22 acceptance not claimed")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--report',type=Path,action='append')
    p.add_argument('--check',type=Path)
    args=p.parse_args()
    if args.check:verify(args.check);return
    reports=args.report or [ROOT/'build/nds/n22/frontend-ru/route-1-level1-v18/report.json',
                           ROOT/'build/nds/n22/frontend-en/route-2-level1-saved-v17/report.json']
    for report in reports:archive(report)

if __name__=='__main__':main()
