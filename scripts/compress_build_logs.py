"""Losslessly pack completed build logs; retain raw SHA pins for restoration.

Only untracked build/**/*.log of at least 1 MiB are considered. Open writers,
links and existing archives are skipped. Research archives are never changed.
"""
import argparse
from contextlib import contextmanager
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from prune_test_storage import ROOT, local, sha, write


@contextmanager
def completed_file(path):
    """On Windows exclude existing and future writers until removal completes."""
    handle = None
    if os.name == 'nt':
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD,
            wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
            wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.CreateFileW(str(path), 0x80000000, 5, None, 3, 0, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield
    finally:
        if handle is not None:
            kernel.CloseHandle(handle)


def compress(path, apply=False):
    path = local(path)
    if not path.is_relative_to(ROOT/'build') or path.suffix != '.log':
        raise ValueError(f'Not a build log: {path}')
    target = local(path.with_suffix('.log.gz'))
    temporary = local(path.with_suffix('.log.gz.partial'))
    if target.exists() or temporary.exists():
        return dict(path=path.relative_to(ROOT).as_posix(), status='skipped-existing-archive')
    with completed_file(path):
        row = dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                   sha256=sha(path), status='planned')
        if not apply:
            return row
        try:
            with path.open('rb') as source, temporary.open('xb') as output:
                with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as packed:
                    shutil.copyfileobj(source, packed)
            with gzip.open(temporary, 'rb') as restored:
                digest = hashlib.file_digest(restored, 'sha256').hexdigest()
            if digest != row['sha256'] or sha(path) != digest:
                raise ValueError(f'Log changed or gzip verification failed: {path}')
            row.update(archive=target.relative_to(ROOT).as_posix(),
                       archive_bytes=temporary.stat().st_size, archive_sha256=sha(temporary))
            write(local(target.with_suffix('.gz.json')), row)
            temporary.rename(target)
            path.unlink()
            row['status'] = 'compressed'
            write(local(target.with_suffix('.gz.json')), row)
        finally:
            temporary.unlink(missing_ok=True)
        return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    manifest = local(args.manifest)
    if manifest.exists():
        raise ValueError('Use a new manifest')
    tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT)
                  .decode('utf-8').split('\0'))
    rows = []
    for base, dirs, names in os.walk(ROOT/'build', followlinks=False):
        dirs[:] = [d for d in dirs if not (Path(base)/d).is_symlink()
                   and not (Path(base)/d).is_junction()]
        for name in names:
            if not name.endswith('.log'):
                continue
            path = local(Path(base)/name)
            if path.relative_to(ROOT).as_posix() in tracked or path.stat().st_size < 1024**2:
                continue
            try:
                rows.append(compress(path, args.apply))
            except PermissionError as error:
                rows.append(dict(path=path.relative_to(ROOT).as_posix(),
                                 status='skipped-open-or-inaccessible', error=str(error)))
    packed = [r for r in rows if r['status'] == 'compressed']
    report = dict(applied=args.apply, files=len(packed),
        saved_bytes=sum(r['bytes']-r['archive_bytes'] for r in packed), logs=rows)
    write(manifest, report)
    print(json.dumps({k: report[k] for k in ('applied', 'files', 'saved_bytes')}))


if __name__ == '__main__':
    main()
