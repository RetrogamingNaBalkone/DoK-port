"""Pin the completed N22 EN LEVEL2 control, without running an emulator."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
RUN = ROOT / 'build/nds/n22/candidate-partial02-en/route-2-level2-original-en-l2-v3'
OUT = ROOT / 'research/evidence/nds/n22/en-level2-v3'


def pin(path):
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    data = path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
                bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def check_pin(item):
    actual = pin(item['path'])
    assert actual['sha256'] == item['sha256'], item['path']
    assert actual['bytes'] == item.get('bytes', actual['bytes']), item['path']


def main():
    report = json.loads((RUN / 'report.json').read_text('utf8'))
    assert report['passed'] and report['completed_playthrough']
    assert report['rom']['sha256'] == '7115d1c2c68472d1cedb29cf441fe5baac45625c46ad6fbe6f94ab57c8b4319b'
    assert report['elf']['sha256'] == 'a18030a9083405bafce626970a22963a4f34cbd2c15a7a2f4383ddf46fe08246'
    sources = [x['snapshot'] for x in report['source_snapshots'] + report['profile_observer_inputs']]
    commands = sorted(RUN.glob('command-*.json'), key=lambda p: int(p.stem.split('-')[-1]))
    checkpoints = sorted(RUN.glob('checkpoint-*.json'), key=lambda p: int(p.stem.split('-')[-1]))
    for path in commands:
        sources.append(json.loads(path.read_text('utf8'))['host_controller'])
    sources = list({x['path']: x for x in sources}.values())
    for item in sources + [report['rom'], report['elf'], report['core'], report['performance_report']]:
        check_pin(item)
    assert len(commands) == len(checkpoints) == 75
    assert [json.loads(p.read_text('utf8'))['checkpoint'] for p in commands] == list(range(1, 76))
    rows, inputs = report['rows'], report['input_steps']
    assert len(rows) == 14608 and all(a['presented'] < b['presented'] for a, b in zip(rows, rows[1:]))
    assert all(a['video_frame'] <= b['video_frame'] for a, b in zip(inputs, inputs[1:]))
    assert all(len(s['buttons']) == len(set(s['buttons'])) and all(0 <= k < 12 for k in s['buttons']) for s in inputs)
    assert report['finish_probe'][3] == 10 and report['finish_flow'][26] == 3
    assert report['level_select_after_finish'][3] == 3 and report['finish_flow'][3] == 0
    assert report['disk']['passed'] and report['disk']['changed_files'] == []
    assert report['last_renderer']['error_bits'] == report['last_renderer']['disp3d_sticky'] == report['last_renderer']['gxstat_sticky'] == 0
    retries = [(a, b) for a, b in zip(rows, rows[1:]) if b['retries'] > a['retries']]
    assert len(retries) == 1 and retries[0][1]['keys'] == 7
    manual = []
    for path in checkpoints:
        cp = json.loads(path.read_text('utf8'))
        result = cp['reason'].get('manual_result')
        if result:
            manual.append(dict(checkpoint=cp['ordinal'], **result))
    files = set(commands + checkpoints + list(RUN.glob('*.png')) + list((RUN / 'sources').glob('*')))
    files.update([RUN / 'report.json', RUN / 'runtime-profile.json', Path(__file__).resolve()])
    manifest = dict(task='N22', scope='Completed ordinary EN LEVEL2 source/input/probe/screen provenance',
                    files=[pin(p) for p in sorted(files)])
    OUT.mkdir(parents=True, exist_ok=True)
    manifest_path = OUT / 'provenance.json'
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', 'utf8')
    summary = dict(task='N22', passed=True, completed_playthrough=True,
        scope='Ordinary EN LEVEL2 on immutable candidate02; no whole-N22 or N23 performance acceptance',
        run=RUN.relative_to(ROOT).as_posix(), report=pin(RUN / 'report.json'),
        provenance=pin(manifest_path), rom=report['rom'], elf=report['elf'], core=report['core'],
        controller_sources=sources, input_steps=len(inputs), commands=len(commands),
        checkpoints=len(checkpoints), manual_actions=len(manual),
        manual_callback_batch_overshoot_max=max(x['callback_batch_overshoot'] for x in manual),
        route_cells_reached=report['route_cells_reached'], presented=len(rows),
        final_gameplay_row=rows[-1], retry_before=retries[0][0], retry_after=retries[0][1],
        finish_frontend_state=report['finish_probe'][3], finish_result=report['finish_flow'][26],
        active_world_after_finish=report['finish_flow'][3], level_select_state=report['level_select_after_finish'][3],
        disk_unchanged=report['disk'], renderer_errors=0, play_audio=report['play_audio'],
        profile=report['performance_report'], metrics=report['steady_metrics'],
        limitations=['Emulator-only ordinary control; physical DS/DS Lite and subjective listening remain N24.',
                     'Cadence budget failed; this successful flow proof supplies N23 profiling evidence.'])
    target = ROOT / 'analysis/nds-n22-level2-en-complete.json'
    target.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(passed=True, summary=pin(target), provenance_files=len(files),
                         manual_actions=len(manual), input_steps=len(inputs))))


if __name__ == '__main__':
    main()
