"""Retain selected N23 review reports/images/source snapshots, without emulator disks."""
import argparse
import json
from pathlib import Path
import shutil
from audit_pipeline import ROOT, pin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--input', type=Path, action='append', required=True)
    args = parser.parse_args()
    destination = args.destination.resolve()
    assert destination.is_relative_to(ROOT/'research/evidence/nds/n23') and not destination.exists()
    roots = [p.resolve() for p in args.input]
    assert all(p.is_relative_to(ROOT/'build/nds/n23') and p.is_dir() for p in roots)
    files = set()
    for root in roots:
        for source in root.rglob('*'):
            if not source.is_file() or any(part in ('obj', 'generated', '__pycache__', 'system', 'save')
                                            for part in source.relative_to(root).parts):
                continue
            if source.suffix in ('.json', '.png', '.py'):
                files.add(source)
    mappings = []
    for source in sorted(files):
        target = destination/'files'/source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        before = pin(source)
        shutil.copyfile(source, target)
        after = pin(target)
        assert (before['bytes'], before['sha256']) == (after['bytes'], after['sha256'])
        mappings.append(dict(original=before, archive=after))
    result = dict(task='N23', passed=True, scope=__doc__, driver=pin(Path(__file__)), mappings=mappings)
    manifest = destination/'manifest.json'
    manifest.write_text(json.dumps(result, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(passed=True, files=len(mappings), bytes=sum(r['original']['bytes'] for r in mappings),
                          manifest=pin(manifest))))


if __name__ == '__main__':
    main()
