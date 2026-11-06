"""Own one disposable SDL2 compiler cache, shared with nested build commands."""
from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import subprocess
import sys
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))
from experiment_cleanup import plain_path

MARKER = 'DOK_SDL2_BUILD_CACHE'


@contextmanager
def compiler_cache(root=ROOT):
    parent = plain_path(Path(root) / 'build/sdl2/.compiler-cache')
    inherited = os.environ.get(MARKER)
    path = plain_path(Path(inherited)) if inherited else parent / ('run-' + uuid4().hex)
    if path.parent != parent or not path.name.startswith('run-'):
        raise ValueError(f'Unexpected SDL2 compiler cache: {path}')
    if inherited:
        if not path.is_dir():
            raise ValueError(f'Missing inherited compiler cache: {path}')
    else:
        path.mkdir(parents=True)
    keys = (MARKER, 'ZIG_GLOBAL_CACHE_DIR', 'ZIG_LOCAL_CACHE_DIR')
    previous = {key: os.environ.get(key) for key in keys}
    os.environ.update({MARKER: str(path), keys[1]: str(path / 'global'), keys[2]: str(path / 'local')})
    try:
        yield path
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        if not inherited and os.environ.get('DOK_KEEP_COMPILER_CACHE') != '1':
            # Check the entire tree before deleting, including junctions.
            plain_path(path)
            for base, directories, files in os.walk(path):
                for name in directories + files:
                    plain_path(Path(base) / name)
            shutil.rmtree(path)


if __name__ == '__main__':
    with compiler_cache():
        result = subprocess.run(sys.argv[1:], check=False)
    sys.exit(result.returncode)
