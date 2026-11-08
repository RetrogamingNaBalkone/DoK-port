"""Publish qualified paired results for the N23 cell-plane optimization."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    final = ROOT/'build/nds/n23/angles51d-ru'
    baseline = ROOT/'build/nds/n23/wrap49b-ru'
    proofs = []
    comparisons = []
    for section in ('firstroom','approach'):
        paths = [base/f'angles51exact-{section}-live/result.json' for base in (baseline,final)]
        reports = [json.loads(p.read_text('utf8')) for p in paths]
        assert all(r['passed'] and r['exact_heading_fixture'] for r in reports)
        for path in paths:
            proofs += [pin(path),pin(path.parent/'runtime-profile.json')]
        for old,new in zip(reports[0]['samples'],reports[1]['samples'],strict=True):
            assert old['name']==new['name']
            # The moving executioner approach has a65-unit arrival tolerance.
            if old['name']!='executioners':assert old['pose']==new['pose']
            comparisons.append(dict(section=section,name=old['name'],
                baseline_pose=old['pose'],final_pose=new['pose'],
                baseline_fps=old['fps'],final_fps=new['fps'],
                baseline_minimum_window_fps=old['minimum_window_fps'],
                final_minimum_window_fps=new['minimum_window_fps'],
                baseline_geometry_ms=old['stages_ms']['geometry'],
                final_geometry_ms=new['stages_ms']['geometry'],
                baseline_cpu_ms=old['stages_ms']['cpu_excluding_wait'],
                final_cpu_ms=new['stages_ms']['cpu_excluding_wait'],
                baseline_ai=old['ai_active'],final_ai=new['ai_active']))
    checks = {}
    for key,path in {
        'host':ROOT/'build/nds/n23/angles51-host/result.json',
        'stream':ROOT/'build/nds/n23/stream51-host/result.json',
        'discovery':ROOT/'build/nds/n23/static-discovery-check/result.json',
        'ui':final/'playtest34/result.json',
        'reloads':final/'map38-restarts/result.json',
        'live':final/'joint-frontend/live/result.json',
    }.items():
        report=json.loads(path.read_text('utf8'));assert report['passed'],key
        checks[key]=pin(path)
    manifest=json.loads((final/'build-manifest.json').read_text('utf8'))
    for row in manifest['sources']:
        path=ROOT/row['path']
        if path.suffix in ('.c','.h','.inc','.S','.ld','.lds') or path.name.startswith('Makefile'):
            assert pin(path)['sha256']==row['sha256'],row['path']
    rom=ROOT/'artifacts/nds/dok.nds'
    assert rom.read_bytes()==(final/'dok_nds_gameplay.nds').read_bytes()
    summary=dict(task='N23',passed=True,optimization_accepted=True,
        all_heavy_views_60fps=False,rom=pin(rom),build_manifest=pin(final/'build-manifest.json'),
        memory=dict(extra_assets_bytes=256,slab_and_reserve_changed=False),
        checks=checks,comparisons=comparisons,proofs=proofs,
        limitations=[
            'DS-mode melonDS; no physical DS or DraStic test.',
            'Explicit spawn/retry/HP/key and heading fixtures; not a full playthrough.',
            'Exact paired camera poses do not freeze enemy/projectile histories.',
            'Remaining effects/texture-planning/map tails can still miss one VBlank.',
            'LEVEL1 fixtures do not reproduce the exact user route from the blaster.'],
        next_task='N23: remaining renderer tails and full route acceptance; N24 not started')
    path=ROOT/'analysis/nds-angles51-20261108.json'
    path.write_text(json.dumps(summary,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,rom=summary['rom'],comparisons=[
        {k:r[k] for k in ('section','name','baseline_fps','final_fps')} for r in comparisons])))


if __name__=='__main__':
    main()
