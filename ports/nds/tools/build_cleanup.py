"""Discard only intermediate ARM objects, retaining ELF/map/stack-use evidence."""
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent/'map'))
from compiler_cache import _plain_path


def discard_objects(target, root):
    root, target = Path(root).absolute(), Path(target).absolute()
    if target.parent != root/'build/nds/n23' or '..' in target.parts:
        raise ValueError(f'Unexpected candidate directory: {target}')
    directory = target/'obj'
    _plain_path(directory, root)
    if os.environ.get('NDS_KEEP_COMPILER_CACHE') == '1' or not directory.exists():
        return
    files = list(directory.iterdir())
    for path in files:
        _plain_path(path, root)
    for path in files:
        if path.is_file() and path.suffix in ('.o', '.d'):
            path.unlink()
