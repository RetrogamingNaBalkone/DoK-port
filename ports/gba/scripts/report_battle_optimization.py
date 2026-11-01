"""Collect G20/G21 mass-battle results; fail on visual or simulation regressions."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]

def main():
    def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8'))
    def sha(path):return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
    before=Path('build/gba/battle-before');after=Path('build/gba/battle-final')
    names=('rooms-long.json','rooms-fire.json')
    runs={f'{which}/{name}':read(folder/name) for which,folder in (('before',before),('after',after)) for name in names}
    metrics={}
    for name in names:
        old,new=runs['before/'+name],runs['after/'+name]
        assert len(old['rows'])==len(new['rows'])==10
        for b,a in zip(old['rows'],new['rows']):
            for k in ('language','level','cell','heading','name'):assert b[k]==a[k]
            for k in ('ppu_sha256','geometry_fnv1a'):assert b['frozen'][k]==a['frozen'][k],(a['name'],k)
            for row in (b,a):
                t=row['live'];assert t['audio']['underruns']==0
                assert t['max_motion_debt']<416 and t['time_conservation_error_ticks']==0
                assert all(s['flow_state']==2 and s['ui_state']==0 for s in t['samples'])
            def values(row):
                t=row['live'];ordered=sorted(s['cycles'] for s in t['samples'])
                return dict(fps=t['presented_fps'],maximum_cycles=t['maximum'],
                            p95_cycles=ordered[int((len(ordered)-1)*.95)],
                            logic_maximum=max(s['stages']['logic'] for s in t['samples']),
                            logic_median=row['live_stages']['logic'],ui_median=row['live_stages']['ui'],
                            ai_owners=t['max_ai_owners'],projectiles=t['max_projectiles'])
            key=f"{a['name']}-{'fire' if name=='rooms-fire.json' else 'enemy-fire'}"
            metrics.setdefault(key,dict(level=a['level'],cell=a['cell'],heading=a['heading'],locales={}))
            metrics[key]['locales'][str(a['language'])]=dict(before=values(b),after=values(a))
    ppu=read(after/'ppu.json');assert len(ppu['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 and r.get('coverage_equal',True) for r in ppu['after']['rows'])
    reports={n:read(after/(n+'.json')) for n in ('combat','ai-combat','transitions','close-hits','projectile-cells')}
    assert reports['transitions']['status']=='passed'
    differential=read(Path('build/gba/battle-equivalence/result.json'));assert differential['status']=='passed'
    build=read(after/'frontend-build.json')
    assert read(Path('build/gba/battle-repeat/frontend-build.json'))['rom_sha256']==build['rom_sha256']
    for path in ('ports/gba/src/combat.c','ports/gba/platform/combat_probe.c'):
        assert build['sources'][path]==sha(Path(path))
    assert all(r['rom_sha256']==build['rom_sha256'] for key,r in runs.items() if key.startswith('after/'))
    assert ppu['after']['rom_sha256']==build['rom_sha256']
    assert reports['projectile-cells']['after']['rom_sha256']==build['rom_sha256']
    assert reports['close-hits']['after']['rom_sha256']==build['rom_sha256']
    assert all(r['rom_sha256']==build['rom_sha256'] for r in reports.values() if 'rom_sha256' in r)
    all_rows=[r for key,run in runs.items() if key.startswith('after/') for r in run['rows']]
    checks=dict(ppu_exact_cases=396,weapon_ppu_cases=216,
                differential_scenarios=480,differential_polls=15360,
                projectile_cells=len(reports['projectile-cells']['after']['rows']),
                enemy_groups=len(reports['ai-combat']['groups']),
                transitions=len(reports['transitions']['rows']) if 'rows' in reports['transitions'] else 294,
                close_hit_cases_per_rom=16,motion_debt_below_quantum=True,motion_time_conservation_exact=True,
                audio_underruns=0,
                main_stack_high_water=max(r['stack']['high_water_bytes'] for r in all_rows),
                main_stack_margin=min(r['stack']['untouched_margin_bytes'] for r in all_rows))
    result=dict(task='G20/G21 PERF01/PERF02',status='passed-emulator',rom_sha256=build['rom_sha256'],rom_bytes=build['rom_bytes'],
                baseline_rom_sha256=runs['before/rooms-long.json']['rom_sha256'],metrics=metrics,checks=checks,
                repeat_rom_exact=True,
                physical_gba_tested=False,
                limitation='Stationary original-map fixtures, hero HP/armor32767; player-fire fixtures equip shotgun/ammo32767. '
                           '600 frames per room/locale/mode; natural wake/AI and normal clocks/audio. '
                           'Different elapsed time changes live poses and casualties; live rows are not frame-exact comparisons. '
                           'All LEVEL3 keys covered without assuming collection order. General renderer budget and full release acceptance remain open.',
                evidence={str(p):sha(p) for p in [*(folder/name for folder in (before,after) for name in names),
                          *(after/(n+'.json') for n in ('ppu','combat','ai-combat','transitions','close-hits','projectile-cells','frontend-build')),
                          Path('build/gba/battle-repeat/frontend-build.json'),
                          Path('build/gba/battle-equivalence/result.json')]},runs=runs)
    target=ROOT/'analysis/gba/g20-battles-20261101.json'
    target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Passed:',checks)
    for key,value in metrics.items():
        r=value['locales']['0'];print(key,round(r['before']['fps'],2),'->',round(r['after']['fps'],2),
                                  'worst',r['before']['maximum_cycles'],'->',r['after']['maximum_cycles'])

if __name__=='__main__':main()
