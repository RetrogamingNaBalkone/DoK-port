"""Bind BUG01 before/after evidence, regression and paired battle measurements."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from verify_gba import sha


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ROOT/'build/gba/g21-bat-fixed')
    ap.add_argument('--control',type=Path,default=ROOT/'build/gba/g21-final/hardware/hw_v128_vblank_only')
    args=ap.parse_args()
    out=args.out;build=out/'hardware/hw_v128_vblank_only'
    before=read(out/'bat-hits-before.json');after=read(out/'bat-hits.json')
    bsha=sha(args.control/'dok-diagnostic.gba');asha=sha(build/'dok-diagnostic.gba')
    assert before['rom_sha256']==bsha and after['rom_sha256']==asha
    assert before['script_sha256']==after['script_sha256']==sha(ROOT/'ports/gba/scripts/audit_g21_bat_hits.py')
    assert len(before['rows'])==len(after['rows'])==2880 and not before['centered_policy'] and after['centered_policy']
    fixed=0
    for a,b in zip(before['rows'],after['rows']):
        for key in ('language','level','target','weapon','heading','distance','offset','hero','target_position'):
            assert a[key]==b[key]
        assert b['oracle_hit'] and b['hits']==1 and not b['misses']
        assert b['health_after']==100-(14 if b['weapon']==1 else 4)
        fixed+=not a['oracle_hit']
    assert fixed==216
    bperf=read(out/'battles-before.json');aperf=read(out/'battles-after.json')
    assert bperf['rom_sha256']==bsha and aperf['rom_sha256']==asha
    assert bperf['samples']==aperf['samples']==600 and bperf['fire'] and aperf['fire']
    comparisons=[]
    for b,a in zip(bperf['rows'],aperf['rows']):
        assert (b['language'],b['name'])==(a['language'],a['name'])
        assert b['frozen']['ppu_sha256']==a['frozen']['ppu_sha256']
        assert a['live']['audio']['underruns']==0 and a['live']['max_motion_debt']<416
        assert a['live']['time_conservation_error_ticks']==0
        comparisons.append(dict(language=a['language'],scene=a['name'],
            fps_before=b['live']['presented_fps'],fps_after=a['live']['presented_fps'],
            delta_percent=(a['live']['presented_fps']/b['live']['presented_fps']-1)*100,
            max_cycles_before=b['live']['maximum'],max_cycles_after=a['live']['maximum'],
            max_vblanks_before=max(s['vblanks'] for s in b['live']['samples']),
            max_vblanks_after=max(s['vblanks'] for s in a['live']['samples']),
            frozen_ppu_exact=True))
    references={}
    for name in ('bat-hits-before','bat-hits','centered-projectiles','midrange-bats','combat','ai-combat',
                 'projectile-cells','close-hits','battles-before','battles-after','frontend-build'):
        p=out/(name+'.json');value=read(p)
        if name not in ('bat-hits-before','battles-before'):
            if 'rom_sha256' in value:
                assert value['rom_sha256']==asha,name
            if 'after' in value:
                assert value['after']['rom_sha256']==asha,name
        references[name]=dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p))
    native=read(ROOT/'build/gba/battle-equivalence/result.json')
    assert native['source_sha256']==sha(ROOT/'ports/gba/src/combat.c')
    assert 'non-player owners' in native['stdout']
    references['non-player-differential']=dict(path='build/gba/battle-equivalence/result.json',sha256=sha(ROOT/'build/gba/battle-equivalence/result.json'))
    memory=read(build/'build-report.json');old_memory=read(args.control/'build-report.json')
    assert memory['ewram_static_bytes']==old_memory['ewram_static_bytes']
    assert memory['symbols']['__iwram_end']==old_memory['symbols']['__iwram_end']
    live=read(out/'midrange-bats.json')
    ceiling_before=ROOT/'build/gba/g21-ceiling-sweep/ceiling.json'
    ceiling_after=ROOT/'build/gba/g21-bat-ceiling/ceiling.json'
    cb,ca=read(ceiling_before),read(ceiling_after)
    assert cb['rom_sha256']==bsha and ca['rom_sha256']==asha and len(cb['rows'])==len(ca['rows'])==750
    assert all(b['name']==a['name'] and b['ppu_sha256']==a['ppu_sha256'] for b,a in zip(cb['rows'],ca['rows']))
    references['ceiling-before']=dict(path=ceiling_before.relative_to(ROOT).as_posix(),sha256=sha(ceiling_before))
    references['ceiling-after']=dict(path=ceiling_after.relative_to(ROOT).as_posix(),sha256=sha(ceiling_after))
    result=dict(task='G21/BUG01',status='fixed_emulator_verified',
        correction='Player-shot collision uses a cell-sized half-open square centered on the target actor. '
            'Exact swept path and first-contact ordering, not sprite-pixel tests. '
            'Flight vectors/damage/timing and non-player native cell collision retained.',
        original_behavior_reference_commit='d628e63',baseline_rom_sha256=bsha,
        rom_sha256=asha,rom_bytes=(build/'dok-diagnostic.gba').stat().st_size,
        aimed_cases=2880,old_misses=fixed,new_misses=0,
        geometry_cases=read(out/'centered-projectiles.json')['cases'],
        live_midrange_cases=live['cases'],live_midrange_targets_moved=sum(any(h['position'][0]!=r['target_start'][0] or h['position'][2]!=r['target_start'][1] for h in r['history']) for r in live['rows']),
        non_player_differential_scenarios=480,non_player_differential_polls=15360,
        ewram_static_bytes=memory['ewram_static_bytes'],iwram_end=memory['symbols']['__iwram_end'],
        observed_stack_high_water=max(r['stack']['high_water_bytes'] for r in aperf['rows']),
        performance=comparisons,ppu_frozen_pairs_exact=len(comparisons),reports=references,
        ceiling_ppu_pairs_exact=750,
        performance_scope='600 live held-A frames per room/language/ROM, original maps/AI, stationary fixture hero HP32767. '
            'Changed contacts can change AI poses/death timing; live rows are not lockstep CPU equivalence. '
            'Maximum cycles can increase even when presentation VBlank minima are unchanged.',
        physical_gba_tested=False,full_navigation_playthrough=False,
        G21_complete=False,remaining=['BUG02','final_release_acceptance'],next_id='G21',
        script_sha256=sha(__file__),source_sha256=sha(ROOT/'ports/gba/src/combat.c'),
        geometry_source_sha256=sha(ROOT/'ports/gba/src/combat_player_hits.inc'))
    target=ROOT/'analysis/gba/g21-bat-fix-20261101.json'
    target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('BUG01: fixed216/216 reproduced misses;',asha)


if __name__=='__main__':main()
