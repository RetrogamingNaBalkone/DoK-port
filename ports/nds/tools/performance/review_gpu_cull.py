"""Review the rejected GPU-only static-world clipping experiment, without FPS claims."""
import argparse
import json
from pathlib import Path
from audit_pipeline import ROOT, pin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT / 'research/evidence/nds/n23') and not output.exists()
    targets = [ROOT / 'build/nds/n23' / label for label in ('cull29-cpu-scn-ru', 'cull29-gpu-scn-ru')]
    reports = [[json.loads((p / name).read_text()) for name in
                ('build-manifest.json', 'cost-result.json', 'pipeline-result.json')] for p in targets]
    left, right = reports
    assert left[0]['sources'] == right[0]['sources']
    for key in set(left[0]) | set(right[0]):
        if key == 'generated_inputs':
            normalize = lambda rows: [(Path(r['path']).name, r['bytes'], r['sha256']) for r in rows]
            assert normalize(left[0][key]) == normalize(right[0][key])
        elif key not in ('label', 'feature_selection', 'artifacts', 'gpu_cull'):
            assert left[0][key] == right[0][key], key
    assert not left[0]['gpu_cull'] and right[0]['gpu_cull']
    assert not left[0]['audit'] and left[0]['fine_profile'] and left[0]['compiled_inputs_unchanged']
    assert all(r[1]['diagnostic_completed'] and r[2]['passed'] for r in reports)
    assert len(left[2]['rows']) == len(right[2]['rows']) == 154
    for a, b in zip(left[2]['rows'], right[2]['rows']):
        assert all(a[k] == b[k] for k in ('ordinal', 'camera_x', 'camera_z'))
    images = []
    for source in sorted(targets[0].rglob('*.png')):
        other = targets[1] / source.relative_to(targets[0])
        assert source.read_bytes() == other.read_bytes()
        images.append(dict(control=pin(source), candidate=pin(other)))
    assert len(images) == 13
    cases = {}
    for case, a in left[1]['cases'].items():
        b = right[1]['cases'][case]
        assert a['frames'] == b['frames']
        delta = b['mean_ms']['cpu_excluding_wait'] - a['mean_ms']['cpu_excluding_wait']
        assert delta > 0
        cases[case] = dict(frames=a['frames'], control_ms=a['mean_ms'], candidate_ms=b['mean_ms'], extra_cpu_ms=delta)
    output.write_text(json.dumps(dict(task='N23', passed=True, candidate_accepted=False,
        acceptance_eligible=False, reviewer=pin(Path(__file__)),
        decision='Retain CPU frustum culling. More offscreen quads cost more preparation/effect work and submission than the removed CPU tests saved.',
        reports=[pin(p / name) for p in targets for name in ('build-manifest.json', 'cost-result.json', 'pipeline-result.json')],
        exact_image_pairs=images, cases=cases,
        control_quads=sum(r['quads'] for r in left[2]['rows']), candidate_quads=sum(r['quads'] for r in right[2]['rows']),
        limitation='Fine diagnostic timings only, not product FPS or proof of a physical GPU throughput ceiling. Experimental source is retained with frozen candidates, not promoted.'), indent=2) + '\n')
    print(json.dumps(dict(passed=True, candidate_accepted=False, exact_images=len(images), report=pin(output))))


if __name__ == '__main__':
    main()
