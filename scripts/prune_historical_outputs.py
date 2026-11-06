"""Plan/apply deletion of superseded outputs; never archive test history.

Current targets and external build inputs are explicit. Every deletion is
pinned by SHA-256, revalidated before applying, and excludes Git-tracked files.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import subprocess
import time

from compress_build_logs import completed_file
from prune_test_storage import ROOT, local, relative, sha, write
from prune_obsolete_test_outputs import original_evidence

KEEP = (
    'build/gba/assets/', 'build/gba/g18/', 'build/gba/ui04-title/',
    'build/nds/assets/', 'build/nds/system/', 'build/nds/n18/arm7/',
    'build/nds/n23/discovery32-ru/',
    'research/evidence/nds/n23/candidates/discovery32-ru/',
)
COMPILER = {'.gba', '.nds', '.elf', '.o', '.obj', '.pdb', '.a', '.lib', '.exe', '.dll', '.so'}
MEDIA = {'.png', '.bmp', '.rgba', '.rgb', '.rgb555', '.wav'}
CACHES = {'zig-cache', '.zig-cache', 'zig-global-cache', 'zig-local-cache', '.compiler-cache', '__pycache__'}


def walk(area):
    for base, dirs, files in os.walk(ROOT / area, followlinks=False):
        for name in dirs + files:
            local(Path(base) / name)  # Refuse junctions and paths outside root.
        for name in files:
            yield Path(base) / name


def reason(path, canonical):
    name = relative(path)
    if name.startswith(KEEP):
        return None
    suffix = path.suffix.lower()
    if name.startswith('build/') and CACHES.intersection(path.parts):
        return 'compiler-cache'
    if name.startswith('build/sdl2/') and suffix == '.pdb':
        return 'regenerable-debug-symbols'
    if name.startswith(('build/gba/', 'build/nds/')):
        if original_evidence(name):
            return None
        if suffix in COMPILER or path.name == 'ui-tests-linux':
            return 'superseded-compiler-output'
        if suffix in {'.map', '.d', '.su'}:
            return 'superseded-linker-output'
        if suffix == '.json' and path.stat().st_size > 262144:
            return 'historical-detail-report'
        if suffix in MEDIA:
            return 'superseded-port-capture'
        if name.startswith('build/gba/') and suffix == '.bin':
            return 'generated-gba-bank'
        if '/nitrofs/' in name and suffix in {'.ndp', '.dat'}:
            return 'per-target-resource-copy'
        if suffix == '.ndp' and (path.stat().st_size, sha(path)) in canonical:
            return 'shared-resource-copy'
    if name.startswith('research/evidence/') and '/maintenance/' not in name:
        if suffix in {'.res', '.ndp'} and (path.stat().st_size, sha(path)) in canonical:
            return 'canonical-resource-copy'
        if original_evidence(name):
            return None
        if suffix in MEDIA:
            return 'reproducible-port-capture'
        if suffix in COMPILER:
            return 'historical-build-copy'
        if suffix == '.sav':
            return 'generated-test-profile'
        if suffix == '.json' and path.stat().st_size > 262144:
            return 'historical-detail-report'
    return None


def compact(value, depth=0):
    """Retain scalar results; describe bulky collections without inventing passes."""
    if isinstance(value, dict):
        if depth >= 5 or len(value) > 100:
            return dict(omitted_object_members=len(value))
        return {k: compact(v, depth + 1) for k, v in value.items()}
    if isinstance(value, list):
        if len(value) > 12 or depth >= 5:
            return dict(omitted_array_items=len(value))
        return [compact(v, depth + 1) for v in value]
    if isinstance(value, str) and len(value) > 1024:
        return dict(omitted_string_characters=len(value))
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--min-age-hours', type=float, default=24,
                        help='Skip recent files; run only after build/test processes stop')
    args = parser.parse_args()
    destination = local(args.report)
    if destination.exists():
        raise ValueError('Use a new report path')
    tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf8').split('\0'))
    canonical = {}
    for area in ('originals', 'build/nds/assets'):
        for path in walk(area):
            canonical[(path.stat().st_size, sha(path))] = relative(path)
    # Protect original samples, published packages, Ghidra/decompilations,
    # current build trees and every retained source file in the affected areas.
    protected = {}
    for area in ('originals', 'artifacts', 'research/ghidra', 'decompiled', 'ports', 'build', 'research/evidence'):
        for path in walk(area):
            name = relative(path)
            if (name.startswith(('originals/', 'artifacts/', 'research/ghidra/', 'decompiled/', 'ports/'))
                    or name.startswith(KEEP)
                    or path.suffix.lower() in {'.c', '.h', '.s', '.inc', '.ld', '.py', '.ps1'}):
                protected[name] = sha(path)
    before = Counter()
    rows = []
    cutoff = time.time() - args.min_age_hours * 3600
    for area in ('build', 'research'):
        for path in walk(area):
            name = relative(path)
            before[name.split('/')[0]] += path.stat().st_size
            if name in tracked or path.stat().st_mtime >= cutoff:
                continue
            why = reason(path, canonical)
            if why:
                row = dict(path=name, bytes=path.stat().st_size, sha256=sha(path), reason=why)
                if why in {'canonical-resource-copy', 'shared-resource-copy'}:
                    row['canonical'] = canonical[(row['bytes'], row['sha256'])]
                rows.append(row)
    result = dict(schema=1, applied=False, before=dict(before), keep=KEEP,
                  min_age_hours=args.min_age_hours,
                  files=len(rows), bytes=sum(r['bytes'] for r in rows), delete=rows)
    write(destination, result)
    print({k: result[k] for k in ('files', 'bytes')}, flush=True)
    if not args.apply:
        return
    for row in rows:
        path = local(ROOT / row['path'])
        if path.stat().st_size != row['bytes'] or sha(path) != row['sha256']:
            raise ValueError(f'Changed output: {path}')
        if row['path'] in protected or row['path'] in tracked:
            raise ValueError(f'Protected output: {path}')
    for row in rows:
        path = local(ROOT / row['path'])
        with completed_file(path):
            if sha(path) != row['sha256']:
                raise ValueError(f'Changed output: {path}')
            if row['reason'] == 'historical-detail-report':
                summary = path.with_suffix('.summary.json')
                if summary.exists():
                    raise ValueError(f'Summary already exists: {summary}')
                write(summary, dict(source=row['path'], source_sha256=row['sha256'],
                                    source_bytes=row['bytes'], full_report_deleted=True,
                                    result=compact(json.loads(path.read_text('utf8')))))
            path.unlink()
    changed = [name for name, digest in protected.items()
               if not (ROOT / name).is_file() or sha(ROOT / name) != digest]
    result.update(applied=True, protected_files_verified=len(protected),
                  externally_changed_protected_files=changed,
                  deleted_by_reason=dict(Counter({why: sum(r['bytes'] for r in rows if r['reason'] == why)
                                                  for why in {r['reason'] for r in rows}})))
    write(destination, result)
    print(dict(deleted_files=len(rows), deleted_bytes=result['bytes'],
               protected_files_verified=len(protected)), flush=True)


if __name__ == '__main__':
    main()
