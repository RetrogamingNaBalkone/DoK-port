"""Remove completed build banks and byte-identical copies of durable evidence.

No originals, artifacts or research files are removed. Historical build copies
can be restored from the manifest; compiler inputs and current targets stay.
Run after builds/emulators have stopped. Plan first, then apply that manifest.
"""
import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from compress_build_logs import completed_file
from prune_test_storage import ROOT, local, relative, sha, write

ARCHIVED_SUFFIXES = {'.json', '.wav', '.png', '.elf', '.gba', '.nds', '.map', '.log', '.rgb', '.rgb555'}
# N23 is the active DS target. GBA baseline objects/packages are external inputs
# to prototype_gba_bsp.py/research_gba_fps.py and are deliberately not candidates.
PROTECTED = ('build/nds/n23/', 'build/gba/releases/', 'build/gba/assets/',
             'build/gba/host/', 'build/gba/select-menu-fix/',
             'build/gba/level1-fixes/', 'build/gba/g20/', 'build/gba/g18/',
             'build/gba/g22/verified/', 'build/gba/perf04/final/')
BASE_INPUTS = {'build/gba/g22/base-control/base',
               'build/gba/g20d/qualification/clean-control/base',
               'build/gba/g20d/qualification/verified/base'}


def walk(directory):
    """Never traverse a link; inaccessible historical directories are skipped."""
    for base, dirs, names in os.walk(directory, followlinks=False):
        for name in dirs + names:
            local(Path(base)/name)
        for name in names:
            yield Path(base)/name


def eligible(path, tracked, cutoff):
    name = relative(path)
    return (name not in tracked and not name.startswith(PROTECTED)
            and path.stat().st_mtime < cutoff)


def plan(destination, min_age_hours=24):
    if destination.exists():
        raise ValueError('Use a new manifest; never overwrite an applied plan')
    tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT)
                  .decode('utf8').split('\0'))
    cutoff = time.time() - min_age_hours*3600
    outputs = list(walk(ROOT/'build'))
    candidates = [p for p in outputs if eligible(p, tracked, cutoff)
                  and p.suffix.lower() in ARCHIVED_SUFFIXES
                  and not any(part in {'zig-cache', '__pycache__', '.compiler-cache'} for part in p.parts)]
    sizes = {p.stat().st_size for p in candidates}
    archives = defaultdict(list)
    for area in ('research/evidence', 'research/experiments', 'artifacts'):
        for path in walk(ROOT/area):
            if path.suffix.lower() in ARCHIVED_SUFFIXES and path.stat().st_size in sizes:
                archives[path.stat().st_size].append(path)
    # Hash durable files once, not once for every repeated build copy.
    pins = {}
    for size, paths in archives.items():
        for path in paths:
            pins.setdefault((size, sha(path)), path)
    deleted = {}
    for path in candidates:
        size = path.stat().st_size
        if size not in archives:
            continue
        digest = sha(path)
        archive = pins.get((size, digest))
        if archive:
            deleted[relative(path)] = dict(path=relative(path), bytes=size,
                sha256=digest, reason='archived-copy', archive=relative(archive))
    # A successful prototype manifest proves these are regenerated link banks,
    # not the shared asset package. Keep source snapshots and ROM/ELF/map pins.
    for manifest in (p for p in outputs if p.name == 'prototype-build.json'
                     and relative(p).startswith('build/gba/')):
        out = manifest.parent
        report = json.loads(manifest.read_text('utf8'))
        if not report.get('variants'):
            continue
        for assembly in out.glob('*.S'):
            for name in re.findall(r'^\s*\.incbin\s+"([^"]+)"', assembly.read_text('utf8'), re.M):
                path = local(Path(name) if Path(name).is_absolute() else ROOT/name)
                if (path.parent == out and path.suffix == '.bin' and path.exists()
                        and eligible(path, tracked, cutoff)):
                    deleted[relative(path)] = dict(path=relative(path), bytes=path.stat().st_size,
                        sha256=sha(path), reason='generated-prototype-bank',
                        build_manifest=relative(manifest))
    for manifest in (p for p in outputs if p.name == 'frontend-build.json'
                     and relative(p).startswith('build/gba/')):
        base=manifest.parent/'base'
        if relative(base) in BASE_INPUTS or not json.loads(manifest.read_text('utf8')).get('rom_sha256'):
            continue
        paths=list(base.glob('*.o'))+[base/name for name in
                ('render-columns.bin','dok-diagnostic.elf','dok-diagnostic.gba')]
        for path in paths:
            if path.is_file() and eligible(path,tracked,cutoff):
                deleted[relative(path)]=dict(path=relative(path),bytes=path.stat().st_size,
                    sha256=sha(path),reason='consumed-frontend-base',build_manifest=relative(manifest))
    rows = sorted(deleted.values(), key=lambda row: row['path'])
    report = dict(schema=1, workspace=str(ROOT), min_age_hours=min_age_hours,
                  protected=list(PROTECTED), delete=rows, files=len(rows),
                  bytes=sum(row['bytes'] for row in rows))
    write(destination, report)
    print(json.dumps({k:report[k] for k in ('files','bytes')}))


