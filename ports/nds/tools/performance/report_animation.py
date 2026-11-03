"""Archive the final bounded checks and update N23's self-contained handoff."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
def pin(p):
    return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def main():
    target=ROOT/'build/nds/n23/animation26-ru'
    evidence=ROOT/'research/evidence/nds/n23/animation-20261103';evidence.mkdir(parents=True,exist_ok=True)
    inputs={
        'host':ROOT/'build/nds/n23/animation-clock/result.json',
        'animation':target/'animation-observer/result.json',
        'route':target/'ordinary-level2-door-final/report.json',
        'live':target/'joint-frontend/live/result.json',
    }
    reports={k:json.loads(p.read_text('utf8'))for k,p in inputs.items()}
    assert all(r['passed']for r in reports.values())
    archived=[]
    for k,p in inputs.items():
        destination=evidence/(k+'.json');shutil.copyfile(p,destination);archived.append(pin(destination))
    for p in (target/'animation-observer').glob('*.png'):
        shutil.copyfile(p,evidence/p.name)
    for p in target.glob('*.log.gz'):shutil.copyfile(p,evidence/p.name)
    shutil.copyfile(target/'ai-layout.json',evidence/'ai-layout.json')
    package=json.loads((ROOT/'artifacts/nds/manifest.json').read_text('utf8'))
    assert package['published']['sha256']=='6271c50db8183fc9f897d1bd38bd1afff00d36193c2966e957563923a8cf220f'
    a=reports['animation'];r=reports['route']
    report=dict(task='N23',date='2026-11-03',passed=True,package=package['published'],
        host_checks=reports['host']['checks'],core_regression_checks=reports['host']['core_regression_checks'],
        animation={k:a[k]for k in ('water_samples','distinct_phases','pause_frozen','fps_counter','lcd_fps_tile_colors')},
        route=dict(level=2,cells=r['route_cells_reached'],video_frames=r['actual_video_frames'],
            door_openings=r['playtest_audit']['door_openings'],metrics=r['performance']['metrics']),
        live_levels=[v['level']for v in reports['live']['live_levels']],evidence=archived,
        limitations=['Physical DS and DraStic untested','Bounded regressions, not full N23 acceptance',
            'Door and water timing are documented NDS adaptations of frame-dependent DOS behavior'],next_task='N23')
    (ROOT/'analysis/nds-animation-20261103.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    for name in ('plan.md','handoff.md','tasks/N23.md'):
        p=ROOT/'docs/nds'/name;s=p.read_text('utf8')
        link='../spec/animation23-20261103.md' if name.startswith('tasks/') else 'spec/animation23-20261103.md'
        notice=f'''3 ноября 2026: [FPS, вода и потайные двери]({link}), кандидат `animation26-ru`.
FPS справа вверху MAIN; вода60 фаз/с без задержки на стыке18 фаз;
двери используют elapsed time, core state/collision/occupancy сохранены.
Host: 4453 + 101 PASS; LEVEL2 water: 240 samples / 18 phases / pause и LCD FPS PASS;
маршрут100клеток, дверь glyph103/extent58 —10223ticks (~1,020с) PASS.
Live/LCD всех3уровней, пауза/язык/touch PASS на финальном ROM.
`artifacts/nds/dok.nds`, SHA `6271c50db8183fc9f897d1bd38bd1afff00d36193c2966e957563923a8cf220f`.
Сводка `analysis/nds-animation-20261103.json`. Физическая DS/DraStic не проверены.
N23 остаётся в работе, следующий ID N23; N24 не начата. Другие порты не менялись.

'''
        if notice not in s:
            first,rest=s.split('\n',1);p.write_text(first+'\n\n'+notice+rest.lstrip('\n'),'utf8')
    p=ROOT/'artifacts/nds/README.md';s=p.read_text('utf8')
    s=s.replace('[playtest09 от 31 октября 2026 года](../../docs/nds/spec/playtest09-20261031.md)',
        '[FPS и анимации от 3 ноября 2026 года](../../docs/nds/spec/animation23-20261103.md)')
    p.write_text(s,'utf8');print(json.dumps(dict(passed=True,evidence=evidence.as_posix(),rom=package['published'])))
if __name__=='__main__':main()
