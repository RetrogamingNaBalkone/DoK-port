"""Archive N23 playtest39 evidence and emit a compact, qualified summary."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]


def pin(path):
    raw=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())


def main():
    target=ROOT/'build/nds/n23/playtest39b-ru';evidence=ROOT/'research/evidence/nds/n23/playtest39'
    evidence.mkdir(parents=True,exist_ok=True)
    paths=dict(death=target/'death39/result.json',ui=target/'playtest34/result.json',
        reloads=target/'map38-restarts/result.json',level2=target/'level2-39/result.json',
        baseline=ROOT/'build/nds/n23/profiling38-ru/level2-39/result.json',
        room_spikes=target/'room39-spikes/result.json',room_spikes60=target/'room39-spikes60/result.json',
        baseline_spikes=ROOT/'build/nds/n23/profiling38-ru/room39-spikes/result.json',
        frontend_host=ROOT/'research/evidence/nds/n21/host-state-settings.json',
        flow_host=ROOT/'research/evidence/nds/n22/host/check.json',
        discovery_host=ROOT/'build/nds/n23/static-discovery-check/result.json')
    reports={k:json.loads(p.read_text('utf8'))for k,p in paths.items()}
    assert all(r['passed']for r in reports.values())
    archived=[]
    for name,path in paths.items():
        dest=evidence/(name+'.json');shutil.copyfile(path,dest);archived.append(pin(dest))
    # Retain one copy of the full timing rings used for this comparison.
    for name,subdir in [('baseline','level2-39'),('final','level2-39'),('spikes','room39-spikes'),('spikes60','room39-spikes60')]:
        parent=ROOT/'build/nds/n23/profiling38-ru' if name=='baseline' else target
        dest=evidence/(name+'-runtime-profile.json');shutil.copyfile(parent/subdir/'runtime-profile.json',dest);archived.append(pin(dest))
    for language,attempts in ((1,2),(1,0),(2,0)):
        src=target/f'death39/{language}/death-{language}-{attempts}.png'
        dest=evidence/src.name;shutil.copyfile(src,dest);archived.append(pin(dest))
    def samples(key):
        return [dict(name=s['name'],angle=s['angle'],pose=s['pose'],final_pose=s['final_pose'],
            fps=s['fps'],cpu_ms=s['stages_ms']['cpu_excluding_wait'],
            minimum_fps_500ms=s.get('min_fps_0.5s'),minimum_fps_1s=s.get('min_fps_1s'))for s in reports[key]['samples']]
    manifest=json.loads((target/'build-manifest.json').read_text('utf8'))
    # Build-time sources are frozen. Subsequent observer/report changes do not
    # affect the ROM; validate every C/header/assembly/build input currently.
    compiled=[r for r in manifest['sources']if Path(r['path']).suffix in ('.c','.h','.inc','.S','.ld','.lds') or Path(r['path']).name.startswith('Makefile')]
    assert all(pin(ROOT/r['path'])==r for r in compiled)
    summary=dict(task='N23',build='playtest39b-ru',death_screen_fixed=True,level2_fps_fixed=False,
        user_stability_observation='About 30 minutes active play on profiling38 without reported crash; user report, not exhaustive proof',
        user_fps_observation='Brief dips, including stationary view toward bars in LEVEL2 left third room from bottom',
        artifact=pin(ROOT/'artifacts/nds/dok.nds'),build_manifest=pin(target/'build-manifest.json'),
        compiled_source_pins_verified=len(compiled),checks=dict(frontend=reports['frontend_host']['checks'],
            flow=reports['flow_host']['test']['checks'],death_screens=len(reports['death']['rows']),
            discovery_updates=sum(r['cases']for r in reports['discovery_host']['results']),
            projection_vectors=sum(r['projection_vectors']for r in reports['discovery_host']['results'])),
        baseline=samples('baseline'),candidate=samples('level2'),room_spikes=samples('room_spikes'),room_spikes60=samples('room_spikes60'),
        reports=archived,limitations=['Temporary spawn/HP RAM fixtures; normal native retry and live AI, not a playthrough',
            'Dynamic poses differ; no identical-scene causal FPS proof','Stationary user view/history not exactly matched',
            'Physical DS and new DraStic candidate untested','No new long soak; FPS acceptance remains open'],next_id='N23')
    summary['queued_scenarios']=[dict(level=3,user_minimum_fps=50,executed=False,
        reproduction='Turn around from spawn, go left; retain four executioners alive; open key/monster room; run while enemies fire actively'),
        dict(scope='Monster shots, not player shots',user_sustained_fps_loss='1-2',executed=False,
             verification='Compare same view/world without and with monster fire; separate AI/combat/projectiles/sprites/audio')]
    assert summary['artifact']['sha256']==next(r['sha256']for r in manifest['artifacts']if r['path'].endswith('.nds'))
    path=ROOT/'analysis/nds-playtest39-20261108.json';path.write_text(json.dumps(summary,indent=2)+'\n','utf8')
    print(json.dumps(dict(summary=pin(path),checks=summary['checks'],compiled_pins=len(compiled))))


if __name__=='__main__':main()
