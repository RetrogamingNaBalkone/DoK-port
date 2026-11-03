"""SP01: audit existing Ghidra exports, without changing original samples."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    samples = json.loads((ROOT / 'analysis/originals/reference-manifest.json').read_text())['samples']
    profiles = [(x['language'], x['path'], x['inventory_sha256'], x['language'], 'Kremlin') for x in samples]
    og = json.loads((ROOT / 'analysis/originals/old-gold/verification.json').read_text())
    profiles.append(('old-gold', og['ghidra']['sample'], og['ghidra']['sha256_before']['OG.EXE'], 'old-gold-full', 'OldGold'))
    result = {'schema': 1, 'scope': 'Existing Ghidra export inventory; not semantic equivalence', 'profiles': []}
    for name, sample, expected, project_dir, project_name in profiles:
        before = digest(ROOT / sample)
        if before != expected:
            raise RuntimeError(f'Unexpected original: {sample}')
        export = ROOT / 'decompiled' / name
        project = Path('research/ghidra/projects') / project_dir / (project_name + '.gpr')
        if not (ROOT / project).is_file():
            raise RuntimeError(f'Missing project: {project}')
        files = {}
        for filename in ('decompiled.c', 'listing.asm', 'functions.tsv', 'references.tsv', 'blocks.tsv', 'unrecovered.tsv', 'ghidra.log', 'summary.txt'):
            p = export / filename
            files[filename] = {'bytes': p.stat().st_size, 'sha256': digest(p)}
        summary = dict(line.split('=', 1) for line in (export / 'summary.txt').read_text().splitlines() if '=' in line)
        warnings = [p.name for p in sorted((export / 'functions').glob('*.c')) if 'WARNING:' in p.read_text(encoding='utf-8')]
        raw_log = (export / 'ghidra.log').read_bytes()
        log = raw_log.decode('utf-16' if raw_log.startswith(b'\xff\xfe') else 'utf-8-sig')
        result['profiles'].append({'id': name, 'sample': sample, 'sha256_before': before,
            'sha256_after': digest(ROOT / sample), 'project': project.as_posix(),
            'export': export.relative_to(ROOT).as_posix(), 'summary': summary, 'files': files,
            'warning_functions': warnings,
            'log_error_lines': [line for line in log.splitlines() if 'ERROR' in line]})
    out = ROOT / 'analysis/source-port/sp01-inventory.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    for p in result['profiles']:
        print(p['id'], p['summary']['functions'], 'functions;', len(p['warning_functions']), 'warning functions; hashes verified')


if __name__ == '__main__':
    main()
