"""Retire historical GBA build trees; keep current inputs and one ROM oracle.

Source snapshots, compact reports and original observations move to research.
Identical copies refer to one canonical file. No archive is created.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess

from compress_build_logs import completed_file
from prune_test_storage import ROOT, local, relative, sha, write
from prune_historical_outputs import walk
from prune_obsolete_test_outputs import original_evidence

SOURCE = {'.c', '.h', '.s', '.inc', '.ld', '.py', '.ps1', '.asm'}
KEEP = ('build/gba/assets/', 'build/gba/ui04-title/')
ORACLE = {'build/gba/g18/dok-diagnostic.gba', 'build/gba/g18/dok-diagnostic.map',
          'build/gba/g18/build-report.json'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    report = local(args.report)
    if report.exists():
        raise ValueError('Use a new manifest')
    tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf8').split('\0'))
    protected = {}
    # Current inputs, originals and deliveries are pinned independently.
    for area in ('originals', 'artifacts/gba', 'ports/gba', 'build/gba'):
        for path in walk(area):
            name = relative(path)
            if not name.startswith('build/gba/') or name.startswith(KEEP) or name in ORACLE or name in tracked:
                protected[name] = sha(path)
    candidates = [p for p in walk('build/gba') if relative(p) not in protected]
    retain = [p for p in candidates if p.suffix.lower() in SOURCE or
              p.suffix.lower() == '.json' or original_evidence(relative(p))]
    sizes = {p.stat().st_size for p in retain}
    canonical = {}
    for area in ('originals', 'ports/gba', 'research'):
        for path in walk(area):
            if path.stat().st_size in sizes:
                canonical.setdefault((path.stat().st_size, sha(path)), relative(path))
    rows = []
    retained_paths = {relative(p) for p in retain}
    for path in candidates:
        name = relative(path)
        row = dict(path=name, bytes=path.stat().st_size, sha256=sha(path), action='delete')
        if name in retained_paths:
            key = (row['bytes'], row['sha256'])
            if key in canonical:
                row.update(action='duplicate', canonical=canonical[key])
            else:
                target = ('research/experiments/gba-source-snapshots/' if path.suffix.lower() in SOURCE
                          else 'research/evidence/gba-retired/') + name.removeprefix('build/gba/')
                local(ROOT / target)
                if (ROOT / target).exists():
                    raise ValueError(f'Destination already exists: {target}')
                row.update(action='move', destination=target)
                canonical[key] = target
        rows.append(row)
    result = dict(schema=1, applied=False, keep=list(KEEP), oracle=sorted(ORACLE), files=rows,
                  build_before_bytes=sum(p.stat().st_size for p in walk('build/gba')),
                  bytes_by_action={a: sum(r['bytes'] for r in rows if r['action'] == a)
                                   for a in ('delete', 'duplicate', 'move')})
    write(report, result)
    print(result['bytes_by_action'], flush=True)
    if not args.apply:
        return
    # Validate the complete selection before the first mutation.
    for row in rows:
        path = local(ROOT / row['path'])
        if sha(path) != row['sha256'] or path.stat().st_size != row['bytes']:
            raise ValueError(f'Changed output: {path}')
    for row in rows:
        path = local(ROOT / row['path'])
        with completed_file(path):
            if sha(path) != row['sha256']:
                raise ValueError(f'Changed output: {path}')
            if row['action'] == 'move':
                target = local(ROOT / row['destination'])
                target.parent.mkdir(parents=True, exist_ok=True)
                # Keep the handle protecting the source until the copy is verified.
                with path.open('rb') as src, target.open('xb') as dst:
                    import shutil
                    shutil.copyfileobj(src, dst)
                if sha(target) != row['sha256']:
                    raise ValueError(f'Bad copy: {target}')
            elif row['action'] == 'duplicate':
                target = local(ROOT / row['canonical'])
                if sha(target) != row['sha256']:
                    raise ValueError(f'Canonical file changed: {target}')
            path.unlink()
    for name, digest in protected.items():
        if sha(local(ROOT / name)) != digest:
            raise ValueError(f'Protected file changed: {name}')
    # Only empty directories inside the verified GBA tree are removed.
    import os
    removed_dirs = 0
    for base, dirs, files in os.walk(ROOT / 'build/gba', topdown=False):
        path = local(Path(base))
        if path == ROOT / 'build/gba' or relative(path).startswith(KEEP):
            continue
        try:
            path.rmdir()
            removed_dirs += 1
        except OSError:
            pass
    result.update(applied=True, protected_files_verified=len(protected),
                  removed_empty_directories=removed_dirs,
                  build_after_bytes=sum(p.stat().st_size for p in walk('build/gba')),
                  freed_bytes=sum(r['bytes'] for r in rows if r['action'] != 'move'))
    write(report, result)
    print({k: result[k] for k in ('build_after_bytes', 'freed_bytes', 'protected_files_verified')}, flush=True)


if __name__ == '__main__':
    main()
