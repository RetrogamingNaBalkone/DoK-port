"""Execute recorded physical inputs from boot, verify a prefix, then navigate.

This creates a new game. No state or memory is restored. Continuation is
permitted only after every recorded scalar row and the actual native PAUSE
descriptor match the source recording exactly.
"""
import argparse
import copy
import json
from pathlib import Path
import traceback

from run_routes import RouteObserver, ROOT, pin, front, transport, render, Level
from frontend_fat_oracle import volume_files, audit_changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-report', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--language', type=int, choices=(1, 2), required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--core', type=Path, default=front.CORE)
    args = parser.parse_args()
    target, source, point = (p.resolve() for p in (args.target, args.source_report, args.checkpoint))
    assert target.is_relative_to(ROOT/'build/nds/n22')
    assert source.is_relative_to(ROOT) and point.is_relative_to(ROOT)
    original = json.loads(source.read_text('utf8'))
    checkpoint = json.loads(point.read_text('utf8'))
    level, stop = checkpoint['level'], checkpoint['video_frame']
    assert original['task'] == 'N22' and checkpoint['frontend'][3] == 9
    assert checkpoint['flow'][3] == 1 and checkpoint['flow'][6] > 0
    assert checkpoint['frontend'][4] == args.language
    for key, name in (('rom', 'dok_nds_gameplay.nds'), ('elf', 'dok_nds_gameplay.elf')):
        assert original[key] == pin(target/name), ('Prefix must use the exact same compiled game', key)
    for entry in original['source_snapshots']:
        assert pin(ROOT/entry['snapshot']['path']) == entry['snapshot']
    ordered_inputs = [copy.deepcopy(r) for r in original['input_steps'] if r['video_frame'] < stop]
    assert ordered_inputs and all('touch' in r for r in ordered_inputs), 'Explicit physical touch transitions required'
    assert all(a['video_frame']<=b['video_frame'] for a,b in zip(ordered_inputs,ordered_inputs[1:])), 'Input order must be monotone'
    # Several host-only latch changes may occur before the next retro_run.
    # Only the last full joypad/touch state at that video_frame was polled.
    # Keep the complete ordered recording as evidence, never replay unsampled
    # intermediate latch changes as extra time or extra physical callbacks.
    events = {r['video_frame']: r for r in ordered_inputs}
    inputs = list(events.values())
    expected = [r for r in original['rows'] if r['video_frame'] <= stop]
    assert expected and all(r['level'] == level for r in expected)
    touches = [copy.deepcopy(r) for r in original.get('touch', []) if r['video_frame'] <= stop]
    touch_before, touch_after = {}, {}
    for touch in touches:
        presses = [r for r in inputs if r['video_frame'] < touch['video_frame'] and r['touch'] == touch['xy']]
        assert presses
        press = presses[-1]['video_frame']
        releases = [r for r in inputs if press < r['video_frame'] < touch['video_frame'] and r['touch'] is None]
        assert releases, 'Recorded physical touch release required'
        touch_before[press] = touch['hero_before']
        touch_after[touch['video_frame']] = touch['hero_after']
    directory = target/f'route-{args.language}-level{level}-{args.run_id}'
    assert not directory.exists(), 'Use a unique immutable run-id'
    directory.mkdir(parents=True)
    f = None
    report = dict(task='N22', passed=False, completed_playthrough=False)
    try:
        assert transport.sha(args.core) == render.CORE_SHA
        f = RouteObserver(args.core, target, directory)
        report = f.report
        snapshot = directory/'sources'/(transport.sha(Path(__file__))+'-'+Path(__file__).name)
        snapshot.write_bytes(Path(__file__).read_bytes())
        expectation = directory/'prefix-expectation.json'
        expectation.write_text(json.dumps(dict(source=pin(source), checkpoint=pin(point),
            inputs=inputs, ordered_source_input_steps=ordered_inputs, expected_rows=expected, expected_checkpoint=checkpoint,
            source_snapshots=original['source_snapshots'], touches=touches), indent=2)+'\n', 'utf8')
        prefix = dict(passed=False, source_report=pin(source), checkpoint=pin(point),
            expectation=pin(expectation), runner=pin(snapshot), stop_video_frame=stop,
            method='Fresh boot and every physical joypad/touch event; no memory, state, HP, inventory or clock writes')
        report['verified_physical_prefix'] = prefix
        f.level_data = Level(level)
        f.review_pauses = False
        image = f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image, front.settings_bytes(args.language))
        f.load(target/'dok_nds_gameplay.nds')
        transport.flush_native_stdio()
        before = volume_files(image)
        matched = 0
        for frame in range(stop):
            assert f.frames == frame
            if frame in touch_before:
                assert f.probe[41] == touch_before[frame], ('Touch callback before', frame, f.probe[41])
            if frame in events:
                event = copy.deepcopy(events[frame])
                f.joypad = sum(1 << button for button in event['buttons'])
                f.touch = tuple(event['touch']) if event['touch'] is not None else None
                f.previous_touch = f.touch
                f.input_steps.append(event)
            previous = len(f.rows)
            f.tick()
            for row in f.rows[previous:]:
                assert matched < len(expected), ('Unexpected prefix row', row)
                assert row == expected[matched], ('Exact scalar prefix mismatch', matched, expected[matched], row)
                matched += 1
            if f.frames in touch_after:
                assert f.probe[41] == touch_after[f.frames], ('Touch callback after', f.frames, f.probe[41])
        assert matched == len(expected)
        assert f.input_steps == inputs
        assert list(f.flow) == checkpoint['flow'], ('Prefix flow mismatch', f.flow, checkpoint['flow'])
        assert list(f.probe) == checkpoint['frontend'], ('Prefix frontend mismatch', f.probe, checkpoint['frontend'])
        assert list(f.last_pose) == checkpoint['pose']
        maps = f.map_snapshot(f.level_data)
        assert maps and bytes(maps[0]).hex() == checkpoint['mmap'] and bytes(maps[1]).hex() == checkpoint['tmap']
        prefix.update(passed=True, matched_rows=matched, physical_input_changes=len(inputs),
            exact_flow=list(f.flow), exact_frontend=list(f.probe), exact_pose=list(f.last_pose),
            ordered_source_latch_changes=len(ordered_inputs), effective_polled_input_changes=len(inputs),
            input_normalization="Last complete physical state per video_frame; original ordered latch changes retained in expectation",
            actual_touch_callbacks_verified=len(touches), boot_and_prefix_audio=copy.deepcopy(report.get('play_audio')))
        # These metadata describe touch events observed again in this new game;
        # they are copied only after physical events and native counters match.
        report['touch'] = touches
        report['route_arguments'] = dict(target=str(target), level=level, language=args.language,
            run_id=args.run_id, review_start=True, continuation_from_actual_pause=True)
        f.review_start = True
        f.review_pauses = True
        # navigate rebuilds a chosen goal from the actual published current map.
        # It never calls start_level; the replayed world stays active throughout.
        f.navigate(level)
        report['passed'] = bool(report.get('completed_playthrough'))
        report['continuation_route_cells_reached'] = report.get('route_cells_reached', 0)
        report['prefix_route_cells_reached'] = checkpoint['navigation_goal']['completed']
        report['route_cells_reached'] = report['continuation_route_cells_reached'] + report['prefix_route_cells_reached']
    except Exception:
        report['error'] = traceback.format_exc()
        if f and f.last_video:
            report['failure_images'] = f.capture('failure')
    finally:
        if f:
            report['last_flow'] = list(f.flow) if f.flow else None
            f.close()
            transport.flush_native_stdio()
            report['last_probe'] = list(f.probe) if f.probe else None
            if 'before' in locals():
                report['disk'] = audit_changes(before, volume_files(image), ['/DOK/SETTINGS.DAT'])
        (directory/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', 'utf8')
    print(json.dumps({key: report.get(key) for key in ('passed', 'completed_playthrough', 'error')}, ensure_ascii=False))
    return 0 if report.get('passed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
