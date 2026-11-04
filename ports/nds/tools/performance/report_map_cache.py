"""Archive map-cache measurements and publish the bounded N23 result."""
import json,shutil
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact

def main():
    target=ROOT/'build/nds/n23/mapcache31-ru'
    base=ROOT/'build/nds/n23/animation26-ru'
    evidence=ROOT/'research/evidence/nds/n23/map-cache-20261103'
    evidence.mkdir(parents=True,exist_ok=True)
    paths={'host':ROOT/'build/nds/n23/map-arrow-check/result.json',
           'turn_baseline':base/'turning-observer-v2/result.json',
           'turn_candidate':target/'turning-observer-v2/result.json',
           'route_baseline':base/'ordinary-level1-mapcache-control/report.json',
           'route_candidate':target/'ordinary-level1-first-fights/report.json',
           'live':target/'joint-frontend/live/result.json'}
    reports={k:json.loads(p.read_text('utf8'))for k,p in paths.items()}
    assert all(r['passed']for r in reports.values())
    pins=[]
    for key,p in paths.items():
        dest=evidence/(key+'.json');shutil.copyfile(p,dest);pins.append(artifact(dest))
    for p in target.glob('*.log.gz'):shutil.copyfile(p,evidence/p.name)
    for p in (target/'turning-observer-v2').glob('right*.png'):shutil.copyfile(p,evidence/p.name)
    for label in ('maparrow27-ru','maparrow28-ru','maparrow29-ru','mapcache30-ru'):
        t=ROOT/'build/nds/n23'/label
        for rel in ('turning-observer/result.json','turning-observer-v2/result.json','ordinary-level1-first-fights/report.json'):
            p=t/rel
            if p.exists():shutil.copyfile(p,evidence/(label+'-'+rel.replace('/','-')))
    package=json.loads((ROOT/'artifacts/nds/manifest.json').read_text('utf8'))
    assert package['source']['path']==(target/'dok_nds_gameplay.nds').relative_to(ROOT).as_posix()
    routes={k:dict(cells=reports[k]['route_cells_reached'],metrics=reports[k]['performance']['metrics'],
                   map_stages={n:v for n,v in reports[k]['performance']['stages_ms'].items()if n.startswith('map')})
            for k in ('route_baseline','route_candidate')}
    summary=dict(task='N23',date='2026-11-03',passed=True,candidate='mapcache31-ru',
        published=package['published'],host=reports['host']['results'],
        turns={k:reports[k]['summary']for k in ('turn_baseline','turn_candidate')},routes=routes,
        live_levels=[r['level']for r in reports['live']['live_levels']],
        evidence=pins,next_task='N23',
        limitations=['melonDS DS 1.4.0 only; physical DS and DraStic untested',
            'Equal-duration turning input; route policies and simulated poses can differ, route averages are not causal pairs',
            'Pan/zoom, inventory reveal and glyph metadata changes retain complete redraw',
            'Map DMA remains the complete visible bitmap; hardware affine pan/zoom not implemented',
            'Full N23 FPS acceptance and N24 remain open'])
    (ROOT/'analysis/nds-map-cache-20261103.json').write_text(json.dumps(summary,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,published=summary['published'],turns=summary['turns'],routes={k:v['metrics']['virtual_fps']for k,v in routes.items()})))
if __name__=='__main__':main()