def validate(row):
    path = local(ROOT/row['path'])
    if not path.is_relative_to(ROOT/'build') or relative(path).startswith(PROTECTED):
        raise ValueError(f'Unexpected build output: {path}')
    if row['reason'] == 'archived-copy':
        archive = local(ROOT/row['archive'])
        if not any(archive.is_relative_to(ROOT/area) for area in
                   ('research/evidence','research/experiments','artifacts')):
            raise ValueError(f'Not a durable archive: {archive}')
        if archive.stat().st_size != row['bytes'] or sha(archive) != row['sha256']:
            raise ValueError(f'Archive changed: {archive}')
    elif row['reason'] == 'consumed-frontend-base':
        manifest=local(ROOT/row['build_manifest'])
        if (manifest.name != 'frontend-build.json' or path.parent != manifest.parent/'base'
                or not path.is_relative_to(ROOT/'build/gba')
                or relative(path.parent) in BASE_INPUTS
                or not (path.suffix == '.o' or path.name in
                        ('render-columns.bin','dok-diagnostic.elf','dok-diagnostic.gba'))
                or not json.loads(manifest.read_text('utf8')).get('rom_sha256')):
            raise ValueError(f'Not a consumed frontend base: {path}')
    elif row['reason'] == 'generated-prototype-bank':
        manifest = local(ROOT/row['build_manifest'])
        linked_banks = {name for assembly in path.parent.glob('*.S')
                        for name in re.findall(r'^\s*\.incbin\s+"([^"]+)"', assembly.read_text('utf8'), re.M)}
        if (path.suffix != '.bin' or manifest.parent != path.parent
                or manifest.name != 'prototype-build.json'
                or not path.is_relative_to(ROOT/'build/gba')
                or not json.loads(manifest.read_text('utf8')).get('variants')
                or not any(local(Path(name) if Path(name).is_absolute() else ROOT/name) == path
                           for name in linked_banks)):
            raise ValueError(f'Not a completed prototype bank: {path}')
    else:
        raise ValueError(f'Unknown removal reason: {row}')
    return path


def apply(manifest):
    report = json.loads(manifest.read_text('utf8'))
    if report['workspace'] != str(ROOT) or report['schema'] != 1:
        raise ValueError('Wrong workspace/schema')
    tracked = set(subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0'))
    # Preflight the whole manifest before the first deletion.
    for row in report['delete']:
        path = validate(row)
        if row['path'] in tracked or path.stat().st_size != row['bytes'] or sha(path) != row['sha256']:
            raise ValueError(f'Output changed or tracked: {path}')
    for row in report['delete']:
        path = local(ROOT/row['path'])
        with completed_file(path):
            if path.stat().st_size != row['bytes'] or sha(path) != row['sha256']:
                raise ValueError(f'Output changed: {path}')
            path.unlink()
    result = dict(passed=all(not (ROOT/r['path']).exists() for r in report['delete']),
                  deleted_files=report['files'], deleted_bytes=report['bytes'],
                  manifest=relative(manifest), manifest_sha256=sha(manifest))
    write(manifest.with_name(manifest.stem+'-verified.json'),result)
    print(json.dumps(result))


def restore(manifest, paths=()):
    report=json.loads(manifest.read_text('utf8'))
    if report['workspace'] != str(ROOT) or report['schema'] != 1:
        raise ValueError('Wrong workspace/schema')
    restored=0
    for row in report['delete']:
        if row['reason'] != 'archived-copy':
            continue
        if paths and not any(row['path'] == name.rstrip('/') or
                             row['path'].startswith(name.rstrip('/')+'/') for name in paths):
            continue
        path=validate(row)
        if path.exists():
            if sha(path) != row['sha256']:
                raise ValueError(f'Refusing to overwrite changed output: {path}')
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as output, (ROOT/row['archive']).open('rb') as source:
            shutil.copyfileobj(source,output)
        if sha(path) != row['sha256']:
            raise ValueError(f'Restore verification failed: {path}')
        restored+=1
    print(json.dumps(dict(restored_files=restored)))


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('plan','apply','restore'))
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--min-age-hours',type=float,default=24)
    parser.add_argument('--path',action='append',default=[],help='Restore only this build path/prefix (repeatable)')
    args=parser.parse_args()
    if args.min_age_hours < 0:
        parser.error('--min-age-hours must be nonnegative')
    target=local(args.manifest)
    if args.action=='plan':
        plan(target,args.min_age_hours)
    elif args.action=='restore':
        restore(target,args.path)
    else:
        apply(target)
