"""Remove owned objects and generated banks after an isolated experiment build."""
from contextvars import ContextVar
from contextlib import contextmanager
from functools import wraps
from pathlib import Path
import os
import shutil
import stat
from uuid import uuid4

_outputs = ContextVar('experiment_object_outputs', default=None)


def plain_path(path):
    """Check lexical paths before resolving; never delete through a junction."""
    path = Path(os.path.abspath(path))
    for item in (path, *path.parents):
        attributes = getattr(item.lstat(), 'st_file_attributes', 0) if item.exists() else 0
        if item.is_symlink() or item.is_junction() or attributes & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0):
            raise ValueError(f'Experiment output traverses a reparse point: {item}')
    return path


def write_generated_bytes(path, payload):
    """Register a bank before writing, including a partial write on failure.

    Standalone table generators retain their requested outputs. Only an active
    isolated build owns and discards these files; caller-supplied assets are
    never registered.
    """
    path = plain_path(path)
    tracked = _outputs.get()
    if tracked is not None:
        root, paths = tracked
        if path.suffix != '.bin' or not path.is_relative_to(root):
            raise ValueError(f'Generated bank outside experiment output: {path}')
        paths.add(path)
    return path.write_bytes(payload)


def discard_frontend_base(directory):
    """Called only after the frontend's own base has been consumed by linking.

    Standalone baseline builders still retain their objects. Qualification
    drivers explicitly request retention before reusing a frontend base.
    """
    directory = plain_path(directory)
    if directory.name != 'base':
        raise ValueError(f'Unexpected frontend base: {directory}')
    paths = list(directory.glob('*.o')) + [directory/name for name in
            ('render-columns.bin','dok-diagnostic.elf','dok-diagnostic.gba')]
    for path in paths:
        plain_path(path)
    for path in paths:
        path.unlink(missing_ok=True)


@contextmanager
def temporary_workspace(parent, prefix):
    """Use inherited Windows ACLs (tempfile's mode 0700 blocks sandbox access)."""
    parent = Path(parent).resolve()
    parent.mkdir(parents=True, exist_ok=True)
    path = parent / (prefix + uuid4().hex)
    path.mkdir()
    try:
        yield path
    finally:
        if path.resolve().parent != parent or path.is_symlink():
            raise ValueError(f'Unsafe temporary workspace: {path}')
        shutil.rmtree(path)


def record_object_output(args):
    """Called before a compiler runs, so partial failed outputs are covered."""
    tracked = _outputs.get()
    if tracked is None:
        return
    root, paths = tracked
    args = list(map(str, args))
    if '-o' in args:
        path = plain_path(args[args.index('-o') + 1])
        if path.suffix == '.o':
            if not path.is_relative_to(root):
                raise ValueError(f'Experiment object outside output directory: {path}')
            paths.add(path)


def clean_objects_after_build(function):
    @wraps(function)
    def wrapped(base, out, *args, keep_objects=False, **kwargs):
        base, out = plain_path(base), plain_path(out)
        if out.is_relative_to(base) or base.is_relative_to(out):
            raise ValueError('Experiment and baseline directories must be separate')
        paths = set()
        token = _outputs.set((out, paths))
        try:
            return function(base, out, *args, **kwargs)
        finally:
            _outputs.reset(token)
            if not keep_objects:
                for path in sorted(paths):
                    plain_path(path)
                    path.unlink(missing_ok=True)
    return wrapped
