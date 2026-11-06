"""Discard obsolete generated pictures/benchmarks; retain original DOS evidence.

Dry run by default. No recursive deletion, no originals/artifacts/build inputs.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

from prune_test_storage import ROOT, local, relative, write
from compress_build_logs import completed_file


def original_evidence(name):
    parts = Path(name).as_posix().lower().split('/')
    return ('sp07-render' in parts or any(part.startswith(
        ('dos', 'ghidra', 'decomp', 'original', 'reference', 'render-reference')) for part in parts))


def disposable(name):
    if original_evidence(name):
        return False
    if name.startswith(('build/sdl2/benchmarks/',
                        'research/evidence/source-port/render-benchmark/')):
        return True
    if name.startswith(('build/', 'research/evidence/')):
        # G18 is a frozen pixel oracle and a retained GBA build input.
        if name.startswith('build/gba/g18/'):
            return False
        return Path(name).suffix.lower() in {'.png', '.bmp', '.rgba', '.rgb', '.rgb555'}
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report_path = local(args.report)
    if report_path.exists():
        raise ValueError('Use a new report path')
    tracked = set(subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0'))
    rows, skipped = [], []
    for area in ('build', 'research/evidence'):
        for base, dirs, files in os.walk(ROOT/area):
            dirs[:] = [d for d in dirs if not (Path(base)/d).is_symlink() and not (Path(base)/d).is_junction()]
            for name in files:
                p = Path(base)/name
                rel = relative(p)
                if rel in tracked or not disposable(rel):
                    continue
                local(p)  # Resolve/check absolute boundaries before deleting.
                rows.append(dict(path=rel, bytes=p.stat().st_size))
    result = dict(applied=args.apply, files=len(rows), bytes=sum(r['bytes'] for r in rows), delete=rows, skipped=skipped)
    write(report_path, result)
    print(json.dumps({k:v for k,v in result.items() if k!='delete'}), flush=True)
    if args.apply:
        deleted = 0
        for row in rows:
            p = local(ROOT/row['path'])
            try:
                with completed_file(p):
                    if p.stat().st_size != row['bytes']:
                        raise ValueError(f'Changed output: {p}')
                    p.unlink()
                deleted += row['bytes']
            except FileNotFoundError:
                continue  # Already removed by another cleanup.
            except (OSError, ValueError) as error:
                skipped.append(dict(path=row['path'], reason=str(error)))
        result['deleted_bytes'] = deleted
        write(report_path, result)
        print(json.dumps(dict(deleted_bytes=deleted, skipped_files=len(skipped))), flush=True)


if __name__ == '__main__':
    main()
