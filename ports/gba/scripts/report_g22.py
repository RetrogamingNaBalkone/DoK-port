"""Validate bounded G22 evidence and package the selected single-port candidate."""
import json
from pathlib import Path
import shutil
import statistics
from check_g22 import ROOT, BUILDS
from verify_gba import sha

OUT=ROOT/'build/gba/g22'

def read(name):return json.loads((OUT/name).read_text(encoding='utf-8-sig'))
def keyed(rows):return {(r['level'],r['cell'],r['heading']):r for r in rows}
def compare(before,after):
    b=keyed(before);a=keyed(after);assert a.keys()==b.keys()
    delta=[(r['median']/b[k]['median']-1)*100 for k,r in a.items()]
    return dict(cases=len(delta),median_cpu_percent=statistics.median(delta),
        best_cpu_percent=min(delta),worst_cpu_percent=max(delta),
        before_min_fps=min((r['presented_fps'] for r in before if r['presented_fps'] is not None),default=None),
        after_min_fps=min((r['presented_fps'] for r in after if r['presented_fps'] is not None),default=None))

def main():
    variants={}
    for name,relative in BUILDS.items():
        build=OUT/relative
        meta=json.loads((build.parent/'prototype-build.json').read_text())['variants'][build.name]
        assert meta['rom_sha256']==sha(build/'dok-diagnostic.gba')
        heavy=read(name+'-heavy.json');live=read(name+'-live.json');ppu=read(name+'-ppu.json')
        assert heavy['rom_sha256']==live['rom_sha256']==meta['rom_sha256']
        assert ppu['after']['rom_sha256']==meta['rom_sha256']
        if name!='rows96':assert all(r['different_rgb_pixels']==0 and r['coverage_equal'] for r in ppu['after']['rows'])
        for r in live['rows']:
            assert r['emulated_frames']==600 and r['audio']['underruns']==0
            assert len(r['input_observation_latency'])==3 and r['pending_input'] is None
        reference='sparse_control' if name=='sparse' else 'control'
        variants[name]=dict(rom_sha256=meta['rom_sha256'],rom_bytes=meta['rom_bytes'],
            defines=meta['prototype_defines'],ewram_bytes=meta['ewram_static_bytes'],
            iwram_bytes=next(s['size'] for s in meta['sections'] if s['name']=='.iwram'),stack_reserve=8192,
            heavy_reference=reference,heavy=compare(read(reference+'-heavy.json')['rows'],heavy['rows']),
            ppu_frames=len(ppu['after']['rows']),ppu_max_diff=max(r['different_rgb_pixels'] for r in ppu['after']['rows']),
            live=[{k:r[k] for k in ('level','language','median','p95','maximum','presented_fps','game_ticks','input_observation_latency')} for r in live['rows']])
        variants[name]['live_position_checkpoint_differences']=sum(
            x['player']!=y['player'] for b,a in zip(read('control-live.json')['rows'],live['rows'])
            for x,y in zip(b['checkpoints'],a['checkpoints']))
        if (OUT/(name+'-cold.json')).exists():
            variants[name]['cold']=compare(read('control-cold.json')['rows'],read(name+'-cold.json')['rows'])
        if (OUT/(name+'-spatial-0.json')).exists():
            variants[name]['spatial']={str(lang):compare(read(f'control-spatial-{lang}.json')['rows'],read(f'{name}-spatial-{lang}.json')['rows']) for lang in (0,1)}
    selected=OUT/BUILDS['active'];digest=sha(selected/'dok-diagnostic.gba')
    verified=read('verified/frontend-build.json');assert verified['verified'] and verified['rom_sha256']==digest
    assert sha(OUT/'verified/hardware/g22_split_active/dok-diagnostic.gba')==digest
    assert sha(OUT/'repeat/g22_split_active/dok-diagnostic.gba')==digest
    for name in ('control','active'):
        assert [r['median'] for r in read(name+'-heavy.json')['rows']]==[r['median'] for r in read(name+'-heavy-repeat.json')['rows']]
    for name in ('latency','frontend','ui','flow','credits','audio','time','arm','water','sprites','transitions'):
        assert read('verified/'+name+'.json').get('status','passed')=='passed',name
    for name in ('combat','ai-combat','projectile-cells','centered-projectiles','midrange-bats','close-hits','ceiling-sides','story','automap','cache','mode4'):
        assert read('active-'+name+'.json').get('status','passed')=='passed',name
    spatial=read('active-spatial-ppu.json');assert spatial['frames']==880
    for b,a in zip(spatial['before']['rows'],spatial['after']['rows']):
        for key in ('rgb_sha256','coverage_sha256','lease_sha256'):assert b[key]==a[key]
    detail=read('active-detail-ppu.json');assert len(detail['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 and r['coverage_equal'] for r in detail['after']['rows'])
    game=read('active-profile-game.json');assert game['status']=='passed'
    work=read('control-work.json');active_work=read('active-work.json')
    for b,a in zip(work['rows'],active_work['rows']):
        assert b['coverage_sha256']==a['coverage_sha256'] and b['ceiling_uv_span_sha256']==a['ceiling_uv_span_sha256']
    integrated=OUT/'integrated/hardware/g22_split_active'
    integration=read('integrated/frontend-build.json')
    assert integration['verified'] and integration['rom_sha256']==sha(integrated/'dok-diagnostic.gba')
    assert read('integrated-control-heavy.json')['rom_sha256']=='e15e8cb2f9ea85cb15fd7509f26a9a3a2ddd873df8023e33e53d04081b2448d3'
    for name in ('latency','frontend','ui','flow','credits','audio','time','arm','water','sprites','transitions'):
        assert read('integrated/'+name+'.json').get('status','passed')=='passed',name
    integration_ppu=read('integrated-ppu.json')
    assert all(r['different_rgb_pixels']==0 and r['coverage_equal'] for r in integration_ppu['after']['rows'])
    assert read('integrated-cache.json')['status']=='passed'
    assert read('integrated-mode4.json')['status']=='passed'
    destination=ROOT/'artifacts/gba/DoK-port_G22.gba'
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(integrated/'dok-diagnostic.gba',destination)
    decisions={'H01':'accept separate warm/cold/fixed-duration methodology; moving states are not assumed equal',
        'H02':'reject tested transparency-only counter removal: negligible benefit; exact written count preserved',
        'H03':'reject tested exact paired edge UV setup; shared world UV not implemented',
        'H04':'defer broader black-distance renderer: little far ceiling/masked work in six views; opaque/intersecting faces not exhausted',
        'H05':'performance verified; gameplay impact of minor quality reduction remains to be assessed in G25; pixel differences alone do not reject it',
        'H06':'reject tested enlarged zero-word shortcut against its matched control; other sparse kernels not disproved',
        'H07':'accept split legacy path plus hot active edges after full emulator regression',
        'H08':'defer raster cache pending bounded memory/composition design; only 668 B EWRAM free',
        'H09':'continue in G25: allow minor degradation without material gameplay impact; exact pixel equality is not the acceptance gate',
        'H10':'conditional defer: no new transport model; old feedback not rerun',
        'H11':'conditional defer: no new proven offline packing/admission; old GPU fallback not rerun'}
    result=dict(task='G22',status='passed-bounded-emulator',selected='g22_split_active',
        artifact=destination.relative_to(ROOT).as_posix(),rom_sha256=sha(destination),measurement_rom_sha256=digest,
        integration=dict(ui02=True,rom_sha256=sha(destination),rom_bytes=destination.stat().st_size,
            control_sha256=read('integrated-control-heavy.json')['rom_sha256'],
            heavy=compare(read('integrated-control-heavy.json')['rows'],read('integrated-selected-heavy.json')['rows']),
            detail_ppu=len(integration_ppu['after']['rows']),cache_mutations=48,full_frontend_verified=True),
        decisions=decisions,variants=variants,
        actual_ppu=dict(spatial=880,detail=396,cache_mutations=48),stack=game['stack'],irq_stack=game['irq_stack'],
        attribution=work['rows'],
        limitations=['No physical GBA or original BIOS acceptance; BUG02 remains open/deferred.',
            'Fixed 600 video frames and timestamped inputs do not guarantee identical live world states or published tick counts.',
            'Cold experiment invalidates geometry only; assets/PCM stay warm, presentation FPS intentionally unavailable.',
            'Old battle presentation-count fixture is a load test, not equal-state causal timing.',
            'No claim of global speed optimum or completion of all possible H02/H03/H04/H06/H08/H09 implementations.',
            '96-row quality not accepted. No separate Performance product or public release.'],
        evidence={p.relative_to(OUT).as_posix():sha(p) for p in OUT.rglob('*.json') if 'source' not in p.parts})
    target=ROOT/'analysis/gba/g22-optimization-20261101.json'
    target.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    archive=ROOT/'research/evidence/gba-g22';archive.mkdir(parents=True,exist_ok=True)
    for p in OUT.glob('*.json'):shutil.copyfile(p,archive/p.name)
    for p in OUT.glob('*.log'):shutil.copyfile(p,archive/p.name)
    for name,relative in BUILDS.items():
        shutil.copyfile(OUT/relative/'build-report.json',archive/(name+'-build.json'))
    shutil.copyfile(integrated/'build-report.json',archive/'integrated-build.json')
    shutil.copyfile(target,archive/target.name)
    print('G22 selected',digest,variants['active']['spatial'],flush=True)

if __name__=='__main__':main()
