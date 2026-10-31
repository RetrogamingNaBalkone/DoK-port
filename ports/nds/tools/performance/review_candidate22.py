"""Verify C22 evidence and report its remaining gates without accepting N23."""
import argparse
import json
from pathlib import Path
import shutil
from audit_pipeline import ROOT, pin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT / 'research/evidence/nds/n23') and not output.exists()
    archive = ROOT / 'research/evidence/nds/n23/review26/manifest.json'
    index = json.loads(archive.read_text())
    mappings = {r['original']['path']: r['archive'] for r in index['mappings']}
    for original, frozen in mappings.items():
        assert pin(ROOT / frozen['path']) == frozen
        assert frozen['sha256'] == next(r['original']['sha256'] for r in index['mappings'] if r['original']['path'] == original)

    def read_frozen(path):
        row = mappings[path]
        assert pin(ROOT / row['path']) == row
        return json.loads((ROOT / row['path']).read_text()), row

    proof_names = ('static17-host.json', 'relative20-math.json', 'simplification22-review.json')
    proofs = []
    retained = []
    inputs_folder = output.parent / (output.stem + '-inputs')
    assert not inputs_folder.exists()
    inputs_folder.mkdir()

    def retain(row):
        source = ROOT / row['path']
        assert pin(source) == row
        destination = inputs_folder / (row['sha256'] + '-' + source.name)
        if destination.exists():
            assert destination.read_bytes() == source.read_bytes()
        else:
            shutil.copyfile(source, destination)
        retained.append(dict(original=row, archive=pin(destination)))

    for name in proof_names:
        path = output.parent / name
        proof = json.loads(path.read_text())
        assert proof['passed']
        proofs.append(pin(path))
        for key in ('sources', 'historical_sources'):
            for row in proof.get(key, []):
                retain(row)
        for key in ('derived_harness', 'driver', 'backend', 'generated', 'reviewer'):
            if key in proof:
                retain(proof[key])
    for source in sorted((ROOT / 'ports/nds/tools/performance').glob('*.py')):
        if source.name in ('check_static_geometry.py', 'check_relative_matrices.py',
                           'test_hud_palette_observer.py', 'review_simplification.py', Path(__file__).name):
            retain(pin(source))
    retain(pin(ROOT / 'ports/nds/tools/joint_hud_oracle.py'))

    matrix = []
    for language in ('ru', 'en'):
        label = 'candidate22-scn-' + language
        reports = {}
        report_pins = []
        manifest_path = ROOT / 'research/evidence/nds/n23/candidates' / label / 'build-manifest.json'
        manifest = json.loads(manifest_path.read_text())
        assert manifest['compiled_inputs_unchanged']
        assert manifest['static_geometry'] and manifest['simple_quads'] and manifest['relative_matrices'] == 1
        assert not manifest['fine_profile'] and not manifest['pipeline_profile']
        for kind in ('emulator', 'audio', 'hud', 'map', 'joint-profile'):
            report, row = read_frozen(f'build/nds/n23/{label}/{kind}/result.json')
            assert report['passed']
            reports[kind] = report
            report_pins.append(row)
            rom = ROOT / 'research/evidence/nds/n23/candidates' / label / 'dok_nds_gameplay.nds'
            assert report.get('rom_sha256', pin(rom)['sha256']) == pin(rom)['sha256']
        frame_report = reports['emulator']
        assert len(frame_report['cases']) == 63
        assert len(reports['joint-profile']['rows']) == 12880
        assert not reports['joint-profile']['missing_frames']
        assert not frame_report['last_observation']['disp3d_sticky'] and not frame_report['last_observation']['gxstat_sticky']
        metrics = frame_report['history']
        assert metrics['intervals'] == 6637 and metrics['budget_passed'] and not metrics['goal_60_passed']
        matrix.append(dict(label=label, manifest=pin(manifest_path), reports=report_pins,
            normal={k: metrics[k] for k in ('intervals', 'virtual_fps', 'fraction_over_one',
                'fraction_over_two', 'minimum_window_cadence_fps', 'budget_passed', 'goal_60_passed')}))

    routes = []
    for name in ('c22-level1-complete01', 'c22-level3-prefix23-negative01'):
        path = ROOT / 'research/evidence/nds/n23/route-profiles' / name
        manifest = json.loads((path / 'manifest.json').read_text())
        assert pin(ROOT / manifest['summary']['path']) == manifest['summary']
        for row in manifest['artifact_mappings']:
            assert pin(ROOT / row['archive']['path']) == row['archive']
        summary = json.loads((path / 'summary.json').read_text())
        report = json.loads((path / 'run/report.json').read_text())
        if 'level1' in name:
            assert summary['completed_playthrough'] and summary['route_passed']
        else:
            assert not summary['completed_playthrough'] and report['verified_physical_prefix']['passed']
            assert report['max_naturally_active_ai'] == 56
        routes.append(dict(archive=pin(path / 'manifest.json'), summary=manifest['summary'],
            completed=summary['completed_playthrough'], metrics=summary['summary']['metrics'],
            max_naturally_active_ai=report.get('max_naturally_active_ai')))

    live, live_pin = read_frozen('build/nds/n23/candidate22-en/joint-frontend/live/result.json')
    assert live['passed'] and live['steady_metrics']['budget_passed'] and not live['steady_metrics']['missing_profiles']
    retain(pin(output.parent / 'c22-map-correlation.json'))
    result = dict(task='N23', passed=True, n23_accepted=False, n24_started=False,
        scope='Evidence integrity and completed C22 subset only; pending gates remain mandatory.',
        reviewer=pin(Path(__file__)), archive=pin(archive), verified_archive_files=len(mappings),
        proofs=proofs, retained_inputs=retained, matrix=matrix, routes=routes,
        english_live=dict(report=live_pin, metrics=live['steady_metrics'], touch=live['transition_latency_summary']['touch']),
        pending=['Natural >=61 actual active AI', 'Current complete ordinary LEVEL2 and LEVEL3',
                 'Final integrated memory/audio/latency review and playtest package'],
        deferred_to_n24=['Physical DS/DS Lite FPS/compatibility', 'Listening', 'Physical stack high-water'])
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(passed=True, n23_accepted=False, files=len(mappings), report=pin(output))))


if __name__ == '__main__':
    main()
