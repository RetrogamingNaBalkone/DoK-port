"""Validate recorded G20D evidence, publish budget and a private optimized ROM."""
import argparse
import json
from pathlib import Path
import shutil
import statistics
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from check_g20d import PIN,TRIALS,MEASURE,PROFILES
from verify_gba import sha


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ROOT/'build/gba/g20d/qualification')
    args=ap.parse_args();out=args.out
    def read(name):return json.loads((out/name).read_text(encoding='utf-8-sig'))
    def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    def keyed(rows):return {(r['level'],r['cell'],r['heading']):r for r in rows}
    def comparison(before,after):
        b=keyed(before);d=[(r['median']/b[k]['median']-1)*100 for k,r in keyed(after).items()]
        return dict(cases=len(d),median_cpu_change_percent=statistics.median(d),
            best_cpu_change_percent=min(d),worst_cpu_change_percent=max(d),
            faster=sum(v<0 for v in d),slower=sum(v>0 for v in d),equal=sum(v==0 for v in d),
            before_minimum_fps=min(r['presented_fps'] for r in before),
            after_minimum_fps=min(r['presented_fps'] for r in after))
    selected=out/'hardware/g20d_compact'
    rom=selected/'dok-diagnostic.gba';build=read('hardware/g20d_compact/build-report.json')
    control=read('hardware/hw_v128_vblank_only/build-report.json')
    assert control['rom_sha256']==PIN
    assert sha(rom)==sha(out/'repeat/g20d_compact/dok-diagnostic.gba')
    assert sha(rom)==sha(out/'verified/hardware/g20d_compact/dok-diagnostic.gba')
    verified=read('verified/frontend-build.json')
    assert verified['verified'] and verified['rom_sha256']==sha(rom)
    for name in ('latency','frontend','ui','flow','credits','audio','time','arm','water','sprites','transitions'):
        r=read('verified/'+name+'.json')
        assert r.get('status','passed')=='passed',(name,r.get('status'))
    from check_g21 import EXTRA
    for name,_,_ in EXTRA:assert read(name+'.json').get('status','passed')=='passed',name
    for variant in MEASURE:
        r=read(variant+'-arm.json');assert r['status']=='passed' and r['rom_sha256']==sha(out/'hardware'/variant/'dok-diagnostic.gba')
    ppu={}
    for variant in TRIALS[1:]:
        rows=read(variant+'-ppu.json')['after']['rows']
        assert all(r['different_rgb_pixels']==0 and r['coverage_equal'] for r in rows)
        ppu[variant]=len(rows)
    spatial_ppu=read('spatial-ppu.json');assert spatial_ppu['status']=='passed'
    heavy=keyed(read('hw_v128_vblank_only-heavy.json')['rows'])
    trials={}
    for variant in TRIALS[1:]:
        trials[variant]=[dict(level=r['level'],cell=r['cell'],heading=r['heading'],
            before_cycles=heavy[k]['median'],after_cycles=r['median'],
            cpu_change_percent=(r['median']/heavy[k]['median']-1)*100,
            before_fps=heavy[k]['presented_fps'],after_fps=r['presented_fps'])
            for k,r in keyed(read(variant+'-heavy.json')['rows']).items()]
    spatial={}
    for variant in MEASURE[1:]:
        spatial[variant]={}
        for lang in (0,1):
            before=read('hw_v128_vblank_only-spatial-'+str(lang)+'.json')['rows']
            after=read(variant+'-spatial-'+str(lang)+'.json')['rows']
            assert all(r['audio']['underruns']==0 for r in before+after)
            spatial[variant][str(lang)]={str(level):comparison([r for r in before if r['level']==level],
                [r for r in after if r['level']==level]) for level in (1,2,3)}
    live_data=read('live.json');live={}
    for variant in MEASURE[1:]:
        live[variant]={}
        for route,measurement in live_data[variant].items():
            before=live_data['hw_v128_vblank_only'][route]['rows'];rows=[]
            for b,a in zip(before,measurement['rows']):
                assert (a['level'],a['language'])==(b['level'],b['language'])
                assert a['audio']['underruns']==0 and a['max_motion_debt']<416
                assert a['time_conservation_error_ticks']==0
                rows.append(dict(level=a['level'],language=a['language'],before_fps=b['presented_fps'],
                    after_fps=a['presented_fps'],fps_change_percent=(a['presented_fps']/b['presented_fps']-1)*100,
                    before_maximum_cycles=b['maximum'],after_maximum_cycles=a['maximum'],
                    max_motion_debt=a['max_motion_debt'],time_conservation_error_ticks=a['time_conservation_error_ticks']))
            live[variant][route]=rows
    work={variant:read(variant+'-work.json') for variant in MEASURE}
    original=work['hw_v128_vblank_only']['rows']
    for variant in MEASURE[1:]:
        for b,a in zip(original,work[variant]['rows']):
            assert a['ceiling_uv_span_sha256']==b['ceiling_uv_span_sha256'],(variant,'UV steps changed')
            assert a['coverage_sha256']==b['coverage_sha256']
    profile={}
    for variant in PROFILES:
        profile[variant]=[]
        for r in read(variant+'.json')['rows']:
            sampled=r['kernel_work'][0]*64;inclusive=r['plane_work'][1]*64
            profile[variant].append(dict(level=r['level'],cell=r['cell'],heading=r['heading'],
                ceiling_inclusive_cycles=inclusive,ceiling_sampler_cycles=sampled,
                ceiling_setup_and_wrapper_cycles=inclusive-sampled,
                masked_sampler_cycles=r['kernel_work'][2]*64,sampler_calls=r['kernel_work'][1::2]))
    memory=dict(ewram_bytes=build['ewram_static_bytes'],ewram_free_bytes=262144-build['ewram_static_bytes'],
        iwram_bytes=build['symbols']['__iwram_end']-0x03000000,
        iwram_free_before_stacks_bytes=0x03006000-build['symbols']['__iwram_end'],
        stacks_bios_reserved_bytes=8192,heap_bytes=0,
        observed_main_stack=live_data['g20d_compact']['walk-turn-fire']['stack'],
        observed_irq_stack=live_data['g20d_compact']['walk-turn-fire']['irq_stack'])
    assert memory['iwram_free_before_stacks_bytes']>=0 and memory['ewram_free_bytes']>=0
    reports={p.relative_to(ROOT).as_posix():sha(p) for p in out.glob('*.json')}
    report=dict(task='G20/G20D',status='bounded_optimization_pass_verified',target_fps=None,
        baseline_sha256=PIN,rom_sha256=sha(rom),rom_bytes=rom.stat().st_size,
        repeated_rom_exact=True,default_ui01_replaced=False,published=False,
        decision='Select paired exact span division and compact open masked groups. '
                 'No significant general FPS reserve was demonstrated; no hardware-limit claim.',
        heavy_trials=trials,spatial=spatial,live=live,ppu_frames=ppu,spatial_ppu_frames=spatial_ppu['frames'],
        profile=profile,profile_scope='Separate instrumented ROMs, orchestration in ROM. '
            'Sampler elapsed includes wrapper/timer overhead and PCM IRQs. Setup is inclusive minus sampled, '
            'not production CPU or a pure isolated kernel timer.',
        work=work,memory=memory,reports=reports,
        build_manifest_sha256=sha(out/'hardware/prototype-build.json'),
        verified_build_manifest_sha256=sha(out/'verified/frontend-build.json'),
        host_cross_log_sha256=sha(out/'host-cross.log'),
        limitations=['Native-clock mGBA with replacement BIOS; physical GBA not measured.',
            'Spatial fixtures freeze simulation; routes are finite 120-presentations, not whole-level minima.',
            'Live gameplay advances by real time: optimized/control frame numbers are not lockstep.',
            'Artificial all-awake overload remains outside these ordinary route claims.',
            'BUG02 deferred by user; this exact-output change does not fix or accept it.',
            'No Performance profile, distance/AI/resolution/rules reductions.'],next_id='G22')
    save(ROOT/'analysis/gba/g20d-optimization-20261101.json',report)
    # Retain the old detailed G20 budget as history, not as a current 30-FPS gate.
    budget_path=ROOT/'analysis/gba/gba-final-budget.json'
    old=json.loads(budget_path.read_text(encoding='utf-8'))
    historical=ROOT/'analysis/gba/gba-final-budget-20261026.json'
    if not historical.exists():save(historical,old)
    budget=dict(task='G20/G20D',status='measured_no_fixed_fps_gate',target_fps=None,
        report='analysis/gba/g20d-optimization-20261101.json',rom_sha256=sha(rom),rom_bytes=rom.stat().st_size,
        memory=memory,heavy=trials['g20d_compact'],spatial=spatial['g20d_compact'],live=live['g20d_compact'],
        historical_budget='analysis/gba/gba-final-budget-20261026.json',limitations=report['limitations'])
    save(budget_path,budget)
    artifact=ROOT/'artifacts/gba/DoK-port_G20_Optimized.gba'
    shutil.copyfile(rom,artifact)
    save(artifact.with_suffix('.json'),dict(rom_sha256=sha(rom),rom_bytes=rom.stat().st_size,
        report_sha256=sha(ROOT/'analysis/gba/g20d-optimization-20261101.json'),
        status='private_verified_optimization_candidate',physical_gba_tested=False,default_ui01_replaced=False))
    print(sha(rom),memory,flush=True)


if __name__=='__main__':main()
