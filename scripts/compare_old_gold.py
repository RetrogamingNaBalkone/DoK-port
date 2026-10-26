"""Byte comparison plus saved Ghidra loader evidence for the two OG.EXE samples."""
from pathlib import Path
import hashlib
import json
import struct
import difflib

ROOT = Path(__file__).resolve().parents[1]
VERSIONS = ('Old_Gold_Old_Version', 'Old_Gold_3D')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    samples = []
    for version in VERSIONS:
        directory = ROOT / 'originals' / version
        data = (directory / 'OG.EXE').read_bytes()
        header = struct.unpack_from('<14H', data)
        declared_size = header[2] * 512 if header[1] == 0 else (header[2]-1)*512+header[1]
        evidence = ROOT / 'build/old-gold' / version
        manifest = json.loads((evidence / 'manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['sha256_before'] == manifest['sha256_after'] == sha(data)
        samples.append(dict(version=version, size=len(data), sha256=sha(data),
                            mz_declared_size=declared_size, mz_header_size=header[4]*16,
                            relocation_count=header[3], trailing_hex=data[declared_size:].hex(),
                            files={p.name: dict(size=p.stat().st_size, sha256=sha(p.read_bytes()))
                                   for p in sorted(directory.iterdir()) if p.is_file()},
                            ghidra=manifest))
    old, new = [(ROOT / 'originals' / v / 'OG.EXE').read_bytes() for v in VERSIONS]
    memory = [(ROOT / 'build/old-gold' / v / 'memory.tsv').read_bytes() for v in VERSIONS]
    listing = [(ROOT / 'build/old-gold' / v / 'listing.asm').read_bytes() for v in VERSIONS]
    readmes = [(ROOT / 'originals' / v / 'README.TXT').read_bytes().decode('cp866').splitlines()
               for v in VERSIONS]
    report = dict(samples=samples, old_is_exact_prefix=new.startswith(old),
                  added_bytes=len(new)-len(old), ghidra_memory_tables_equal=memory[0] == memory[1],
                  ghidra_instruction_listings_equal=listing[0] == listing[1],
                  res_equal=samples[0]['files']['OLD_GOLD.RES'] == samples[1]['files']['OLD_GOLD.RES'],
                  readme_diff=list(difflib.unified_diff(*readmes, fromfile=VERSIONS[0], tofile=VERSIONS[1])))
    assert report['old_is_exact_prefix'] and report['res_equal']
    destination = ROOT / 'analysis/old-gold-comparison.json'
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('samples','readme_diff')}))


if __name__ == '__main__':
    main()
