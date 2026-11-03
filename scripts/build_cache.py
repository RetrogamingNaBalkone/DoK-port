"""One owned GBA Zig cache per build tree, shared with child processes.

Only the creator removes the session on exit. Set DOK_KEEP_COMPILER_CACHE=1
to retain it for debugging. A forcibly killed process may leave a directory
for prune_test_storage.py; ordinary failures still run the exit cleanup.
"""
import atexit
import os
from pathlib import Path
import shutil
from uuid import uuid4
import warnings

from experiment_cleanup import plain_path

MARKER = 'DOK_GBA_BUILD_CACHE'
_sessions = {}


def session_path(root, path):
    root, path = plain_path(root), plain_path(path)
    if path.parent != root/'build/gba/.compiler-cache' or not path.name.startswith('run-'):
        raise ValueError(f'Unexpected GBA compiler cache: {path}')
    return path


def remove_session(root, path):
    path = session_path(root,path)
    if not path.exists():
        return
    # Check the entire tree before recursive deletion, including junctions.
    def failed(error):
        raise error
    for base, directories, files in os.walk(path,followlinks=False,onerror=failed):
        for name in directories+files:
            plain_path(Path(base)/name)
    shutil.rmtree(path)


def _finish(root,path):
    try:
        remove_session(root,path)
    except (OSError,ValueError) as error:
        warnings.warn(f'GBA compiler cache retained: {error}',RuntimeWarning)


def compiler_environment(root):
    root=plain_path(root)
    inherited=os.environ.get(MARKER)
    if inherited:
        path=session_path(root,inherited)
        if not path.is_dir():
            raise ValueError(f'Missing inherited compiler cache: {path}')
    else:
        if root not in _sessions:
            parent=plain_path(root/'build/gba/.compiler-cache')
            parent.mkdir(parents=True,exist_ok=True)
            path=parent/('run-'+uuid4().hex)
            path.mkdir()
            _sessions[root]=path
            if os.environ.get('DOK_KEEP_COMPILER_CACHE') != '1':
                atexit.register(_finish,root,path)
        path=_sessions[root]
    return dict(os.environ, **{MARKER:str(path)},
                ZIG_GLOBAL_CACHE_DIR=str(path/'global'),
                ZIG_LOCAL_CACHE_DIR=str(path/'local'))


if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--remove',type=Path,required=True)
    args=parser.parse_args()
    remove_session(Path(__file__).resolve().parents[1],args.remove)
