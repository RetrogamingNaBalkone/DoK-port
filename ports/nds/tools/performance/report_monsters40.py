"""Keep the qualified N23 monster-shot/crowd results and selected raw proofs."""
import hashlib
import json
import shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    final=ROOT/'build/nds/n23/shots40b-ru'
    baseline=ROOT/'build/nds/n23/playtest39b-ru'
    evidence=ROOT/'research/evidence/nds/n23/monsters40'
    evidence.mkdir(parents=True,exist_ok=True)
    paths={
        'host':ROOT/'build/nds/n23/monsters40-host/result.json',
        'ui':final/'playtest34/result.json',
        'reloads':final/'map38-restarts/result.json',
        'baseline_exit':baseline/'shots40final-exit-live/result.json',
        'baseline_crowd':baseline/'shots40final-crowd-live/result.json',
        'final_exit':final/'shots40final-exit-live/result.json',
        'final_crowd':final/'shots40final-crowd-live/result.json',
        'final_shotgun':final/'shots40final-shotgun-live/result.json',
        'paired_live':baseline/'monsters40b-exit-live/result.json',
        'paired_quiet':baseline/'monsters40b-exit-quiet/result.json',
        'paired_silent':baseline/'monsters40b-exit-silent/result.json',
    }
    reports={k:json.loads(p.read_text('utf8'))for k,p in paths.items()}
    assert all(r['passed']for r in reports.values())
    proofs=[]
    for name,path in paths.items():
        dest=evidence/(name+'.json');shutil.copyfile(path,dest);proofs.append(pin(dest))
        if name.startswith(('baseline_','final_','paired_')):
            dest=evidence/(name+'-runtime-profile.json')
            shutil.copyfile(path.parent/'runtime-profile.json',dest);proofs.append(pin(dest))
    samples={k:[{n:v for n,v in s.items()if n not in ('peaks','captures')}
                for s in r['samples']]for k,r in reports.items()if 'samples'in r}
    comparisons=[]
    for scene in ('crowd','exit'):
        a=samples['baseline_'+scene][0];b=samples['final_'+scene][0]
        per_callback=lambda s:s['callback_ticks']['combat']/s['callbacks']['combat']/33513.982
        old,new=per_callback(a),per_callback(b)
        comparisons.append(dict(scene=scene,baseline_combat_ms_per_callback=old,
            final_combat_ms_per_callback=new,reduction_percent=(1-new/old)*100,
            baseline_fps=a['fps'],final_fps=b['fps'],
            limitation='Live callback counts, projectile states and tail clock timings differ; not an instruction-cycle benchmark.'))
    packaged=ROOT/'artifacts/nds/dok.nds'
    assert packaged.read_bytes()==(final/'dok_nds_gameplay.nds').read_bytes()
    summary=dict(task='N23',passed=True,optimization_accepted=True,crowd_fps_issue_closed=False,
        physical_ds_tested=False,drastic_tested=False,whole_playthrough=False,
        rom=pin(packaged),build_manifest=pin(final/'build-manifest.json'),
        memory=dict(extra_world_bytes=768,world_region_bytes=393216,reserve_bytes=655360,
                    limits_changed=False),
        host_checks_per_variant=sum(x['checks']for x in reports['host']['variants']['production'].values()),
        host_variants=list(reports['host']['variants']),comparisons=comparisons,samples=samples,
        user_reports=dict(monster_shots_fps_loss='1-2',key_room_fps=50,shotgun_room_fps=50,exit_room_fps=36),
        limitations=['Spawn/HP/max-HP fixtures and retry; native AI/combat/doors remain active.',
                     'Quiet resets burst clocks; silent suppresses combat audio notification sink only.',
                     'Shotgun room is the W pickup at cell (17,30); V at (36,60) is a separate control.',
                     'All results are emulator measurements; user exit 36 FPS not reproduced.',
                     'Runtime reports retain actual intervals and stage peaks; averages do not close short spikes.'],
        proofs=proofs,next_task='N23: reproduce the natural crowded exit history and remaining renderer peaks')
    (ROOT/'analysis/nds-monsters40-20261108.json').write_text(json.dumps(summary,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,rom=summary['rom'],comparisons=comparisons)))


if __name__=='__main__':main()
