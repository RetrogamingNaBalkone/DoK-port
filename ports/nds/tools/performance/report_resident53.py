"""N23 bounded VRAM residency: qualified paired lure and regression results."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    baseline=ROOT/'build/nds/n23/angles51d-ru'
    final=ROOT/'build/nds/n23/resident53c-ru'
    pairs=[];proofs=[];totals=[]
    reports=[]
    for target in (baseline,final):
        directory=target/'resident53exact-lure-live'
        result=json.loads((directory/'result.json').read_text('utf8'))
        runtime=json.loads((directory/'runtime-profile.json').read_text('utf8'))
        assert result['passed'] and result['all_room_enemies_out'] and result['all_executioners_alive']
        assert result['exact_heading_fixture']
        reports.append(result)
        rows=runtime['history_records']
        totals.append(dict(presentations=len(rows),upload_bytes=sum(r['upload_bytes']for r in rows),
            large_uploads=sum(r['upload_bytes']>32768 for r in rows),
            maximum_resident_bytes=max(r['texture_bytes']for r in rows)))
        proofs += [pin(directory/n)for n in ('result.json','runtime-profile.json')]
    for a,b in zip(reports[0]['samples'],reports[1]['samples'],strict=True):
        assert a['name']==b['name'] and a['pose']==b['pose']
        pairs.append(dict(name=a['name'],pose=a['pose'],baseline_fps=a['fps'],final_fps=b['fps'],
            baseline_windows=a['minimum_window_fps'],final_windows=b['minimum_window_fps'],
            baseline_stages_ms=a['stages_ms'],final_stages_ms=b['stages_ms']))
    checks={}
    for name,path in {
        'host':ROOT/'build/nds/n23/stream53-host/result.json',
        'ui':final/'playtest34/result.json',
        'live':final/'joint-frontend/live/result.json',
        'reloads':final/'map38-restarts/result.json',
        'turning':final/'turning-observer-v2/result.json',
    }.items():
        report=json.loads(path.read_text('utf8'));assert report['passed'],name
        checks[name]=pin(path)
    manifest=json.loads((final/'build-manifest.json').read_text('utf8'))
    for row in manifest['sources']:
        p=ROOT/row['path']
        if p.suffix in ('.c','.h','.inc','.S','.ld','.lds') or p.name.startswith('Makefile'):
            assert pin(p)['sha256']==row['sha256'],row['path']
    rom=ROOT/'artifacts/nds/dok.nds'
    assert rom.read_bytes()==(final/'dok_nds_gameplay.nds').read_bytes()
    reduction=1-totals[1]['upload_bytes']/totals[0]['upload_bytes']
    result=dict(task='N23',passed=True,optimization_accepted=True,all_heavy_views_60fps=False,
        rom=pin(rom),manifest=pin(final/'build-manifest.json'),checks=checks,proofs=proofs,
        baseline_totals=totals[0],final_totals=totals[1],upload_reduction_percent=reduction*100,
        comparisons=pairs,memory=dict(texture_limit_bytes=393216,extra_cursor_bytes=4,
            new_pixel_buffers=False,indexed_bits_per_pixel=8,palette_bytes=8192,capture_bits_per_pixel=16),
        limitations=['DS-mode melonDS; no new physical DS or DraStic test.',
            'Hero spawn/retry/HP/key fixtures; no enemy writes or complete user route.',
            'Equal cameras do not freeze live enemy/projectile histories.',
            'Short FPS dips remain; coarse pressure fallback can still reload textures.'],
        next_task='N23: user playtest and remaining raw frame/map/render tails; N24 not started')
    path=ROOT/'analysis/nds-resident53-20261108.json'
    path.write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,rom=result['rom'],baseline=totals[0],final=totals[1],
        upload_reduction_percent=reduction*100)))


if __name__=='__main__':main()
