"""Losslessly archive cold evidence with SHA-256 verification and selective restore.

Archive members use repository-relative names. Source files are removed only
after the whole ZIP and every original have passed a second SHA-256 check.
Current build inputs, originals and artifacts cannot be archive targets.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import time
import zipfile

from compress_build_logs import completed_file
from prune_test_storage import ROOT, local, relative, sha, write

ARCHIVES=ROOT/'research/experiments/storage-archives'
MANIFEST='__dok_storage_manifest__.json'
ALLOWED=('build/gba/','build/nds/','research/evidence/','research/experiments/')


def target(name):
    path=local(ROOT/name)
    if not relative(path).startswith(ALLOWED):
        raise ValueError(f'Not a cold build/evidence path: {path}')
    if path.is_relative_to(ARCHIVES):
        raise ValueError(f'Do not recursively archive the storage archives: {path}')
    return path


def pack(archive, paths, apply=False):
    archive=local(archive)
    if archive.parent != ARCHIVES or archive.suffix != '.zip' or archive.exists():
        raise ValueError(f'Use a new storage archive: {archive}')
    paths=sorted(set(target(relative(p)) for p in paths))
    if not paths:
        raise ValueError('Empty archive selection')
    rows=[]
    for i,path in enumerate(paths):
        rows.append(dict(path=relative(path),bytes=path.stat().st_size,sha256=sha(path)))
        if (i+1)%5000==0:
            print(f'Pinned {i+1}/{len(paths)} files',flush=True)
    archive.parent.mkdir(parents=True,exist_ok=True)
    temporary=local(archive.with_suffix('.zip.partial'))
    if temporary.exists():
        raise ValueError(f'Partial archive already exists: {temporary}')
    try:
        with zipfile.ZipFile(temporary,'x',compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6,allowZip64=True) as packed:
            for i,row in enumerate(rows):
                path=target(row['path'])
                with completed_file(path):
                    packed.write(path,row['path'])
                if (i+1)%2000==0:
                    print(f'Packed {i+1}/{len(rows)} files',flush=True)
            packed.writestr(MANIFEST,json.dumps(dict(schema=1,files=rows),ensure_ascii=False))
        with zipfile.ZipFile(temporary) as packed:
            for row in rows:
                with packed.open(row['path']) as source:
                    if hashlib.file_digest(source,'sha256').hexdigest()!=row['sha256']:
                        raise ValueError(f'Archive verification failed: {row["path"]}')
        # Preflight every original before deleting the first. Changes by other
        # running sessions abort the operation with all sources still present.
        for row in rows:
            path=target(row['path'])
            if path.stat().st_size!=row['bytes'] or sha(path)!=row['sha256']:
                raise ValueError(f'Source changed during archival: {path}')
        temporary.rename(archive)
        result=dict(schema=1,archive=relative(archive),archive_bytes=archive.stat().st_size,
                    archive_sha256=sha(archive),files=len(rows),
                    source_bytes=sum(row['bytes'] for row in rows),applied=apply)
        write(archive.with_suffix('.json'),result)
        if apply:
            for row in rows:
                path=target(row['path'])
                with completed_file(path):
                    if path.stat().st_size!=row['bytes'] or sha(path)!=row['sha256']:
                        raise ValueError(f'Source changed before removal: {path}')
                    path.unlink()
        result.update(saved_bytes=result['source_bytes']-result['archive_bytes'] if apply else 0)
        write(archive.with_suffix('.json'),result)
        print(json.dumps(result),flush=True)
        return result
    finally:
        temporary.unlink(missing_ok=True)


def restore_member(packed,row):
    name=row['path'];path=target(name)
    if path.exists():
        return False
    path.parent.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256();length=0;created=False
    try:
        with path.open('xb') as output:
            created=True
            with packed.open(name) as source:
                while chunk:=source.read(1024**2):
                    output.write(chunk);digest.update(chunk);length+=len(chunk)
        if digest.hexdigest()!=row['sha256'] or length!=row['bytes']:
            raise ValueError(f'Restored bytes do not match manifest: {path}')
    except BaseException:
        if created:
            path.unlink(missing_ok=True)
        raise
    return True


def restore_path(name):
    if target(name).exists():
        return False
    for archive in sorted(ARCHIVES.glob('*.zip')):
        local(archive)
        with zipfile.ZipFile(archive) as packed:
            try:
                packed.getinfo(name)
            except KeyError:
                continue
            manifest=json.loads(packed.read(MANIFEST))
            row=next(row for row in manifest['files'] if row['path']==name)
            return restore_member(packed,row)
    raise FileNotFoundError(f'No storage archive contains {name}')


def restore_prefix(prefix):
    count=0
    for archive in sorted(ARCHIVES.glob('*.zip')):
        with zipfile.ZipFile(local(archive)) as packed:
            manifest=json.loads(packed.read(MANIFEST))
            for row in manifest['files']:
                if row['path']==prefix.rstrip('/') or row['path'].startswith(prefix.rstrip('/')+'/'):
                    count+=restore_member(packed,row)
    print(json.dumps(dict(restored_files=count)))


def pack_history(port,archive,apply=False):
    """Cold reports/captures, preserving prepared data and checkpoint guards."""
    from prune_superseded_builds import KEEP
    suffixes={'.json','.wav','.png','.log','.rgb','.rgb555','.idx','.map','.gz','.csv','.tsv'}
    keep=KEEP+('build/nds/assets/','build/nds/n07/','build/nds/system/')
    paths=[];cutoff=time.time()-24*3600
    for base,dirs,files in os.walk(ROOT/'build'/port,followlinks=False):
        for name in dirs:
            local(Path(base)/name)
        for name in files:
            path=Path(base)/name;rel=relative(path)
            if (rel.startswith(keep) or '/dos/' in rel or '/dos-' in rel
                    or path.suffix.lower() not in suffixes or path.stat().st_mtime>=cutoff
                    or 'manifest' in name.lower() or name in {'sources.json','inputs.json','index.json'}):
                continue
            paths.append(path)
    return pack(archive,paths,apply)


def pack_cold_build(port,archive,apply=False):
    """Retire remaining source snapshots while keeping actual rebuild inputs."""
    from prune_archived_builds import BASE_INPUTS
    keep=('build/gba/assets/','build/gba/host/','build/gba/g18/',
          'build/gba/select-menu-fix/','build/gba/level1-fixes/','build/gba/g20/',
          'build/gba/g22/verified/','build/gba/.compiler-cache/',
          'build/nds/assets/','build/nds/system/','build/nds/n07/')+tuple(p+'/' for p in BASE_INPUTS)
    current=('build/nds/n23/audio21-ru/','build/nds/n23/audio21b-ru/',
             'build/nds/n23/crashdiag22-ru/','build/nds/n23/candidate22-ru/',
             'build/nds/n23/candidate22-en/','build/nds/n23/standalone01-ru/',
             'build/gba/releases/gba-beta2/hardware/g22_split_active/')
    # These GBA experiments were explicitly rejected in favour of Beta2.
    closed=('build/gba/perf06/','build/gba/g25/','build/gba/g26/','build/gba/g27/')
    prepared={'.dat','.ndp','.ndspak','.inc','.S'}
    paths=[];cutoff=time.time()-24*3600
    for base,dirs,files in os.walk(ROOT/'build'/port,followlinks=False):
        for name in dirs:
            local(Path(base)/name)
        for name in files:
            path=Path(base)/name;rel=relative(path)
            if rel.startswith(keep) or '/dos/' in rel or '/dos-' in rel:
                continue
            if path.stat().st_mtime>=cutoff and not rel.startswith(closed):
                continue
            snapshot='/source/' in rel or '/sources/' in rel
            if not snapshot and (path.suffix in prepared or 'manifest' in name.lower()
                                 or name in {'sources.json','inputs.json','index.json'}):
                continue
            if rel.startswith(current) and (path.suffix.lower() in {'.elf','.gba','.nds','.map','.o','.d','.su'}
                    or name in {'build-report.json','frontend-build.json','prototype-build.json'}):
                continue
            if rel.startswith('build/gba/releases/gba-beta2/') and name=='frontend-build.json':
                continue
            paths.append(path)
    return pack(archive,paths,apply)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='action',required=True)
    restore=commands.add_parser('restore')
    restore.add_argument('path',help='Repository-relative file or directory prefix')
    history=commands.add_parser('history')
    history.add_argument('--port',choices=('gba','nds'),required=True)
    history.add_argument('--archive',required=True,type=Path)
    history.add_argument('--apply',action='store_true')
    cold=commands.add_parser('cold-build')
    cold.add_argument('--port',choices=('gba','nds'),required=True)
    cold.add_argument('--archive',required=True,type=Path)
    cold.add_argument('--apply',action='store_true')
    packing=commands.add_parser('pack')
    packing.add_argument('--area',required=True,type=Path)
    packing.add_argument('--archive',required=True,type=Path)
    packing.add_argument('--keep',action='append',default=[],help='Retain a repository-relative path/prefix')
    packing.add_argument('--suffix',action='append',default=[],help='Pack only these extensions (repeatable)')
    packing.add_argument('--min-age-hours',type=float,default=24)
    packing.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    if args.action=='restore':
        restore_prefix(args.path.replace('\\','/'))
    elif args.action=='history':
        pack_history(args.port,local(args.archive),args.apply)
    elif args.action=='cold-build':
        pack_cold_build(args.port,local(args.archive),args.apply)
    else:
        area=local(args.area)
        if not any(area.is_relative_to(ROOT/p.rstrip('/')) for p in ALLOWED):
            parser.error('Not a cold build/evidence area')
        paths=[];cutoff=time.time()-args.min_age_hours*3600
        for base,dirs,files in os.walk(area,followlinks=False):
            for name in dirs+files:
                local(Path(base)/name)
            for name in files:
                path=Path(base)/name;rel=relative(path)
                if path.stat().st_mtime>=cutoff or any(rel==k.rstrip('/') or rel.startswith(k.rstrip('/')+'/') for k in args.keep):
                    continue
                if args.suffix and path.suffix.lower() not in args.suffix:
                    continue
                paths.append(path)
        pack(local(args.archive),paths,args.apply)
