"""Plan, update references, and verify the one-time research storage migration.

Directory moves are performed by relocate-research.ps1 using Move-Item.
Historical reports and archived files are never rewritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'analysis/research-relocation-20261027.json'
FILES = ROOT / 'research/_migration/files-20261027.json'
EVIDENCE = set('ai animations audio cadence collision combat enemy-fields flow '
               'interactions items levels movement old-gold reference render '
               'render-layout render-projection render-reference rendering '
               'resource-research resources runtime sounds sprites stage2 stage3 '
               'stage4 stage5 stage6 surfaces time ui weapon-fields worlds'.split())


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def destination(name):
    if name in EVIDENCE or name.startswith('decompiled-'):
        return 'research/evidence/' + name
    if name == 'ghidra':
        return 'research/ghidra/projects'
    if name.startswith('ghidra-'):
        return 'research/ghidra/' + name.removeprefix('ghidra-')
    if name.startswith(('g20a-', 'g20c-')):
        return 'research/experiments/' + name
    return None


def regular_files(directory):
    if directory.is_symlink() or directory.is_junction():
        raise ValueError(f'Reparse point: {directory}')
    for path in sorted(directory.rglob('*')):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f'Reparse point: {path}')
        if path.is_file():
            yield path


def source_files():
    paths = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT
    ).decode('utf-8').split('\0')
    for name in sorted(set(paths)):
        path = ROOT / name
        if (name.startswith(('scripts/', 'docs/', '.vscode/', 'tools/'))
                or name in ('README.md', 'AGENTS.md', '.gitignore')):
            if path.suffix in ('.py', '.ps1', '.java', '.md', '.json') and path.is_file():
                if name not in ('scripts/relocate_research.py', 'scripts/relocate-research.ps1'):
                    yield path


def rewrite(text):
    # Match a complete first directory component; also covers future experiments.
    def replace(match):
        target = destination(match['name'])
        return target if target else match[0]
    return re.sub(r'\bbuild[/\\](?P<name>[A-Za-z0-9_-]+)', replace, text)


def plan():
    if REPORT.exists() or FILES.exists():
        raise ValueError('Migration already planned; use the existing manifest')
    moves, files, protected = [], [], []
    for directory in sorted((ROOT / 'build').iterdir()):
        target = destination(directory.name) if directory.is_dir() else None
        if not target:
            continue
        if (ROOT / target).exists():
            raise ValueError(f'Destination exists: {target}')
        source = directory.relative_to(ROOT).as_posix()
        moves.append(dict(source=source, destination=target))
        print(f'Hashing {source}', flush=True)
        for path in regular_files(directory):
            old = path.relative_to(ROOT).as_posix()
            new = target + '/' + path.relative_to(directory).as_posix()
            files.append(dict(source=old, destination=new, bytes=path.stat().st_size, sha256=sha(path)))
    for directory in ('originals', 'artifacts', 'build'):
        for path in regular_files(ROOT / directory):
            relative = path.relative_to(ROOT).as_posix()
            if directory == 'build' and any(relative.startswith(m['source'] + '/') for m in moves):
                continue
            protected.append(dict(path=relative, sha256=sha(path)))
    write(FILES, dict(files=files, protected=protected))
    write(REPORT, dict(status='planned', moves=moves, files=len(files),
                       bytes=sum(f['bytes'] for f in files), protected_files=len(protected),
                       file_manifest=FILES.relative_to(ROOT).as_posix(), file_manifest_sha256=sha(FILES)))
    print(f'{len(moves)} directories, {len(files)} files, {sum(f["bytes"] for f in files)} bytes', flush=True)


def update():
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    if report['status'] != 'planned':
        raise ValueError('Expected a planned migration')
    edits = []
    for path in source_files():
        raw = path.read_bytes()
        text = raw.decode('utf-8-sig')
        changed = rewrite(text)
        # These collectors construct experiment paths from a directory name.
        if path.name in ('report_gba_hardware_bringup.py', 'report_gba_spans.py'):
            changed = changed.replace("ROOT/'build'/", "ROOT/'research/experiments'/")
        if path.name == 'clean-experiments.ps1':
            changed = changed.replace("Join-Path $taskRoot 'build'", "Join-Path $taskRoot 'research/experiments'")
        if path.name == 'report_gba_world_obj.py':
            changed = changed.replace("Path('build')/folder", "Path('research/experiments')/folder")
        if changed != text:
            before = sha(path)
            saved = ROOT / 'research/_migration/source-before' / path.relative_to(ROOT)
            saved.parent.mkdir(parents=True, exist_ok=True)
            saved.write_bytes(raw)
            # Retain original encoding/BOM and line endings.
            encoding = 'utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'utf-8'
            path.write_bytes(changed.encode(encoding))
            edits.append(dict(path=path.relative_to(ROOT).as_posix(), before=before, after=sha(path),
                              original_copy=saved.relative_to(ROOT).as_posix()))
    report.update(status='moved_references_updated', source_edits=edits)
    write(REPORT, report)
    print(f'Updated {len(edits)} source/document files')


def include_logs():
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    assert report['status'] == 'planned'
    manifest = json.loads(FILES.read_text(encoding='utf-8'))
    sources = {item['source'] for item in manifest['files']}
    for path in sorted((ROOT / 'build').glob('g20*-*.log')):
        if not path.name.startswith(('g20a-', 'g20c-')):
            continue
        old = path.relative_to(ROOT).as_posix()
        new = 'research/experiments/logs/' + path.name
        if old in sources:
            continue
        assert not (ROOT / new).exists()
        report['moves'].append(dict(source=old, destination=new))
        manifest['files'].append(dict(source=old, destination=new, bytes=path.stat().st_size, sha256=sha(path)))
        manifest['protected'] = [item for item in manifest['protected'] if item['path'] != old]
    write(FILES, manifest)
    report.update(files=len(manifest['files']), bytes=sum(item['bytes'] for item in manifest['files']),
                  protected_files=len(manifest['protected']), file_manifest_sha256=sha(FILES))
    write(REPORT, report)


def verify():
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    assert sha(FILES) == report['file_manifest_sha256']
    manifest = json.loads(FILES.read_text(encoding='utf-8'))
    for move in report['moves']:
        assert not (ROOT / move['source']).exists(), move['source']
        assert (ROOT / move['destination']).exists(), move['destination']
    for item in manifest['files']:
        path = ROOT / item['destination']
        assert not (ROOT / item['source']).exists(), item['source']
        assert path.stat().st_size == item['bytes'] and sha(path) == item['sha256'], str(path)
    for item in manifest['protected']:
        assert sha(ROOT / item['path']) == item['sha256'], item['path']
    for item in report.get('source_edits', []):
        assert sha(ROOT / item['original_copy']) == item['before'], item['original_copy']
    report.update(status='verified', moved_sha256_verified=len(manifest['files']),
                  protected_sha256_verified=len(manifest['protected']))
    write(REPORT, report)
    print(f'Verified {len(manifest["files"])} relocated and {len(manifest["protected"])} protected files')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'include_logs', 'update', 'verify'))
    args = parser.parse_args()
    globals()[args.action]()
