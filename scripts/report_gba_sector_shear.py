"""Collect the bounded-error affine/shear trial and architecture decision."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    read=lambda p:json.loads((ROOT/p).read_text(encoding='utf-8-sig'))
    sha=lambda p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    folder=Path('build/g20a-sector-shear-v4');final=Path('build/g20a-sector-shear-final-v4')
    manifest=read(final/'prototype-build.json');measured=read(folder/'prototype-build.json')
    trial=manifest['variants']['hw_v128_sector_gpu_shear'];control=manifest['variants']['hw_v128_entity_rect']['rom_sha256']
    assert control=='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    assert trial['rom_sha256']==measured['variants']['hw_v128_sector_gpu_shear']['rom_sha256']
    assert all(sha(Path(p))==v for p,v in manifest['sources'].items()),'Current native sources'
    for name in ('hw_v128_sector_gpu_shear','hw_v128_entity_rect'):
        for directory in (folder,final):assert sha(directory/name/'dok-diagnostic.gba')==manifest['variants'][name]['rom_sha256']
    original=read(Path('build/g20a-sector-gpu-v8/prototype-build.json'))
    for name in ('hw_v128_sector_gpu_thumb','hw_v128_sector_gpu_off','hw_v128_entity_rect'):
        assert manifest['variants'][name]['rom_sha256']==original['variants'][name]['rom_sha256']
        assert sha(final/name/'dok-diagnostic.gba')==original['variants'][name]['rom_sha256']
    qa=read(final/'visibility.json');pressure=read(folder/'pressure.json');profile=read(folder/'profile.json')
    target=read(folder/'target.json');base=read(folder/'control-target.json')
    live=read(folder/'hw_v128_sector_gpu_shear-live24.json');live_base=read(folder/'hw_v128_entity_rect-live24.json')
    for report in (profile,target,live):assert report['rom_sha256']==trial['rom_sha256']
    assert qa['after']['rom_sha256']==pressure['after']['rom_sha256']==trial['rom_sha256']
    assert qa['before']['rom_sha256']==base['rom_sha256']==live_base['rom_sha256']==control
    assert not qa['pressure_probe'] and pressure['pressure_probe']
    assert qa['script_sha256']==pressure['script_sha256']==sha(Path('scripts/verify_gba_sector_gpu.py'))
    assert profile['script_sha256']==sha(Path('scripts/profile_gba_sector_gpu.py'))
    rows=qa['after']['rows'];assert len(rows)==58 and sum(bool(r['gpu_pieces']) for r in rows)==46
    assert sum(r['nonzero_pc_matrices']>0 for r in rows)==8
    assert all(r['shear_work'][2]<=2 and r['shear_work'][3]<=2 and r['ppu_pressure_mismatches']==0 for r in rows)
    assert all(r['state'][1]==1 and r['work'][6]>0 for r in profile['rows'])
    p_rows=pressure['after']['rows'];assert len(p_rows)==5
    assert all(r['ppu_pressure_mismatches']==r['ppu_capacity_tail_mismatches'] for r in p_rows)
    assert sum(r['ppu_pressure_mismatches']>0 for r in p_rows)==4
    assert p_rows[3]['gpu_fallback'] and p_rows[3]['gpu_work'][9]>0
    for r in target['rows']+live['rows']+live_base['rows']:assert r['audio']['underruns']==0
    assert all(r['time_conservation_error_ticks']==0 for r in live['rows']+live_base['rows'])
    key=lambda r:(r['level'],r['cell'],r['heading']);ref={key(r):r for r in base['rows']}
    heavy=[dict(level=r['level'],cell=r['cell'],heading=r['heading'],cpu_ratio=r['median']/ref[key(r)]['median'],
        fps=r['presented_fps'],baseline_fps=ref[key(r)]['presented_fps']) for r in target['rows']]
    assert all(r['cpu_ratio']>1 for r in heavy)
    count=lambda name:sum(r[name] for r in rows)
    preserved={}
    for name,expected in (
        ('DoK-port_hardware_cached_prototype.gba',control),
        ('DoK-port_hardware_rows96_prototype.gba','97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
        ('DoK-port_hardware_rows72_prototype.gba','17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')):
        assert sha(Path('artifacts/gba')/name)==expected;preserved[name]=expected
    short=lambda rs:[dict(level=r['level'],language=r['language'],fps=r['presented_fps'],debt=r['max_motion_debt']) for r in rs]
    paths=[final/'prototype-build.json',folder/'prototype-build.json',final/'visibility.json',folder/'pressure.json',
        folder/'profile.json',folder/'target.json',folder/'control-target.json',folder/'hw_v128_sector_gpu_shear-live24.json',
        folder/'hw_v128_entity_rect-live24.json',Path('build/g20a-sector-gpu-v8/prototype-build.json')]
    result=dict(task='G20A',status='bounded-affine-shear-not-selected-architecture-comparison-closed',goal_complete=False,
        performance_build_in_current_goal=False,trial_rom_sha256=trial['rom_sha256'],rom_bytes=trial['rom_bytes'],
        control_rom_sha256=control,byte_exact_repeat=True,core_sha256=qa['core_sha256'],heavy_cases=heavy,
        live24=dict(before=short(live_base['rows']),after=short(live['rows'])),profile=profile,
        verification=dict(poses=58,mode4_cycles=count('mode4_cycle'),weapon_poses=sum(bool(r['weapon_fixture']) for r in rows),
            active_gpu_poses=46,nonzero_pc_poses=8,source_texels=count('source_texels'),gpu_clip_candidate_pixels=count('gpu_clip_pixels'),
            ppu_pixels=count('ppu_pixels'),base_pixels=count('base_pixels'),scope=qa['scope'],
            missing_mask=count('missing_physical_mask_pixels'),extra_mask=count('extra_physical_mask_pixels'),
            max_missing=max(r['missing_physical_mask_pixels'] for r in rows),max_extra=max(r['extra_physical_mask_pixels'] for r in rows),
            missing_interior=count('missing_interior_radius2_pixels'),extra_beyond2=count('extra_beyond_radius2_pixels'),
            max_missing_interior=max(r['missing_interior_radius2_pixels'] for r in rows),max_extra_beyond2=max(r['extra_beyond_radius2_pixels'] for r in rows)),
        pressure_probe=[dict(level=r['level'],cell=r['cell'],pieces=r['gpu_pieces'],cpu_fallback=r['gpu_fallback'],
            actual_ppu_lost_pixels=r['ppu_pressure_mismatches'],classified_capacity_tail=r['ppu_capacity_tail_mismatches'],work=r['gpu_work']) for r in p_rows],
        memory=dict(ewram_bytes=trial['ewram_static_bytes'],ewram_free=262144-trial['ewram_static_bytes'],
            iwram_bytes=trial['symbols']['__iwram_end']-trial['symbols']['__iwram_start'],
            iwram_free_before_stack=24576-(trial['symbols']['__iwram_end']-trial['symbols']['__iwram_start']),
            main_stack=live['stack'],irq_stack=live['irq_stack']),preserved_roms=preserved,
        decision='Select existing2775 hardware base planes + CPU foreground as the main architecture. Close the planned representative architecture comparison; this is not a proof that no future GBA renderer can improve it. Limit remaining work to one justified transparent-column format experiment and final remaining-cost handoff. No Performance build.',
        limitations=['Five heavy production views use full CPU dynamic fallback after GPU preparation; do not call their FPS successful full-GPU FPS.',
            'Pressure probe bypasses admission and may skip hard-cap commands; it is solely evidence of PPU loss in the uploaded command set, not full-scene/FPS/quality acceptance.',
            'Actual static walls remain CPU. Adding commands for them does not solve capacity overflow already seen in dynamic command sets under this representation.',
            'The bounded fit controls quantized X boundaries and native height variation; source clipping/mips still change silhouettes, which are measured and not accepted as minor.',
            'Current mGBA upstream source informed budget accounting; compiled core identified separately by SHA. Actual pressure loss is verified in that compiled core, not physical GBA.',
            'No full440/new map edits/full gameplay/physical GBA acceptance for this unselected prototype.'],
        evidence=[dict(path=p.as_posix(),sha256=sha(p)) for p in paths])
    (ROOT/'analysis/g20a-sector-shear-trial.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(result['status']);print(heavy);print(result['verification']);print(result['pressure_probe'])


if __name__=='__main__':main()
