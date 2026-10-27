"""Collect sector clip/span experiments with byte-exact control and repeats."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    read=lambda p:json.loads((ROOT/p).read_text(encoding='utf-8-sig'))
    sha=lambda p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    measured=Path('research/experiments/g20a-sector-clip-v4');final=Path('research/experiments/g20a-sector-clip-final')
    manifest=read(final/'prototype-build.json');prior=read(measured/'prototype-build.json')
    repeated=read(Path('research/experiments/g20a-sector-clip-repeat/prototype-build.json'))
    control='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    assert manifest['variants']['hw_v128_entity_rect']['rom_sha256']==control
    for name,entry in manifest['variants'].items():
        expected=entry['rom_sha256']
        assert expected==prior['variants'][name]['rom_sha256']==repeated['variants'][name]['rom_sha256']
        for folder in (measured,final,Path('research/experiments/g20a-sector-clip-repeat')):
            assert sha(folder/name/'dok-diagnostic.gba')==expected
    assert all(sha(Path(p))==v for p,v in manifest['sources'].items()),'Current prototype source hashes'
    base=read(measured/'control-target.json');live_base=read(measured/'hw_v128_entity_rect-live24.json')
    assert base['rom_sha256']==live_base['rom_sha256']==control
    key=lambda r:(r['level'],r['cell'],r['heading']);base_rows={key(r):r for r in base['rows']}
    paths=[final/'prototype-build.json',measured/'prototype-build.json',
        Path('research/experiments/g20a-sector-clip-repeat/prototype-build.json'),measured/'control-target.json',
        measured/'hw_v128_entity_rect-live24.json',final/'image-cache-eligibility.json',measured/'ppu18.json']
    results={}
    for name,prefix in (('hw_v128_sector_clip_hot',''),('hw_v128_sector_bitmap_hot','bitmap-')):
        files=[measured/(prefix+n+'.json') for n in ('target','profile','visibility')]+[measured/(name+'-live24.json')]
        paths+=files;target,profile,qa,live=map(read,files);rom=manifest['variants'][name]['rom_sha256']
        assert target['rom_sha256']==profile['rom_sha256']==qa['after']['rom_sha256']==live['rom_sha256']==rom
        assert qa['before']['rom_sha256']==control and qa['script_sha256']==sha(Path('scripts/verify_gba_sector.py'))
        assert profile['script_sha256']==sha(Path('scripts/profile_gba_sector.py'))
        assert len(qa['after']['rows'])==40 and len(qa['after']['fallbacks'])==12
        for row in target['rows']+live['rows']:
            assert row['audio']['underruns']==0 and row['sound_active_frames']>0
        assert all(row['time_conservation_error_ticks']==0 for row in live['rows'])
        heavy=[dict(level=r['level'],cell=r['cell'],heading=r['heading'],cpu_ratio=r['median']/base_rows[key(r)]['median'],
            baseline_fps=base_rows[key(r)]['presented_fps'],fps=r['presented_fps']) for r in target['rows']]
        assert len(heavy)==5 and all(r['cpu_ratio']>1 for r in heavy)
        build=read(final/name/'build-report.json')
        count=lambda field:sum(r.get(field,0) for r in qa['after']['rows'])
        results[name]=dict(rom_sha256=rom,rom_bytes=build['rom_bytes'],heavy_cases=heavy,
            live24=[dict(level=r['level'],language=r['language'],fps=r['presented_fps'],max_motion_debt=r['max_motion_debt']) for r in live['rows']],
            profile=profile,verification=dict(poses=40,mode4_cycles=count('mode4_cycle'),map_edit_fixtures=12,
                wall_columns=count('wall_columns_checked'),roof_rows=count('roof_rows_checked'),
                dynamic_pixels=count('dynamic_pixels_checked'),roof_pixels=count('plane_pixels_checked'),
                software_base_pixels=count('software_base_pixels_checked'),wall_endpoint_columns_excluded=count('wall_endpoint_columns_excluded'),
                opaque_edge_rows_excluded=count('opaque_edge_rows_excluded'),scope=qa['scope'],
                world_grid_rows_excluded=count('world_grid_rows_excluded'),
                adjacent_roof_rows_excluded=count('adjacent_roof_rows_excluded'),
                wall_depth_edge_rows_excluded=count('wall_depth_edge_rows_excluded'),
                full_ppu_composition_pixels=40*38400 if prefix else 0),
            memory=dict(ewram_bytes=build['ewram_static_bytes'],ewram_free_bytes=262144-build['ewram_static_bytes'],
                iwram_bytes=build['symbols']['__iwram_end']-build['symbols']['__iwram_start'],
                iwram_free_before_stack_reserve=24576-(build['symbols']['__iwram_end']-build['symbols']['__iwram_start']),
                main_stack=live['stack'],irq_stack=live['irq_stack']))
    ppu=read(measured/'ppu18.json')
    assert ppu['before']['rom_sha256']==control and ppu['after']['rom_sha256']==results['hw_v128_sector_clip_hot']['rom_sha256']
    assert len(ppu['after']['rows'])==18 and all(r['different_overlay_rgb_pixels']==0 for r in ppu['after']['rows'])
    cache=read(final/'image-cache-eligibility.json')
    assert cache['rom_sha256']==control and len(cache['rows'])==9
    assert cache['script_sha256']==sha(Path('scripts/audit_gba_image_cache.py'))
    preserved={}
    for name,expected in (
        ('DoK-port_hardware_cached_prototype.gba',control),
        ('DoK-port_hardware_rows96_prototype.gba','97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
        ('DoK-port_hardware_rows72_prototype.gba','17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')):
        assert sha(Path('artifacts/gba')/name)==expected;preserved[name]=expected
    result=dict(task='G20A',status='sector-clip-and-direct-bitmap-not-selected',goal_complete=False,
        performance_build_in_current_goal=False,control_rom_sha256=control,core_sha256=base['core_sha256'],
        byte_exact_repeat=True,variants=results,sector_tables=manifest['sector_tables'],
        live24_control=[dict(level=r['level'],language=r['language'],fps=r['presented_fps']) for r in live_base['rows']],
        hybrid_ppu=dict(cases=18,max_scene_rgb_changes=max(r['different_rgb_pixels'] for r in ppu['after']['rows']),overlay_changes=0),
        image_cache_eligibility=cache,preserved_roms=preserved,
        limitations=[
            'Specific segment-BSP closed-X/top-clip/shared-plane/deferred-dynamics implementation. Not a proof against all Doom-style engines.',
            'Dynamic source/alpha/order oracle uses the static clip contract. Original-segment wall rays and original-world-cell roof checks independently validate static geometry, with separately counted grid, endpoint, adjacent roof, wall-depth and opaque-edge exclusions in each verification summary.',
            'Bitmap base planes retain32x32 /LIGHT0,7,15 /animated water phases and depth3600; all128 CPU rows retained. Native PPU affine PD228 changes vertical duplication relative to16 separately stretched OBJ bands.',
            'Bitmap map-edit fallback uses the complete legacy CPU background; its faces/leases match the control, background surface lists intentionally differ from retained hardware planes.',
            'V2 bitmap showed PCM underruns during row-DMA output; V3/final insert an IRQ service window and pass the specified PCM scope. Exact IRQ starvation mechanism is inferred, not physically traced. V2 timings are not accepted PCM results.',
            'Five heavy PCM views, six short live24 runs per candidate,40 native poses and12 map edits; no full440 sweep/full gameplay/physical GBA acceptance for these losing candidates.',
            'Exact-camera image cache eligibility was audited on nine120-frame routes; no image cache or camera reprojection was implemented.',
            'Pure GPU/sector-fed output remains a separate architectural question. No Performance build or AI/draw-distance rebalance was implemented.'],
        decision='Preserve2775/96/72; stop local tuning of these two sector prototypes. Keep goal open until remaining architecture disposition and final remaining-cost handoff.',
        evidence=[dict(path=p.as_posix(),sha256=sha(p)) for p in paths])
    (ROOT/'analysis/g20a-sector-trial.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],variants={k:dict(heavy=v['heavy_cases'],memory=v['memory']) for k,v in results.items()}),indent=2))


if __name__=='__main__':main()
