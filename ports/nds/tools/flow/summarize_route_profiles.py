"""Aggregate pinned ordinary-route profiles without copying raw frame arrays."""
from __future__ import annotations
import argparse
import json
from pathlib import Path

from route_profile import ROOT, pin
from archive_route_profile import verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, default=Path('analysis/nds-n22-route-performance.json'))
    args = parser.parse_args()
    runs = []
    for path in args.manifest:
        path = ROOT/path
        manifest = json.loads(path.read_text('utf8'))
        verify(manifest['summary'])
        summary = json.loads((ROOT/manifest['summary']['path']).read_text('utf8'))
        assert summary['summary']['active_presentations'] == summary['summary']['profile_coverage']
        runs.append(dict(archive=pin(path), **summary))
    overview = {}
    for run in runs:
        summary, metrics = run['summary'], run['summary']['metrics']
        label = Path(run['archive']['path']).parent.name
        overview[label] = dict(route_passed=run['route_passed'], completed_playthrough=run['completed_playthrough'],
            active_presentations=summary['active_presentations'], profile_coverage=summary['profile_coverage'],
            average_fps=metrics['virtual_fps'], minimum_one_second_fps=metrics['minimum_window_cadence_fps'],
            fraction_over_two_vblanks=metrics['fraction_over_two'], maximum_vblanks=metrics['max'],
            budget_passed=metrics['budget_passed'], rom_sha256=run['rom']['sha256'])
    output = dict(task='N22', scope='Ordinary-route performance evidence for optimization in N23; emulator only',
        measured_overview=overview,
        runs=runs, ordinary_runs_completed=sum(run['completed_playthrough'] for run in runs),
        all_measured_budgets_passed=all(run['summary']['metrics']['budget_passed'] for run in runs),
        acceptance='These measurements do not establish N22 functional completion or N23 optimization acceptance.',
        limitations=['Prefix runs cover only their recorded traversal.',
            'N21P prototype performance is not a substitute for measurements throughout complete levels.',
            'Discovery is contained in sprites, detailed map stages in map; do not add these twice.',
            'CPU render_one timing omits frontend/flow/probe overhead included in presentation cadence.',
            'No claim about physical DS/DS Lite FPS; hardware and listening remain N24.'],
        generator=pin(Path(__file__).resolve()))
    target = ROOT/args.output
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(output, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(report=pin(target), runs=len(runs),
                         all_measured_budgets_passed=output['all_measured_budgets_passed'])))


if __name__ == '__main__':
    main()
