"""Resolve historical paths through research and repository-layout migrations."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def script_path(name):
    """Return the current repository-relative path of a former scripts/ file."""
    return resolve_research_path(Path('scripts') / name).relative_to(ROOT)


def resolve_research_path(path):
    path = Path(path)
    absolute = path if path.is_absolute() else ROOT / path
    relative = absolute.resolve().relative_to(ROOT).as_posix()
    for filename in ('research-relocation-20261027.json', 'repository-layout-20261028.json'):
        manifest = json.loads((ROOT / 'analysis' / filename).read_text(encoding='utf-8'))
        moves = manifest['moves'] + manifest.get('directory_moves', [])
        for move in sorted(moves, key=lambda item: len(item['source']), reverse=True):
            old = move['source']
            if relative == old or relative.startswith(old + '/'):
                relative = move['destination'] + relative[len(old):]
                break
    return ROOT / relative


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path')
    args = parser.parse_args()
    result = resolve_research_path(args.path)
    if not result.exists():
        parser.error(f'Path does not exist: {result}')
    print(result)
