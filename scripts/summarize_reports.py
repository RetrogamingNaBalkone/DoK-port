"""Publish compact Git summaries of explicitly classified local JSON reports.

Full reports keep their canonical paths for existing producers and consumers.
Policy changes require reviewing the data; file size alone never classifies it.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / 'analysis/report-policy.json'


def encoded(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False)


def compact(value, pointer='', depth=0):
    if isinstance(value, dict):
        # Large maps of source hashes are already bound by the full-report hash.
        if len(value) > 16 and all(isinstance(v, str) and len(v) == 64 for v in value.values()):
            return {'detail_pointer': pointer, 'entries': len(value)}
        if depth >= 5:
            return {'detail_pointer': pointer, 'entries': len(value),
                    'scalars': {k: v for k, v in value.items() if not isinstance(v, (list, dict))}}
        return {k: compact(v, pointer + '/' + k.replace('~', '~0').replace('/', '~1'), depth + 1)
                for k, v in value.items()}
    if isinstance(value, list):
        # Preserve readable decisions/limitations, but not per-frame/vector dumps.
        if all(isinstance(v, str) for v in value) and len(encoded(value)) <= 4096:
            return value
        if len(value) <= 6 and len(encoded(value)) <= 2048:
            return [compact(v, pointer + '/' + str(i), depth + 1) for i, v in enumerate(value)]
        return {'detail_pointer': pointer, 'items': len(value)}
    return value


def summary(relative):
    path = ROOT / relative
    raw = path.read_bytes()
    report = json.loads(raw.decode('utf-8-sig'))
    return {'summary_schema': 1,
            'full_report': {'path': relative, 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)},
            'scope': 'Summary of a recorded run; not a new verification. Full report is local and ignored by Git.',
            'result': compact(report)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check freshness without changing reports or summaries')
    args = parser.parse_args()
    policy = json.loads(POLICY.read_text(encoding='utf-8'))
    total_full = total_summary = 0
    for relative in policy['local_reports']:
        path = Path(relative)
        if path.parts[0] != 'analysis' or len(path.parts) != 2 or path.suffix != '.json':
            raise ValueError(f'Unexpected report path: {relative}')
        target = ROOT / 'analysis/summaries' / path.name
        value = summary(relative)
        output = json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n'
        if args.check:
            assert json.loads(target.read_text(encoding='utf-8')) == value, f'Stale summary: {target}'
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(output, encoding='utf-8', newline='\n')
        total_full += value['full_report']['bytes']
        total_summary += len(output.encode('utf-8'))
    print(f'{len(policy["local_reports"])} summaries: {total_full} full-report bytes -> {total_summary} summary bytes')


if __name__ == '__main__':
    main()
