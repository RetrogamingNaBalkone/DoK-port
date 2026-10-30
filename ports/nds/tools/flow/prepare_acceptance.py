"""Index completed N22 evidence; never infer success from a planned route.

The default output is a draft. --final requires all three ordinary worlds,
FINISH coverage in both languages, and the named supporting gates. This is an
index of existing results, not a replacement for independent result review.
"""
import argparse
import json
from pathlib import Path

from archive import E, ROOT, candidate_inputs, evidence_file, pin

REQUIRED = {
    'original', 'flow_host', 'retry_overlap_host', 'retry_overlap_native',
    'retry_overlap_arm9', 'regression', 'audio', 'vram_planner', 'vram_sdk',
    'memory_rebalance', 'root_lifetime', 'root_memory_oracle',
}
TARGETS = ('build/nds/n22/candidate-partial02-ru',
           'build/nds/n22/candidate-partial02-en')


def local_path(value):
    path = (ROOT / value).resolve()
    assert path.is_relative_to(ROOT.resolve()), value
    return path


def load_report(value, scope=None):
    path = local_path(value)
    report = json.loads(path.read_text('utf8'))
    if scope == 'original':
        assert report['task'] == 'N22' and report['status'] == 'passed'
    elif scope == 'retry_overlap_host':
        assert report['task'] == 'N22' and report['expect_fixed'] is True
        observed = report['observed']
        assert observed['reproduced'] is False and observed['checks'] == 160
        assert observed['game_fault'] == observed['ai_fault'] == observed['callback_status'] == 0
        assert observed['total_ai_callbacks'] == 40
    else:
        assert report.get('passed') is True, ('Unsuccessful report', value)
    if scope == 'root_lifetime':
        assert len(report['reports']) == 4
        languages = []
        for row in report['reports']:
            language = row['language']
            assert language in (1, 2)
            languages.append(language)
            target = ROOT / TARGETS[language - 1]
            assert row['rom'] == pin(target / 'dok_nds_gameplay.nds')
            result_path = local_path(row['report']['path'])
            assert pin(result_path) == row['report']
            result = json.loads(result_path.read_text('utf8'))
            assert result['passed'] is True
            observer = result['observer']
            for key, filename in (('rom', 'dok_nds_gameplay.nds'),
                                  ('elf', 'dok_nds_gameplay.elf')):
                expected = pin(target / filename)
                if key in observer:
                    assert observer[key] == expected
                else:
                    assert observer[key + '_sha256'] == expected['sha256']
        assert sorted(languages) == [1, 1, 2, 2]
    return path, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ordinary', nargs=3, action='append', default=[],
                        metavar=('LEVEL', 'LANGUAGE', 'REPORT'))
    parser.add_argument('--check', nargs=2, action='append', default=[],
                        metavar=('SCOPE', 'REPORT'))
    parser.add_argument('--evidence-list', type=Path,
                        help='JSON array of explicit workspace artifact paths')
    parser.add_argument('--final', action='store_true')
    args = parser.parse_args()
    compiled = []
    for relative in TARGETS:
        target, _, manifest, pairs = candidate_inputs(relative)
        compiled.append(dict(target=relative, inputs=len(pairs),
                             rom=pin(target / 'dok_nds_gameplay.nds'),
                             elf=pin(target / 'dok_nds_gameplay.elf')))
    reports = set()
    ordinary = []
    for level, language, value in args.ordinary:
        level, language = int(level), int(language)
        assert level in (1, 2, 3) and language in (1, 2)
        path, report = load_report(value)
        assert report['task'] == 'N22' and report['completed_playthrough'] is True
        target = TARGETS[language - 1]
        for key, name in (('rom', 'dok_nds_gameplay.nds'),
                          ('elf', 'dok_nds_gameplay.elf')):
            assert report[key] == pin(ROOT / target / name), (path, key)
        rows = report['rows']
        assert rows and all(row['level'] == level for row in rows)
        assert max(row['hero_callbacks'] for row in rows) > 0
        assert max(row['ai_callbacks'] for row in rows) > 0
        assert max(row['combat_callbacks'] for row in rows) > 0
        assert report['play_audio']['nonzero_frames'] > 0
        assert report['touch'] and all(t['hero_after'] > t['hero_before']
                                       for t in report['touch'])
        finish = report['finish_flow']
        assert finish[3] == 0 and finish[5] == level and finish[18] == 0
        assert finish[26] == 3, 'FINISH must come from ordinary gameflow'
        assert report['finish_probe'][3] == 10, 'EINTRO required'
        assert report['finish_probe'][4] == language, 'FINISH presentation locale must be explicit'
        assert report['level_select_after_finish'][3] == 3
        assert report['disk']['passed'] is True
        assert all(name.upper() == '/DOK/SETTINGS.DAT'
                   for name in report['disk']['changed_files'])
        profile_path = path.with_name('runtime-profile.json')
        _, profile = load_report(profile_path)
        assert profile['completed_playthrough'] is True
        assert profile['rom'] == report['rom'] and profile['elf'] == report['elf']
        assert not profile['summary']['metrics']['missing_profiles']
        reports.update((path.relative_to(ROOT).as_posix(),
                        profile_path.relative_to(ROOT).as_posix()))
        ordinary.append(dict(level=level, language=language, report=pin(path),
                             profile=pin(profile_path), presentations=len(rows),
                             finish_flow=finish, rom=report['rom'], elf=report['elf'],
                             performance_budget_passed=profile['summary']['metrics']['budget_passed']))
    checks = []
    scopes = set()
    nonstandard_reports = []
    for scope, value in args.check:
        assert scope in REQUIRED and scope not in scopes, scope
        path, report = load_report(value, scope)
        scopes.add(scope)
        if report.get('passed') is True:
            reports.add(path.relative_to(ROOT).as_posix())
        else:
            nonstandard_reports.append(path.relative_to(ROOT).as_posix())
        checks.append(dict(scope=scope, report=pin(path)))
    for relative in TARGETS:
        for mode in ('settings', 'restart'):
            path, report = load_report(ROOT / relative / ('frontend-' + mode) / 'result.json')
            assert report['rom_sha256'] == pin(ROOT / relative / 'dok_nds_gameplay.nds')['sha256']
            assert report['elf_sha256'] == pin(ROOT / relative / 'dok_nds_gameplay.elf')['sha256']
            reports.add(path.relative_to(ROOT).as_posix())
            checks.append(dict(scope=Path(relative).name + '-' + mode, report=pin(path)))
    pending = []
    if {row['level'] for row in ordinary} != {1, 2, 3}:
        pending.append('Complete ordinary LEVEL1-3 on the current candidates')
    if {row['language'] for row in ordinary} != {1, 2}:
        pending.append('Ordinary FINISH/EINTRO/selection coverage in RU and EN')
    pending.extend('Supporting gate: ' + scope for scope in sorted(REQUIRED - scopes))
    evidence = nonstandard_reports
    if args.evidence_list:
        for value in json.loads(args.evidence_list.read_text('utf8')):
            path = local_path(value)
            assert path.is_file(), value
            assert evidence_file(path), value
            evidence.append(path.relative_to(ROOT).as_posix())
    result = dict(task='N22', passed=not pending,
                  ordinary_playthrough_completed=not pending,
                  scope='Software/emulator N22; N23 performance and N24 hardware/listening remain separate',
                  targets=list(TARGETS), compiled_inputs=compiled,
                  ordinary=ordinary, supporting_checks=checks, pending=pending,
                  reports=sorted(reports), evidence_artifacts=sorted(set(evidence)),
                  generator=pin(Path(__file__).resolve()))
    if args.final:
        assert not pending, pending
        assert evidence, 'Final archive needs explicit supporting artifacts'
        destination = E / 'root-acceptance.json'
    else:
        destination = E / 'acceptance-draft.json'
    destination.write_text(json.dumps(result, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(output=destination.relative_to(ROOT).as_posix(),
                          passed=result['passed'], pending=pending,
                          ordinary=len(ordinary), reports=len(reports))))


if __name__ == '__main__':
    main()
