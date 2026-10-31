"""Inventory disposable compiler caches and generated NDS emulator disks.

Deletion is performed by prune-test-storage.ps1 after checking this manifest.
Reports, captures, build inputs and frozen research archives are retained.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CACHES = {'zig-cache', 'zig-global-cache', 'zig-local-cache', '.zig-cache', '.compiler-cache', '__pycache__'}
FIXTURE_FILES = {'/DOK_ND~1.NDS', '/DOK/NDS-DIAG.TXT', '/DOK/ASSETS.NDP',
    '/DOK/BADHASH.NDP', '/DOK/BADSCH~1.NDP', '/DOK/CONTROL.NDP', '/DOK/TRUNC.NDP',
    '/DOK/WRONG.NDP', '/DOK/INDEX.DAT', '/DOK/SCENE.DAT', '/DOK/UI.DAT',
    '/DOK/FRONTEND.DAT', '/DOK/SETTINGS.DAT'}


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def relative(path):
    return path.relative_to(ROOT).as_posix()


def local(path):
    path = Path(os.path.abspath(path))
    if not path.is_relative_to(ROOT) or path == ROOT:
        raise ValueError(f'Outside workspace: {path}')
    for item in (path, *path.parents):
        if item == ROOT:
            break
        attributes = getattr(item.lstat(), 'st_file_attributes', 0) if item.exists() else 0
        if item.is_symlink() or item.is_junction() or attributes & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0):
            raise ValueError(f'Reparse point: {item}')
    return path


def plan(destination, exclude=()):
    if destination.exists():
        raise ValueError('Use a new manifest path; never overwrite an applied plan')
    sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
    from frontend_fat_oracle import volume_files
    tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0'))
    candidates, disks, skipped, indices = [], [], [], []
    excluded = {local(ROOT/p) for p in exclude}
    if any(not p.is_relative_to(ROOT/'build') for p in excluded):
        raise ValueError('Exclusions must be inside build')
    skipped.extend(dict(path=relative(p), reason='Explicit active-run exclusion') for p in sorted(excluded))
    def fail(error):
        if not isinstance(error, PermissionError):
            raise error
        skipped.append(dict(path=relative(Path(error.filename)),
                            reason=f'Inaccessible directory: {error}'))
    for area in ('build', 'ports', 'scripts', 'tools'):
        for base, dirs, names in os.walk(ROOT / area, followlinks=False, onerror=fail):
            parent = Path(base)
            if parent in excluded:
                dirs[:] = []
                continue
            for directory in dirs:
                child = parent / directory
                if child.is_symlink() or child.is_junction() or getattr(child.lstat(), 'st_file_attributes', 0) & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0):
                    raise ValueError(f'Reparse directory: {child}')
            for name in names:
                path = parent / name
                rel = relative(path)
                cache = bool(CACHES.intersection(path.relative_to(ROOT).parts))
                # Compiler caches belong in build; elsewhere only Python caches.
                cache = cache and (area == 'build' or '__pycache__' in path.parts)
                disk = area == 'build' and rel.startswith('build/nds/') and name == 'dldi_sd_card.bin'
                index = area == 'build' and rel.startswith('build/nds/') and name == 'dldi_sd_card.bin.idx'
                if not (cache or disk or index):
                    continue
                local(path)
                if rel in tracked:
                    raise ValueError(f'Refusing to delete tracked file: {rel}')
                if disk:
                    try:
                        contents = volume_files(path)
                        if not contents or any(p not in FIXTURE_FILES and not re.fullmatch(r'/DOK/V\d\d(?:TOP|BOT)\.BIN', p) for p in contents):
                            raise ValueError('Unknown fixture file names')
                    except (ValueError, AssertionError, IndexError, KeyError, RuntimeError) as error:
                        skipped.append({'path': rel, 'reason': f'Unrecognised test FAT: {error}'})
                        continue
                    disks.append({'path': rel, 'files': [dict(path=p, bytes=len(data),
                        sha256=hashlib.sha256(data).hexdigest(),
                        **({'hex': data.hex()} if len(data) <= 4096 else {}))
                        for p, data in sorted(contents.items())]})
                info = path.stat()
                item = dict(path=rel, bytes=info.st_size, sha256=sha(path),
                    reason='compiler-cache' if cache else 'generated-test-disk' if disk else 'generated-test-disk-index')
                (indices if index else candidates).append(item)
    accepted_disks = {d['path'] for d in disks}
    candidates.extend(item for item in indices if item['path'][:-4] in accepted_disks or not (ROOT / item['path'][:-4]).exists())
    report = dict(schema=1, workspace=str(ROOT), delete=candidates, disk_contents=disks,
        skipped=skipped, files=len(candidates), bytes=sum(p['bytes'] for p in candidates),
        exclusions=['originals', 'research', 'analysis', 'artifacts', 'all non-cache build outputs'])
    write(destination, report)
    print(json.dumps({k: report[k] for k in ('files', 'bytes', 'skipped')}))


def verify(manifest):
    report = json.loads(manifest.read_text(encoding='utf-8'))
    remaining = [p['path'] for p in report['delete'] if local(ROOT / p['path']).exists()]
    result = dict(passed=not remaining, manifest=relative(manifest), manifest_sha256=sha(manifest),
        deleted_files=report['files'], deleted_bytes=report['bytes'], remaining=remaining)
    write(manifest.with_name(manifest.stem + '-verified.json'), result)
    print(json.dumps(result))
    return bool(remaining)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'verify'))
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--exclude', action='append', default=[], help='Active build directory to leave untouched (repeatable)')
    args = parser.parse_args()
    target = local(args.manifest)
    raise SystemExit(plan(target, args.exclude) if args.action == 'plan' else verify(target))
