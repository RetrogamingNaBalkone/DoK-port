"""Resolve a historical build path using the recorded research migration."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def resolve_research_path(path):
    path = Path(path)
    absolute = path if path.is_absolute() else ROOT / path
    relative = absolute.resolve().relative_to(ROOT).as_posix()
    manifest = json.loads((ROOT / 'analysis/research-relocation-20261027.json').read_text(encoding='utf-8'))
    for move in sorted(manifest['moves'], key=lambda item: len(item['source']), reverse=True):
        old = move['source']
        if relative == old or relative.startswith(old + '/'):
            return ROOT / (move['destination'] + relative[len(old):])
    return absolute


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path')
    args = parser.parse_args()
    result = resolve_research_path(args.path)
    if not result.exists():
        parser.error(f'Path does not exist: {result}')
    print(result)
