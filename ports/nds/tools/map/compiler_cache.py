"""Share Zig's reusable cache and discard only this process's local cache.

Set NDS_KEEP_COMPILER_CACHE=1 before a check to retain its local cache for
debugging. Executables, object proofs and reports in the check's OUT are never
removed here. A killed process can leave a cache for the maintenance cleaner.
"""
from __future__ import annotations

import atexit
import os
from pathlib import Path
import shutil
import stat
from uuid import uuid4
import warnings

_sessions = {}


def _reparse(path):
    if path.is_symlink() or path.is_junction():
        return True
    try:
        return bool(getattr(path.lstat(), 'st_file_attributes', 0) &
                    getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0))
    except FileNotFoundError:
        return False


def _plain_path(path, root):
    """Reject links/junctions before creating or deleting anything below root."""
    if not path.is_relative_to(root):
        raise ValueError(f'Compiler cache outside workspace: {path}')
    current = path
    while True:
        if _reparse(current):
            raise ValueError(f'Compiler cache traverses a reparse point: {current}')
        if current == root:
            break
        current = current.parent


def _remove_cache(path, root):
    parent = root / 'build/nds/.compiler-cache'
    if path.parent != parent or not path.name.startswith('run-'):
        raise ValueError(f'Unexpected compiler cache target: {path}')
    _plain_path(path, root)
    if not path.exists():
        return
    # Validate the entire tree before recursive removal; never follow a reparse
    # point planted in an otherwise owned, uniquely named cache directory.
    def scan_error(error):
        raise error
    for base, directories, names in os.walk(path, followlinks=False, onerror=scan_error):
        for name in directories + names:
            item = Path(base) / name
            if _reparse(item):
                raise ValueError(f'Compiler cache contains a reparse point: {item}')
    shutil.rmtree(path)


def _cleanup_at_exit(path, root):
    try:
        _remove_cache(path, root)
    except (OSError, ValueError) as error:
        # Do not replace a compiler/check exception with a cleanup failure.
        warnings.warn(f'Local compiler cache retained: {error}', RuntimeWarning)


def compiler_environment(root):
    """Return an independent environment for one shared-cache compiler session."""
    root = Path(root).absolute()
    if root not in _sessions:
        parent = root / 'build/nds/.compiler-cache'
        shared = root / 'build/zig-global-cache'
        _plain_path(parent, root)
        _plain_path(shared, root)
        parent.mkdir(parents=True, exist_ok=True)
        shared.mkdir(parents=True, exist_ok=True)
        local = parent / ('run-' + uuid4().hex)
        local.mkdir()  # Inherit workspace ACLs on Windows.
        _sessions[root] = (local, shared)
        if os.environ.get('NDS_KEEP_COMPILER_CACHE') != '1':
            atexit.register(_cleanup_at_exit, local, root)
    local, shared = _sessions[root]
    return dict(os.environ, ZIG_LOCAL_CACHE_DIR=str(local),
                ZIG_GLOBAL_CACHE_DIR=str(shared))
