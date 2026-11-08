"""Compact N23 angled-door diagnosis; no accepted gameplay optimization."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    target = ROOT / 'build/nds/n23/wrap49b-ru'
    reports = {}
    for name, directory in [('level3', 'approach50d-approach-live'),
                            ('level1', 'room50b-firstroom-live')]:
        path = target / directory / 'result.json'
        report = json.loads(path.read_text('utf8'))
        assert report['passed'], name
        reports[name] = dict(result=pin(path), runtime=pin(path.parent/'runtime-profile.json'),
            samples=[{key: value for key, value in sample.items()
                      if key not in ('peaks', 'captures')} for sample in report['samples']])
    assert (ROOT/'artifacts/nds/dok.nds').read_bytes() == (target/'dok_nds_gameplay.nds').read_bytes()
    result = dict(task='N23', diagnostic_passed=True, issue_fixed=False,
        gameplay_changed=False, rom=pin(ROOT/'artifacts/nds/dok.nds'),
        reports=reports, drastic_tested=False, physical_ds_tested=False,
        limitations=[
            'Explicit spawn/retry/HP fixtures; LEVEL3 also grants key8.',
            'LEVEL1 room survey does not reproduce the exact user route from the blaster.',
            'Discovery is included in sprites; effects includes texture planning.',
            'Angle correlation does not prove a defect in door geometry.',
            'The ARM candidate was redundant: sprites already used ARM; no optimization accepted.'],
        next_task='N23: isolate angled-view geometry/sprite costs and effects/texture-planning peaks')
    path = ROOT/'analysis/nds-room50-20261108.json'
    path.write_text(json.dumps(result, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(diagnostic_passed=True, issue_fixed=False, summary=pin(path))))


if __name__ == '__main__':
    main()
