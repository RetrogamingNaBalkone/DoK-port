"""Freeze an ordinary route/profile and derive an inspectable N23 baseline.

The original reports remain byte-exact, including their original build paths.
The manifest maps each retained build artifact to its durable research copy.
No emulator system files or disk images are duplicated.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from types import SimpleNamespace

from route_profile import ROOT, pin, summarize


def verify(item):
    actual = pin(ROOT/item['path'])
    assert actual == item, (actual, item)


def durable_candidate(item):
    """Reuse the frozen candidate so profile verification survives build cleanup."""
    original = ROOT/item['path']
    target = ROOT/'research/evidence/nds/n22/candidates'/original.parent.name/original.name
    archived = pin(target)
    assert (archived['bytes'], archived['sha256']) == (item['bytes'], item['sha256'])
    return dict(original=item, archive=archived)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--verify', type=Path, help='Verify an existing frozen manifest without rewriting it')
    args = parser.parse_args()
    if args.verify:
        manifest = json.loads((ROOT/args.verify).read_text('utf8'))
        items = [manifest['summary']]
        external = manifest.get('external_artifact_mappings')
        if external is None:
            # Original archives predate explicit durable mappings. Candidate
            # labels and content hashes determine the existing frozen copy.
            external = [durable_candidate(item) for item in manifest['pinned_external_artifacts']]
        for mapping in external:
            assert mapping['original']['sha256'] == mapping['archive']['sha256']
            assert mapping['original']['bytes'] == mapping['archive']['bytes']
            items.append(mapping['archive'])
        for mapping in manifest['artifact_mappings']+manifest['derivation_inputs']:
            assert mapping['original']['sha256'] == mapping['archive']['sha256']
            assert mapping['original']['bytes'] == mapping['archive']['bytes']
            items.append(mapping['archive'])
        for item in items:
            verify(item)
        print(json.dumps(dict(passed=True, verified_pins=len(items), manifest=pin(ROOT/args.verify),
                              durable_external_artifacts=external)))
        return
    if not args.report or not args.destination:
        parser.error('Use --report with --destination, or --verify')
    source = (ROOT/args.report).resolve()
    destination = (ROOT/args.destination).resolve()
    destination.relative_to(ROOT/'research/evidence/nds/n22/route-profiles')
    assert not destination.exists(), f'Use a new archive destination: {destination}'
    report = json.loads(source.read_text('utf8'))
    raw_pin = report['performance_report']
    verify(raw_pin)
    raw = json.loads((ROOT/raw_pin['path']).read_text('utf8'))
    for name in ('rom', 'elf'):
        verify(report[name])
        assert report[name] == raw[name]
    assert raw['passed'], 'Incomplete profile coverage'
    # The accepted metrics implementation must equal the run-time snapshot.
    # route_profile.py itself can gain a new derived summary after the run;
    # its original snapshot is still retained and separately identified.
    for item in raw['observer_inputs']:
        if item['source']['path'] in ('ports/nds/tools/run_joint_frontend_emulator.py',
                                      'ports/nds/tools/render_metrics.py'):
            verify(item['source'])
        verify(item['snapshot'])
    derived = dict(report)
    derived.update(history_records=raw['history_records'], profile_rows=raw['profile_rows'],
                   resource_rows=raw.get('resource_rows', []))
    observer = SimpleNamespace(report=derived, av=SimpleNamespace(timing=SimpleNamespace(fps=report['video_refresh_hz'])))
    summary = summarize(observer)
    assert json.loads(json.dumps(summary['metrics'])) == raw['summary']['metrics']
    files = [p for p in source.parent.iterdir() if p.is_file() and p.suffix in ('.json', '.png')]
    files.extend(p for p in (source.parent/'sources').rglob('*') if p.is_file())
    files = sorted(set(files))
    destination.mkdir(parents=True)
    mappings = []
    for original in files:
        target = destination/'run'/original.relative_to(source.parent)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
        mappings.append(dict(original=pin(original), archive=pin(target)))
    tools = []
    for original in (Path(__file__).resolve(), Path(__file__).with_name('route_profile.py').resolve()):
        target = destination/'derived-sources'/original.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
        tools.append(dict(original=pin(original), archive=pin(target)))
    output = dict(task='N22', scope='Measured ordinary-route baseline for N23; emulator only',
        completed_playthrough=bool(report.get('completed_playthrough')),
        route_passed=report['passed'], profile_coverage_passed=raw['passed'],
        rom=raw['rom'], elf=raw['elf'], core=report['core'],
        route_cells_reached=report.get('route_cells_reached'),
        archived_report=pin(destination/'run'/source.name),
        archived_profile=pin(destination/'run'/Path(raw_pin['path']).name),
        summary=summary)
    path = destination/'summary.json'
    path.write_text(json.dumps(output, indent=2)+'\n', 'utf8')
    manifest = dict(schema=1, task='N22', artifact_mappings=mappings, derivation_inputs=tools, summary=pin(path),
        pinned_external_artifacts=[raw['rom'], raw['elf']],
        external_artifact_mappings=[durable_candidate(raw['rom']), durable_candidate(raw['elf'])],
        limitations=['Prefix runs do not establish full-level cadence or completion.',
                      'Physical DS/DS Lite and listening remain N24.',
                      'Derived summary adds disjoint slow windows; original run reports are unchanged.'])
    (destination/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(manifest=pin(destination/'manifest.json'), summary=pin(path), artifacts=len(mappings),
        average_fps=summary['metrics']['virtual_fps'], budget_passed=summary['metrics']['budget_passed'])))


if __name__ == '__main__':
    main()
