"""Summarize G23 retained measurements; no inferred native speedup."""
from collections import Counter
import json
from pathlib import Path
from verify_gba import sha

ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/gba-g23'


def load(name):return json.loads((E/(name+'.json')).read_text())


def main():
    result=dict(task='G23',status='passed',decision='Do not open H04 native prototype on this evidence',
                production_rom_sha256=load('warm-work')['rom_sha256'],groups={})
    warm=load('warm-work')['rows'];cold=load('cold-work')['rows']
    assert len(warm)==len(cold)
    for a,b in zip(warm,cold):
        for key in ('classes','coverage_sha256','ceiling_uv_span_sha256'):assert a[key]==b[key]
    result['cold_warm_attribution_exact']=True
    for group in ('warm','cold','moving','battle'):
        rows=[];total=Counter()
        for r in load(group+'-work')['rows']:
            classes=r['classes']
            count=lambda field:sum(c.get(field,0) for c in classes.values())
            opened=count('open');far=count('light15_open')
            opaque_far=sum(c.get('light15_open',0) for k,c in classes.items() if 'masked' not in k)
            row=dict(language=r['language'],level=r['level'],cell=r.get('cell'),heading=r.get('heading'),segment=r.get('segment'),
                     requested=count('requested'),open=opened,light15_open=far,
                     light15_percent=100*far/opened if opened else 0,
                     opaque_surface_light15_open=opaque_far,
                     masked_light15_open=far-opaque_far,
                     threshold_checks_if_per_column_or_span=count('calls'),
                     far_column_or_span_calls=count('depth3720_calls'),
                     crossing_face_open=count('crossing_face_open'),crossing_face_far_open=count('crossing_face_far_open'))
            rows.append(row)
            for key in ('requested','open','light15_open','opaque_surface_light15_open','masked_light15_open'):total[key]+=row[key]
        result['groups'][group]=dict(samples=len(rows),totals=dict(total),
                                   aggregate_light15_percent=100*total['light15_open']/total['open'],
                                   maximum_light15_percent=max(r['light15_percent'] for r in rows),rows=rows)
    budget=[]
    for lang in (0,1):
        profiles=load(f'profile-{lang}')['rows']
        timings={(r['level'],r['cell'],r['heading']):r for r in load(f'warm-{lang}')['rows']}
        coldtimes={(r['level'],r['cell'],r['heading']):r for r in load(f'cold-{lang}')['rows']}
        attribution={(r['level'],r['cell'],r['heading']):r for r in warm if r['language']==lang}
        for r in profiles:
            key=r['level'],r['cell'],r['heading'];c=attribution[key]['classes'];t=timings[key];ct=coldtimes[key]
            kw=r['kernel_work'];rw=r['raster_work']
            surface_open=c.get('surface',{}).get('open',0)
            masked_open=sum(v['open'] for k,v in c.items() if 'masked' in k)
            sf=c.get('surface',{}).get('light15_open',0)
            mf=sum(v.get('light15_open',0) for k,v in c.items() if 'masked' in k)
            # Explicit proportional *proxy*, not a bound: sample distribution,
            # IRQs, coverage and call/setup costs are not uniform.
            proxy=(kw[0]*64*sf/surface_open if surface_open else 0)+(kw[2]*64*mf/masked_open if masked_open else 0)
            budget.append(dict(language=lang,level=key[0],cell=key[1],heading=key[2],
                production_warm_cycles=t['median'],production_cold_cycles=ct['median'],production_fps=t['presented_fps'],
                instrumented_surface_cycles=rw[2]*64,instrumented_face_cycles=rw[1]*64,
                instrumented_surface_sampling_cycles=kw[0]*64,instrumented_masked_sampling_cycles=kw[2]*64,
                instrumented_surface_setup_and_other_cycles=(rw[2]-kw[0])*64,
                surface_open=surface_open,masked_open=masked_open,
                proportional_far_sampling_proxy_cycles=proxy))
    result['budget_rows']=budget
    result['limitations']=[
        'Operation counts are coverage-aware foreground CPU samples, not output RGB pixels or native cycles.',
        'HW base planes excluded: PPU texture fetches are not CPU sampling. UI/weapon overlay excluded from renderer replay.',
        'Masked far samples still require alpha/silhouette reads. Opaque black fill still requires coverage and destination writes.',
        'Uniform sampling proxy is not an upper bound or speedup prediction; opaque sampling/setup not separately timed.',
        'Moving/battle attribution has extra held completions per 100-frame segment; continuous live timing is a separate run.',
        'Cold means geometry invalidation, not cold resources/boot. Physical GBA and original BIOS not tested.',
        'LIGHT15 is not black; no quality change, native black/clip implementation or release was made.'
    ]
    ppu=load('profile-ppu')
    for a,b in zip(ppu['before']['rows'],ppu['after']['rows']):
        for key in ('rgb_sha256','coverage_sha256','lease_sha256'):assert a[key]==b[key]
    result['profile_ppu_frames_exact']=len(ppu['after']['rows'])
    result['live_timing_rows']=[{k:r[k] for k in ('language','level','median','p95','maximum','presented_fps')} for r in load('live')['rows']]
    result['evidence']={p.name:sha(p) for p in sorted(E.glob('*.json'))}
    dest=ROOT/'analysis/gba/g23-attribution-20261101.json'
    dest.write_text(json.dumps(result,indent=2)+'\n')
    for k,g in result['groups'].items():print(k,g['samples'],g['totals'],g['aggregate_light15_percent'],g['maximum_light15_percent'])
    for r in result['groups']['warm']['rows'][:9]:print(r)
    for r in budget[:9]:print(r)


if __name__=='__main__':main()
