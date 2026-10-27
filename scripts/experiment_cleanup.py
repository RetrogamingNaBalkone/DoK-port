"""Remove only object outputs produced by one isolated experiment build."""
from contextvars import ContextVar
from contextlib import contextmanager
from functools import wraps
from pathlib import Path
import shutil
from uuid import uuid4

_outputs = ContextVar('experiment_object_outputs', default=None)


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
        path = Path(args[args.index('-o') + 1]).resolve()
        if path.suffix == '.o':
            if not path.is_relative_to(root):
                raise ValueError(f'Experiment object outside output directory: {path}')
            paths.add(path)


def clean_objects_after_build(function):
    @wraps(function)
    def wrapped(base, out, *args, keep_objects=False, **kwargs):
        base, out = Path(base).resolve(), Path(out).resolve()
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
                    path.unlink(missing_ok=True)
    return wrapped
