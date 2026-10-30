"""Discard owned emulator test disks after runners finish all post-close audits.

NDS_KEEP_EMULATOR_DISK=1 retains a disk for debugging. Settings-producer runs
may also retain it explicitly as an input to the next restart process.
"""
from __future__ import annotations

import atexit
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import warnings

from frontend_fat_oracle import volume_files

_sessions = {}
_FIXTURE_FILES = {'/DOK_ND~1.NDS', '/DOK/NDS-DIAG.TXT', '/DOK/ASSETS.NDP',
    '/DOK/BADHASH.NDP', '/DOK/BADSCH~1.NDP', '/DOK/CONTROL.NDP', '/DOK/TRUNC.NDP',
    '/DOK/WRONG.NDP', '/DOK/INDEX.DAT', '/DOK/SCENE.DAT', '/DOK/UI.DAT',
    '/DOK/FRONTEND.DAT', '/DOK/SETTINGS.DAT'}


def _safe(path, root):
    path = Path(os.path.abspath(path))
    if not path.is_relative_to(root / 'build/nds'):
        raise ValueError(f'Emulator temporary disk outside build/nds: {path}')
    for item in (path, *path.parents):
        if item == root:
            break
        if item.is_symlink() or item.is_junction():
            raise ValueError(f'Emulator temporary disk traverses a reparse point: {item}')
        try:
            attributes = getattr(item.lstat(), 'st_file_attributes', 0)
        except FileNotFoundError:
            continue
        if attributes & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0):
            raise ValueError(f'Emulator temporary disk traverses a reparse point: {item}')
    return path


def _identity(path):
    try:
        info = path.stat()
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(info.st_mode):
        raise ValueError(f'Emulator disk is not a regular file: {path}')
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def _sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class _DiskSession:
    def __init__(self, owner, root, flush):
        self.root = root
        self.directory = _safe(owner.directory, root)
        self.image = _safe(self.directory / 'save/melonDS DS/dldi_sd_card.bin', root)
        self.index = self.image.with_name(self.image.name + '.idx')
        self.metadata = _safe(self.directory / 'disk-retention.json', root)
        self.initial_identity = _identity(self.image)
        self.owners = [owner]
        self.flush = flush

    def _write(self, report):
        _safe(self.metadata, self.root)
        self.metadata.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n',
                                 encoding='utf-8')

    def cleanup(self):
        """Called only at process exit, after runner finally/report sections."""
        # Several Frontends can own one output directory in a single process.
        # Close all of them before inspecting or removing a shared disk.
        for owner in reversed(self.owners):
            if owner.loaded or owner.initialized:
                owner.close()
            if owner.loaded or owner.initialized:
                raise RuntimeError('Core remained active; emulator disk retained')
        _safe(self.image, self.root)
        _safe(self.index, self.root)
        current = _identity(self.image)
        if current is None:
            # An earlier idempotent callback may already have cleaned the disk.
            # Do not overwrite its completed metadata with a missing-disk record.
            return
        if self.flush:
            self.flush()
        report = dict(schema=1, image=self.image.relative_to(self.root).as_posix(),
                      bytes=self.image.stat().st_size, sha256=_sha(self.image))
        try:
            contents = volume_files(self.image)
            unknown = [p for p in contents if p not in _FIXTURE_FILES and
                       not re.fullmatch(r'/DOK/V\d\d(?:TOP|BOT)\.BIN', p)]
            report['files'] = [dict(path=p, bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
                **({'hex': data.hex()} if len(data) <= 4096 else {}))
                for p, data in sorted(contents.items())]
            if unknown:
                report.update(status='retained', reason='unknown-disk-files', unknown=unknown)
                self._write(report)
                return
        except (ValueError, AssertionError, IndexError, KeyError, RuntimeError) as error:
            report.update(status='retained', reason='unrecognised-or-failed-fixture', error=str(error))
            self._write(report)
            return
        reason = None
        if os.environ.get('NDS_KEEP_EMULATOR_DISK') == '1':
            reason = 'NDS_KEEP_EMULATOR_DISK=1'
        elif any(getattr(owner, '_keep_disk_for_restart', False) for owner in self.owners):
            reason = 'settings-producer-input-for-downstream-restart'
        elif self.initial_identity == current:
            reason = 'unchanged-preexisting-disk-not-created-by-this-run'
        if reason:
            report.update(status='retained', reason=reason)
            self._write(report)
            return
        report.update(status='planned-delete', reason='completed-generated-test-disk')
        self._write(report)  # Preserve hashes/settings before deleting any bytes.
        for path in (self.index, self.image):
            _safe(path, self.root)
            if path.exists():
                if not stat.S_ISREG(path.stat().st_mode):
                    raise ValueError(f'Not a regular emulator output: {path}')
                path.unlink()
        report['status'] = 'deleted'
        self._write(report)

    def at_exit(self):
        try:
            self.cleanup()
        except Exception as error:
            # Keep original runner exception/exit status; cleanup errors go to stderr.
            warnings.warn(f'Emulator disk retained after cleanup error: {error}', RuntimeWarning)


def register_disk_cleanup(owner, root, flush=None):
    root = Path(root).resolve()
    key = _safe(owner.directory, root)
    if key in _sessions:
        session = _sessions[key]
        session.owners.append(owner)
    else:
        session = _DiskSession(owner, root, flush)
        _sessions[key] = session
        atexit.register(session.at_exit)
    return session
