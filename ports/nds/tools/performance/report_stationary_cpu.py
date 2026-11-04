"""Archive the stationary CPU fix, publish its summary and update N23 handoff."""
import json,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact

def main():
    target=ROOT/'build/nds/n23/discovery32-ru';base=ROOT/'build/nds/n23/mapcache31-ru'
    evidence=ROOT/'research/evidence/nds/n23/ceiling-20261103';evidence.mkdir(parents=True,exist_ok=True)
    paths={'host':ROOT/'build/nds/n23/static-discovery-check/result.json',
        'map_host':ROOT/'build/nds/n23/map-arrow-check/result.json',
        'stationary_baseline':base/'stationary-user-mushrooms/report.json',
        'stationary_candidate':target/'stationary-user-mushrooms/report.json',
        'turn':target/'turning-observer-v2/result.json',
        'live':target/'joint-frontend/live/result.json',
        'route':target/'ordinary-level1-stationary-regression/report.json'}
    reports={key:json.loads(p.read_text('utf8'))for key,p in paths.items()}
    assert all(r['passed']for r in reports.values())
    archived=[]
    for key,p in paths.items():
        dest=evidence/(key+'.json');shutil.copyfile(p,dest);archived.append(artifact(dest))
    for label,t in [('baseline',base),('candidate',target)]:
        p=t/'stationary-user-mushrooms/runtime-profile.json'
        shutil.copyfile(p,evidence/(label+'-stationary-runtime-profile.json'))
        for p in (t/'stationary-user-mushrooms').glob('angle-233*.png'):
            shutil.copyfile(p,evidence/(label+'-'+p.name))
    for p in target.glob('*.log.gz'):shutil.copyfile(p,evidence/p.name)
    for p in (Path(__file__),Path(__file__).with_name('inspect_live_emulator.py'),
              Path(__file__).with_name('check_stationary_room.py'),Path(__file__).with_name('check_static_discovery_cache.py')):
        dest=evidence/'tools'/p.name;dest.parent.mkdir(exist_ok=True);shutil.copyfile(p,dest)
    package=json.loads((ROOT/'artifacts/nds/manifest.json').read_text('utf8'))
    assert package['source']['path']==(target/'dok_nds_gameplay.nds').relative_to(ROOT).as_posix()
    route=reports['route']['performance']['metrics'];turns=reports['turn']['summary']
    report=dict(task='N23',passed=True,candidate='discovery32-ru',published=package['published'],
        evidence=archived,live_process_observation=artifact(evidence/'live-stationary80.json'),
        live_observation_limitations='Original live sampler checked even sequence once; possible publication crossing, not strict causal replay. Corrected double-read sampler could not rerun after user closed PID40272.',
        host=reports['host']['results'],
        stationary={key:reports[key]['samples']for key in ('stationary_baseline','stationary_candidate')},
        turns=turns,route=route,live_levels=[x['level']for x in reports['live']['live_levels']],
        arm_cache_bytes=3644,arm_map_runtime_bytes=11852,
        limitations=['Nearby stationary poses differ; no identical-pose causal FPS comparison',
            'New ROM not yet measured in the user standalone melonDS',
            'Physical DS and stable60 entire-game acceptance remain open'],next_task='N23')
    summary=ROOT/'analysis/nds-stationary-cpu-20261103.json';summary.write_text(json.dumps(report,indent=2)+'\n','utf8')
    for rel in ('plan.md','handoff.md','tasks/N23.md'):
        p=ROOT/'docs/nds'/rel;s=p.read_text('utf8');first,rest=s.split('\n',1)
        # Repair only this conversation's previous notice damaged by the
        # PowerShell stdin encoding; preserve older historical notices.
        clean=rest.lstrip('\n')
        if 'map-cache-20261103.md' in clean.split('\n',1)[0] and '?'in clean.split('\n',1)[0]:
            end=clean.index('\n3 ноября 2026:')
            cachelink='../spec/map-cache-20261103.md'if rel.startswith('tasks/')else'spec/map-cache-20261103.md'
            rest=f'''3 ноября 2026: [кэш нижней карты]({cachelink}), кандидат `mapcache31-ru`.
Сохранённый INDEX8-растр и dirty regions; host и melonDS live/LCD всех трёх
уровней, вращение и маршрут100клеток PASS. Вращение56,59/57,83FPS;
стабильные60 не достигнуты. ROM заменён актуальным discovery32-ru.
Подробности — `analysis/nds-map-cache-20261103.json`. Следующий ID N23.

'''+clean[end:].lstrip('\n')
        link='../spec/stationary-cpu-20261103.md'if rel.startswith('tasks/')else'spec/stationary-cpu-20261103.md'
        route_fps=f"{route['virtual_fps']:.2f}".replace('.',',')
        route_min=f"{route['minimum_window_cadence_fps']:.2f}".replace('.',',')
        notice=f'''Продолжение 3–4 ноября 2026: [stationary CPU]({link}), текущий ROM `discovery32-ru`.
В пользовательском melonDS1.1 прочитаны probes: неподвижный ракурс X23133/Z23900/
heading233, CPU16,315–22,950мс; simulation0,738–4,935мс, discovery3,327мс.
Это диагностическое чтение running process с ограничением исходного seqlock sampler;
эмулятор и RAM не менялись. Повтор после исправления sampler невозможен: процесс закрыт.
Кэшируются только static geometry projection records; sprites/sort/visited выполняются
каждый кадр. Cache3644байта в прежнем scene tail; overflow даёт полный fallback.
Host18432 differential cases, map regression и live/LCD всех3уровней/pause/RUEN/touch PASS.
Близкая stationary-позиция48,061→59,826FPS (poses различаются), discovery3,167→1,340мс.
Вращение58,58/58,21FPS; маршрут LEVEL1 100 клеток: {route_fps}FPS,
минимум секундного окна{route_min}FPS, все≤2VB.
Поставка `artifacts/nds/dok.nds`, SHA `{package['published']['sha256']}`.
Сводка `analysis/nds-stationary-cpu-20261103.json`; команды, ограничения, доказательства —
в спецификации. N23 не принята, N24 не начата; следующий ID N23: повтор нового ROM
на пользовательском stationary-ракурсе и профиль оставшихся CPU пиков.
Физическая DS и stable60 всей игры не подтверждены. GBA/SDL2 статусы не менялись.

'''
        if 'stationary-cpu-20261103.md' in rest.lstrip('\n').split('\n',1)[0]:
            rest=rest[rest.index('\n3 ноября 2026:'):]
        s=first+'\n\n'+notice+rest.lstrip('\n')
        if rel=='plan.md':
            rows=s.splitlines()
            for i,row in enumerate(rows):
                if row.startswith('| [N23](tasks/N23.md) |'):
                    rows[i]='| [N23](tasks/N23.md) | Оптимизация и аппаратная приёмка FPS | N22 | Не принята. discovery32: static disclosure projection reuse, host18432/all3/LCD/100-cell PASS. Близкая stationary-позиция48,06→59,83FPS; пользовательский повтор, stable60 и физическая DS открыты. См. spec/stationary-cpu-20261103.md. |'
            s='\n'.join(rows)+'\n'
        p.write_text(s,'utf8')
    (ROOT/'artifacts/nds/README.md').write_text('''# Текущая NDS-сборка

`dok.nds`: discovery32-ru, один ROM с RU/EN и встроенными ресурсами.
[Stationary CPU и проверки](../../docs/nds/spec/stationary-cpu-20261103.md).
Кэш проекций неподвижной геометрии добавлен к кэшу растра нижней карты.
Стабильные60FPS всей игры пока не подтверждены; нужен повтор на устройстве.
SHA и состав NitroFS — в `manifest.json`.
''','utf8')
    print(json.dumps(dict(passed=True,published=report['published'],route_fps=route['virtual_fps'])))
if __name__=='__main__':main()
