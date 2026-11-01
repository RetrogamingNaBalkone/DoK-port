"""Bind G24 native negative experiments to their evidence and exact ROM pins."""
import json, hashlib, statistics
from pathlib import Path
from check_g24 import ROOT, OUT, CONTROL, TRIAL
from verify_gba import sha

def main():
    read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
    assert sha(CONTROL/'dok-diagnostic.gba')==sha(ROOT/'build/gba/g24/repeat/g22_split_active/dok-diagnostic.gba')=='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
    result=dict(task='G24',status='passed',decision='Reject both measured four-span raster caches for current release; retain G22 unchanged',control_rom_sha256=sha(CONTROL/'dok-diagnostic.gba'),variants={})
    for name,folder,build in [('uniform',OUT,TRIAL),('top',OUT/'top',ROOT/'build/gba/g24/top/g24_span_cache_top')]:
        manifest=read(build.parent/'prototype-build.json')['variants'][build.name]
        symbols=manifest['symbols'];rows=[]
        for lang in (0,1):
            for mode in ('warm','cold'):
                before=read(OUT/f'control-{mode}-{lang}.json')['rows'];after=read(folder/f'trial-{mode}-{lang}.json')['rows']
                for a,b in zip(before,after):
                    assert (a['level'],a['cell'],a['heading'])==(b['level'],b['cell'],b['heading'])
                    rows.append(dict(language=lang,mode=mode,level=a['level'],cell=a['cell'],heading=a['heading'],before_median=a['median'],after_median=b['median'],cpu_change_percent=(b['median']/a['median']-1)*100,before_p95=a['p95'],after_p95=b['p95'],before_fps=a['presented_fps'],after_fps=b['presented_fps']))
        stability=read(folder/'stability.json');work={}
        for mode in sorted({r['mode'] for r in stability['rows']}):
            counts=[sum(r['work'][i] for r in stability['rows'] if r['mode']==mode) for i in range(4)]
            work[mode]=dict(eligible=counts[0],hits=counts[1],misses=counts[2],hit_writes=counts[3])
        ppu=read(folder/'ppu.json');water=read(folder/'water.json');transitions=read(folder/'transitions.json');mut=read(folder/'cache-mutations.json')
        assert all(x['status']=='passed' for x in (water,transitions,mut,stability))
        for a,b in zip(ppu['before']['rows'],ppu['after']['rows']):
            assert a['name']==b['name'] and all(a[k]==b[k] for k in ('rgb_sha256','coverage_sha256','lease_sha256'))
        result['variants'][name]=dict(rom_sha256=manifest['rom_sha256'],ewram_bytes=manifest['ewram_static_bytes'],ewram_free=262144-manifest['ewram_static_bytes'],iwram_bytes=symbols['__iwram_end']-symbols['__iwram_start'],stack_reserved=symbols['__iwram_reserved_bytes'],iwram_free=32768-(symbols['__iwram_end']-symbols['__iwram_start'])-symbols['__iwram_reserved_bytes'],pairs=rows,cache_work=work,checks=dict(ppu=len(ppu['after']['rows']),cache_mutations=mut['cases'],destruction_mutation_pairs=len(stability['mutation_pairs']),water_phases=water['phase_cases'],water_ppu_pixels=water['ppu_pixels'],transitions=len(transitions['rows'])),live=read(folder/'trial-live.json'),battles=read(folder/'trial-battles.json'))
    result['control_live']=read(OUT/'control-live.json');result['control_battles']=read(OUT/'control-battles.json')
    result['evidence']=[dict(path=str(p.relative_to(ROOT)).replace('\\','/'),sha256=sha(p)) for p in sorted(OUT.rglob('*.json'))]
    result['limitations']=['Two concrete four-span schemes, not every possible cache algorithm. Neither selected for release.','Cache stores immutable sample colors only; geometry, leases, coverage and draw order remain live.','Moving and battle timings do not imply identical game state across different cadence.','No physical GBA/original BIOS/full playthrough; production ROM unchanged; G21/BUG02 unaffected.']
    target=ROOT/'analysis/gba/g24-raster-cache-20261101.json';target.write_text(json.dumps(result,indent=2)+'\n')
    for n,v in result['variants'].items():
        for mode in ('warm','cold'):
            vals=[r['cpu_change_percent'] for r in v['pairs'] if r['mode']==mode]
            print(n,mode,'median/min/max',statistics.median(vals),min(vals),max(vals))
        print(n,v['checks'],v['cache_work'])
if __name__=='__main__':main()
