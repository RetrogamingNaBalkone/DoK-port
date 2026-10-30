"""Replay recorded physical inputs and retain bounded read-only RAM evidence.

This is diagnostic evidence, never a completed-playthrough claim. Main RAM is
read through the libretro API; there are no writes or injected cache flushes.
Private ARM9 data may have dirty cache lines and must be interpreted carefully.
"""
import argparse
import json
from pathlib import Path
import sys

import replay_routes
from run_routes import RouteObserver, ROOT, pin, render


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--source-report', type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--language', type=int, choices=(1, 2), required=True)
    parser.add_argument('--before-frame', type=int, required=True)
    parser.add_argument('--frames', type=int, required=True)
    args = parser.parse_args()
    directory = args.target.resolve() / ('replay-' + args.run_id)
    directory.mkdir(parents=True, exist_ok=True)
    original_tick, original_fault = RouteObserver.tick, RouteObserver.record_fault
    captured = set()
    evidence = dict(method=__doc__, driver=pin(Path(__file__).resolve()),
                    source=pin(args.source_report.resolve()), snapshots=[])

    def capture(observer, kind):
        if kind in captured:
            return
        captured.add(kind)
        path = directory / (kind + '-main-ram.bin')
        path.write_bytes(render.ram(observer, 0x02000000, 4 * 1024 * 1024))
        evidence['snapshots'].append(dict(kind=kind, video_frame=observer.frames,
                                         memory=pin(path)))
        (directory / 'ram-evidence.json').write_text(
            json.dumps(evidence, ensure_ascii=False, indent=2) + '\n', 'utf8')

    def tick(observer, count=1):
        for _ in range(count):
            if observer.frames >= args.before_frame:
                capture(observer, 'before')
            original_tick(observer, 1)
        return observer.probe

    def fault(observer):
        capture(observer, 'fault')
        original_fault(observer)

    RouteObserver.tick, RouteObserver.record_fault = tick, fault
    sys.argv = [str(Path(replay_routes.__file__)), '--target', str(args.target),
                '--source-report', str(args.source_report), '--run-id', args.run_id,
                '--language', str(args.language), '--frames', str(args.frames)]
    return replay_routes.main()


if __name__ == '__main__':
    raise SystemExit(main())
