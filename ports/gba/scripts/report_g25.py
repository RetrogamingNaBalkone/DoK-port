"""Bind G25 quality decision to paired heavy, spatial and gameplay evidence."""
import json
import statistics
from pathlib import Path
from check_g25 import ROOT, OUT, BUILDS, PIN
from verify_gba import sha

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))

def pairs(before,after):
    result=[]
    assert len(before)==len(after)
    for a,b in zip(before,after):
        key=('level','cell','heading')
        assert all(a[k]==b[k] for k in key)
        result.append(dict(**{k:a[k] for k in key},language=a['language'],
            before_cpu=a['median'],after_cpu=b['median'],
            cpu_change_percent=100*(b['median']/a['median']-1),
            before_p95=a['p95'],after_p95=b['p95'],
            before_fps=a['presented_fps'],after_fps=b['presented_fps']))
    return result

def main():
    assert sha(BUILDS['control']/'dok-diagnostic.gba')==PIN
    assert sha(ROOT/'build/gba/g25/repeat/g22_split_active/dok-diagnostic.gba')==PIN
    selected=sha(BUILDS['rows96']/'dok-diagnostic.gba')
    verified=read(ROOT/'build/gba/g25/verified/frontend-build.json')
    assert verified['verified'] and verified['rom_sha256']==selected
    current=read(ROOT/'build/gba/g25/current/frontend-build.json')
    assert current['rom_sha256']==selected
    for path,digest in current['sources'].items():assert sha(ROOT/path)==digest,path
    assert sha(ROOT/'build/gba/g25/repeat/g25_rows96/dok-diagnostic.gba')==selected
    result=dict(task='G25',status='passed',decision='Select H05 96 rows as the single-port emulator-qualified candidate; reject this H09 horizontal material-pair kernel. Physical/release acceptance remains G21.',
        control_rom_sha256=PIN,selected_rom_sha256=selected,variants={})
    for name in ('rows96','lod'):
        build=BUILDS[name];manifest=read(build.parent/'prototype-build.json')['variants'][build.name]
        assert manifest['rom_sha256']==sha(build/'dok-diagnostic.gba')
        v=dict(rom_sha256=manifest['rom_sha256'],ewram_bytes=manifest['ewram_static_bytes'],
            iwram_bytes=manifest['symbols']['__iwram_end']-manifest['symbols']['__iwram_start'],
            stack_reserve=manifest['symbols']['__iwram_reserved_bytes'],pairs={})
        for mode in ('warm','cold'):
            rows=[]
            for lang in (0,1):
                before=read(OUT/f'control-{mode}-{lang}.json')
                after=read(OUT/f'{name}-{mode}-{lang}.json')
                assert after['rom_sha256']==v['rom_sha256']
                rows+=pairs(before['rows'],after['rows'])
            v['pairs'][mode]=rows
            values=[r['cpu_change_percent'] for r in rows]
            v[mode+'_cpu_percent']=dict(minimum=min(values),median=statistics.median(values),maximum=max(values))
        v['live']=read(OUT/f'{name}-live.json');v['battles']=read(OUT/f'{name}-battles.json')
        assert v['live']['rom_sha256']==v['battles']['rom_sha256']==v['rom_sha256']
        result['variants'][name]=v
    spatial=[]
    for lang in (0,1):
        spatial+=pairs(read(OUT/f'control-spatial-{lang}.json')['rows'],read(OUT/f'rows96-spatial-{lang}.json')['rows'])
    result['spatial']=spatial
    values=[r['cpu_change_percent'] for r in spatial]
    result['spatial_summary']=dict(poses=len(spatial),minimum_cpu_change_percent=min(values),
        median_cpu_change_percent=statistics.median(values),maximum_cpu_change_percent=max(values),
        before_min_fps=min(r['before_fps'] for r in spatial),after_min_fps=min(r['after_fps'] for r in spatial))
    result['tail']=pairs(read(OUT/'control-tail.json')['rows'],read(OUT/'rows96-tail.json')['rows'])
    result['control_live']=read(OUT/'control-live.json');result['control_battles']=read(OUT/'control-battles.json')
    quality=read(OUT/'quality.json')
    assert quality['status']=='passed'
    assert all(r['coverage_equal'] and r['leases_equal'] for r in quality['comparisons']['lod'])
    assert all(quality['captures'][n]['rom_sha256']==sha(b/'dok-diagnostic.gba') for n,b in BUILDS.items())
    result['quality']=dict(native_captures=sum(len(v['rows']) for v in quality['captures'].values()),
        detail_captures=len(read(OUT/'rows96-detail-ppu.json')['after']['rows']),
        decision='Visible coarser textures and small contours; sampled grilles, items and navigation remain readable. No claim of imperceptibility or physical comfort acceptance.')
    checks={}
    for p in sorted(OUT.glob('qualify-*.json')):
        v=read(p);assert v.get('status','passed')=='passed'
        if 'rom_sha256' in v:assert v['rom_sha256']==selected
        checks[p.stem]=dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p))
    assert len(checks)==11,len(checks)
    result['checks']=checks
    result['frontend_verified']=verified
    result['final_source_build']=current
    result['evidence']=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(OUT.glob('*.json'))]
    result['limitations']=[
        'Warm/cold frozen pairs isolate renderer changes; live and120-presentation battles diverge in state and duration, so their percentages are not causal renderer estimates.',
        'Cold geometry CPU excludes held presentation time; cold FPS is null.',
        'Quality includes equal-pose short frozen walks/turns and bat views, not an interactive user playtest; coarse textures/contours remain visible.',
        'Initial LOD measurements with wrong ROM placement/prelit/coverage flags are excluded; archived invalid controls are not performance evidence.',
        'No physical GBA, original BIOS or full playthrough. G21/BUG02 and NDS/SDL2 remain unchanged.'
    ]
    target=ROOT/'analysis/gba/g25-quality-20261102.json'
    target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({n:v['warm_cpu_percent'] for n,v in result['variants'].items()},indent=2))
    print(result['spatial_summary'])

if __name__=='__main__':main()
