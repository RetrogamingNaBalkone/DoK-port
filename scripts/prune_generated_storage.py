"""Inventory redundant generated files; deletion is done by the PowerShell driver."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / 'research/_migration/prune-generated-files.json'
REPORT = ROOT / 'analysis/storage-cleanup-20261027.json'
TEXT = {'.json', '.log', '.txt', '.md', '.csv', '.tsv', '.asm'}
SOURCE = {'.c', '.h', '.inc', '.S', '.ld', '.py', '.ps1', '.java'}
CONTROLS = ('research/experiments/g20a-hw-entity-final/hw_v128_entity_rect/',
            'build/gba/g20b-playtest2/hardware/hw_v128_entity_rect/')
INPUTS = ('build/gba/select-menu-fix/', 'build/gba/level1-fixes/', 'build/gba/g20/',
          'build/gba/assets/', 'build/gba/host/')
ROMS = {'artifacts/gba/DoK-port_UI01.gba', 'artifacts/gba/DoK-port_hardware_cached_prototype.gba'}


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def files(directory):
    for path in (ROOT / directory).rglob('*'):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f'Reparse point: {path}')
        if path.is_file():
            yield path


def keep(path):
    name = path.relative_to(ROOT).as_posix()
    if name.startswith('artifacts/'):
        return path.suffix != '.gba' or name in ROMS
    if name.startswith(('build/gba/zig-cache/', 'build/gba/downloads/', 'scripts/__pycache__/')):
        return False
    if name.startswith(INPUTS) or name.startswith(CONTROLS):
        return True
    # Frozen G18 is an independent pixel oracle, not a reconstructible latest build.
    if name.startswith('build/gba/g18/') and path.name in ('dok-diagnostic.gba', 'dok-diagnostic.elf', 'dok-diagnostic.map', 'build-report.json'):
        return True
    # Original DOS observations are distinct from generated GBA screenshots.
    if name.startswith(('build/gba/g20b/dos-ru/', 'build/gba/g20b/dos-en/')):
        return '/game/' not in name
    if name.startswith('build/') and path.parent == ROOT / 'build' and path.suffix == '.png':
        return True  # user-supplied playtest references
    if path.suffix in TEXT:
        return True
    return '/source/' in name and path.suffix in SOURCE


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def plan():
    if PLAN.exists():
        raise ValueError('Plan already exists')
    deleted, protected = [], []
    # Reuse the inventory just made for relocation instead of rereading 18 GiB
    # of files about to be discarded. Retained bytes are checked after deletion.
    migration = json.loads((ROOT / 'research/_migration/files-20261027.json').read_text(encoding='utf-8'))
    known = {item['path']: item['sha256'] for item in migration['protected']}
    known.update({item['destination']: item['sha256'] for item in migration['files']})
    def record(path):
        name = path.relative_to(ROOT).as_posix()
        digest = known[name] if name in known else sha(path)
        return dict(path=name, bytes=path.stat().st_size, sha256=digest)
    directories = ('research/experiments', 'build', 'artifacts', 'scripts/__pycache__')
    for directory in directories:
        for path in files(directory):
            item = record(path)
            (protected if keep(path) else deleted).append(item)
    # Preserve all original samples and the relocated Ghidra/evidence bytes.
    for directory in ('originals', 'research/ghidra', 'research/evidence'):
        for path in files(directory):
            protected.append(record(path))
    write(PLAN, dict(delete=deleted, protected=protected))
    summary = dict(status='planned', deleted_files=len(deleted), deleted_bytes=sum(p['bytes'] for p in deleted),
                   protected_files=len(protected), manifest=PLAN.relative_to(ROOT).as_posix(), manifest_sha256=sha(PLAN),
                   retained_roms=sorted(ROMS), controls=list(CONTROLS), build_inputs=list(INPUTS))
    write(REPORT, summary)
    print(json.dumps(summary, ensure_ascii=False), flush=True)


def verify():
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    assert sha(PLAN) == report['manifest_sha256']
    manifest = json.loads(PLAN.read_text(encoding='utf-8'))
    for item in manifest['delete']:
        assert not (ROOT / item['path']).exists(), item['path']
    for item in manifest['protected']:
        assert sha(ROOT / item['path']) == item['sha256'], item['path']
    report.update(status='verified', protected_sha256_verified=len(manifest['protected']))
    report['remaining_bytes'] = {directory: sum(path.stat().st_size for path in files(directory))
                               for directory in ('build', 'research', 'artifacts')}
    write(REPORT, report)
    print(json.dumps(report, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('plan', 'verify'))
    globals()[parser.parse_args().action]()
