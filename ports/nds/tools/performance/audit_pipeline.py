"""Read-only CPU pipeline audit of a SHA-bound ordinary route and optional split run."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[4]


def pin(path):
    raw = path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=len(raw),
                sha256=hashlib.sha256(raw).hexdigest())


def distribution(values):
    values = sorted(values)
    assert values
    return dict(samples=len(values), mean=statistics.mean(values),
                p95=values[min(len(values)-1, int(len(values)*.95))], maximum=values[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--split-target', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT / 'research/evidence/nds/n23')
    assert not output.exists(), 'Use a fresh evidence filename'
    source = args.profile.resolve()
    raw = json.loads(source.read_text('utf8'))
    assert raw['passed']
    manifest = ROOT / 'research/evidence/nds/n23/candidates' / Path(raw['rom']['path']).parent.name / 'build-manifest.json'
    build = json.loads(manifest.read_text('utf8'))
    for key in ('rom', 'elf'):
        item = raw[key]
        archived = manifest.parent / Path(item['path']).name
        actual = pin(archived)
        assert all(actual[k] == item[k] for k in ('bytes', 'sha256'))
        assert item in build['artifacts']
    history = [r for r in raw['history_records'] if r['flags'] & 8 and not r['flags'] & (1|4|32|64)]
    profiles = {r['frame_id']: r for r in raw['profile_rows']}
    assert len(profiles) == len(raw['profile_rows'])
    selected = [profiles[r['frame_id']] for r in history]
    stages = ('simulation', 'geometry', 'sprites', 'discovery', 'overlay', 'effects',
              'submit_including_wait', 'hud', 'map', 'map_snapshot', 'map_raster',
              'map_dma', 'map_publish', 'telemetry', 'cpu_excluding_wait', 'wait_vblank')
    costs = {name: distribution([r[name]*1000/r['timer_hz'] for r in selected]) for name in stages}
    costs['sprites_excluding_discovery'] = distribution([
        (r['sprites']-r['discovery'])*1000/r['timer_hz'] for r in selected])
    hz = selected[0]['timer_hz']
    for name in ('submit_ticks', 'geometry_wait_ticks', 'io_upload_ticks'):
        costs[name] = distribution([r[name]*1000/hz for r in history])
    repeat = [r['geometry']+r['discovery']+r['submit_including_wait'] for r in selected]
    costs['geometry_discovery_submit'] = distribution([x*1000/hz for x in repeat])
    result = dict(task='N23', passed=True, acceptance_eligible=False, reviewer=pin(Path(__file__)),
        profile=pin(source), manifest=pin(manifest), rom=raw['rom'], elf=raw['elf'],
        active_presentations=len(history), costs_ms=costs, metrics=raw['summary']['metrics'],
        one_vblank_ms=1000/raw['summary']['metrics']['refresh_hz'],
        caveats=['discovery is included in sprites; detailed map stages are included in map',
                 'CPU excludes the explicit VBlank wait, but includes synchronous FIFO store time',
                 'final geometry fence time alone cannot exclude FIFO stalls or raster bottlenecks',
                 'means/p95/max from different stages must not be added as a worst-case frame',
                 'costs of this partial route do not prove full-game performance or physical DS FPS'])
    if args.split_target:
        target = args.split_target.resolve()
        split = json.loads((target / 'pipeline-result.json').read_text('utf8'))
        stage = json.loads((target / 'joint-profile/result.json').read_text('utf8'))
        diagnostic = json.loads((target / 'cost-result.json').read_text('utf8'))
        assert split['passed'] and diagnostic['diagnostic_completed']
        assert split['rom_sha256'] == pin(target/'dok_nds_gameplay.nds')['sha256']
        assert split['elf_sha256'] == pin(target/'dok_nds_gameplay.elf')['sha256']
        assert split['rom_sha256'] == stage['rom_sha256']
        rows = {r['ordinal']: r for r in split['rows']}
        groups = {}
        for item in stage['rows']:
            group = groups.setdefault(item['case_id'], [])
            group.append((item, rows[item['frame_id']]))
        split_costs = {}
        for case, pairs in groups.items():
            split_costs[case] = dict(frames=len(pairs), submit_ms={
                name: distribution([r[name]*1000/hz for _, r in pairs])
                for name in ('camera_ticks', 'validation_ticks', 'initial_fence_ticks',
                             'emission_ticks', 'completion_ticks', 'final_fence_ticks')},
                geometry_ms={name: distribution([s[name]*1000/hz for s, _ in pairs])
                    for name in ('geometry_build', 'geometry_cull', 'geometry_merge',
                                 'geometry_other', 'geometry_doors') if name in pairs[0][0]})
        result['split'] = dict(inputs=[pin(target/name) for name in (
            'pipeline-result.json', 'joint-profile/result.json', 'cost-result.json', 'build-manifest.json')],
            cases=split_costs, acceptance_eligible=False,
            caveat='Diagnostic geometry per-cell timers add overhead; these times are not product cadence.')
    output.write_text(json.dumps(result, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(passed=True, active_presentations=len(history), output=pin(output),
        cpu_ms=costs['cpu_excluding_wait'], geometry_discovery_submit_ms=costs['geometry_discovery_submit'],
        split_cases=list(result.get('split', {}).get('cases', {})))))


if __name__ == '__main__':
    main()
