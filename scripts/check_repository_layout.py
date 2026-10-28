"""Check migrated evidence, Python syntax, CMake paths and Markdown links."""
import ast
import hashlib
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]


def main():
    manifest = json.loads((ROOT / 'analysis/repository-layout-20261028.json').read_text(encoding='utf-8'))
    errors = []
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    for relative, expected in manifest['originals_sha256'].items():
        if digest(ROOT / relative) != expected:
            errors.append(f'Original changed: {relative}')
    evidence = 0
    for move in manifest['moves']:
        if move['source'].startswith('analysis/'):
            evidence += 1
            if digest(ROOT / move['destination']) != move['sha256_before']:
                errors.append(f'Historical evidence changed: {move["destination"]}')
    python_files = [p for base in ('ports', 'tools', 'scripts') for p in (ROOT / base).rglob('*.py') if '__pycache__' not in p.parts]
    for path in python_files:
        try:
            source = path.read_text(encoding='utf-8-sig')
            ast.parse(source, filename=str(path))
            if path.parent == ROOT / 'ports/gba/scripts' and 'script_path(' in source and 'from research_paths import script_path' not in source:
                errors.append(f'Missing script_path import: {path}')
        except SyntaxError as error:
            errors.append(str(error))
    for path in (ROOT / 'ports/gba/scripts').glob('*.ps1'):
        if '"scripts/$' in path.read_text(encoding='utf-8-sig'):
            errors.append(f'Unmigrated driver array path: {path}')
    cmake = ROOT / 'ports/gba/CMakeLists.txt'
    cmake_sources = re.findall(r'\b(?:src|tests|tools|platform)/[\w./-]+\.c\b', cmake.read_text(encoding='utf-8'))
    for relative in cmake_sources:
        if not (cmake.parent / relative).is_file():
            errors.append(f'Missing CMake source: {relative}')
    markdown = [ROOT / 'README.md', ROOT / 'AGENTS.md']
    markdown += [p for base in ('docs', 'ports', 'artifacts') for p in (ROOT / base).rglob('*.md')]
    broken = []
    for path in markdown:
        for match in re.finditer(r'\]\(([^)]+)\)', path.read_text(encoding='utf-8-sig')):
            link = match[1].strip('<>')
            if re.match(r'\w+://|mailto:|#', link):
                continue
            target = unquote(link.split('#')[0])
            if target and not (path.parent / target).exists():
                broken.append(f'{path.relative_to(ROOT).as_posix()}: {link}')
    print(json.dumps({'originals_unchanged': len(manifest['originals_sha256']),
                      'historical_data_unchanged': evidence, 'python_syntax': len(python_files),
                      'cmake_source_paths': len(set(cmake_sources)),
                      'markdown_files': len(markdown), 'broken_links': broken, 'errors': errors},
                     ensure_ascii=False, indent=2))
    return bool(errors or broken)


if __name__ == '__main__':
    sys.exit(main())
