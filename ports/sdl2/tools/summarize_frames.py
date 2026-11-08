"""Summarize complete application frames; present includes VSync and pacing."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def summarize(path, warmup):
    with path.open(newline='', encoding='utf-8') as file:
        rows = list(csv.DictReader(file))[warmup:]
    if not rows:
        raise ValueError('No samples after warmup')
    report = {'samples': len(rows), 'warmup_frames': warmup,
              'capture_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
              'boundary': 'Full frame including input, simulation, rendering, present and SDL_Delay; CPU work overlaps GPU execution; not GPU timer queries.',
              'capture_limit_frames': 120000}
    for key in ['work_ms', 'present_ms', 'total_ms']:
        values = sorted(float(row[key]) for row in rows)
        if not all(math.isfinite(x) and x >= 0 for x in values):
            raise ValueError('Invalid frame time')
        report[key] = {f'p{p}': values[max(0, math.ceil(p / 100 * len(values)) - 1)] for p in [50, 95, 99]}
    elapsed = sum(float(row['total_ms']) for row in rows)
    if elapsed <= 0:
        raise ValueError('Zero elapsed time')
    report['fps'] = len(rows) * 1000 / elapsed
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--warmup', type=int, default=300)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.warmup < 0 or args.output.exists():
        parser.error('warmup must be nonnegative and output must be new')
    args.output.write_text(json.dumps(summarize(args.csv, args.warmup), indent=2) + '\n', encoding='utf-8')
